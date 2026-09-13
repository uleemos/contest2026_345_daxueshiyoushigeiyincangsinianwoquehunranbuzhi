/****************************************************************************
 * app/c6_wifi/c6_wifi_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <arch/board/board.h>
#include <netutils/ntpclient.h>

#include "c6_mimo_tls.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define C6_WIFI_VALID_TIME_MIN 1704067200 /* 2024-01-01 UTC */
#define C6_WIFI_NTP_WAIT_SEC   20

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void c6_wifi_secure_zero(FAR void *buffer, size_t size)
{
  FAR volatile unsigned char *cursor = buffer;

  while (size-- > 0)
    {
      *cursor++ = 0;
    }
}

static int c6_wifi_read_line(FAR const char *prompt, FAR char *buffer,
                             size_t size, bool secret)
{
  struct termios saved;
  struct termios noecho;
  bool changed = false;

  printf("%s", prompt);
  fflush(stdout);

  if (secret)
    {
      if (tcgetattr(STDIN_FILENO, &saved) < 0)
        {
          fprintf(stderr, "Cannot disable terminal echo: %d\n", errno);
          return -errno;
        }

      noecho = saved;
      noecho.c_lflag &= ~ECHO;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &noecho) < 0)
        {
          fprintf(stderr, "Cannot disable terminal echo: %d\n", errno);
          return -errno;
        }

      changed = true;
    }

  if (fgets(buffer, size, stdin) == NULL)
    {
      int ret = ferror(stdin) ? -EIO : -ECANCELED;

      if (changed)
        {
          tcsetattr(STDIN_FILENO, TCSANOW, &saved);
          printf("\n");
        }

      return ret;
    }

  if (changed)
    {
      tcsetattr(STDIN_FILENO, TCSANOW, &saved);
      printf("\n");
    }

  buffer[strcspn(buffer, "\r\n")] = '\0';
  return OK;
}

static int c6_wifi_tcp_probe(FAR const char *host, FAR const char *service)
{
  struct addrinfo hints;
  FAR struct addrinfo *addresses;
  FAR struct addrinfo *address;
  struct timeval timeout;
  long port;
  int sockfd;
  int ret;

  port = strtol(service, NULL, 10);
  if (port < 1 || port > 65535)
    {
      fprintf(stderr, "Invalid TCP port: %s\n", service);
      return 1;
    }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  ret = getaddrinfo(host, service, &hints, &addresses);
  if (ret != 0)
    {
      fprintf(stderr, "TCP probe DNS failed: host=%s error=%d\n", host,
              ret);
      return 1;
    }

  timeout.tv_sec = 10;
  timeout.tv_usec = 0;
  ret = 1;
  for (address = addresses; address != NULL; address = address->ai_next)
    {
      sockfd = socket(address->ai_family, address->ai_socktype,
                      address->ai_protocol);
      if (sockfd < 0)
        {
          continue;
        }

      setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                 sizeof(timeout));
      if (connect(sockfd, address->ai_addr, address->ai_addrlen) == 0)
        {
          printf("TCP connect PASS: host=%s port=%ld\n", host, port);
          ret = 0;
          close(sockfd);
          break;
        }

      close(sockfd);
    }

  freeaddrinfo(addresses);
  if (ret != 0)
    {
      fprintf(stderr, "TCP connect FAIL: host=%s port=%ld errno=%d\n",
              host, port, errno);
    }

  return ret;
}

