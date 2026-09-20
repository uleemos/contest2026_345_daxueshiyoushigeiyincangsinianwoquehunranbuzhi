/****************************************************************************
 * app/c6_wifi/c6_mimo_tls.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <netutils/cJSON.h>

#include "c6_mimo_tls.h"
#include "c6_asr_body.h"
#include "c6_http_policy.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MIMO_HOST              "api.xiaomimimo.com"
#define MIMO_PORT              "443"
#define MIMO_PATH              "/v1/chat/completions"
#define MIMO_RESPONSE_MAX      32768
#define MIMO_REQUEST_MAX       2048
#define MIMO_TLS_TIMEOUT_MS    30000
#define MIMO_MAX_ATTEMPTS      3
#define MIMO_SSE_MAX           65536

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* DigiCert Global Root G2.  This is the trust anchor for the certificate
 * chain served by api.xiaomimimo.com during the 2026-09-13 acceptance.  The
 * leaf and intermediate are supplied by the server and are not pinned.
 * Root SHA-256: CB:3C:CB:B7:60:31:E5:E0:13:8F:8D:D3:9A:23:F9:DE:
 *              47:FF:C3:5E:43:C1:14:4C:EA:27:D4:6A:5A:B1:CB:5F
 */

static const char g_digicert_global_root_g2[] =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
  "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
  "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
  "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
  "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
  "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
  "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
  "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
  "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
  "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
  "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
  "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
  "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
  "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
  "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
  "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
  "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
  "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
  "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
  "MrY=\n"
  "-----END CERTIFICATE-----\n";

static const char g_mimo_body[] =
  "{\"model\":\"mimo-v2.5\",\"messages\":[{\"role\":\"user\","
  "\"content\":\"Reply exactly: VelaFit MiMo device cloud OK\"}],"
  "\"max_completion_tokens\":512}";

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct c6_tls_s
{
  mbedtls_net_context net;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config config;
  mbedtls_x509_crt ca;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
};

struct c6_tts_stream_s
{
  c6_mimo_pcm_callback_t callback;
  FAR void *arg;
  FAR struct c6_mimo_tts_result_s *result;
  FAR char *line;
  FAR char *event;
  size_t line_len;
  size_t event_len;
  uint8_t pending_pcm;
  bool has_pending_pcm;
  bool done;
  struct timespec started;
};

struct c6_chunk_decoder_s
{
  char size_line[32];
  size_t size_len;
  size_t remaining;
  unsigned int state;
  bool done;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void c6_secure_zero(FAR void *buffer, size_t size)
{
  FAR volatile unsigned char *cursor = buffer;

  while (size-- > 0)
    {
      *cursor++ = 0;
    }
}

static unsigned long c6_elapsed(FAR const struct timespec *start)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (unsigned long)((int64_t)(now.tv_sec - start->tv_sec) * 1000 +
                         (now.tv_nsec - start->tv_nsec) / 1000000);
}

static void c6_tls_init(FAR struct c6_tls_s *tls)
{
  mbedtls_net_init(&tls->net);
  mbedtls_ssl_init(&tls->ssl);
  mbedtls_ssl_config_init(&tls->config);
  mbedtls_x509_crt_init(&tls->ca);
  mbedtls_entropy_init(&tls->entropy);
  mbedtls_ctr_drbg_init(&tls->drbg);
}

static void c6_tls_free(FAR struct c6_tls_s *tls)
{
  mbedtls_net_free(&tls->net);
  mbedtls_ssl_free(&tls->ssl);
  mbedtls_ssl_config_free(&tls->config);
  mbedtls_x509_crt_free(&tls->ca);
  mbedtls_ctr_drbg_free(&tls->drbg);
  mbedtls_entropy_free(&tls->entropy);
}

