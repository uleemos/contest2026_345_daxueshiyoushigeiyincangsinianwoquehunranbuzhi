/****************************************************************************
 * app/sc2336_probe/sc2336_probe_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * Reference: https://github.com/espressif/esp-video-components
 * Component: esp_cam_sensor/sensors/sc2336/
 * Commit:    2e924b614d7095898ac88c7ba34d43a6c98261ea
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/video/fb.h>

#ifdef CONFIG_ESP32P4_MIPI_CSI
#include <arch/chip/esp32p4_mipi_csi.h>
#endif
#ifdef CONFIG_ESP32P4_MIPI_DSI
#include <arch/chip/esp32p4_mipi_dsi.h>
#endif

#include "sc2336_tables.h"
#include "sc2336_capture.h"
#include "sc2336_model_preview.h"
#include "../es8311_audio/velafit_wake_monitor.h"
#include "../c6_wifi/velafit_coach_service.h"
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
#include "../velafit_ai/models/velafit_pose_model.h"
#include "../velafit_ai/pipeline/velafit_pose_worker.h"
#include "../velafit_ai/render/velafit_render.h"
#include "../velafit_ai/algo/squat_fsm.h"
#include "../velafit_ai/algo/velafit_body_check.h"
#include "../velafit_ai/algo/geometry.h"
#include "../velafit_ai/pipeline/velafit_session.h"
#ifdef VELAFIT_TINY_POSE
#include "../velafit_ai/models/velafit_tiny_pose_preprocess.h"
#include "../velafit_ai/models/velafit_tiny_pose_tflm.h"
#endif
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SC2336_DEVICE_PATH       "/dev/i2c0"
#define SC2336_SCCB_ADDRESS      0x30
#define SC2336_SCCB_FREQUENCY    100000
#define SC2336_EXPECTED_ID       0xcb3a
#define SC2336_DEFAULT_RETRIES   5
#define TINYPOSE_FSM_MIN_SCORE   0.20f
#define TINYPOSE_PREPARE_SECONDS 5

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int sc2336_read_register(int fd, uint16_t regaddr, uint8_t *value)
{
  struct i2c_transfer_s transfer;
  struct i2c_msg_s messages[2];
  uint8_t address[2];

  address[0] = (uint8_t)(regaddr >> 8);
  address[1] = (uint8_t)regaddr;

  messages[0].frequency = SC2336_SCCB_FREQUENCY;
  messages[0].addr = SC2336_SCCB_ADDRESS;
  messages[0].flags = I2C_M_NOSTOP;
  messages[0].buffer = address;
  messages[0].length = sizeof(address);

  messages[1].frequency = SC2336_SCCB_FREQUENCY;
  messages[1].addr = SC2336_SCCB_ADDRESS;
  messages[1].flags = I2C_M_READ;
  messages[1].buffer = value;
  messages[1].length = 1;

  transfer.msgv = messages;
  transfer.msgc = 2;
  return ioctl(fd, I2CIOC_TRANSFER,
               (unsigned long)((uintptr_t)&transfer));
}

static int sc2336_write_register(int fd, uint16_t regaddr, uint8_t value)
{
  struct i2c_transfer_s transfer;
  struct i2c_msg_s msg;
  uint8_t buffer[3];

  buffer[0] = (uint8_t)(regaddr >> 8);
  buffer[1] = (uint8_t)regaddr;
  buffer[2] = value;

  msg.frequency = SC2336_SCCB_FREQUENCY;
  msg.addr = SC2336_SCCB_ADDRESS;
  msg.flags = 0;
  msg.buffer = buffer;
  msg.length = sizeof(buffer);

  transfer.msgv = &msg;
  transfer.msgc = 1;
  return ioctl(fd, I2CIOC_TRANSFER,
               (unsigned long)((uintptr_t)&transfer));
}

static int sc2336_read_retry(int fd, uint16_t regaddr, uint8_t *value)
{
  int ret = -EIO;
  int retry;

  for (retry = 1; retry <= SC2336_DEFAULT_RETRIES; retry++)
    {
      ret = sc2336_read_register(fd, regaddr, value);
      if (ret >= 0)
        {
          return 0;
        }

      printf("SC2336 read retry=%d reg=0x%04x ret=%d errno=%d\n",
             retry, regaddr, ret, errno);
      usleep(10000);
    }

  return ret;
}

static int sc2336_write_retry(int fd, uint16_t regaddr, uint8_t value)
{
  int ret = -EIO;
  int retry;

  for (retry = 1; retry <= SC2336_DEFAULT_RETRIES; retry++)
    {
      ret = sc2336_write_register(fd, regaddr, value);
      if (ret >= 0)
        {
          return 0;
        }

      printf("SC2336 write retry=%d reg=0x%04x val=0x%02x ret=%d errno=%d\n",
             retry, regaddr, value, ret, errno);
      usleep(10000);
    }

  return ret;
}

static int sc2336_probe_id(int fd, uint16_t *chip_id)
{
  uint8_t high = 0;
  uint8_t low = 0;
  int ret;

  ret = sc2336_read_retry(fd, SC2336_REG_CHIP_ID_HIGH, &high);
  if (ret < 0)
    {
      fprintf(stderr, "SC2336 FAIL read ID high (0x%04x) ret=%d\n",
              SC2336_REG_CHIP_ID_HIGH, ret);
      return ret;
    }

  ret = sc2336_read_retry(fd, SC2336_REG_CHIP_ID_LOW, &low);
  if (ret < 0)
    {
      fprintf(stderr, "SC2336 FAIL read ID low (0x%04x) ret=%d\n",
              SC2336_REG_CHIP_ID_LOW, ret);
      return ret;
    }

  *chip_id = ((uint16_t)high << 8) | low;
  if (*chip_id != SC2336_EXPECTED_ID)
    {
      fprintf(stderr,
              "SC2336 ID MISMATCH: addr=0x%02x id=0x%04x expected=0x%04x\n",
              SC2336_SCCB_ADDRESS, *chip_id, SC2336_EXPECTED_ID);
      return -ENODEV;
    }

  return 0;
}

static int sc2336_software_reset(int fd)
{
  int ret;

  printf("SC2336 soft-reset: writing reg 0x%04x = 0x01...\n",
         SC2336_REG_SOFTWARE_RST);

  ret = sc2336_write_retry(fd, SC2336_REG_SOFTWARE_RST, 0x01);
  if (ret < 0)
    {
      fprintf(stderr, "SC2336 FAIL write soft-reset register\n");
      return ret;
    }

  /* Sensor requires ~10-20ms to complete internal reset */

  usleep(20000);

  /* Verify ID after reset */

  uint16_t id = 0;
  ret = sc2336_probe_id(fd, &id);
  if (ret < 0)
    {
      fprintf(stderr, "SC2336 FAIL probe after reset\n");
      return ret;
    }

  printf("SC2336 soft-reset PASS, chip_id=0x%04x restored\n", id);
  return 0;
}

static int sc2336_init_table(int fd, const struct sc2336_reg_s *table,
                             const char *mode_name)
{
  size_t idx = 0;
  int ret;

  printf("SC2336 init mode: %s...\n", mode_name);

  while (table[idx].reg != SC2336_REG_END)
    {
      if (table[idx].reg == SC2336_REG_DELAY)
        {
          usleep((useconds_t)table[idx].val * 1000);
        }
      else
        {
          ret = sc2336_write_retry(fd, table[idx].reg, table[idx].val);
          if (ret < 0)
            {
              fprintf(stderr, "SC2336 FAIL write reg idx=%zu reg=0x%04x"
                              " val=0x%02x ret=%d\n",
                      idx, table[idx].reg, table[idx].val, ret);
              return ret;
            }
        }

      idx++;
    }

  printf("SC2336 init table written successfully (%zu registers)\n", idx);
  return 0;
}

static const struct sc2336_reg_s *
sc2336_get_mode_table(const char *mode_name)
{
  if (strcmp(mode_name, "1080p25") == 0)
    {
      return g_sc2336_1080p_25fps;
    }
  else if (strcmp(mode_name, "1080p") == 0 ||
           strcmp(mode_name, "1080p30") == 0)
    {
      return g_sc2336_1080p_30fps;
    }

  return g_sc2336_720p_30fps;
}

static int sc2336_verify_key_registers(int fd, const char *mode_name)
{
  uint8_t clk_val = 0;
  uint8_t ana1_val = 0;
  uint8_t ana2_val = 0;
  uint8_t vtsh_val = 0;
  uint8_t vtsl_val = 0;
  uint8_t expected_ana1 = 0x53;
  uint8_t expected_ana2 = 0x53;
  uint16_t vts = 0;
  int ret;

  if (strcmp(mode_name, "1080p25") == 0)
    {
      expected_ana1 = 0x20;
      expected_ana2 = 0x27;
    }

  ret = sc2336_read_retry(fd, SC2336_REG_CLK_CTRL, &clk_val);
  if (ret < 0) return ret;

  ret = sc2336_read_retry(fd, SC2336_REG_ANA_INIT_1, &ana1_val);
  if (ret < 0) return ret;

  ret = sc2336_read_retry(fd, SC2336_REG_ANA_INIT_2, &ana2_val);
  if (ret < 0) return ret;

  ret = sc2336_read_retry(fd, SC2336_REG_VTS_H, &vtsh_val);
  if (ret < 0) return ret;

  ret = sc2336_read_retry(fd, SC2336_REG_VTS_L, &vtsl_val);
  if (ret < 0) return ret;

  vts = ((uint16_t)vtsh_val << 8) | vtsl_val;

  printf("SC2336 readback verification (%s):\n", mode_name);
  printf("  CLK_CTRL (0x3106)   = 0x%02x (expected 0x05)\n", clk_val);
  printf("  ANA_INIT_1 (0x36e9) = 0x%02x (expected 0x%02x)\n",
         ana1_val, expected_ana1);
  printf("  ANA_INIT_2 (0x37f9) = 0x%02x (expected 0x%02x)\n",
         ana2_val, expected_ana2);
  printf("  VTS      (0x320e/f) = %u\n", vts);

  if (clk_val != 0x05 || ana1_val != expected_ana1 ||
      ana2_val != expected_ana2)
    {
      fprintf(stderr, "SC2336 FAIL register verification mismatch\n");
      return -EIO;
    }

  printf("SC2336 register verification PASS\n");
  return 0;
}

static int sc2336_set_stream(int fd, bool enable)
{
  uint8_t target = enable ? 0x01 : 0x00;
  int ret;

  printf("SC2336 stream control: setting 0x%04x = 0x%02x (%s)...\n",
         SC2336_REG_STANDBY_STREAM, target,
         enable ? "STREAM_ON" : "STREAM_OFF");

  ret = sc2336_write_retry(fd, SC2336_REG_STANDBY_STREAM, target);
  if (ret < 0)
    {
      fprintf(stderr, "SC2336 FAIL set stream state\n");
      return ret;
    }

  usleep(10000); /* 10ms settling */

  printf("SC2336 %s PASS (target=0x%02x, I2C ACK OK)\n",
         enable ? "STREAM_ON" : "STREAM_OFF", target);
  return 0;
}