static int c6_wifi_sync_time(void)
{
  time_t now;
  int ret;
  int seconds;

  now = time(NULL);
  if (now >= C6_WIFI_VALID_TIME_MIN)
    {
      printf("NTP time ready: epoch=%lld\n", (long long)now);
      return OK;
    }

  ret = ntpc_start();
  if (ret < 0)
    {
      fprintf(stderr, "NTP start FAIL: %d\n", ret);
      return ret;
    }

  for (seconds = 0; seconds < C6_WIFI_NTP_WAIT_SEC; seconds++)
    {
      sleep(1);
      now = time(NULL);
      if (now >= C6_WIFI_VALID_TIME_MIN)
        {
          printf("NTP time PASS: epoch=%lld wait_seconds=%d\n",
                 (long long)now, seconds + 1);
          return OK;
        }
    }

  fprintf(stderr, "NTP time FAIL: clock is not trustworthy\n");
  return -ETIMEDOUT;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
  char ssid[33];
  char password[65];
  char api_key[160];
  struct c6_mimo_result_s mimo_result;
  FAR const char *connect_ssid;
  FAR const char *connect_password;
  FAR const char *ifname;
  int ret;

  if (!((argc == 2 &&
         (strcmp(argv[1], "probe") == 0 ||
          strcmp(argv[1], "version") == 0 ||
          strcmp(argv[1], "connect") == 0 ||
          strcmp(argv[1], "tls") == 0 ||
          strcmp(argv[1], "mimo") == 0)) ||
        (argc == 3 && strcmp(argv[1], "tls") == 0) ||
        (argc == 4 &&
         (strcmp(argv[1], "connect") == 0 ||
          strcmp(argv[1], "tcp") == 0))))
    {
      fprintf(stderr,
              "Usage: c6_wifi {probe|version|connect "
              "[<ssid> <password>]|tcp <host> <port>|"
              "tls [<verify-host>]|mimo}\n");
      return 1;
    }

  ret = board_c6_wifi_initialize();
  if (ret < 0)
    {
      fprintf(stderr, "ESP32-C6 SDIO probe failed: %d\n", ret);
      return 1;
    }

  if (strcmp(argv[1], "probe") == 0)
    {
      printf("ESP32-C6 SDIO probe passed\n");
      return 0;
    }

  if (strcmp(argv[1], "tcp") == 0)
    {
      return c6_wifi_tcp_probe(argv[2], argv[3]);
    }

  if (strcmp(argv[1], "tls") == 0)
    {
      ret = c6_wifi_sync_time();
      if (ret < 0)
        {
          return 1;
        }

      ret = c6_mimo_tls_probe(argc == 3 ? argv[2] :
                              "api.xiaomimimo.com");
      return ret == OK ? 0 : 1;
    }

  if (strcmp(argv[1], "mimo") == 0)
    {
      ret = c6_wifi_sync_time();
      if (ret < 0)
        {
          return 1;
        }

      ret = c6_wifi_read_line("MiMo API key (hidden): ", api_key,
                              sizeof(api_key), true);
      if (ret < 0)
        {
          return 1;
        }

      if (strncmp(api_key, "sk-", 3) != 0)
        {
          c6_wifi_secure_zero(api_key, sizeof(api_key));
          fprintf(stderr, "MiMo API key rejected: ordinary sk- key required\n");
          return 1;
        }

      ret = c6_mimo_chat(api_key, &mimo_result);
      c6_wifi_secure_zero(api_key, sizeof(api_key));
      if (ret < 0)
        {
          fprintf(stderr, "MiMo device request FAIL: error=%d status=%d\n",
                  ret, mimo_result.http_status);
          return 1;
        }

      printf("MiMo device PASS: status=%d latency_ms=%lu "
             "reasoning_chars=%lu id=%s\n",
             mimo_result.http_status, mimo_result.elapsed_ms,
             (unsigned long)mimo_result.reasoning_chars,
             mimo_result.response_id);
      printf("MiMo response: %s\n", mimo_result.content);
      return 0;
    }

  if (strcmp(argv[1], "connect") == 0)
    {
      if (argc == 2)
        {
          ret = c6_wifi_read_line("Wi-Fi SSID: ", ssid, sizeof(ssid),
                                  false);
          if (ret < 0)
            {
              return 1;
            }

          ret = c6_wifi_read_line("Wi-Fi password (hidden): ", password,
                                  sizeof(password), true);
          if (ret < 0)
            {
              return 1;
            }

          connect_ssid = ssid;
          connect_password = password;
        }
      else
        {
          fprintf(stderr,
                  "WARNING: password argument may be visible; use "
                  "interactive 'c6_wifi connect' instead\n");
          connect_ssid = argv[2];
          connect_password = argv[3];
        }

      ret = board_c6_wifi_connect(connect_ssid, connect_password);
      c6_wifi_secure_zero(password, sizeof(password));
      if (ret < 0)
        {
          fprintf(stderr, "ESP32-C6 Wi-Fi connect failed: %d\n", ret);
          return 1;
        }

      ifname = board_c6_wifi_netdev_name();
      printf("ESP32-C6 Wi-Fi connected: SSID=%s netdev=%s\n",
             connect_ssid, ifname != NULL ? ifname : "(missing)");
      return 0;
    }

  ret = board_c6_wifi_version(&major, &minor, &patch);
  if (ret < 0)
    {
      fprintf(stderr, "ESP32-C6 Hosted RPC failed: %d\n", ret);
      return 1;
    }

  printf("ESP32-C6 Hosted firmware: %lu.%lu.%lu\n",
         (unsigned long)major, (unsigned long)minor,
         (unsigned long)patch);
  return 0;
}