static int c6_tls_connect(FAR struct c6_tls_s *tls,
                          FAR const char *verify_hostname)
{
  static const unsigned char personalization[] = "velafit-mimo-device";
  uint32_t verify_flags;
  int ret;

  c6_tls_init(tls);

  ret = mbedtls_ctr_drbg_seed(&tls->drbg, mbedtls_entropy_func,
                              &tls->entropy, personalization,
                              sizeof(personalization) - 1);
  if (ret != 0)
    {
      printf("TLS entropy FAIL: error=-0x%04x\n", -ret);
      return ret;
    }

  ret = mbedtls_x509_crt_parse(&tls->ca,
                               (FAR const unsigned char *)
                               g_digicert_global_root_g2,
                               sizeof(g_digicert_global_root_g2));
  if (ret != 0)
    {
      printf("TLS trust-anchor FAIL: error=-0x%04x\n", -ret);
      return ret;
    }

  ret = mbedtls_ssl_config_defaults(&tls->config, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0)
    {
      printf("TLS config FAIL: error=-0x%04x\n", -ret);
      return ret;
    }

  mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&tls->config, &tls->ca, NULL);
  mbedtls_ssl_conf_rng(&tls->config, mbedtls_ctr_drbg_random, &tls->drbg);
  mbedtls_ssl_conf_read_timeout(&tls->config, MIMO_TLS_TIMEOUT_MS);

  ret = mbedtls_net_connect(&tls->net, MIMO_HOST, MIMO_PORT,
                            MBEDTLS_NET_PROTO_TCP);
  if (ret != 0)
    {
      printf("TLS TCP FAIL: error=-0x%04x\n", -ret);
      return ret;
    }

  ret = mbedtls_ssl_setup(&tls->ssl, &tls->config);
  if (ret != 0)
    {
      printf("TLS setup FAIL: error=-0x%04x\n", -ret);
      return ret;
    }

  /* Keep real SNI even for the negative diagnostic.  Sending invalid SNI
   * can make the server abort before our certificate verifier runs.
   */

  ret = mbedtls_ssl_set_hostname(&tls->ssl, MIMO_HOST);
  if (ret != 0)
    {
      printf("TLS hostname setup FAIL: error=-0x%04x\n", -ret);
      return ret;
    }

  mbedtls_ssl_set_bio(&tls->ssl, &tls->net, mbedtls_net_send,
                      mbedtls_net_recv, mbedtls_net_recv_timeout);

  do
    {
      ret = mbedtls_ssl_handshake(&tls->ssl);
    }
  while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
         ret == MBEDTLS_ERR_SSL_WANT_WRITE);

  if (ret != 0)
    {
      verify_flags = mbedtls_ssl_get_verify_result(&tls->ssl);
      printf("TLS handshake FAIL: verify_host=%s error=-0x%04x flags=0x%08lx hostname_mismatch=%s\n",
             verify_hostname, -ret, (unsigned long)verify_flags,
             ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED &&
             verify_flags == MBEDTLS_X509_BADCERT_CN_MISMATCH ? "yes" : "no");
      return ret;
    }

  verify_flags = mbedtls_ssl_get_verify_result(&tls->ssl);
  if (verify_flags != 0)
    {
      printf("TLS certificate FAIL: verify_host=%s flags=0x%08lx\n",
             verify_hostname, (unsigned long)verify_flags);
      return -EACCES;
    }

  if (strcmp(verify_hostname, MIMO_HOST) != 0)
    {
      const mbedtls_x509_crt *peer;
      mbedtls_x509_crt chain;

      /* Verify an owned copy: the verifier accepts a mutable chain, whereas
       * the SSL session exposes a const certificate.  No application data
       * or credentials have been sent at this point.
       */

      mbedtls_x509_crt_init(&chain);
      peer = mbedtls_ssl_get_peer_cert(&tls->ssl);
      ret = peer == NULL ? -EIO : 0;
      while (peer != NULL && ret == 0)
        {
          ret = mbedtls_x509_crt_parse_der(&chain, peer->raw.p,
                                         peer->raw.len);
          peer = peer->next;
        }

      verify_flags = 0;
      if (ret == 0)
        {
          ret = mbedtls_x509_crt_verify(&chain, &tls->ca, NULL,
                                       verify_hostname, &verify_flags,
                                       NULL, NULL);
        }

      printf("TLS hostname check FAIL: verify_host=%s error=%d flags=0x%08lx hostname_mismatch=%s phase=post-handshake\n",
             verify_hostname, ret, (unsigned long)verify_flags,
             ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED &&
             verify_flags == MBEDTLS_X509_BADCERT_CN_MISMATCH ? "yes" : "no");
      mbedtls_x509_crt_free(&chain);
      return -EACCES;
    }

  printf("TLS verify PASS: host=%s version=%s cipher=%s\n",
         verify_hostname, mbedtls_ssl_get_version(&tls->ssl),
         mbedtls_ssl_get_ciphersuite(&tls->ssl));
  return OK;
}

static int c6_tls_write_all(FAR mbedtls_ssl_context *ssl,
                            FAR const unsigned char *buffer, size_t length)
{
  size_t offset = 0;
  int ret;

  while (offset < length)
    {
      size_t chunk = length - offset;
      if (chunk > 1024) chunk = 1024;
      ret = mbedtls_ssl_write(ssl, buffer + offset, chunk);
      if (ret > 0)
        {
          offset += ret;
        }
      else if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
               ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          return ret == 0 ? -EPIPE : ret;
        }
    }

  return OK;
}