/* The USB Serial/JTAG TX path may block after sustained simultaneous CSI and
 * DSI traffic. Preview shutdown therefore stops the sensor without printing;
 * final evidence is emitted only after CSI and DMA have been disabled. */
static int sc2336_stop_stream_quiet(int fd)
{
  int ret = sc2336_write_retry(fd, SC2336_REG_STANDBY_STREAM, 0x00);
  if (ret == 0)
    {
      usleep(10000);
    }

  return ret;
}

static int sc2336_run_full_test(int fd, const char *mode_name)
{
  const struct sc2336_reg_s *table = sc2336_get_mode_table(mode_name);
  uint16_t chip_id = 0;
  int ret;

  printf("========================================\n");
  printf(" SC2336 Control Plane Full Smoke Test\n");
  printf(" Mode: %s, I2C Addr: 0x%02x, Freq: %d Hz\n",
         mode_name, SC2336_SCCB_ADDRESS, SC2336_SCCB_FREQUENCY);
  printf("========================================\n");

  /* Step 1: Probe ID */

  printf("[Step 1/6] Probing Chip ID...\n");
  ret = sc2336_probe_id(fd, &chip_id);
  if (ret < 0) return ret;
  printf("  -> Chip ID: 0x%04x (MATCH 0x%04x)\n",
         chip_id, SC2336_EXPECTED_ID);

  /* Step 2: Software Reset */

  printf("[Step 2/6] Software Reset...\n");
  ret = sc2336_software_reset(fd);
  if (ret < 0) return ret;

  /* Step 3: Initialize Register Table */

  printf("[Step 3/6] Writing Mode Table (%s)...\n", mode_name);
  ret = sc2336_init_table(fd, table, mode_name);
  if (ret < 0) return ret;

  /* Step 4: Verify Key Registers */

  printf("[Step 4/6] Verifying Key Registers...\n");
  ret = sc2336_verify_key_registers(fd, mode_name);
  if (ret < 0) return ret;

  /* Step 5: Stream ON */

  printf("[Step 5/6] Activating Stream ON...\n");
  ret = sc2336_set_stream(fd, true);
  if (ret < 0) return ret;

  printf("  Holding stream for 200 ms...\n");
  usleep(200000);

  /* Step 6: Stream OFF */

  printf("[Step 6/6] Returning to Standby (Stream OFF)...\n");
  ret = sc2336_set_stream(fd, false);
  if (ret < 0) return ret;

  printf("========================================\n");
  printf(" SC2336 Control Plane Test: ALL PASS\n");
  printf("========================================\n");

  return 0;
}

static int sc2336_run_cycles(int fd, int cycles, const char *mode_name)
{
  int c;
  int ret;

  printf("========================================\n");
  printf(" SC2336 Stream Transition Cycle Test\n");
  printf(" Cycles: %d, Mode: %s\n", cycles, mode_name);
  printf("========================================\n");

  /* Run full initialization first */

  ret = sc2336_run_full_test(fd, mode_name);
  if (ret < 0)
    {
      fprintf(stderr, "Initial setup failed\n");
      return ret;
    }

  for (c = 1; c <= cycles; c++)
    {
      printf("--- Cycle %d/%d ---\n", c, cycles);

      ret = sc2336_set_stream(fd, true);
      if (ret < 0)
        {
          fprintf(stderr, "Cycle %d: stream-on FAIL\n", c);
          return ret;
        }

      usleep(100000); /* 100ms active */

      ret = sc2336_set_stream(fd, false);
      if (ret < 0)
        {
          fprintf(stderr, "Cycle %d: stream-off FAIL\n", c);
          return ret;
        }

      usleep(50000); /* 50ms standby */
    }

  printf("========================================\n");
  printf(" SC2336 Stream Cycle Test: %d/%d PASS\n", cycles, cycles);
  printf("========================================\n");

  return 0;
}

#ifdef CONFIG_ESP32P4_MIPI_CSI
static int sc2336_csi_init_mode(const char *mode_name)
{
  struct esp32p4_mipi_csi_config_s cfg;

  memset(&cfg, 0, sizeof(cfg));
  cfg.lanes_num    = 2;
  cfg.data_type    = ESP32P4_CSI_DT_RAW10;
  cfg.in_bpp       = 10;
  cfg.out_bpp      = 10;
  cfg.byte_swap_en = false;

  if (strcmp(mode_name, "1080p") == 0 || strcmp(mode_name, "1080p30") == 0 ||
      strcmp(mode_name, "1080p25") == 0)
    {
      cfg.frame_width        = 1920;
      cfg.frame_height       = 1080;
      cfg.lane_bit_rate_mbps = 480;
    }
  else /* 720p */
    {
      cfg.frame_width        = 1280;
      cfg.frame_height       = 720;
      cfg.lane_bit_rate_mbps = 480;
    }

  printf("Initializing ESP32-P4 MIPI CSI controller"
         " (%s: %ux%u, %d lanes, %d Mbps)...\n",
         mode_name, cfg.frame_width, cfg.frame_height,
         cfg.lanes_num, cfg.lane_bit_rate_mbps);

  return esp32p4_mipi_csi_init(&cfg);
}

static int sc2336_run_csi_smoke_test(int fd, const char *mode_name)
{
  struct esp32p4_mipi_csi_status_s st;
  const struct sc2336_reg_s *table;
  int ret;

  printf("========================================\n");
  printf(" ESP32-P4 MIPI CSI D-PHY Smoke Test\n");
  printf(" Mode: %s, Sensor I2C: 0x%02x\n", mode_name, SC2336_SCCB_ADDRESS);
  printf("========================================\n");

  /* Step 1: Initialize CSI controller */

  printf("[Step 1/7] Initializing ESP32-P4 MIPI CSI...\n");
  ret = sc2336_csi_init_mode(mode_name);
  if (ret < 0)
    {
      fprintf(stderr, "CSI init FAIL: %d\n", ret);
      return ret;
    }

  printf("  -> CSI Controller initialized PASS\n");

  /* Step 2: Query Standby D-PHY status */

  printf("[Step 2/7] Checking D-PHY Standby Lane States...\n");
  ret = esp32p4_mipi_csi_get_status(&st);
  if (ret < 0) return ret;

  printf("  Standby state: clk_stop=%d, clk_hs=%d, data_stop=0x%02x\n",
         st.clk_stopstate, st.clk_activehs, st.data_stopstate);

  /* Step 3: Sensor Reset and Table init */

  printf("[Step 3/7] Initializing Sensor (%s)...\n", mode_name);
  ret = sc2336_software_reset(fd);
  if (ret < 0) return ret;

  table = sc2336_get_mode_table(mode_name);
  ret = sc2336_init_table(fd, table, mode_name);
  if (ret < 0) return ret;

  ret = sc2336_verify_key_registers(fd, mode_name);
  if (ret < 0) return ret;

  /* Step 4: Stream ON */

  printf("[Step 4/7] Activating Sensor Stream ON...\n");
  ret = sc2336_set_stream(fd, true);
  if (ret < 0) return ret;

  usleep(100000); /* 100ms active */

  /* Step 5: Check Active D-PHY status */

  printf("[Step 5/7] Verifying D-PHY Active High-Speed Reception...\n");
  ret = esp32p4_mipi_csi_get_status(&st);
  if (ret < 0) return ret;

  printf("  Active state : clk_stop=%d, clk_hs=%d, data_stop=0x%02x\n",
         st.clk_stopstate, st.clk_activehs, st.data_stopstate);
  printf("  Interrupts   : main=0x%08" PRIx32
         ", phy_fatal=0x%08" PRIx32 ", pkt_fatal=0x%08" PRIx32 "\n",
         st.int_st_main, st.int_st_phy_fatal, st.int_st_pkt_fatal);

  if (st.int_st_phy_fatal != 0)
    {
      fprintf(stderr,
              "CSI FAIL: PHY Fatal Error detected (0x%08" PRIx32 ")\n",
              st.int_st_phy_fatal);
      sc2336_set_stream(fd, false);
      return -EIO;
    }

  printf("  Holding stream for 200 ms...\n");
  usleep(200000);

  /* Step 6: Stream OFF */

  printf("[Step 6/7] Returning Sensor to Standby...\n");
  ret = sc2336_set_stream(fd, false);
  if (ret < 0) return ret;

  usleep(50000); /* 50ms standby settling */

  /* Step 7: Final Status Check */

  printf("[Step 7/7] Verifying D-PHY Return to Standby...\n");
  ret = esp32p4_mipi_csi_get_status(&st);
  if (ret < 0) return ret;

  printf("  Final state  : clk_stop=%d, clk_hs=%d, data_stop=0x%02x\n",
         st.clk_stopstate, st.clk_activehs, st.data_stopstate);

  printf("========================================\n");
  printf(" ESP32-P4 MIPI CSI D-PHY Smoke Test: ALL PASS\n");
  printf("========================================\n");
  return 0;
}

