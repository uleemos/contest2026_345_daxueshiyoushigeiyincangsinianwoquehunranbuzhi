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

  ret = mbedtls_ssl_set_hostname(&tls->ssl, verify_hostname);
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
      printf("TLS handshake FAIL: verify_host=%s error=-0x%04x\n",
             verify_hostname, -ret);
      return ret;
    }

  verify_flags = mbedtls_ssl_get_verify_result(&tls->ssl);
  if (verify_flags != 0)
    {
      printf("TLS certificate FAIL: verify_host=%s flags=0x%08lx\n",
             verify_hostname, (unsigned long)verify_flags);
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
      ret = mbedtls_ssl_write(ssl, buffer + offset, length - offset);
      if (ret > 0)
        {
          offset += ret;
        }
      else if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
               ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          return ret;
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
                           FAR struct c6_mimo_result_s *result)
{
  FAR struct c6_tls_s *tls;
  FAR char *request;
  FAR char *response;
  FAR char *body;
  struct timespec started;
  struct timespec ended;
  int64_t elapsed_ms;
  size_t response_len;
  size_t body_len;
  int request_len;
  int ret;

  tls = calloc(1, sizeof(*tls));
  request = malloc(MIMO_REQUEST_MAX);
  response = malloc(MIMO_RESPONSE_MAX);
  if (tls == NULL || request == NULL || response == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  clock_gettime(CLOCK_MONOTONIC, &started);
  ret = c6_tls_connect(tls, MIMO_HOST);
  if (ret < 0)
    {
      goto out;
    }

  request_len = snprintf(request, MIMO_REQUEST_MAX,
                         "POST " MIMO_PATH " HTTP/1.1\r\n"
                         "Host: " MIMO_HOST "\r\n"
                         "api-key: %s\r\n"
                         "Content-Type: application/json\r\n"
                         "Accept: application/json\r\n"
                         "User-Agent: VelaFit-ESP32P4/1.0\r\n"
                         "Content-Length: %lu\r\n"
                         "Connection: close\r\n\r\n%s",
                         api_key, (unsigned long)strlen(g_mimo_body),
                         g_mimo_body);
  if (request_len < 0 || request_len >= MIMO_REQUEST_MAX)
    {
      ret = -E2BIG;
      goto out;
    }

  ret = c6_tls_write_all(&tls->ssl, (FAR unsigned char *)request,
                         request_len);
  c6_secure_zero(request, MIMO_REQUEST_MAX);
  if (ret < 0)
    {
      printf("MiMo HTTPS send FAIL: error=-0x%04x\n", -ret);
      goto out;
    }

  ret = c6_tls_read_response(&tls->ssl, response, MIMO_RESPONSE_MAX,
                             &response_len);
  if (ret < 0)
    {
      printf("MiMo HTTPS receive FAIL: error=-0x%04x\n", -ret);
      goto out;
    }

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
      ret = -EREMOTEIO;
      goto out;
    }

  ret = c6_mimo_parse_json(body, result);
  if (ret < 0)
    {
      printf("MiMo semantic validation FAIL: missing non-empty content\n");
      goto out;
    }

  clock_gettime(CLOCK_MONOTONIC, &ended);
  elapsed_ms = (int64_t)(ended.tv_sec - started.tv_sec) * 1000 +
               (ended.tv_nsec - started.tv_nsec) / 1000000;
  result->elapsed_ms = elapsed_ms > 0 ? (unsigned long)elapsed_ms : 0;
  ret = OK;

out:
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
      c6_tls_free(tls);
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
      ret = c6_mimo_attempt(api_key, result);
      if (ret == OK)
        {
          return OK;
        }

      /* Authentication and malformed-request errors are not transient. */

      if (result->http_status >= 400 && result->http_status < 500 &&
          result->http_status != 408 && result->http_status != 429)
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