static int c6_tls_read_response(FAR mbedtls_ssl_context *ssl,
                                FAR char *response, size_t capacity,
                                FAR size_t *response_len)
{
  size_t used = 0;
  int ret;

  while (used + 1 < capacity)
    {
      ret = mbedtls_ssl_read(ssl, (FAR unsigned char *)response + used,
                             capacity - used - 1);
      if (ret > 0)
        {
          used += ret;
          continue;
        }

      if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        {
          break;
        }

      if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
          ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          continue;
        }

      return ret;
    }

  if (used + 1 == capacity)
    {
      return -EFBIG;
    }

  response[used] = '\0';
  *response_len = used;
  return used > 0 ? OK : -ENODATA;
}

static int c6_http_dechunk(FAR char *body, size_t body_len,
                           FAR size_t *decoded_len)
{
  FAR char *readp = body;
  FAR char *writep = body;
  FAR char *end = body + body_len;

  while (readp < end)
    {
      FAR char *line_end = strstr(readp, "\r\n");
      unsigned long chunk_len;
      FAR char *parse_end;

      if (line_end == NULL)
        {
          return -EPROTO;
        }

      *line_end = '\0';
      chunk_len = strtoul(readp, &parse_end, 16);
      if (parse_end == readp || (*parse_end != '\0' && *parse_end != ';'))
        {
          return -EPROTO;
        }

      readp = line_end + 2;
      if (chunk_len == 0)
        {
          *writep = '\0';
          *decoded_len = writep - body;
          return OK;
        }

      if (chunk_len > (unsigned long)(end - readp) ||
          end - readp - chunk_len < 2 ||
          readp[chunk_len] != '\r' || readp[chunk_len + 1] != '\n')
        {
          return -EPROTO;
        }

      memmove(writep, readp, chunk_len);
      writep += chunk_len;
      readp += chunk_len + 2;
    }

  return -EPROTO;
}

static int c6_http_parse(FAR char *response, size_t response_len,
                         FAR int *status, FAR char **body,
                         FAR size_t *body_len)
{
  FAR char *header_end;
  bool chunked;

  if (sscanf(response, "HTTP/%*u.%*u %d", status) != 1)
    {
      return -EPROTO;
    }

  header_end = strstr(response, "\r\n\r\n");
  if (header_end == NULL)
    {
      return -EPROTO;
    }

  chunked = strstr(response, "\r\nTransfer-Encoding: chunked\r\n") != NULL ||
            strstr(response, "\r\ntransfer-encoding: chunked\r\n") != NULL;
  *body = header_end + 4;
  *body_len = response_len - (*body - response);

  if (chunked)
    {
      return c6_http_dechunk(*body, *body_len, body_len);
    }

  (*body)[*body_len] = '\0';
  return OK;
}

static int c6_base64_value(unsigned char ch)
{
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+') return 62;
  if (ch == '/') return 63;
  return -1;
}

static int c6_tts_emit(FAR struct c6_tts_stream_s *stream,
                       FAR const uint8_t *data, size_t bytes)
{
  uint8_t joined[3074];
  size_t offset = 0;
  int ret;

  if (bytes > 0 && stream->result->pcm_bytes == 0)
    stream->result->first_pcm_ms = c6_elapsed(&stream->started);

  if (stream->has_pending_pcm && bytes > 0)
    {
      joined[0] = stream->pending_pcm;
      joined[1] = data[0];
      ret = stream->callback(stream->arg, joined, 2);
      if (ret < 0) return ret;
      stream->result->pcm_bytes += 2;
      stream->has_pending_pcm = false;
      offset = 1;
    }

  size_t even = (bytes - offset) & ~(size_t)1;
  if (even > 0)
    {
      ret = stream->callback(stream->arg, data + offset, even);
      if (ret < 0) return ret;
      stream->result->pcm_bytes += even;
      offset += even;
    }

  if (offset < bytes)
    {
      stream->pending_pcm = data[offset];
      stream->has_pending_pcm = true;
    }
  return 0;
}