static int sc2336_run_dma_capture(int fd, const char *mode_name,
                                  uint32_t frame_limit,
                                  uint32_t duration_seconds)
{
  struct esp32p4_csi_capture_config_s dma_cfg;
  struct esp32p4_csi_dma_stats_s stats;
  struct esp32p4_csi_frame_s frame;
  const struct sc2336_reg_s *table;
  struct timeval started;
  struct timeval now;
  uint32_t first_crc = 0;
  uint32_t crc_changes = 0;
  uint32_t captured = 0;
  bool csi_ready = false;
  bool dma_ready = false;
  bool streaming = false;
  int ret;

  printf("========================================\n");
  printf(" ESP32-P4 CSI DW-GDMA PSRAM Capture Test\n");
  printf(" Mode=%s frames=%" PRIu32 " duration=%" PRIu32 "s\n",
         mode_name, frame_limit, duration_seconds);
  printf("========================================\n");

  ret = sc2336_csi_init_mode(mode_name);
  if (ret < 0)
    {
      printf("CSI DMA FAIL: CSI init ret=%d\n", ret);
      return ret;
    }

  csi_ready = true;
  ret = sc2336_software_reset(fd);
  if (ret < 0)
    {
      goto out;
    }

  table = sc2336_get_mode_table(mode_name);
  ret = sc2336_init_table(fd, table, mode_name);
  if (ret < 0)
    {
      goto out;
    }

  ret = sc2336_verify_key_registers(fd, mode_name);
  if (ret < 0)
    {
      goto out;
    }

  memset(&dma_cfg, 0, sizeof(dma_cfg));
  dma_cfg.frame_width = 1280;
  dma_cfg.frame_height = 720;
  dma_cfg.bpp = 10;
  dma_cfg.dma_chan = 0;
  dma_cfg.buf_count = 2;
  dma_cfg.mem_type = ESP32P4_CSI_BUF_PSRAM;
  dma_cfg.enable_guard = true;

  if (strcmp(mode_name, "1080p") == 0 ||
      strcmp(mode_name, "1080p30") == 0 ||
      strcmp(mode_name, "1080p25") == 0)
    {
      dma_cfg.frame_width = 1920;
      dma_cfg.frame_height = 1080;
    }

  ret = esp32p4_csi_dma_init(&dma_cfg);
  if (ret < 0)
    {
      printf("CSI DMA FAIL: dma_init ret=%d\n", ret);
      goto out;
    }

  dma_ready = true;
  esp32p4_csi_reset_dma_stats();
  gettimeofday(&started, NULL);

  while (frame_limit == 0 || captured < frame_limit)
    {
      if (duration_seconds != 0)
        {
          gettimeofday(&now, NULL);
          if ((uint32_t)(now.tv_sec - started.tv_sec) >= duration_seconds)
            {
              break;
            }
        }

      ret = esp32p4_csi_dma_start();
      if (ret < 0)
        {
          printf("CSI DMA FAIL: arm frame=%" PRIu32 " ret=%d\n",
                 captured + 1, ret);
          goto out;
        }

      ret = sc2336_set_stream(fd, true);
      if (ret < 0)
        {
          esp32p4_csi_dma_stop();
          goto out;
        }

      streaming = true;
      memset(&frame, 0, sizeof(frame));
      ret = esp32p4_csi_capture_frame(&frame, 1000);

      sc2336_set_stream(fd, false);
      streaming = false;
      usleep(5000);

      if (ret < 0)
        {
          printf("CSI DMA FAIL: capture frame=%" PRIu32 " ret=%d\n",
                 captured + 1, ret);
          esp32p4_csi_dump_dma();
          goto out;
        }

      captured++;
      if (captured == 1)
        {
          first_crc = frame.crc32;
        }
      else if (frame.crc32 != first_crc)
        {
          crc_changes++;
        }

      if (frame.bytes_received != frame.buflen || !frame.guard_valid)
        {
          printf("CSI DMA FAIL: frame=%" PRIu32
                 " bytes=%zu/%zu guard=%d\n",
                 captured, frame.bytes_received, frame.buflen,
                 frame.guard_valid);
          ret = -EIO;
          goto out;
        }

      if (captured == 1 || (captured % 100) == 0)
        {
          printf("CSI FRAME PASS seq=%" PRIu32 " addr=%p bytes=%zu"
                 " crc32=%08" PRIx32 " guard=PASS crc_changes=%" PRIu32
                 "\n",
                 frame.seq_no, frame.buffer, frame.bytes_received,
                 frame.crc32, crc_changes);
          fflush(stdout);
        }
    }

  ret = esp32p4_csi_get_dma_stats(&stats);
  if (ret < 0)
    {
      goto out;
    }

  printf("CSI DMA SUMMARY frames=%" PRIu32 " crc_changes=%" PRIu32
         " timeouts=%" PRIu32 " dec=%" PRIu32 " slv=%" PRIu32
         " lli=%" PRIu32 " guard=%" PRIu32 "\n",
         stats.frames_captured, crc_changes, stats.dma_timeouts,
         stats.dma_err_dec, stats.dma_err_slv, stats.dma_err_lli,
         stats.dma_guard_errors);

  if (stats.frames_captured != captured || captured == 0 ||
      (captured > 1 && crc_changes == 0) ||
      stats.dma_timeouts != 0 || stats.dma_err_dec != 0 ||
      stats.dma_err_slv != 0 || stats.dma_err_lli != 0 ||
      stats.dma_guard_errors != 0)
    {
      printf("CSI DMA STABILITY FAIL\n");
      ret = -EIO;
    }
  else
    {
      printf("CSI DMA STABILITY PASS\n");
      ret = 0;
    }

out:
  if (streaming)
    {
      sc2336_set_stream(fd, false);
    }

  if (dma_ready)
    {
      esp32p4_csi_dma_stop();
      esp32p4_csi_dma_deinit();
    }

  if (csi_ready)
    {
      esp32p4_mipi_csi_deinit();
    }

  return ret;
}

static uint64_t preview_time_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
static int sc2336_show_dataset_prompt(bool done)
{
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  velafit_canvas_t canvas;
  const char *title = done ? "CAPTURE DONE" : "CAPTURING";
  const char *detail = done ? "YOU MAY STOP" : "DO SQUATS - STAY IN FRAME";
  velafit_color_t color = done ? VELAFIT_COLOR_GREEN :
                                 VELAFIT_COLOR_YELLOW;
  int fd = open("/dev/fb0", O_RDWR);
  int ret = 0;

  if (fd < 0)
    {
      return -errno;
    }

  memset(&vinfo, 0, sizeof(vinfo));
  memset(&pinfo, 0, sizeof(pinfo));
  if (ioctl(fd, FBIOGET_VIDEOINFO,
            (unsigned long)((uintptr_t)&vinfo)) < 0 ||
      ioctl(fd, FBIOGET_PLANEINFO,
            (unsigned long)((uintptr_t)&pinfo)) < 0)
    {
      ret = -errno;
      goto out;
    }

  if (vinfo.fmt != FB_FMT_RGB16_565 || pinfo.bpp != 16 ||
      pinfo.fbmem == NULL)
    {
      ret = -ENOTSUP;
      goto out;
    }

  velafit_canvas_init(&canvas, (uint8_t *)pinfo.fbmem, vinfo.xres,
                      vinfo.yres, VELAFIT_PIXFMT_RGB565);
  velafit_draw_rect_filled(&canvas, 0, 0, vinfo.xres, vinfo.yres,
                           VELAFIT_COLOR_DARKGRAY);
  velafit_draw_rect(&canvas, 70, 150, vinfo.xres - 140, 300, color);
  velafit_draw_string(&canvas, 220, 235, title, color,
                      VELAFIT_COLOR_DARKGRAY, 5);
  velafit_draw_string(&canvas, 150, 335, detail, VELAFIT_COLOR_WHITE,
                      VELAFIT_COLOR_DARKGRAY, 3);
  if (ioctl(fd, FBIOPAN_CLEAR, FB_NO_OVERLAY) < 0 ||
      ioctl(fd, FBIOPAN_DISPLAY,
            (unsigned long)((uintptr_t)&pinfo)) < 0)
    {
      ret = -errno;
      goto out;
    }

  printf("DATASET CAPTURE PROMPT %s\n", done ? "DONE" : "ACTIVE");

out:
  close(fd);
  return ret;
}
#endif

#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
static int preview_infer(void *ctx,const uint8_t *rgb,pose_frame_t *pose)
{
  (void)ctx;
  velafit_perf_t perf;
  return velafit_pose_infer(rgb,pose,&perf);
}
#ifdef VELAFIT_TINY_POSE
struct tiny_preview_context
{
  uint64_t preprocess_us;
  uint64_t invoke_us;
  uint64_t postprocess_us;
  uint32_t calls;
  uint32_t low_confidence;
};

static int tiny_preview_infer(void *ctx,const uint8_t *rgb,pose_frame_t *pose)
{
  struct tiny_preview_context *stats=ctx;
  velafit_perf_t perf;
  int ret=velafit_tiny_pose_infer_rgb192(rgb,pose,&perf);
  stats->preprocess_us+=perf.preprocess_us;
  stats->invoke_us+=perf.infer_us;
  stats->postprocess_us+=perf.postprocess_us;
  ++stats->calls;
  if(ret==-ENODATA)
    {
      ++stats->low_confidence;
      /* Publish the frame so the UI and FSM explicitly handle lost or
       * partial tracking instead of retaining an older valid pose. */
      return 0;
    }
  return ret;
}

static bool tiny_pose_fsm_ready(const pose_frame_t *pose)
{
  static const int required[] = {KPT_LEFT_SHOULDER,KPT_RIGHT_SHOULDER,
    KPT_LEFT_HIP,KPT_RIGHT_HIP,KPT_LEFT_KNEE,KPT_RIGHT_KNEE,
    KPT_LEFT_ANKLE,KPT_RIGHT_ANKLE};
  for(unsigned int i=0;i<sizeof(required)/sizeof(required[0]);i++)
    if(pose->kpts[required[i]].score<TINYPOSE_FSM_MIN_SCORE) return false;
  return true;
}

static void tiny_preview_draw_phase(velafit_canvas_t *canvas,
                                    bool capture_active,
                                    bool capture_done,
                                    uint32_t seconds)
{
  char line[32];
  int center_x;

  if (!canvas)
    {
      return;
    }

  center_x = (int)canvas->width / 2;
  if (capture_done)
    {
      velafit_draw_rect_filled(canvas, center_x - 230, 230, 460, 110,
                               VELAFIT_COLOR_DARKGRAY);
      velafit_draw_rect(canvas, center_x - 230, 230, 460, 110,
                        VELAFIT_COLOR_GREEN);
      velafit_draw_string(canvas, center_x - 180, 270, "CAPTURE DONE",
                          VELAFIT_COLOR_GREEN, VELAFIT_COLOR_DARKGRAY, 4);
      return;
    }

  if (!capture_active)
    {
      velafit_draw_rect_filled(canvas, center_x - 230, 210, 460, 150,
                               VELAFIT_COLOR_DARKGRAY);
      velafit_draw_rect(canvas, center_x - 230, 210, 460, 150,
                        VELAFIT_COLOR_YELLOW);
      velafit_draw_string(canvas, center_x - 135, 235, "STAND READY",
                          VELAFIT_COLOR_YELLOW, VELAFIT_COLOR_DARKGRAY, 3);
      snprintf(line, sizeof(line), "START IN %lu",
               (unsigned long)seconds);
      velafit_draw_string(canvas, center_x - 120, 290, line,
                          VELAFIT_COLOR_WHITE, VELAFIT_COLOR_DARKGRAY, 4);
      return;
    }

  velafit_draw_rect_filled(canvas, (int)canvas->width - 290, 32, 280, 42,
                           VELAFIT_COLOR_DARKGRAY);
  velafit_draw_circle_filled(canvas, (int)canvas->width - 270, 53, 8,
                             VELAFIT_COLOR_RED);
  snprintf(line, sizeof(line), "CAPTURING %03lus",
           (unsigned long)seconds);
  velafit_draw_string(canvas, (int)canvas->width - 250, 44, line,
                      VELAFIT_COLOR_GREEN, VELAFIT_COLOR_DARKGRAY, 2);
  velafit_draw_rect_filled(canvas, center_x - 155,
                           (int)canvas->height - 42, 310, 34,
                           VELAFIT_COLOR_DARKGRAY);
  velafit_draw_string(canvas, center_x - 126,
                      (int)canvas->height - 32, "GO - DO SQUATS",
                      VELAFIT_COLOR_GREEN, VELAFIT_COLOR_DARKGRAY, 2);
}

