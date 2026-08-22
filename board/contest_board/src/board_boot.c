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

#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>

#include "board_gpio.h"
#include "board_i2c.h"
#include "board_spiflash.h"
#include "board_audio.h"
#include "board_display.h"

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

#ifdef CONFIG_ESP32P4_MIPI_DSI
  ret = board_display_initialize();
  if (ret < 0)
    {
      return ret;
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