static int c6_tts_decode_audio(FAR struct c6_tts_stream_s *stream,
                               FAR const char *input)
{
  uint8_t output[3072];
  int quad[4];
  unsigned int qlen = 0;
  size_t used = 0;
  bool padded = false;

  for (; *input != '\0'; input++)
    {
      unsigned char ch = (unsigned char)*input;
      if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
      if (padded) return -EBADMSG;
      if (ch == '=') quad[qlen++] = -2;
      else
        {
          int value = c6_base64_value(ch);
          if (value < 0) return -EBADMSG;
          quad[qlen++] = value;
        }

      if (qlen == 4)
        {
          if (quad[0] < 0 || quad[1] < 0 ||
              (quad[2] == -2 && quad[3] != -2))
            return -EBADMSG;
          output[used++] = (uint8_t)((quad[0] << 2) | (quad[1] >> 4));
          if (quad[2] != -2)
            {
              output[used++] = (uint8_t)((quad[1] << 4) | (quad[2] >> 2));
              if (quad[3] != -2)
                output[used++] = (uint8_t)((quad[2] << 6) | quad[3]);
              else padded = true;
            }
          else padded = true;
          qlen = 0;
          if (used >= sizeof(output) - 3)
            {
              int ret = c6_tts_emit(stream, output, used);
              if (ret < 0) return ret;
              used = 0;
            }
        }
    }

  if (qlen != 0) return -EBADMSG;
  return used > 0 ? c6_tts_emit(stream, output, used) : 0;
}

static int c6_tts_process_event(FAR struct c6_tts_stream_s *stream)
{
  FAR cJSON *root = NULL;
  FAR cJSON *choices;
  FAR cJSON *choice;
  FAR cJSON *delta;
  FAR cJSON *audio;
  FAR cJSON *data;
  int ret = 0;

  while (stream->event_len > 0 &&
         (stream->event[stream->event_len - 1] == '\n' ||
          stream->event[stream->event_len - 1] == '\r'))
    stream->event_len--;
  stream->event[stream->event_len] = '\0';
  if (stream->event_len == 0) return 0;
  if (strcmp(stream->event, "[DONE]") == 0)
    {
      stream->done = true;
      stream->result->stream_done = true;
      return 0;
    }

  root = cJSON_Parse(stream->event);
  if (root == NULL) return -EPROTO;
  choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
  choice = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
  delta = cJSON_IsObject(choice) ?
          cJSON_GetObjectItemCaseSensitive(choice, "delta") : NULL;
  audio = cJSON_IsObject(delta) ?
          cJSON_GetObjectItemCaseSensitive(delta, "audio") : NULL;
  data = cJSON_IsObject(audio) ?
         cJSON_GetObjectItemCaseSensitive(audio, "data") : NULL;
  if (cJSON_IsString(data) && data->valuestring != NULL &&
      data->valuestring[0] != '\0')
    {
      ret = c6_tts_decode_audio(stream, data->valuestring);
      if (ret == 0) stream->result->audio_events++;
    }
  cJSON_Delete(root);
  return ret;
}

static int c6_tts_sse_byte(FAR struct c6_tts_stream_s *stream, uint8_t byte)
{
  if (stream->done) return 0;
  if (byte != '\n')
    {
      if (stream->line_len + 1 >= MIMO_SSE_MAX) return -E2BIG;
      stream->line[stream->line_len++] = byte;
      return 0;
    }

  if (stream->line_len > 0 && stream->line[stream->line_len - 1] == '\r')
    stream->line_len--;
  stream->line[stream->line_len] = '\0';
  if (stream->line_len == 0)
    {
      int ret = c6_tts_process_event(stream);
      stream->event_len = 0;
      stream->line_len = 0;
      return ret;
    }

  if (strncmp(stream->line, "data:", 5) == 0)
    {
      FAR const char *data = stream->line + 5;
      if (*data == ' ') data++;
      size_t length = strlen(data);
      if (stream->event_len + length + 2 >= MIMO_SSE_MAX) return -E2BIG;
      if (stream->event_len > 0) stream->event[stream->event_len++] = '\n';
      memcpy(stream->event + stream->event_len, data, length);
      stream->event_len += length;
      stream->event[stream->event_len] = '\0';
    }
  stream->line_len = 0;
  return 0;
}

