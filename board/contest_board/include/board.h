/****************************************************************************
 * Contest 2026 team 345 ESP32-P4X Function EV Board definitions
 ****************************************************************************/

#ifndef __VENDOR_OPENVELA_BOARDS_CONTEST2026_345_INCLUDE_BOARD_H
#define __VENDOR_OPENVELA_BOARDS_CONTEST2026_345_INCLUDE_BOARD_H

#include <stdint.h>

/* J1 GPIO loopback and BOOT-button test pins for board V1.8.  Connect J1
 * pin 13 (GPIO20) to J1 pin 11 (GPIO21) when running the GPIO loopback
 * smoke test.  GPIO35 also routes through R135 to RMII_TXD1; do not expose
 * the BOOT-button GPIO interrupt when Ethernet is enabled.
 */

#define BOARD_GPIO_OUT      20
#define BOARD_GPIO_IN       21
#define BUTTON_BOOT         35

#define BOARD_NGPIOOUT       1
#define BOARD_NGPIOIN        1
#define BOARD_NGPIOINT       1

#ifdef CONFIG_CONTEST2026_345_C6_WIFI
struct sdio_dev_s;

int board_c6_wifi_initialize(void);
int board_c6_wifi_version(FAR uint32_t *major, FAR uint32_t *minor,
                          FAR uint32_t *patch);
int board_c6_wifi_connect(FAR const char *ssid, FAR const char *password);
FAR const char *board_c6_wifi_netdev_name(void);
FAR struct sdio_dev_s *board_c6_wifi_sdio(void);
#endif

#endif
