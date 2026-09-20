/****************************************************************************
 * board/contest_board/src/board_display.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_ESP32P4_MIPI_DSI

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/video/fb.h>

#include <arch/chip/esp32p4_mipi_dsi.h>
#include <arch/chip/gpio_sig_map.h>

#include "espressif/esp_gpio_p4.h"
#include "board_display.h"

#define BOARD_LCD_RESET_GPIO       27
#define BOARD_LCD_BACKLIGHT_GPIO   26

static int board_display_gpio_initialize(void)
{
  int ret;

  esp_gpiowrite(BOARD_LCD_BACKLIGHT_GPIO, false);
  esp_gpio_matrix_out(BOARD_LCD_BACKLIGHT_GPIO, SIG_GPIO_OUT_IDX,
                      false, false);
  ret = esp_configgpio(BOARD_LCD_BACKLIGHT_GPIO, OUTPUT);
  if (ret < 0)
    {
      return ret;
    }

  esp_gpiowrite(BOARD_LCD_RESET_GPIO, false);
  esp_gpio_matrix_out(BOARD_LCD_RESET_GPIO, SIG_GPIO_OUT_IDX, false, false);
  ret = esp_configgpio(BOARD_LCD_RESET_GPIO, OUTPUT);
  if (ret < 0)
    {
      return ret;
    }

  up_mdelay(10);
  esp_gpiowrite(BOARD_LCD_RESET_GPIO, true);
  up_mdelay(20);
  return OK;
}

static int board_display_write(uint8_t command, uint8_t value)
{
  return esp32p4_mipi_dsi_write_dcs(0, command, &value, 1);
}

static int board_ek79007_initialize(void)
{
  static const struct
  {
    uint8_t command;
    uint8_t value;
  } init[] =
  {
    {0xb2, 0x10}, /* Two DSI data lanes */
    {0x80, 0x8b},
    {0x81, 0x78},
    {0x82, 0x84},
    {0x83, 0x88},
    {0x84, 0xa8},
    {0x85, 0xe3},
    {0x86, 0x88},
  };
  unsigned int i;
  int ret;

  for (i = 0; i < sizeof(init) / sizeof(init[0]); i++)
    {
      ret = board_display_write(init[i].command, init[i].value);
      if (ret < 0)
        {
          return ret;
        }

      up_mdelay(1);
    }

  ret = esp32p4_mipi_dsi_write_dcs(0, 0x11, NULL, 0); /* Sleep out */
  if (ret < 0)
    {
      return ret;
    }

  up_mdelay(120);
  return esp32p4_mipi_dsi_write_dcs(0, 0x29, NULL, 0); /* Display on */
}

static int board_display_clear(FAR struct fb_vtable_s *fb)
{
  struct fb_planeinfo_s pinfo;
  int ret;

  ret = fb->getplaneinfo(fb, 0, &pinfo);
  if (ret < 0)
    {
      return ret;
    }

  if (pinfo.fbmem == NULL)
    {
      return -ENOMEM;
    }

  /* Publish a complete black frame before starting video/backlight.
   * Bring-up color bars must not be the normal boot screen.
   */

  memset(pinfo.fbmem, 0, pinfo.fblen);
  return fb->pandisplay(fb, &pinfo);
}

int board_display_initialize(void)
{
  const struct esp32p4_dsi_config_s config =
  {
    .num_lanes = 2,
    .lane_bit_rate_mbps = 1000,
    .color_format = ESP32P4_DSI_COLOR_RGB565,
    .timing =
    {
      .hsw = 10,
      .hbp = 120,
      .width = 1024,
      .hfp = 120,
      .vsw = 1,
      .vbp = 20,
      .height = 600,
      .vfp = 10,
      .dpi_clk_mhz = 48,
    },
  };
  FAR struct fb_vtable_s *fb;
  int ret;

  ret = board_display_gpio_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = esp32p4_mipi_dsi_initialize(&config);
  if (ret < 0)
    {
      return ret;
    }

  ret = board_ek79007_initialize();
  if (ret < 0)
    {
      return ret;
    }

  fb = esp32p4_fb_initialize(0);
  if (fb == NULL)
    {
      return -ENOMEM;
    }

  ret = board_display_clear(fb);
  if (ret < 0)
    {
      return ret;
    }

  ret = esp32p4_mipi_dsi_start_video();
  if (ret < 0)
    {
      return ret;
    }

  ret = fb_register(0, 0);
  if (ret < 0)
    {
      return ret;
    }

  esp_gpiowrite(BOARD_LCD_BACKLIGHT_GPIO, true);
  syslog(LOG_INFO, "ESP32-P4 EK79007 display ready: /dev/fb0 1024x600 RGB565\n");
  return OK;
}

#endif