static int c6_chunk_feed(FAR struct c6_chunk_decoder_s *decoder,
                         FAR struct c6_tts_stream_s *stream,
                         FAR const uint8_t *data, size_t bytes)
{
  for (size_t i = 0; i < bytes; i++)
    {
      uint8_t byte = data[i];
      if (decoder->done) continue;
      if (decoder->state == 0) /* chunk size line */
        {
          if (byte == '\r') continue;
          if (byte != '\n')
            {
              if (decoder->size_len + 1 >= sizeof(decoder->size_line))
                return -EPROTO;
              decoder->size_line[decoder->size_len++] = byte;
              continue;
            }
          decoder->size_line[decoder->size_len] = '\0';
          FAR char *end;
          unsigned long size = strtoul(decoder->size_line, &end, 16);
          if (end == decoder->size_line || (*end != '\0' && *end != ';'))
            return -EPROTO;
          decoder->size_len = 0;
          decoder->remaining = size;
          if (size == 0) { decoder->done = true; continue; }
          decoder->state = 1;
        }
      else if (decoder->state == 1) /* chunk data */
        {
          int ret = c6_tts_sse_byte(stream, byte);
          if (ret < 0) return ret;
          if (--decoder->remaining == 0) decoder->state = 2;
        }
      else if (decoder->state == 2) /* data CR */
        {
          if (byte != '\r') return -EPROTO;
          decoder->state = 3;
        }
      else /* data LF */
        {
          if (byte != '\n') return -EPROTO;
          decoder->state = 0;
        }
    }
  return 0;
}

static int c6_mimo_parse_json(FAR char *body,
                              FAR struct c6_mimo_result_s *result)
{
  FAR cJSON *root;
  FAR cJSON *choices;
  FAR cJSON *choice;
  FAR cJSON *message;
  FAR cJSON *content;
  FAR cJSON *reasoning;
  FAR cJSON *id;
  int ret = -EPROTO;

  root = cJSON_Parse(body);
  if (root == NULL)
    {
      return -EPROTO;
    }

  choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
  choice = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
  message = cJSON_IsObject(choice) ?
            cJSON_GetObjectItemCaseSensitive(choice, "message") : NULL;
  content = cJSON_IsObject(message) ?
            cJSON_GetObjectItemCaseSensitive(message, "content") : NULL;
  reasoning = cJSON_IsObject(message) ?
              cJSON_GetObjectItemCaseSensitive(message,
                                               "reasoning_content") : NULL;
  id = cJSON_GetObjectItemCaseSensitive(root, "id");

  if (cJSON_IsString(content) && content->valuestring != NULL &&
      content->valuestring[0] != '\0')
    {
      if (strlen(content->valuestring) >= sizeof(result->content))
        {
          cJSON_Delete(root);
          return -E2BIG; /* Never execute a silently truncated command. */
        }
      strlcpy(result->content, content->valuestring,
              sizeof(result->content));
      if (cJSON_IsString(reasoning) && reasoning->valuestring != NULL)
        {
          result->reasoning_chars = strlen(reasoning->valuestring);
        }

      if (cJSON_IsString(id) && id->valuestring != NULL)
        {
          strlcpy(result->response_id, id->valuestring,
                  sizeof(result->response_id));
        }

      ret = OK;
    }

  cJSON_Delete(root);
  return ret;
}