static void velafit_demo_draw(velafit_canvas_t *canvas,
                              const velafit_session_t *session,
                              const velafit_body_check_result_t *body,
                              const squat_fsm_t *squat, unsigned int target,
                              bool tracking, uint64_t now_ms,
                              uint64_t session_start_ms)
{
  char line[32];
  int cx = (int)canvas->width / 2;

  if (session->state == VF_BODY_CHECK)
    {
      const char *prompt = velafit_body_check_user_message(body->status);
      const int prompt_scale = 3;
      const int prompt_width = ((int)strlen(prompt) * 6 - 1) * prompt_scale;
      velafit_render_hud(canvas, "BODY CHECK", body->stable_frames, 0, 0,
                         prompt);
      /* The generic HUD is edge-aligned for landscape coordinates. Keep the
       * active framing instruction centered so its CCW90 raster is never
       * clipped on the physically rotated product LCD. */
      velafit_draw_rect_filled(canvas, cx - 110, 130, 220, 340,
                               VELAFIT_COLOR_DARKGRAY);
      velafit_draw_rect(canvas, cx - 110, 130, 220, 340,
                        body->status == BODY_CHECK_READY ?
                        VELAFIT_COLOR_GREEN : VELAFIT_COLOR_YELLOW);
      velafit_draw_string(canvas, cx - prompt_width / 2,
                          300 - (7 * prompt_scale) / 2, prompt,
                          body->status == BODY_CHECK_READY ?
                          VELAFIT_COLOR_GREEN : VELAFIT_COLOR_YELLOW,
                          VELAFIT_COLOR_DARKGRAY, prompt_scale);
      return;
    }

  velafit_draw_rect_filled(canvas, cx - 230, 205, 460, 175,
                           VELAFIT_COLOR_DARKGRAY);
  velafit_draw_rect(canvas, cx - 230, 205, 460, 175,
                    session->state == VF_FINISHED ? VELAFIT_COLOR_GREEN :
                    VELAFIT_COLOR_YELLOW);
  if (session->state == VF_READY)
    {
      velafit_draw_string(canvas, cx - 145, 245, "GET READY",
                          VELAFIT_COLOR_YELLOW, VELAFIT_COLOR_DARKGRAY, 4);
      velafit_draw_string(canvas, cx - 135, 315, "STAND READY",
                          VELAFIT_COLOR_WHITE, VELAFIT_COLOR_DARKGRAY, 3);
    }
  else if (session->state == VF_COUNTDOWN)
    {
      uint64_t left = session->countdown_end_ms > now_ms ?
                      session->countdown_end_ms - now_ms : 0;
      unsigned int number = (unsigned int)((left + 999) / 1000);
      if (number < 1) number = 1;
      snprintf(line, sizeof(line), "%u", number);
      velafit_draw_string(canvas, cx - 25, 240, line,
                          VELAFIT_COLOR_YELLOW, VELAFIT_COLOR_DARKGRAY, 7);
      velafit_draw_string(canvas, cx - 145, 330, "GET READY",
                          VELAFIT_COLOR_WHITE, VELAFIT_COLOR_DARKGRAY, 3);
    }
  else if (session->state == VF_RUNNING)
    {
      bool show_go = now_ms - session_start_ms < 700;
      velafit_draw_string(canvas, cx - (show_go ? 45 : 105), 225,
                          show_go ? "GO" : "SQUATS", VELAFIT_COLOR_GREEN,
                          VELAFIT_COLOR_DARKGRAY, show_go ? 7 : 4);
      snprintf(line, sizeof(line), "%02lu / %02u",
               (unsigned long)squat->total_reps, target);
      velafit_draw_string(canvas, cx - 105, 315, line,
                          VELAFIT_COLOR_WHITE, VELAFIT_COLOR_DARKGRAY, 4);
      if (!tracking)
        velafit_draw_string(canvas, cx - 145, 380, "LOW CONFIDENCE",
                            VELAFIT_COLOR_RED, VELAFIT_COLOR_DARKGRAY, 2);
    }
  else if (session->state == VF_FINISHED)
    {
      velafit_draw_string(canvas, cx - 205, 255, "WORKOUT COMPLETE",
                          VELAFIT_COLOR_GREEN, VELAFIT_COLOR_DARKGRAY, 3);
      snprintf(line, sizeof(line), "%02lu / %02u",
               (unsigned long)squat->total_reps, target);
      velafit_draw_string(canvas, cx - 105, 325, line,
                          VELAFIT_COLOR_WHITE, VELAFIT_COLOR_DARKGRAY, 4);
    }
}
#endif
#endif

/* pose_mode: 0 camera only, 1 MoveNet, 2 TinyPose squat, 3 body check,
 * 4 exact final RGB96 model-input preview (no inference), 5 product demo.
 */
static int sc2336_run_lcd_preview(int fd, uint32_t duration_seconds,
                                  int pose_mode, uint8_t model_preview_scale,
                                  uint32_t body_prepare_seconds,
                                  unsigned int demo_target,
                                  struct velafit_workout_summary_s *summary)
{
  struct esp32p4_csi_capture_config_s dma_cfg;
  struct esp32p4_csi_frame_s frame;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  const struct sc2336_reg_s *table;
  struct timespec started;
  struct timespec now;
  uint32_t frames = 0;
  uint16_t *render_buffer = NULL;
  uint64_t capture_us = 0;
  uint64_t render_us = 0;
  uint64_t present_us = 0;
  uint64_t preview_started_us;
  uint64_t capture_started_us;
  uint32_t prepare_seconds = pose_mode == 2 ? TINYPOSE_PREPARE_SECONDS :
                             (pose_mode == 3 ? body_prepare_seconds : 0);
  uint32_t active_frames = 0;
  bool capture_active = prepare_seconds == 0;
  bool csi_ready = false;
  bool dma_ready = false;
  bool streaming = false;
  int fbfd = -1;
  int ret;
  if (summary != NULL) memset(summary, 0, sizeof(*summary));
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
  struct vf_pose_worker *worker=NULL;
  uint8_t *model_rgb=NULL;
  uint8_t *model_rgb96=NULL;
  struct vf_pose_worker_stats worker_stats={0};
  bool worker_started=false;
  unsigned pose_visible=0,pose_stale=0,mailbox_busy=0;
  squat_fsm_t squat_fsm;
  squat_fsm_adaptive_t tiny_squat;
  velafit_body_check_t body_check;
  velafit_body_check_result_t body_result;
  pose_frame_t body_debug_pose[10];
  velafit_body_check_result_t body_debug_result[10];
  uint32_t body_debug_count=0;
  uint32_t body_status_counts[BODY_CHECK_READY+1]={0};
  float body_height_samples[256];
  uint32_t body_height_count=0;
  double body_height_sum=0.0;
  uint32_t last_pose_ms=0;
  uint64_t e2e_sum_ms=0,e2e_max_ms=0;
  uint32_t e2e_samples=0;
  uint32_t tiny_ready_frames=0,tiny_rejected_frames=0;
  uint32_t tiny_state_frames[4]={0};
  float tiny_min_knee=360.0f,tiny_max_knee=0.0f;
  float tiny_score_sum[8]={0};
  bool tiny_ready=false;
  velafit_session_t demo_session;
  enum velafit_session_state demo_last_state=VF_IDLE;
  bool demo_running_initialized=false;
  bool demo_tracking=false;
  uint64_t demo_session_start_ms=0,demo_finished_ms=0;
#ifdef VELAFIT_TINY_POSE
  struct tiny_preview_context tiny_stats={0};
#endif
#else
  if(pose_mode) return -ENOSYS;
#endif

  ret = sc2336_csi_init_mode("720p");
  if (ret < 0)
    {
      goto out;
    }

  csi_ready = true;
  ret = sc2336_software_reset(fd);
  if (ret < 0)
    {
      goto out;
    }

  table = sc2336_get_mode_table("720p");
  ret = sc2336_init_table(fd, table, "720p");
  if (ret < 0)
    {
      goto out;
    }

  ret = sc2336_verify_key_registers(fd, "720p");
  if (ret < 0)
    {
      goto out;
    }

  memset(&dma_cfg, 0, sizeof(dma_cfg));
  dma_cfg.frame_width = 1280;
  dma_cfg.frame_height = 720;
  dma_cfg.bpp = 10;
  dma_cfg.dma_chan = 0;
  dma_cfg.buf_count = 2;
  dma_cfg.mem_type = ESP32P4_CSI_BUF_PSRAM;
  dma_cfg.enable_guard = true;
  ret = esp32p4_csi_dma_init(&dma_cfg);
  if (ret < 0)
    {
      goto out;
    }

  dma_ready = true;
  fbfd = open("/dev/fb0", O_RDWR);
  if (fbfd < 0)
    {
      ret = -errno;
      goto out;
    }

  memset(&vinfo, 0, sizeof(vinfo));
  memset(&pinfo, 0, sizeof(pinfo));
  if (ioctl(fbfd, FBIOGET_VIDEOINFO,
            (unsigned long)((uintptr_t)&vinfo)) < 0 ||
      ioctl(fbfd, FBIOGET_PLANEINFO,
            (unsigned long)((uintptr_t)&pinfo)) < 0)
    {
      ret = -errno;
      goto out;
    }

  if (vinfo.fmt != FB_FMT_RGB16_565 || pinfo.bpp != 16 ||
      vinfo.xres != 1024 || vinfo.yres != 600 || pinfo.fbmem == NULL)
    {
      printf("LCD PREVIEW FAIL: unsupported fb fmt=%u size=%ux%u bpp=%u\n",
             vinfo.fmt, vinfo.xres, vinfo.yres, pinfo.bpp);
      ret = -ENOTSUP;
      goto out;
    }

