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

#ifdef CONFIG_ESP32P4_MIPI_CSI
#include <arch/chip/esp32p4_mipi_csi.h>
#endif

#include "sc2336_tables.h"
#include "sc2336_capture.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SC2336_DEVICE_PATH       "/dev/i2c0"
#define SC2336_SCCB_ADDRESS      0x30
#define SC2336_SCCB_FREQUENCY    100000
#define SC2336_EXPECTED_ID       0xcb3a
#define SC2336_DEFAULT_RETRIES   5

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
  printf("  csi-deinit                 De-initialize and gate CSI"
         " controller\n");
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  const char *cmd = "test";
  const char *mode = "720p";
  int fd;
  int ret = 0;

  if (argc > 1)
    {
      if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
        {
          show_usage(argv[0]);
          return EXIT_SUCCESS;
        }

      cmd = argv[1];
    }

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