static int c6_mimo_attempt(FAR const char *api_key,
                           FAR const char *payload, size_t payload_len,
                           FAR struct c6_mimo_result_s *result)
{
  FAR struct c6_tls_s *tls;
  FAR char *request;
  FAR char *response;
  FAR char *body;
  struct timespec started;
  bool tls_initialized = false;
  size_t response_len;
  size_t body_len;
  int request_len;
  int ret;

  clock_gettime(CLOCK_MONOTONIC, &started);
  tls = calloc(1, sizeof(*tls));
  request = malloc(MIMO_REQUEST_MAX);
  response = malloc(MIMO_RESPONSE_MAX);
  if (tls == NULL || request == NULL || response == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  /* c6_tls_connect initializes the net fd to -1 before any fallible work.
   * Before that call, calloc leaves fd=0: net_free would close the console.
   */
  tls_initialized = true;
  ret = c6_tls_connect(tls, MIMO_HOST);
  if (ret < 0)
    {
      goto out;
    }

  printf("MiMo timing tls_ready_ms=%lu payload_bytes=%lu\n",
         c6_elapsed(&started), (unsigned long)payload_len);

  request_len = snprintf(request, MIMO_REQUEST_MAX,
                         "POST " MIMO_PATH " HTTP/1.1\r\n"
                         "Host: " MIMO_HOST "\r\n"
                         "api-key: %s\r\n"
                         "Content-Type: application/json\r\n"
                         "Accept: application/json\r\n"
                         "User-Agent: VelaFit-ESP32P4/1.0\r\n"
                         "Content-Length: %lu\r\n"
                         "Connection: close\r\n\r\n",
                         api_key, (unsigned long)payload_len);
  if (request_len < 0 || request_len >= MIMO_REQUEST_MAX)
    {
      ret = -E2BIG;
      goto out;
    }

  ret = c6_tls_write_all(&tls->ssl, (FAR unsigned char *)request,
                         request_len);
  c6_secure_zero(request, MIMO_REQUEST_MAX);
  if (ret >= 0)
    {
      ret = c6_tls_write_all(&tls->ssl,
                             (FAR const unsigned char *)payload, payload_len);
    }
  if (ret < 0)
    {
      printf("MiMo HTTPS send FAIL: error=-0x%04x\n", -ret);
      goto out;
    }

  printf("MiMo timing upload_done_ms=%lu\n", c6_elapsed(&started));
  ret = c6_tls_read_response(&tls->ssl, response, MIMO_RESPONSE_MAX,
                             &response_len);
  if (ret < 0)
    {
      printf("MiMo HTTPS receive FAIL: error=-0x%04x\n", -ret);
      goto out;
    }

  printf("MiMo timing response_done_ms=%lu\n", c6_elapsed(&started));
  ret = c6_http_parse(response, response_len, &result->http_status,
                      &body, &body_len);
  if (ret < 0)
    {
      printf("MiMo HTTP parse FAIL: error=%d\n", ret);
      goto out;
    }

  printf("MiMo HTTP status=%d response_bytes=%lu\n",
         result->http_status, (unsigned long)body_len);
  if (result->http_status < 200 || result->http_status >= 300)
    {
      ret = c6_http_result_error(result->http_status);
      goto out;
    }

  ret = c6_mimo_parse_json(body, result);
  if (ret < 0)
    {
      printf("MiMo semantic validation FAIL: missing non-empty content\n");
      goto out;
    }

  ret = OK;

out:
  result->elapsed_ms = c6_elapsed(&started);
  if (request != NULL)
    {
      c6_secure_zero(request, MIMO_REQUEST_MAX);
      free(request);
    }

  if (response != NULL)
    {
      free(response);
    }

  if (tls != NULL)
    {
      if (tls_initialized)
        {
          c6_tls_free(tls);
        }
      free(tls);
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int c6_mimo_tls_probe(FAR const char *verify_hostname)
{
  FAR struct c6_tls_s *tls;
  int ret;

  tls = calloc(1, sizeof(*tls));
  if (tls == NULL)
    {
      return -ENOMEM;
    }

  ret = c6_tls_connect(tls, verify_hostname);
  c6_tls_free(tls);
  free(tls);
  return ret;
}

int c6_mimo_chat(FAR const char *api_key,
                 FAR struct c6_mimo_result_s *result)
{
  int attempt;
  int ret = -EINVAL;

  if (api_key == NULL || strncmp(api_key, "sk-", 3) != 0 ||
      result == NULL)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  for (attempt = 1; attempt <= MIMO_MAX_ATTEMPTS; attempt++)
    {
      printf("MiMo device attempt=%d/%d model=mimo-v2.5\n",
             attempt, MIMO_MAX_ATTEMPTS);
      ret = c6_mimo_attempt(api_key, g_mimo_body, strlen(g_mimo_body), result);
      if (ret == OK)
        {
          return OK;
        }

      /* Authentication and malformed-request errors are not transient. */

      if (result->http_status >= 400 && result->http_status < 500 &&
          result->http_status != 408 && result->http_status != 425 &&
          result->http_status != 429)
        {
          break;
        }

      if (attempt < MIMO_MAX_ATTEMPTS)
        {
          unsigned int delay = 1u << (attempt - 1);

          printf("MiMo transient failure; retry_in=%u seconds\n", delay);
          sleep(delay);
          memset(result, 0, sizeof(*result));
        }
    }

  return ret;
}

int c6_mimo_completion(const char *api_key, const char *payload,
                       struct c6_mimo_result_s *result)
{
  if (!api_key || strncmp(api_key, "sk-", 3) || strpbrk(api_key, "\r\n") ||
      !payload || strlen(payload) > 8192 || !result) return -EINVAL;
  memset(result, 0, sizeof(*result));
  /* One attempt; caller owns retry policy and uncertain-delivery handling. */
  return c6_mimo_attempt(api_key, payload, strlen(payload), result);
}

int c6_mimo_asr(FAR const char *api_key, FAR const int16_t *pcm,
                size_t frames, unsigned int rate,
                FAR struct c6_mimo_result_s *result)
{
  char *payload = NULL;
  size_t length = 0;
  int ret;
  if (!result) return -EINVAL;
  memset(result, 0, sizeof(*result));
  if (!api_key || strncmp(api_key, "sk-", 3) ||
      strpbrk(api_key, "\r\n")) return -EINVAL;
  ret = c6_asr_body(pcm, frames, rate, &payload, &length);
  if (ret < 0) return ret;
  /* One explicit upload per capture for now; no hidden repeated billing. */
  ret = c6_mimo_attempt(api_key, payload, length, result);
  c6_secure_zero(payload, length);
  free(payload);
  return ret;
}

static FAR char *c6_tts_payload(FAR const char *text, FAR const char *voice)
{
  FAR cJSON *root = cJSON_CreateObject();
  FAR cJSON *messages = cJSON_CreateArray();
  FAR cJSON *style = cJSON_CreateObject();
  FAR cJSON *speech = cJSON_CreateObject();
  FAR cJSON *audio = cJSON_CreateObject();
  FAR char *payload = NULL;

  if (root == NULL || messages == NULL || style == NULL || speech == NULL ||
      audio == NULL) goto out;
  cJSON_AddStringToObject(root, "model", "mimo-v2.5-tts");
  cJSON_AddItemToObject(root, "messages", messages);
  messages = NULL;

  cJSON_AddStringToObject(style, "role", "user");
  cJSON_AddStringToObject(style, "content",
                          "请使用自然、友好、简洁的健身教练语气，语速适中。");
  cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(root, "messages"),
                       style);
  style = NULL;

  cJSON_AddStringToObject(speech, "role", "assistant");
  cJSON_AddStringToObject(speech, "content", text);
  cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(root, "messages"),
                       speech);
  speech = NULL;

  cJSON_AddStringToObject(audio, "format", "pcm16");
  cJSON_AddStringToObject(audio, "voice", voice);
  cJSON_AddItemToObject(root, "audio", audio);
  audio = NULL;
  cJSON_AddBoolToObject(root, "stream", true);
  payload = cJSON_PrintUnformatted(root);

out:
  cJSON_Delete(audio);
  cJSON_Delete(speech);
  cJSON_Delete(style);
  cJSON_Delete(messages);
  cJSON_Delete(root);
  return payload;
}

int c6_mimo_tts_stream(FAR const char *api_key, FAR const char *text,
                       FAR const char *voice,
                       c6_mimo_pcm_callback_t callback, FAR void *arg,
                       FAR struct c6_mimo_tts_result_s *result)
{
  FAR struct c6_tls_s *tls = NULL;
  FAR char *payload = NULL;
  FAR char *request = NULL;
  FAR char *headers = NULL;
  FAR uint8_t *receive = NULL;
  struct c6_tts_stream_s stream = {0};
  struct c6_chunk_decoder_s decoder = {0};
  bool tls_initialized = false;
  bool chunked = false;
  size_t header_len = 0;
  size_t payload_len;
  int request_len;
  int ret = -EINVAL;

  if (result == NULL) return -EINVAL;
  memset(result, 0, sizeof(*result));
  clock_gettime(CLOCK_MONOTONIC, &stream.started);
  if (api_key == NULL || strncmp(api_key, "sk-", 3) != 0 ||
      strpbrk(api_key, "\r\n") != NULL || text == NULL || text[0] == '\0' ||
      strlen(text) > 2048 || voice == NULL || voice[0] == '\0' ||
      callback == NULL) return -EINVAL;

  payload = c6_tts_payload(text, voice);
  tls = calloc(1, sizeof(*tls));
  request = malloc(MIMO_REQUEST_MAX);
  headers = malloc(8192);
  receive = malloc(4096);
  stream.line = malloc(MIMO_SSE_MAX);
  stream.event = malloc(MIMO_SSE_MAX);
  if (payload == NULL || tls == NULL || request == NULL || headers == NULL ||
      receive == NULL || stream.line == NULL || stream.event == NULL)
    { ret = -ENOMEM; goto out; }

  payload_len = strlen(payload);
  stream.callback = callback;
  stream.arg = arg;
  stream.result = result;
  tls_initialized = true;
  ret = c6_tls_connect(tls, MIMO_HOST);
  if (ret < 0) goto out;
  printf("MiMo TTS request model=mimo-v2.5-tts voice=%s format=pcm16 stream=true payload_bytes=%lu\n",
         voice, (unsigned long)payload_len);

  request_len = snprintf(request, MIMO_REQUEST_MAX,
                         "POST " MIMO_PATH " HTTP/1.1\r\n"
                         "Host: " MIMO_HOST "\r\n"
                         "api-key: %s\r\n"
                         "Content-Type: application/json\r\n"
                         "Accept: text/event-stream\r\n"
                         "User-Agent: VelaFit-ESP32P4/1.0\r\n"
                         "Content-Length: %lu\r\n"
                         "Connection: close\r\n\r\n",
                         api_key, (unsigned long)payload_len);
  if (request_len < 0 || request_len >= MIMO_REQUEST_MAX)
    { ret = -E2BIG; goto out; }
  ret = c6_tls_write_all(&tls->ssl, (FAR const unsigned char *)request,
                         request_len);
  c6_secure_zero(request, MIMO_REQUEST_MAX);
  if (ret == 0)
    ret = c6_tls_write_all(&tls->ssl, (FAR const unsigned char *)payload,
                           payload_len);
  if (ret < 0) goto out;

  /* Read until the complete HTTP header is available.  Preserve any body
   * bytes returned by the same TLS read and pass them through the same
   * incremental decoder as all later bytes. */
  FAR char *header_end = NULL;
  while (header_end == NULL)
    {
      int got = mbedtls_ssl_read(&tls->ssl, receive, 4096);
      if (got == MBEDTLS_ERR_SSL_WANT_READ ||
          got == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
      if (got <= 0) { ret = got == 0 ? -ENODATA : got; goto out; }
      if (header_len + (size_t)got + 1 >= 8192)
        { ret = -E2BIG; goto out; }
      memcpy(headers + header_len, receive, got);
      header_len += got;
      headers[header_len] = '\0';
      header_end = strstr(headers, "\r\n\r\n");
    }

  if (sscanf(headers, "HTTP/%*u.%*u %d", &result->http_status) != 1)
    { ret = -EPROTO; goto out; }
  chunked = strstr(headers, "\r\nTransfer-Encoding: chunked\r\n") != NULL ||
            strstr(headers, "\r\ntransfer-encoding: chunked\r\n") != NULL;
  printf("MiMo TTS HTTP status=%d transfer=%s header_ms=%lu\n",
         result->http_status, chunked ? "chunked" : "identity",
         c6_elapsed(&stream.started));
  if (result->http_status < 200 || result->http_status >= 300)
    { ret = c6_http_result_error(result->http_status); goto out; }

  size_t body_offset = (header_end + 4) - headers;
  size_t body_bytes = header_len - body_offset;
  if (body_bytes > 0)
    {
      result->response_bytes += body_bytes;
      if (chunked)
        ret = c6_chunk_feed(&decoder, &stream,
                            (FAR const uint8_t *)headers + body_offset,
                            body_bytes);
      else
        {
          ret = 0;
          for (size_t i = 0; i < body_bytes && ret == 0; i++)
            ret = c6_tts_sse_byte(&stream, headers[body_offset + i]);
        }
      if (ret < 0) goto out;
    }

  while (!stream.done)
    {
      int got = mbedtls_ssl_read(&tls->ssl, receive, 4096);
      if (got > 0)
        {
          result->response_bytes += got;
          if (chunked) ret = c6_chunk_feed(&decoder, &stream, receive, got);
          else
            {
              ret = 0;
              for (int i = 0; i < got && ret == 0; i++)
                ret = c6_tts_sse_byte(&stream, receive[i]);
            }
          if (ret < 0) goto out;
          continue;
        }
      if (got == MBEDTLS_ERR_SSL_WANT_READ ||
          got == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
      if (got == 0 || got == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
      ret = got;
      goto out;
    }

  if (!stream.done || stream.has_pending_pcm || result->pcm_bytes == 0)
    { ret = -EPROTO; goto out; }
  ret = 0;

out:
  result->elapsed_ms = c6_elapsed(&stream.started);
  if (ret < 0)
    printf("MiMo TTS FAIL error=%d status=%d response_bytes=%lu pcm_bytes=%lu\n",
           ret, result->http_status, (unsigned long)result->response_bytes,
           (unsigned long)result->pcm_bytes);
  else
    printf("MiMo TTS STREAM_DONE status=%d first_pcm_ms=%lu elapsed_ms=%lu events=%u pcm_bytes=%lu\n",
           result->http_status, result->first_pcm_ms, result->elapsed_ms,
           result->audio_events, (unsigned long)result->pcm_bytes);
  free(stream.event);
  free(stream.line);
  free(receive);
  free(headers);
  if (request != NULL)
    {
      c6_secure_zero(request, MIMO_REQUEST_MAX);
      free(request);
    }
  if (payload != NULL)
    {
      c6_secure_zero(payload, strlen(payload));
      free(payload);
    }
  if (tls != NULL)
    {
      if (tls_initialized) c6_tls_free(tls);
      free(tls);
    }
  return ret;
}
