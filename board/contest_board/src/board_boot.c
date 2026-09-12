/****************************************************************************
 * board/contest_board/src/board_boot.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>

#include "board_gpio.h"
#include "board_i2c.h"
#include "board_spiflash.h"
#include "board_audio.h"
#include "board_touch.h"
#include "board_sdmmc.h"
#include "board_c6_wifi.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void esp_board_initialize(void)
{
  /* USB Serial/JTAG is initialized by the ESP32-P4 common architecture
   * layer. No board-specific peripheral is required for the first NSH
   * bring-up milestone.
   */
}

int board_app_initialize(uintptr_t arg)
{
  int ret;

#ifdef CONFIG_FS_PROCFS
  ret = nx_mount(NULL, CONFIG_NSH_PROC_MOUNTPOINT, "procfs", 0, NULL);
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_FS_TMPFS
  ret = nx_mount(NULL, CONFIG_LIBC_TMPDIR, "tmpfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount tmpfs at %s: %d\n",
             CONFIG_LIBC_TMPDIR, ret);
    }
#endif

#if defined(CONFIG_DEV_GPIO) && !defined(CONFIG_GPIO_LOWER_HALF)
  ret = board_gpio_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_ESPRESSIF_I2C0
  ret = board_i2c_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_ESPRESSIF_SPIFLASH_SMARTFS
  ret = board_spiflash_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#if defined(CONFIG_AUDIO_ES8311) && defined(CONFIG_ESP32P4_I2S0)
  ret = board_audio_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_INPUT_TOUCHSCREEN
  ret = board_touch_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_CONTEST2026_345_C6_WIFI_AUTOINIT
  ret = board_c6_wifi_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ESP32-C6 SDIO initialization failed: %d\n",
             ret);
    }
#elif defined(CONFIG_ESP32P4_SDMMC) && \
      !defined(CONFIG_CONTEST2026_345_C6_WIFI)
  ret = board_sdmmc_initialize();
  if (ret < 0)
    {
      /* Non-fatal: SD card may not be inserted at boot */

      ret = 0;
    }
#endif

#ifndef CONFIG_FS_PROCFS
  ret = 0;
#endif

  return ret;
}

#ifdef CONFIG_BOARDCTL_RESET
int board_reset(int status)
{
  up_systemreset();
  return 0;
}
#endif
