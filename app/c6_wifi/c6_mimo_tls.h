/****************************************************************************
 * app/c6_wifi/c6_mimo_tls.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_C6_WIFI_C6_MIMO_TLS_H
#define __APP_C6_WIFI_C6_MIMO_TLS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct c6_mimo_result_s
{
  int http_status;
  unsigned long elapsed_ms;
  size_t reasoning_chars;
  char response_id[96];
  char content[256];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int c6_mimo_tls_probe(FAR const char *verify_hostname);
int c6_mimo_chat(FAR const char *api_key,
                 FAR struct c6_mimo_result_s *result);

#endif /* __APP_C6_WIFI_C6_MIMO_TLS_H */