  render_buffer = malloc(pinfo.fblen);
  if (render_buffer == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  ret = sc2336_set_stream(fd, true);
  if (ret < 0)
    {
      goto out;
    }

  streaming = true;
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
  if(pose_mode)
    {
      model_rgb=malloc(192*192*3);
      if (pose_mode == 3 || pose_mode == 4 || pose_mode == 5)
        {
          model_rgb96=malloc(96*96*3);
          if (!model_rgb || !model_rgb96)
            {
              ret=-ENOMEM;
              goto out;
            }
        }
      if (pose_mode != 4)
        {
#ifdef VELAFIT_TINY_POSE
          if(pose_mode==2 || pose_mode==3 || pose_mode==5)
            {
              if(pose_mode==5) velafit_pose_profile_set(false);
              ret=velafit_tiny_pose_init();
              if(ret<0) goto out;
              tiny_ready=true;
              worker=vf_pose_worker_start(tiny_preview_infer,&tiny_stats);
            }
          else
#endif
            {
              worker=vf_pose_worker_start(preview_infer,NULL);
            }
          if(!model_rgb || !worker) {ret=-ENOMEM;goto out;}
          worker_started=true;
          squat_fsm_init(&squat_fsm);
          squat_fsm_adaptive_init(&tiny_squat);
          velafit_body_check_init(&body_check,NULL);
          memset(&body_result,0,sizeof(body_result));
          body_result.status=BODY_CHECK_LOW_CONFIDENCE;
          body_result.orientation=BODY_ORIENTATION_LOW_CONFIDENCE;
          if (pose_mode == 5)
            {
              velafit_session_init(&demo_session);
              ret=velafit_session_event(&demo_session,VELAFIT_VOICE_WAKE,
                                         preview_time_us()/1000);
              if(ret<0) goto out;
              demo_last_state=demo_session.state;
              printf("[VELAFIT] IDLE -> BODY_CHECK target=%u\n",demo_target);
            }
          printf("PREVIEW POSE: backend=%s worker=async pending<=1 "
                 "result_TTL_ms=1000 confidence_gate=%.2f\n",
                 pose_mode>=2 ? "TinyPose-INT8" : "MoveNet",
                 pose_mode==2 ? (double)TINYPOSE_FSM_MIN_SCORE : 0.30);
        }
    }
#endif
  clock_gettime(CLOCK_MONOTONIC, &started);
  preview_started_us = preview_time_us();
  capture_started_us = preview_started_us +
                       (uint64_t)prepare_seconds * 1000000;
  if (pose_mode == 4)
    {
      printf("MODEL INPUT PREVIEW START capture=%" PRIu32
             "s source=final-prequant-RGB96 display_rotation=CCW90"
             " scale=%ux centered=YES stretch=NO cover=NO\n",
             duration_seconds, model_preview_scale);
    }
  else
    {
      printf("LCD PREVIEW START prepare=%" PRIu32 "s capture=%" PRIu32
             "s sensor=1280x720 RAW10 display=1024x600 RGB565"
             " rotation=CCW90 fit=cover detail=512x300 content=1024x600"
             " present=staged-copy\n",
             prepare_seconds, duration_seconds);
    }

  do
    {
      uint64_t t0 = preview_time_us();
      uint64_t t1;
      uint64_t t2;
      uint64_t t3;

      ret = esp32p4_csi_dma_start();
      if (ret < 0)
        {
          goto out;
        }

      memset(&frame, 0, sizeof(frame));
      ret = esp32p4_csi_capture_frame(&frame, 1000);
      if (ret < 0 || frame.bytes_received != frame.buflen ||
          !frame.guard_valid)
        {
          if (ret == 0)
            {
              ret = -EIO;
            }

          goto out;
        }

      t1 = preview_time_us();
      if (pose_mode == 4)
        {
#ifdef VELAFIT_TINY_POSE
          struct sc2336_rgb_capture_s conversion;
          memset(&conversion, 0, sizeof(conversion));
          ret = sc2336_raw10_bggr_letterbox(frame.buffer,
            frame.bytes_received, 1280, 720, model_rgb, 192*192*3,
            192, 192, &conversion);
          if (ret == 0)
            {
              velafit_tiny_pose_debug_rgb96(model_rgb, model_rgb96);
              ret = sc2336_model_preview_blit_ccw_rgb565(model_rgb96,
                96*96*3, render_buffer, pinfo.fblen, vinfo.xres, vinfo.yres,
                pinfo.stride / sizeof(uint16_t), model_preview_scale);
            }
#else
          ret = -ENOSYS;
#endif
        }
      else
        {
          ret = sc2336_raw10_bggr_preview_rgb565(
            frame.buffer, frame.bytes_received, 1280, 720,
            render_buffer, pinfo.fblen, vinfo.xres, vinfo.yres,
            pinfo.stride / sizeof(uint16_t));
        }
      if (ret < 0)
        {
          goto out;
        }

      t2 = preview_time_us();

      if (pose_mode == 5)
        {
          uint64_t demo_now_ms=t2/1000;
          ret=velafit_session_tick(&demo_session,demo_now_ms);
          if(ret<0) goto out;
          if(demo_session.state!=demo_last_state)
            {
              /* USB Serial/JTAG TX can block while CSI and DSI are both
               * active. Keep transitions in RAM and report after shutdown. */
              demo_last_state=demo_session.state;
            }
          if(demo_session.state==VF_RUNNING && !demo_running_initialized)
            {
              squat_fsm_adaptive_begin_capture(&tiny_squat);
              squat_fsm=tiny_squat.fsm;
              demo_session_start_ms=demo_now_ms;
              demo_running_initialized=true;
            }
        }

      if (!capture_active && t2 >= capture_started_us)
        {
          capture_active = true;
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
          if (pose_mode == 2)
            {
              /* Retain the standing baseline learned during the visible
               * countdown, but start the acceptance counters at zero. */
              squat_fsm_adaptive_begin_capture(&tiny_squat);
              squat_fsm = tiny_squat.fsm;
            }
          else if (pose_mode == 3)
            {
              /* Positioning inference is only visual guidance.  READY
               * stability and every acceptance counter start here, after
               * the user has had time to return to the marked area. */
              velafit_body_check_init(&body_check, NULL);
              memset(&body_result, 0, sizeof(body_result));
              body_result.status = BODY_CHECK_LOW_CONFIDENCE;
              body_result.orientation = BODY_ORIENTATION_LOW_CONFIDENCE;
            }
#endif
          printf("LCD CAPTURE ACTIVE duration=%" PRIu32 "s\n",
                 duration_seconds);
          fflush(stdout);
        }
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
      if(worker)
        {
          struct sc2336_rgb_capture_s conversion;
          memset(&conversion,0,sizeof(conversion));
          ret=sc2336_raw10_bggr_letterbox(frame.buffer,frame.bytes_received,
            1280,720,model_rgb,192*192*3,192,192,&conversion);
          if(ret<0) goto out;
          if (pose_mode == 3 || pose_mode == 5)
            {
#ifdef VELAFIT_TINY_POSE
              /* BODY_CHECK must show the exact image used by TinyPose.  The
               * generic camera preview has a different cover crop and cannot
               * prove whether a required keypoint is inside model space. */
              velafit_tiny_pose_debug_rgb96(model_rgb, model_rgb96);
              ret=sc2336_model_preview_blit_ccw_rgb565(model_rgb96,
                96*96*3,render_buffer,pinfo.fblen,vinfo.xres,vinfo.yres,
                pinfo.stride/sizeof(uint16_t),model_preview_scale);
              if(ret<0) goto out;
#else
              ret=-ENOSYS;
              goto out;
#endif
            }
          ret=vf_pose_worker_publish(worker,model_rgb,t0/1000);
          if(ret==-EBUSY) ++mailbox_busy;
          else if(ret<0) goto out;
          pose_frame_t pose;
          ret=vf_pose_worker_latest(worker,preview_time_us()/1000,1000,
                                    &pose,&worker_stats);
          if(!ret)
            {
              velafit_canvas_t canvas;
              velafit_canvas_init(&canvas,(uint8_t *)render_buffer,vinfo.xres,
                                  vinfo.yres,VELAFIT_PIXFMT_RGB565);
              if (pose_mode != 3 && pose_mode != 5)
                {
                  velafit_render_sc2336_skeleton(&canvas,&pose,1280,720,0);
                }
              if(pose_mode>=2 && pose.timestamp_ms!=last_pose_ms)
                {
                  static const int required[8]={KPT_LEFT_SHOULDER,
                    KPT_RIGHT_SHOULDER,KPT_LEFT_HIP,KPT_RIGHT_HIP,
                    KPT_LEFT_KNEE,KPT_RIGHT_KNEE,KPT_LEFT_ANKLE,
                    KPT_RIGHT_ANKLE};
                  uint64_t now_ms=preview_time_us()/1000;
                  uint64_t e2e_ms=now_ms-pose.timestamp_ms;
                  last_pose_ms=pose.timestamp_ms;
                  if (pose_mode==3)
                    {
                      if (!capture_active)
                        {
                          (void)velafit_body_check_update(&body_check, &pose,
                                                         &body_result);
                        }
                      else if(pose.timestamp_ms >= capture_started_us / 1000)
                        {
                          e2e_sum_ms+=e2e_ms;
                          if(e2e_ms>e2e_max_ms) e2e_max_ms=e2e_ms;
                          ++e2e_samples;
                          velafit_body_check_update(&body_check,&pose,
                                                    &body_result);
                          if((unsigned int)body_result.status<=BODY_CHECK_READY)
                            ++body_status_counts[body_result.status];
                          /* Only retain geometry from frames that passed the
                           * confidence, orientation and boundary gates. */
                          if((body_result.status==BODY_CHECK_TOO_CLOSE ||
                              body_result.status==BODY_CHECK_TOO_FAR ||
                              body_result.status==BODY_CHECK_STABILIZING ||
                              body_result.status==BODY_CHECK_READY) &&
                             body_height_count <
                               sizeof(body_height_samples)/
                               sizeof(body_height_samples[0]))
                            {
                              body_height_samples[body_height_count++]=
                                body_result.body_height;
                              body_height_sum+=body_result.body_height;
                            }
                          if(body_debug_count<10)
                            {
                              body_debug_pose[body_debug_count]=pose;
                              body_debug_result[body_debug_count]=body_result;
                              ++body_debug_count;
                            }
                        }
                    }
                  else if (pose_mode==5)
                    {
                      demo_tracking=tiny_pose_fsm_ready(&pose);
                      if(demo_session.state==VF_BODY_CHECK)
                        {
                          velafit_body_check_update(&body_check,&pose,
                                                    &body_result);
                          if(body_result.status==BODY_CHECK_READY)
                            {
                              ret=velafit_session_body_ready(&demo_session,
                                    now_ms,750);
                              if(ret<0) goto out;
                              demo_last_state=demo_session.state;
                            }
                        }
                      else if(demo_session.state==VF_READY ||
                              demo_session.state==VF_COUNTDOWN)
                        {
                          /* Learn standing geometry, but never count during
                           * READY or COUNTDOWN. */
                          (void)squat_fsm_adaptive_update(&tiny_squat,&pose,
                            pose.timestamp_ms,TINYPOSE_FSM_MIN_SCORE);
                        }
                      else if(demo_session.state==VF_RUNNING)
                        {
                          uint32_t before=tiny_squat.fsm.total_reps;
                          (void)squat_fsm_adaptive_update(&tiny_squat,&pose,
                            pose.timestamp_ms,TINYPOSE_FSM_MIN_SCORE);
                          squat_fsm=tiny_squat.fsm;
                          (void)before;
                          if(squat_fsm.total_reps>=demo_target)
                            {
                              ret=velafit_session_finish(&demo_session,now_ms);
                              if(ret<0) goto out;
                              demo_finished_ms=now_ms;
                              demo_last_state=demo_session.state;
                            }
                        }
                    }
                  else if (!capture_active)
                    {
                      /* Use the countdown only to learn standing geometry. */
                      (void)squat_fsm_adaptive_update(&tiny_squat,&pose,
                        pose.timestamp_ms,TINYPOSE_FSM_MIN_SCORE);
                    }
                  else if (pose.timestamp_ms >= capture_started_us / 1000)
                    {
                      e2e_sum_ms+=e2e_ms;
                      if(e2e_ms>e2e_max_ms) e2e_max_ms=e2e_ms;
                      ++e2e_samples;
                      for(unsigned int i=0;i<8;i++)
                        tiny_score_sum[i]+=pose.kpts[required[i]].score;
                      if(tiny_pose_fsm_ready(&pose))
                        {
                          float knee=(velafit_get_knee_angle_left(&pose)+
                                      velafit_get_knee_angle_right(&pose))*0.5f;
                          ++tiny_ready_frames;
                          if(knee<tiny_min_knee) tiny_min_knee=knee;
                          if(knee>tiny_max_knee) tiny_max_knee=knee;
                        }
                      else
                        {
                          ++tiny_rejected_frames;
                        }
                      (void)squat_fsm_adaptive_update(&tiny_squat,&pose,
                        pose.timestamp_ms,TINYPOSE_FSM_MIN_SCORE);
                      if((unsigned int)tiny_squat.fsm.state<4)
                        ++tiny_state_frames[tiny_squat.fsm.state];
                    }
                  squat_fsm=tiny_squat.fsm;
                }
              if(pose_mode==2)
                {
#ifdef VELAFIT_TINY_POSE
                  velafit_render_hud(&canvas,"TINY SQUAT",
                    squat_fsm.total_reps,
                    worker_stats.completed ?
                      1000.0f/(float)(worker_stats.max_inference_ms ?
                                      worker_stats.max_inference_ms:1) : 0.0f,
                    squat_fsm.last_quality_flags,
                    tiny_pose_fsm_ready(&pose) ? "TRACKING" :
                                               "LOW CONFIDENCE");
#endif
                }
              else if(pose_mode==3)
                {
                  velafit_render_hud(&canvas,"BODY CHECK",
                    body_result.stable_frames,
                    worker_stats.completed ?
                      1000.0f/(float)(worker_stats.max_inference_ms ?
                                      worker_stats.max_inference_ms:1) : 0.0f,
                    0,velafit_body_check_user_message(body_result.status));
                }
              else if(pose_mode==5)
                {
                  velafit_demo_draw(&canvas,&demo_session,&body_result,
                                    &squat_fsm,demo_target,demo_tracking,
                                    preview_time_us()/1000,
                                    demo_session_start_ms);
                }
              ++pose_visible;
            }
          else if(ret==-ESTALE) ++pose_stale;
          else if(ret!=-EAGAIN && ret!=-EBUSY) goto out;
          ret=0;
          t2=preview_time_us();
        }
#endif
      if (pose_mode == 2 || pose_mode == 3)
        {
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
          velafit_canvas_t phase_canvas;
          uint64_t phase_now_us = preview_time_us();
          uint32_t phase_seconds;
          velafit_canvas_init(&phase_canvas, (uint8_t *)render_buffer,
                              vinfo.xres, vinfo.yres,
                              VELAFIT_PIXFMT_RGB565);
          if (!capture_active)
            {
              phase_seconds = (uint32_t)((capture_started_us - phase_now_us +
                                          999999) / 1000000);
            }
          else
            {
              uint64_t elapsed = phase_now_us > capture_started_us ?
                                 phase_now_us - capture_started_us : 0;
              phase_seconds = elapsed < (uint64_t)duration_seconds * 1000000 ?
                duration_seconds - (uint32_t)(elapsed / 1000000) : 0;
              ++active_frames;
            }
          tiny_preview_draw_phase(&phase_canvas, capture_active, false,
                                  phase_seconds);
#endif
        }
      else if (pose_mode == 4 || pose_mode == 5)
        {
          ++active_frames;
        }
      /* Complete rendering off-screen before touching the scanout buffer.
       * This is not a VSYNC-synchronized buffer swap: memcpy can still tear.
       */

      memcpy(pinfo.fbmem, render_buffer, pinfo.fblen);

      /* The preview updates one fixed framebuffer.  The generic framebuffer
       * layer also enqueues every PAN notification for a userspace consumer;
       * without a VSYNC consumer that queue fills after the first frame.
       * Drop the stale notification before PAN, while still invoking the
       * driver callback that flushes PSRAM cache for DSI DMA.
       */

      if (ioctl(fbfd, FBIOPAN_CLEAR, FB_NO_OVERLAY) < 0 ||
          ioctl(fbfd, FBIOPAN_DISPLAY,
                (unsigned long)((uintptr_t)&pinfo)) < 0)
        {
          ret = -errno;
          goto out;
        }

      t3 = preview_time_us();
      capture_us += t1 - t0;
      render_us += t2 - t1;
      present_us += t3 - t2;
      frames++;
      /* USB Serial/JTAG can block after a sustained stream while DSI and CSI
       * are active.  The first-frame record plus final aggregate is enough
       * for acceptance and keeps the live path independent of console TX. */
      if (frames == 1)
        {
          printf("LCD PREVIEW FRAME seq=%" PRIu32 " crc32=%08" PRIx32
                 " guard=PASS capture_us=%" PRIu64 " render_us=%" PRIu64
                 " present_us=%" PRIu64 "\n", frames, frame.crc32,
                 t1 - t0, t2 - t1, t3 - t2);
          fflush(stdout);
        }

      clock_gettime(CLOCK_MONOTONIC, &now);
      if(pose_mode==5 && demo_session.state==VF_FINISHED &&
         preview_time_us()/1000-demo_finished_ms>=3000) break;
    }
  while ((uint32_t)(now.tv_sec - started.tv_sec) <
         duration_seconds + prepare_seconds);

#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
  if (pose_mode == 2 || pose_mode == 3)
    {
      velafit_canvas_t done_canvas;
      velafit_canvas_init(&done_canvas, (uint8_t *)render_buffer, vinfo.xres,
                          vinfo.yres, VELAFIT_PIXFMT_RGB565);
      tiny_preview_draw_phase(&done_canvas, true, true, 0);
      memcpy(pinfo.fbmem, render_buffer, pinfo.fblen);
      if (ioctl(fbfd, FBIOPAN_CLEAR, FB_NO_OVERLAY) < 0 ||
          ioctl(fbfd, FBIOPAN_DISPLAY,
                (unsigned long)((uintptr_t)&pinfo)) < 0)
        {
          ret = -errno;
          goto out;
        }
    }
#endif

  ret = 0;

out:
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
  if(worker)
    {
      /* Join before reporting callback-owned timing fields. */
      vf_pose_worker_stop_with_stats(worker,&worker_stats);
      worker=NULL;
      if(pose_mode!=2)
        {
          velafit_pose_model_deinit();
        }
    }
#ifdef VELAFIT_TINY_POSE
  if(tiny_ready) velafit_tiny_pose_deinit();
  if(pose_mode==5) velafit_pose_profile_set(true);
#endif
  free(model_rgb);
  free(model_rgb96);
#endif
  free(render_buffer);
  if (streaming)
    {
      int stop_ret = sc2336_stop_stream_quiet(fd);
      if (ret == 0 && stop_ret < 0)
        {
          ret = stop_ret;
        }
    }

  if (fbfd >= 0)
    {
      close(fbfd);
    }

  if (dma_ready)
    {
      esp32p4_csi_dma_stop();
      esp32p4_csi_dma_deinit();
    }

  if (csi_ready)
    {
      esp32p4_mipi_csi_deinit();
    }

  /* No console output is attempted between the last displayed frame and
   * complete CSI/DMA shutdown. This keeps USB Serial/JTAG responsive. */
  if (ret == 0)
    {
      printf("LCD PREVIEW PASS frames=%" PRIu32 " active_frames=%" PRIu32
             " prepare=%" PRIu32 "s capture=%" PRIu32 "s\n",
             frames, active_frames, prepare_seconds, duration_seconds);
      printf("LCD PREVIEW MEAN capture_us=%" PRIu64 " render_us=%" PRIu64
             " present_us=%" PRIu64 "\n", capture_us / frames,
             render_us / frames, present_us / frames);
      if(pose_mode==5)
        {
          printf("[VELAFIT] DEMO final=%s target=%u reps=%lu "
                 "body_ready=%s elapsed_ms=%llu\n",
                 velafit_session_state_name(demo_session.state),demo_target,
                 (unsigned long)squat_fsm.total_reps,
                 body_result.status==BODY_CHECK_READY ? "yes" : "no",
                 (unsigned long long)(demo_finished_ms>=demo_session_start_ms ?
                   demo_finished_ms-demo_session_start_ms:0));
          if (summary != NULL && demo_session.state == VF_FINISHED)
            {
              summary->target_reps = demo_target;
              summary->completed_reps = squat_fsm.total_reps;
              summary->form_warning_reps = squat_fsm.total_reps >=
                                           squat_fsm.valid_reps ?
                                           squat_fsm.total_reps -
                                           squat_fsm.valid_reps : 0;
              summary->shallow_warning_reps = squat_fsm.shallow_count;
              summary->knee_caving_warning_reps =
                squat_fsm.knee_caving_count;
              summary->trunk_lean_warning_reps =
                squat_fsm.trunk_lean_count;
              summary->duration_sec =
                (unsigned int)((demo_finished_ms - demo_session_start_ms +
                                999) / 1000);
            }
        }
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
      if (worker_started)
        {
          printf("PREVIEW POSE stats submitted=%lu replaced=%lu completed=%lu errors=%lu empty=%lu max_infer_ms=%llu visible=%u stale=%u busy=%u\n",
            (unsigned long)worker_stats.submitted,(unsigned long)worker_stats.replaced,
            (unsigned long)worker_stats.completed,(unsigned long)worker_stats.errors,
            (unsigned long)worker_stats.empty,
            (unsigned long long)worker_stats.max_inference_ms,pose_visible,pose_stale,mailbox_busy);
#ifdef VELAFIT_TINY_POSE
          if (pose_mode == 2)
            {
              printf("PREVIEW TINY timing calls=%lu low_confidence=%lu preprocess_mean_us=%llu "
                     "invoke_mean_us=%llu postprocess_mean_us=%llu "
                     "capture_to_result_mean_ms=%llu capture_to_result_max_ms=%llu "
                     "reps=%lu valid_reps=%lu shallow=%lu valgus=%lu lean=%lu\n",
                     (unsigned long)tiny_stats.calls,
                     (unsigned long)tiny_stats.low_confidence,
                     (unsigned long long)(tiny_stats.calls ? tiny_stats.preprocess_us/tiny_stats.calls:0),
                     (unsigned long long)(tiny_stats.calls ? tiny_stats.invoke_us/tiny_stats.calls:0),
                     (unsigned long long)(tiny_stats.calls ? tiny_stats.postprocess_us/tiny_stats.calls:0),
                     (unsigned long long)(e2e_samples ? e2e_sum_ms/e2e_samples:0),
                     (unsigned long long)e2e_max_ms,
                     (unsigned long)squat_fsm.total_reps,
                     (unsigned long)squat_fsm.valid_reps,
                     (unsigned long)squat_fsm.shallow_count,
                     (unsigned long)squat_fsm.knee_caving_count,
                     (unsigned long)squat_fsm.trunk_lean_count);
              printf("PREVIEW TINY quality ready=%lu rejected=%lu "
                     "knee_angle_min=%.1f knee_angle_max=%.1f baseline=%.1f "
                     "states=%lu,%lu,%lu,%lu score_mean="
                     "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                     (unsigned long)tiny_ready_frames,
                     (unsigned long)tiny_rejected_frames,
                     tiny_ready_frames ? (double)tiny_min_knee : 0.0,
                     tiny_ready_frames ? (double)tiny_max_knee : 0.0,
                     tiny_squat.standing_valid ?
                       (double)tiny_squat.standing_angle : 0.0,
                     (unsigned long)tiny_state_frames[0],
                     (unsigned long)tiny_state_frames[1],
                     (unsigned long)tiny_state_frames[2],
                     (unsigned long)tiny_state_frames[3],
                     e2e_samples ? tiny_score_sum[0]/e2e_samples:0.0,
                     e2e_samples ? tiny_score_sum[1]/e2e_samples:0.0,
                     e2e_samples ? tiny_score_sum[2]/e2e_samples:0.0,
                     e2e_samples ? tiny_score_sum[3]/e2e_samples:0.0,
                     e2e_samples ? tiny_score_sum[4]/e2e_samples:0.0,
                     e2e_samples ? tiny_score_sum[5]/e2e_samples:0.0,
                     e2e_samples ? tiny_score_sum[6]/e2e_samples:0.0,
                     e2e_samples ? tiny_score_sum[7]/e2e_samples:0.0);
            }
          else if (pose_mode == 3)
            {
              static const int ids[8]={KPT_LEFT_SHOULDER,KPT_RIGHT_SHOULDER,
                KPT_LEFT_HIP,KPT_RIGHT_HIP,KPT_LEFT_KNEE,KPT_RIGHT_KNEE,
                KPT_LEFT_ANKLE,KPT_RIGHT_ANKLE};
              static const char *names[8]={"left_shoulder","right_shoulder",
                "left_hip","right_hip","left_knee","right_knee",
                "left_ankle","right_ankle"};
              printf("BODY-CHECK summary frames=%lu final=%s orientation=%s "
                     "stable=%u confidence_threshold=%.2f confidence_floor=%.2f "
                     "strong_required=%u margin=%.3f "
                     "min_height=%.3f max_height=%.3f pair_dy_max=%.3f "
                     "knee_dy_max=%.3f counts="
                     "%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
                     (unsigned long)e2e_samples,
                     velafit_body_check_status_name(body_result.status),
                     velafit_body_orientation_name(body_result.orientation),
                     body_result.stable_frames,
                     (double)body_check.config.confidence_threshold,
                     (double)body_check.config.confidence_floor,
                     body_check.config.minimum_strong_points,
                     (double)body_check.config.frame_margin,
                     (double)body_check.config.minimum_body_height,
                     (double)body_check.config.maximum_body_height,
                     (double)body_check.config.maximum_pair_vertical_delta,
                     (double)body_check.config.maximum_knee_vertical_delta,
                     (unsigned long)body_status_counts[0],
                     (unsigned long)body_status_counts[1],
                     (unsigned long)body_status_counts[2],
                     (unsigned long)body_status_counts[3],
                     (unsigned long)body_status_counts[4],
                     (unsigned long)body_status_counts[5],
                     (unsigned long)body_status_counts[6]);
              printf("BODY-CHECK timing calls=%lu preprocess_mean_us=%llu "
                     "invoke_mean_us=%llu postprocess_mean_us=%llu "
                     "capture_to_result_mean_ms=%llu "
                     "capture_to_result_max_ms=%llu\n",
                     (unsigned long)tiny_stats.calls,
                     (unsigned long long)(tiny_stats.calls ?
                       tiny_stats.preprocess_us/tiny_stats.calls:0),
                     (unsigned long long)(tiny_stats.calls ?
                       tiny_stats.invoke_us/tiny_stats.calls:0),
                     (unsigned long long)(tiny_stats.calls ?
                       tiny_stats.postprocess_us/tiny_stats.calls:0),
                     (unsigned long long)(e2e_samples ?
                       e2e_sum_ms/e2e_samples:0),
                     (unsigned long long)e2e_max_ms);
              if(body_height_count)
                {
                  /* The preview produces fewer than 256 samples.  Sorting
                   * this local diagnostic copy keeps the runtime gate free
                   * of percentile bookkeeping. */
                  for(uint32_t i=1;i<body_height_count;i++)
                    {
                      float value=body_height_samples[i];
                      uint32_t j=i;
                      while(j>0 && body_height_samples[j-1]>value)
                        {
                          body_height_samples[j]=body_height_samples[j-1];
                          --j;
                        }
                      body_height_samples[j]=value;
                    }
                  uint32_t p95_index=
                    (95u*body_height_count+99u)/100u-1u;
                  printf("BODY-CHECK valid-height samples=%lu min=%.3f "
                         "avg=%.3f p95=%.3f max=%.3f\n",
                         (unsigned long)body_height_count,
                         (double)body_height_samples[0],
                         body_height_sum/body_height_count,
                         (double)body_height_samples[p95_index],
                         (double)body_height_samples[body_height_count-1]);
                }
              else
                {
                  printf("BODY-CHECK valid-height samples=0\n");
                }
              for(uint32_t frame_index=0;frame_index<body_debug_count;
                  ++frame_index)
                {
                  const velafit_body_check_result_t *sample=
                    &body_debug_result[frame_index];
                  printf("BODY-CHECK frame=%lu orientation=%s status=%s "
                         "shoulder_center=%.3f,%.3f hip_center=%.3f,%.3f "
                         "knee_center=%.3f,%.3f ankle_center=%.3f,%.3f "
                         "body_dx=%.3f body_dy=%.3f body_height=%.3f\n",
                         (unsigned long)frame_index,
                         velafit_body_orientation_name(sample->orientation),
                         velafit_body_check_status_name(sample->status),
                         (double)sample->shoulder_center.x,
                         (double)sample->shoulder_center.y,
                         (double)sample->hip_center.x,
                         (double)sample->hip_center.y,
                         (double)sample->knee_center.x,
                         (double)sample->knee_center.y,
                         (double)sample->ankle_center.x,
                         (double)sample->ankle_center.y,
                         (double)sample->body_dx,(double)sample->body_dy,
                         (double)sample->body_height);
                  for(unsigned int point=0;point<8;point++)
                    {
                      const kpt_2d_t *keypoint=
                        &body_debug_pose[frame_index].kpts[ids[point]];
                      printf("BODY-CHECK frame=%lu kpt=%s x=%.3f y=%.3f "
                             "confidence=%.3f\n",
                             (unsigned long)frame_index,names[point],
                             (double)keypoint->x,(double)keypoint->y,
                             (double)keypoint->score);
                    }
                }
            }
#endif
        }
#endif
    }
  else
    {
      printf("LCD PREVIEW FAIL ret=%d\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Name: sc2336_capture_rgb888_letterbox
 *
 * Description:
 *   Capture one guarded 720p packed RAW10 frame and convert it directly to
 *   an aspect-preserving RGB888 model input.  This public entry point lets
 *   the VelaFit application use the already validated sensor/CSI sequence.
 ****************************************************************************/

int sc2336_capture_rgb888_letterbox(uint8_t *rgb, size_t rgb_len,
                                    uint16_t rgb_w, uint16_t rgb_h,
                                    struct sc2336_rgb_capture_s *result)
{
  struct esp32p4_csi_capture_config_s dma_cfg;
  struct esp32p4_csi_frame_s frame;
  const struct sc2336_reg_s *table;
  struct timespec ts0;
  struct timespec ts1;
  bool csi_ready = false;
  bool dma_ready = false;
  bool streaming = false;
  int fd = -1;
  int ret;

  if (rgb == NULL || result == NULL)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  fd = open(SC2336_DEVICE_PATH, O_RDWR);
  if (fd < 0)
    {
      return -errno;
    }

  ret = sc2336_csi_init_mode("720p");
  if (ret < 0)
    {
      goto out;
    }

  csi_ready = true;
  ret = sc2336_software_reset(fd);
  if (ret < 0)
    {
      goto out;
    }

  table = sc2336_get_mode_table("720p");
  ret = sc2336_init_table(fd, table, "720p");
  if (ret < 0)
    {
      goto out;
    }

  ret = sc2336_verify_key_registers(fd, "720p");
  if (ret < 0)
    {
      goto out;
    }

  memset(&dma_cfg, 0, sizeof(dma_cfg));
  dma_cfg.frame_width = 1280;
  dma_cfg.frame_height = 720;
  dma_cfg.bpp = 10;
  dma_cfg.dma_chan = 0;
  dma_cfg.buf_count = 2;
  dma_cfg.mem_type = ESP32P4_CSI_BUF_PSRAM;
  dma_cfg.enable_guard = true;

  ret = esp32p4_csi_dma_init(&dma_cfg);
  if (ret < 0)
    {
      goto out;
    }

  dma_ready = true;
  ret = esp32p4_csi_dma_start();
  if (ret < 0)
    {
      goto out;
    }

  clock_gettime(CLOCK_MONOTONIC, &ts0);
  ret = sc2336_set_stream(fd, true);
  if (ret < 0)
    {
      goto out;
    }

  streaming = true;
  memset(&frame, 0, sizeof(frame));
  ret = esp32p4_csi_capture_frame(&frame, 1000);
  clock_gettime(CLOCK_MONOTONIC, &ts1);
  if (ret < 0)
    {
      goto out;
    }

  result->capture_us = (uint64_t)
    ((int64_t)(ts1.tv_sec - ts0.tv_sec) * 1000000ll +
     (int64_t)(ts1.tv_nsec - ts0.tv_nsec) / 1000ll);
  result->raw_crc32 = frame.crc32;
  if (frame.bytes_received != frame.buflen || !frame.guard_valid)
    {
      ret = -EIO;
      goto out;
    }

  clock_gettime(CLOCK_MONOTONIC, &ts0);
  ret = sc2336_raw10_bggr_letterbox(frame.buffer, frame.bytes_received,
                                    1280, 720, rgb, rgb_len,
                                    rgb_w, rgb_h, result);
  clock_gettime(CLOCK_MONOTONIC, &ts1);
  result->convert_us = (uint64_t)
    ((int64_t)(ts1.tv_sec - ts0.tv_sec) * 1000000ll +
     (int64_t)(ts1.tv_nsec - ts0.tv_nsec) / 1000ll);

out:
  if (streaming)
    {
      int stop_ret = sc2336_set_stream(fd, false);
      if (ret == 0 && stop_ret < 0)
        {
          ret = stop_ret;
        }
    }

  if (dma_ready)
    {
      esp32p4_csi_dma_stop();
      esp32p4_csi_dma_deinit();
    }

  if (csi_ready)
    {
      esp32p4_mipi_csi_deinit();
    }

  close(fd);
  return ret;
}
#endif

static void show_usage(const char *progname)
{
  printf("Usage: %s [command] [args]\n", progname);
  printf("Commands:\n");
  printf("  probe                      (Default) Probe sensor chip ID"
         " (0xcb3a)\n");
  printf("  reset                      Perform soft reset and verify ID"
         " recovery\n");
  printf("  init [720p|1080p|1080p25]  Write mode register table and"
         " verify\n");
  printf("  stream-on                  Enable streaming mode"
         " (0x0100=0x01)\n");
  printf("  stream-off                 Disable streaming mode"
         " (0x0100=0x00)\n");
  printf("  test [720p|1080p|1080p25]  Execute full ID -> Reset ->"
         " Init -> StreamOn/Off test\n");
  printf("  cycle <N> [mode]           Run N stream-on/off transition"
         " cycles (default N=10, mode=720p)\n");
#ifdef CONFIG_ESP32P4_MIPI_DSI
  printf("  display-status             Dump DSI Host/Bridge/DMA state\n");
#endif
#ifdef CONFIG_ESP32P4_MIPI_CSI
  printf("  csi-init [720p|1080p]      Initialize ESP32-P4 MIPI CSI"
         " Host/D-PHY\n");
  printf("  csi-status                 Display ESP32-P4 MIPI CSI/D-PHY"
         " status\n");
  printf("  csi-test [720p|1080p]      Run integrated Sensor + CSI"
         " D-PHY link smoke test\n");
  printf("  dma-capture <N> [mode]      Capture N guarded RAW10 frames"
         " to PSRAM\n");
  printf("  dma-stability <sec> [mode]  Run guarded DMA capture"
         " stability test\n");
  printf("  preview [sec]               Show live camera on /dev/fb0"
         " (default 120 s)\n");
  printf("  preview_pose [sec]          Live MoveNet skeleton overlay\n");
#ifdef VELAFIT_TINY_POSE
  printf("  preview_tiny [sec]          Live TinyPose squat overlay\n");
  printf("  preview_body [sec] [scale] [prepare] BODY_CHECK after LCD countdown\n");
  printf("  preview_model [sec] [scale] Final RGB96 input, CCW to text"
         " (scale 1..6, default 4)\n");
  printf("  demo 3|5|20               Wake -> body check -> real squat demo\n");
#endif
  printf("  capture_prompt active|done LCD dataset collection state\n");
  printf("  csi-deinit                 De-initialize and gate CSI"
         " controller\n");
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int sc2336_dispatch(int argc, char *argv[])
{
  const char *cmd = "test";
  const char *mode = "720p";
  const char *program = strrchr(argv[0], '/');
  unsigned int demo_target = 0;
  int fd;
  int ret = 0;

  program = program ? program + 1 : argv[0];
  if (strcmp(program, "velafit_demo") == 0)
    {
      demo_target = argc == 2 ? (unsigned int)strtoul(argv[1],NULL,0) : 20;
      if(argc>2 || (demo_target!=3 && demo_target!=5 && demo_target!=20))
        {
          fprintf(stderr,"Usage: velafit_demo [3|5|20]\n");
          return EXIT_FAILURE;
        }
      cmd = "demo";
    }

  if (demo_target == 0 && argc > 1)
    {
      if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
        {
          show_usage(argv[0]);
          return EXIT_SUCCESS;
        }

      cmd = argv[1];
    }

  if(strcmp(cmd,"demo")==0)
    {
      if(demo_target==0)
        demo_target=argc==3 ? (unsigned int)strtoul(argv[2],NULL,0) : 20;
      if(demo_target!=3 && demo_target!=5 && demo_target!=20)
        {
          fprintf(stderr,"target must be 3, 5 or 20\n");
          return EXIT_FAILURE;
        }
      ret=velafit_wake_wait_once(60);
      if(ret<0)
        {
          printf("[VELAFIT] wake failed ret=%d\n",ret);
          return EXIT_FAILURE;
        }
      printf("[VELAFIT] WAKE accepted; repeated wake disabled\n");
    }

#ifdef CONFIG_ESP32P4_MIPI_DSI
  if (strcmp(cmd, "display-status") == 0)
    {
      esp32p4_mipi_dsi_dump();
      return EXIT_SUCCESS;
    }
#endif

#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
  if (strcmp(cmd, "capture_prompt") == 0)
    {
      if (argc != 3 || (strcmp(argv[2], "active") != 0 &&
                        strcmp(argv[2], "done") != 0))
        {
          fprintf(stderr, "capture_prompt requires active or done\n");
          return EXIT_FAILURE;
        }

      ret = sc2336_show_dataset_prompt(strcmp(argv[2], "done") == 0);
      return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
#endif

#ifdef CONFIG_ESP32P4_MIPI_CSI
  if (strcmp(cmd, "csi-status") == 0)
    {
      esp32p4_mipi_csi_dump();
      return EXIT_SUCCESS;
    }
  else if (strcmp(cmd, "csi-init") == 0)
    {
      if (argc > 2)
        {
          mode = argv[2];
        }

      ret = sc2336_csi_init_mode(mode);
      if (ret == 0)
        {
          esp32p4_mipi_csi_dump();
        }

      return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
  else if (strcmp(cmd, "csi-deinit") == 0)
    {
      ret = esp32p4_mipi_csi_deinit();
      return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
#endif

  fd = open(SC2336_DEVICE_PATH, O_RDWR);
  if (fd < 0)
    {
      fprintf(stderr, "SC2336 FAIL open %s: %d\n",
              SC2336_DEVICE_PATH, errno);
      return EXIT_FAILURE;
    }

  if (strcmp(cmd, "probe") == 0 || (argc == 1 && strcmp(cmd, "probe") == 0))
    {
      uint16_t chip_id = 0;
      ret = sc2336_probe_id(fd, &chip_id);
      if (ret == 0)
        {
          printf("SC2336 ID PASS address=0x%02x id=0x%04x\n",
                 SC2336_SCCB_ADDRESS, chip_id);
        }
    }
  else if (strcmp(cmd, "reset") == 0)
    {
      ret = sc2336_software_reset(fd);
    }
  else if (strcmp(cmd, "init") == 0)
    {
      if (argc > 2)
        {
          mode = argv[2];
        }

      const struct sc2336_reg_s *table = sc2336_get_mode_table(mode);
      ret = sc2336_init_table(fd, table, mode);
      if (ret == 0)
        {
          ret = sc2336_verify_key_registers(fd, mode);
        }
    }
  else if (strcmp(cmd, "stream-on") == 0)
    {
      ret = sc2336_set_stream(fd, true);
    }
  else if (strcmp(cmd, "stream-off") == 0)
    {
      ret = sc2336_set_stream(fd, false);
    }
  else if (strcmp(cmd, "test") == 0)
    {
      if (argc > 2)
        {
          mode = argv[2];
        }

      ret = sc2336_run_full_test(fd, mode);
    }
  else if (strcmp(cmd, "cycle") == 0)
    {
      int cycles = 10;
      if (argc > 2)
        {
          cycles = atoi(argv[2]);
          if (cycles <= 0) cycles = 10;
        }

      if (argc > 3)
        {
          mode = argv[3];
        }

      ret = sc2336_run_cycles(fd, cycles, mode);
    }
#ifdef CONFIG_ESP32P4_MIPI_CSI
  else if (strcmp(cmd, "csi-test") == 0)
    {
      if (argc > 2)
        {
          mode = argv[2];
        }

      ret = sc2336_run_csi_smoke_test(fd, mode);
    }
  else if (strcmp(cmd, "dma-capture") == 0)
    {
      uint32_t frames = 1;

      if (argc > 2)
        {
          frames = (uint32_t)strtoul(argv[2], NULL, 0);
          if (frames == 0)
            {
              frames = 1;
            }
        }

      if (argc > 3)
        {
          mode = argv[3];
        }

      ret = sc2336_run_dma_capture(fd, mode, frames, 0);
    }
  else if (strcmp(cmd, "dma-stability") == 0)
    {
      uint32_t seconds = 300;

      if (argc > 2)
        {
          seconds = (uint32_t)strtoul(argv[2], NULL, 0);
          if (seconds == 0)
            {
              seconds = 300;
            }
        }

      if (argc > 3)
        {
          mode = argv[3];
        }

      ret = sc2336_run_dma_capture(fd, mode, 0, seconds);
    }
  else if (strcmp(cmd, "preview") == 0 || strcmp(cmd,"preview_pose")==0 ||
           strcmp(cmd,"preview_tiny")==0 || strcmp(cmd,"preview_body")==0 ||
           strcmp(cmd,"preview_model")==0 || strcmp(cmd,"demo")==0)
    {
      struct velafit_workout_summary_s workout_summary;
      uint32_t seconds = strcmp(cmd,"demo")==0 ? 600 : 120;

      if (strcmp(cmd,"demo")!=0 && argc > 2)
        {
          seconds = (uint32_t)strtoul(argv[2], NULL, 0);
          if (seconds == 0)
            {
              seconds = 120;
            }
        }

      int pose_mode=strcmp(cmd,"preview_pose")==0 ? 1 :
                    (strcmp(cmd,"preview_tiny")==0 ? 2 :
                    (strcmp(cmd,"preview_body")==0 ? 3 :
                    (strcmp(cmd,"preview_model")==0 ? 4 :
                    (strcmp(cmd,"demo")==0 ? 5 : 0))));
      uint8_t model_preview_scale = 4;
      uint32_t body_prepare_seconds = pose_mode == 3 ? 15 : 0;
      if ((pose_mode == 3 || pose_mode == 4) && argc > 3)
        {
          unsigned long requested_scale = strtoul(argv[3], NULL, 0);
          if (requested_scale < 1 || requested_scale > 6)
            {
              fprintf(stderr, "model preview scale must be 1..6\n");
              ret = -EINVAL;
              goto preview_done;
            }
          model_preview_scale = (uint8_t)requested_scale;
        }
      if (pose_mode == 3 && argc > 4)
        {
          unsigned long requested_prepare = strtoul(argv[4], NULL, 0);
          if (requested_prepare > 120)
            {
              fprintf(stderr, "body prepare seconds must be 0..120\n");
              ret = -EINVAL;
              goto preview_done;
            }
          body_prepare_seconds = (uint32_t)requested_prepare;
        }
#ifndef VELAFIT_TINY_POSE
      if(pose_mode>=2) ret=-ENOSYS;
      else
#endif
      ret = sc2336_run_lcd_preview(fd, seconds,pose_mode,
                                   model_preview_scale,
                                   body_prepare_seconds,demo_target,
                                   &workout_summary);
      if (ret == 0 && pose_mode == 5 &&
          workout_summary.completed_reps >= demo_target)
        {
          struct velafit_coach_result_s coach_result;
          printf("[VELAFIT] WORKOUT_SUMMARY exercise=squat target_reps=%u "
                 "completed_reps=%u form_warning_reps=%u "
                 "shallow=%u knee_caving=%u trunk_lean=%u duration_sec=%u "
                 "body_lost_count=%u\n",
                 workout_summary.target_reps,
                 workout_summary.completed_reps,
                 workout_summary.form_warning_reps,
                 workout_summary.shallow_warning_reps,
                 workout_summary.knee_caving_warning_reps,
                 workout_summary.trunk_lean_warning_reps,
                 workout_summary.duration_sec,
                 workout_summary.body_lost_count);
          if (velafit_coach_runtime_ready())
            {
              ret = velafit_coach_run_runtime(&workout_summary,
                                               &coach_result);
              printf("[VELAFIT] CLOUD_SESSION %s advice_http=%d advice_ms=%lu tts_first_pcm_ms=%lu\n",
                     ret == 0 ? "PASS" : "FAIL",
                     coach_result.http_status,
                     coach_result.advice_latency_ms,
                     coach_result.tts_first_pcm_ms);
            }
          else
            {
              printf("[VELAFIT] AI SERVICE UNAVAILABLE reason=no-runtime-key\n");
              ret = 0;
            }
        }
preview_done:;
    }
#endif
  else
    {
      fprintf(stderr, "Unknown command: %s\n", cmd);
      show_usage(argv[0]);
      ret = -EINVAL;
    }

  close(fd);
  return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
  return sc2336_dispatch(argc, argv);
}

int velafit_demo_main(int argc, char *argv[])
{
  return sc2336_dispatch(argc, argv);
}
