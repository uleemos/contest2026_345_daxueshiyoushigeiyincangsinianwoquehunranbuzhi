/****************************************************************************
 * board/contest_board/src/board_display.c
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
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/signal.h>
#include <nuttx/video/fb.h>

#include <arch/chip/esp32p4_mipi_dsi.h>
#include "espressif/esp_gpio_p4.h"
#include "board_display.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BOARD_LCD_BL_PIN    26
#define BOARD_LCD_RST_PIN   27

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_display_gpio_init
 ****************************************************************************/

static void board_display_gpio_init(void)
{
  /* Configure LCD Backlight Pin (GPIO26) as Output High */

  esp_gpiowrite(BOARD_LCD_BL_PIN, 1);
  esp_configgpio(BOARD_LCD_BL_PIN, OUTPUT);

  /* Configure LCD Reset Pin (GPIO27) and perform hardware reset cycle */

  esp_gpiowrite(BOARD_LCD_RST_PIN, 1);
  esp_configgpio(BOARD_LCD_RST_PIN, OUTPUT);

  nxsig_usleep(10000);
  esp_gpiowrite(BOARD_LCD_RST_PIN, 0);
  nxsig_usleep(20000);
  esp_gpiowrite(BOARD_LCD_RST_PIN, 1);
  nxsig_usleep(50000);
}

/****************************************************************************
 * Name: board_panel_init_sequence
 ****************************************************************************/

static int board_panel_init_sequence(void)
{
  int ret;

  /* 1. Software Reset (DCS 0x01) */

  ret = esp32p4_mipi_dsi_write_dcs(0, 0x01, NULL, 0);
  if (ret < 0)
    {
      gerr("ERROR: Failed to send DCS soft reset: %d\n", ret);
      return ret;
    }

  nxsig_usleep(120000);

  /* 2. Sleep Out (DCS 0x11) */

  ret = esp32p4_mipi_dsi_write_dcs(0, 0x11, NULL, 0);
  if (ret < 0)
    {
      gerr("ERROR: Failed to send DCS Sleep Out: %d\n", ret);
      return ret;
    }

  nxsig_usleep(120000);

  /* 3. Display On (DCS 0x29) */

  ret = esp32p4_mipi_dsi_write_dcs(0, 0x29, NULL, 0);
  if (ret < 0)
    {
      gerr("ERROR: Failed to send DCS Display On: %d\n", ret);
      return ret;
    }

  nxsig_usleep(20000);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_display_initialize
 ****************************************************************************/

int board_display_initialize(void)
{
  struct esp32p4_dsi_config_s config;
  FAR struct fb_vtable_s *vtable;
  int ret;

  ginfo("Initializing ESP32-P4 MIPI DSI Display...\n");

  /* 1. Reset display panel and turn on backlight */

  board_display_gpio_init();

  /* 2. Setup DSI Configuration and Video Timing */

  config.num_lanes          = CONFIG_ESP32P4_MIPI_DSI_LANES;
  config.lane_bit_rate_mbps =
    (float)CONFIG_ESP32P4_MIPI_DSI_LANE_BITRATE_MBPS;

#ifdef CONFIG_ESP32P4_MIPI_DSI_COLOR_RGB565
  config.color_format       = ESP32P4_DSI_COLOR_RGB565;
#else
  config.color_format       = ESP32P4_DSI_COLOR_RGB888;
#endif

#if defined(CONFIG_ESP32P4_MIPI_DSI_PANEL_EK79007)
  /* EK79007 1024x600 Timing @ ~60Hz */

  config.timing.hsw         = 10;
  config.timing.hbp         = 160;
  config.timing.width       = 1024;
  config.timing.hfp         = 160;
  config.timing.vsw         = 1;
  config.timing.vbp         = 23;
  config.timing.height      = 600;
  config.timing.vfp         = 12;
  config.timing.dpi_clk_mhz = 51.5f;
#elif defined(CONFIG_ESP32P4_MIPI_DSI_PANEL_ILI9881C)
  /* ILI9881C 720x1280 Timing @ ~60Hz */

  config.timing.hsw         = 10;
  config.timing.hbp         = 40;
  config.timing.width       = 720;
  config.timing.hfp         = 40;
  config.timing.vsw         = 4;
  config.timing.vbp         = 20;
  config.timing.height      = 1280;
  config.timing.vfp         = 20;
  config.timing.dpi_clk_mhz = 62.5f;
#elif defined(CONFIG_ESP32P4_MIPI_DSI_PANEL_ST7701S)
  /* ST7701S 480x800 Timing @ ~60Hz */

  config.timing.hsw         = 10;
  config.timing.hbp         = 50;
  config.timing.width       = 480;
  config.timing.hfp         = 50;
  config.timing.vsw         = 2;
  config.timing.vbp         = 20;
  config.timing.height      = 800;
  config.timing.vfp         = 20;
  config.timing.dpi_clk_mhz = 27.5f;
#else
  /* Default Generic Timing */

  config.timing.hsw         = 10;
  config.timing.hbp         = 40;
  config.timing.width       = CONFIG_ESP32P4_MIPI_DSI_WIDTH;
  config.timing.hfp         = 40;
  config.timing.vsw         = 2;
  config.timing.vbp         = 20;
  config.timing.height      = CONFIG_ESP32P4_MIPI_DSI_HEIGHT;
  config.timing.vfp         = 20;
  config.timing.dpi_clk_mhz = 40.0f;
#endif

  ret = esp32p4_mipi_dsi_initialize(&config);
  if (ret < 0)
    {
      gerr("ERROR: esp32p4_mipi_dsi_initialize failed: %d\n", ret);
      return ret;
    }

  /* 3. Send DCS initialization commands to the display panel */

  ret = board_panel_init_sequence();
  if (ret < 0)
    {
      gerr("ERROR: board_panel_init_sequence failed: %d\n", ret);
      return ret;
    }

  /* 4. Initialize Framebuffer structure and allocate PSRAM buffer */

  vtable = esp32p4_fb_initialize(0);
  if (vtable == NULL)
    {
      gerr("ERROR: esp32p4_fb_initialize failed\n");
      return -ENOMEM;
    }

  /* 5. Register /dev/fb0 device node in NuttX */

  ret = fb_register(0, 0);
  if (ret < 0)
    {
      gerr("ERROR: fb_register failed: %d\n", ret);
      return ret;
    }

  /* 6. Start continuous video streaming to panel */

  ret = esp32p4_mipi_dsi_start_video();
  if (ret < 0)
    {
      gerr("ERROR: esp32p4_mipi_dsi_start_video failed: %d\n", ret);
      return ret;
    }

  ginfo("Display subsystem initialized successfully: /dev/fb0 registered\n");
  return OK;
}
