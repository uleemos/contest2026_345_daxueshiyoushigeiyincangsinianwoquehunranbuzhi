/****************************************************************************
 * board/contest_board/src/board_c6_wifi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARD_CONTEST_BOARD_SRC_BOARD_C6_WIFI_H
#define __BOARD_CONTEST_BOARD_SRC_BOARD_C6_WIFI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/sdio.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

int board_c6_wifi_initialize(void);
int board_c6_wifi_connect(FAR const char *ssid, FAR const char *password);
FAR const char *board_c6_wifi_netdev_name(void);
FAR struct sdio_dev_s *board_c6_wifi_sdio(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_CONTEST_BOARD_SRC_BOARD_C6_WIFI_H */
