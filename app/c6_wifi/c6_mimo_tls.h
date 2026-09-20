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
#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct c6_mimo_result_s
{
  int http_status;
  unsigned long elapsed_ms;
  size_t reasoning_chars;
  char response_id[96];
  char content[2048];
};

typedef int (*c6_mimo_pcm_callback_t)(FAR void *arg,
                                      FAR const uint8_t *pcm,
                                      size_t bytes);

struct c6_mimo_tts_result_s
{
  int http_status;
  unsigned long elapsed_ms;
  unsigned long first_pcm_ms;
  size_t response_bytes;
  size_t pcm_bytes;
  unsigned int audio_events;
  bool stream_done;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int c6_mimo_tls_probe(FAR const char *verify_hostname);
int c6_mimo_chat(FAR const char *api_key,
                 FAR struct c6_mimo_result_s *result);
int c6_mimo_completion(const char *api_key, const char *payload,
                       struct c6_mimo_result_s *result);
int c6_mimo_asr(FAR const char *api_key, FAR const int16_t *pcm,
                size_t frames, unsigned int rate,
                FAR struct c6_mimo_result_s *result);
int c6_mimo_tts_stream(FAR const char *api_key, FAR const char *text,
                       FAR const char *voice,
                       c6_mimo_pcm_callback_t callback, FAR void *arg,
                       FAR struct c6_mimo_tts_result_s *result);

#endif /* __APP_C6_WIFI_C6_MIMO_TLS_H */
