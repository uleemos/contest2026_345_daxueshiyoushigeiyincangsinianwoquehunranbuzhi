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
#include "c6_queue_service.h"
#include "velafit_coach_service.h"
int microsd_readonly_identify(void);
int microsd_mount_readonly(void);
int microsd_file_test(bool readback);
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI
int microsd_queue_test(bool readback);
#endif
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI
#include "../velafit_ai/sync/velafit_cloud_agent.h"
#include "../velafit_ai/sync/velafit_outbox.h"
int microsd_cloud_queue_test(vf_outbox_sender sender, void *ctx, bool readback);
int microsd_outbox_ack(const char *id);

struct workout_transport_s
{
  const char *key;
  struct c6_mimo_result_s *result;
};

static int workout_transport(void *opaque, const char *payload,
                             char *content, size_t capacity)
{
  struct workout_transport_s *ctx = opaque;
  int ret = c6_mimo_completion(ctx->key, payload, ctx->result);
  if (ret != 0) return ret;
  if (strlen(ctx->result->content) >= capacity) return -EOVERFLOW;
  strcpy(content, ctx->result->content);
  return 0;
}

static int workout_queue_sender(void *opaque, const char *summary)
{
  velafit_mimo_prescription_t advice;
  int ret = velafit_cloud_agent_submit_workout_via(summary, &advice,
                                                 workout_transport, opaque);
  if (ret == 0)
    printf("Workout queue schema PASS session=%s score=%lu playback=disabled\n",
           advice.session_id, (unsigned long)advice.score_overall);
  return ret;
}
#endif
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI
#include "../velafit_ai/algo/velafit_voice_command.h"
#endif
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_ES8311_AUDIO
#include "../es8311_audio/velafit_mic_capture.h"
#include "../es8311_audio/velafit_pcm_stream.h"
#endif

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

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_ES8311_AUDIO
static int c6_wifi_tts_pcm(FAR void *arg, FAR const uint8_t *pcm,
                           size_t bytes)
{
  return velafit_pcm_stream_write((FAR struct velafit_pcm_stream_s *)arg,
                                  pcm, bytes);
}

static int c6_wifi_tts_test(FAR const char *api_key)
{
  static const char text[] =
    "训练完成，请注意保持膝盖与脚尖方向一致，稍作休息后再继续。";
  struct velafit_pcm_stream_s *player = NULL;
  struct velafit_pcm_stream_stats_s playback = {0};
  struct c6_mimo_tts_result_s tts = {0};
  int ret;

  printf("[TTS-TEST] START source=MiMo text_bytes=%lu file_output=disabled\n",
         (unsigned long)strlen(text));
  ret = velafit_pcm_stream_start(&player, 24000);
  if (ret < 0)
    {
      printf("[TTS-TEST] audio start FAIL ret=%d\n", ret);
      return ret;
    }

  ret = c6_mimo_tts_stream(api_key, text, "mimo_default",
                           c6_wifi_tts_pcm, player, &tts);
  if (ret < 0)
    {
      velafit_pcm_stream_abort(player);
      printf("[TTS-TEST] FAIL phase=cloud ret=%d http=%d\n",
             ret, tts.http_status);
      return ret;
    }

  ret = velafit_pcm_stream_finish(player, &playback);
  printf("[TTS-TEST] %s http=%d first_pcm_ms=%lu request_ms=%lu "
         "pcm_bytes=%lu played_frames=%lu underflows=%u\n",
         ret == 0 ? "PLAYBACK_DONE" : "FAIL", tts.http_status,
         tts.first_pcm_ms, tts.elapsed_ms,
         (unsigned long)tts.pcm_bytes,
         (unsigned long)playback.played_frames, playback.underflows);
  return ret;
}
#endif

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

  /* Isolated storage diagnostic: never initialize the C6 transport. */

  if (argc == 2 && strcmp(argv[1], "demo-config") == 0)
    {
      ret = c6_wifi_read_line("Wi-Fi SSID: ", ssid, sizeof(ssid), false);
      if (ret == 0)
        ret = c6_wifi_read_line("Wi-Fi password (hidden): ", password,
                                sizeof(password), true);
      if (ret == 0)
        ret = c6_wifi_read_line("MiMo API key (hidden): ", api_key,
                                sizeof(api_key), true);
      if (ret == 0)
        ret = velafit_coach_runtime_configure(ssid, password, api_key);
      c6_wifi_secure_zero(password, sizeof(password));
      c6_wifi_secure_zero(api_key, sizeof(api_key));
      printf("[VELAFIT] DEMO_CONFIG %s network_started=no\n",
             ret == 0 ? "PASS" : "FAIL");
      return ret < 0 ? 1 : 0;
    }

  if (argc == 2 && strcmp(argv[1], "queue-status") == 0)
    return c6_queue_status() < 0 ? 1 : 0;
  if (argc == 2 && strcmp(argv[1], "queue-stop") == 0)
    return c6_queue_stop() < 0 ? 1 : 0;
  if (argc == 3 && strcmp(argv[1], "queue-add") == 0)
    {
      char summary[256];
      /* This CLI deliberately creates a fixed summary, not a real workout. */
      if (strlen(argv[2]) > 31 || strspn(argv[2],
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != strlen(argv[2]))
        return 1;
      snprintf(summary, sizeof(summary), "{\"session_id\":\"%s\",\"exercise\":\"squat\",\"reps\":10,\"source\":\"fixed-test-summary\"}", argv[2]);
      ret = c6_queue_submit(argv[2], summary);
      printf("OUTBOX enqueue ret=%d receipt=RAM-only source=fixed-summary\n", ret);
      return ret < 0 ? 1 : 0;
    }
  if (argc == 2 && strcmp(argv[1], "sd-file-write") == 0)
    {
      return microsd_file_test(false) < 0 ? 1 : 0;
    }
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI
  if (argc==2 && strcmp(argv[1],"sd-queue-test")==0)
    return microsd_queue_test(false)<0?1:0;
  if (argc==2 && strcmp(argv[1],"sd-queue-replay")==0)
    return microsd_queue_test(true)<0?1:0;
  if (argc==2 && strcmp(argv[1],"sd-cloud-replay")==0)
    return microsd_cloud_queue_test(NULL, NULL, true)<0?1:0;
  if (argc==3 && strcmp(argv[1],"sd-outbox-ack")==0)
    return microsd_outbox_ack(argv[2])<0?1:0;
#endif
  if (argc == 2 && strcmp(argv[1], "sd-file-readback") == 0)
    {
      return microsd_file_test(true) < 0 ? 1 : 0;
    }
  if (argc == 2 && strcmp(argv[1], "sd-mount-ro") == 0)
    {
      return microsd_mount_readonly() < 0 ? 1 : 0;
    }
  if (argc == 2 && strcmp(argv[1], "sd-identify") == 0)
    {
      return microsd_readonly_identify() < 0 ? 1 : 0;
    }

  if (!((argc == 2 &&
         (strcmp(argv[1], "probe") == 0 ||
          strcmp(argv[1], "version") == 0 ||
          strcmp(argv[1], "connect") == 0 ||
          strcmp(argv[1], "tls") == 0 ||
          strcmp(argv[1], "mimo") == 0 ||
          strcmp(argv[1], "demo-config") == 0 ||
          strcmp(argv[1], "tts") == 0 ||
          strcmp(argv[1], "coach-test") == 0 ||
          strcmp(argv[1], "workout") == 0 ||
          strcmp(argv[1], "queue-workout") == 0 ||
          strcmp(argv[1], "queue-start") == 0 ||
          strcmp(argv[1], "asr") == 0 ||
          strcmp(argv[1], "asr-fixture") == 0)) ||
        (argc == 3 && strcmp(argv[1], "tls") == 0) ||
        (argc == 4 &&
         (strcmp(argv[1], "connect") == 0 ||
          strcmp(argv[1], "tcp") == 0))))
    {
      fprintf(stderr,
              "Usage: c6_wifi {probe|version|connect "
              "[<ssid> <password>]|tcp <host> <port>|"
              "tls [<verify-host>]|mimo|workout|queue-workout|queue-start|"
              "queue-status|queue-stop|queue-add <id>|asr|asr-fixture|tts|"
              "coach-test|demo-config}\n");
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

  if (strcmp(argv[1], "mimo") == 0 || strcmp(argv[1], "workout") == 0 ||
      strcmp(argv[1], "tts") == 0 ||
      strcmp(argv[1], "coach-test") == 0 ||
      strcmp(argv[1], "queue-workout") == 0 || strcmp(argv[1], "asr") == 0 ||
      strcmp(argv[1], "queue-start") == 0 ||
      strcmp(argv[1], "asr-fixture") == 0)
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

      memset(&mimo_result, 0, sizeof(mimo_result));
      if (strcmp(argv[1], "coach-test") == 0)
        {
          const struct velafit_workout_summary_s summary =
          {
            .target_reps = 20,
            .completed_reps = 20,
            .form_warning_reps = 2,
            .shallow_warning_reps = 1,
            .knee_caving_warning_reps = 1,
            .trunk_lean_warning_reps = 0,
            .duration_sec = 48,
            .body_lost_count = 1
          };
          struct velafit_coach_result_s coach;
          ret = velafit_coach_runtime_set_key(api_key);
          if (ret < 0)
            {
              c6_wifi_secure_zero(api_key, sizeof(api_key));
              return 1;
            }
          printf("[COACH-TEST] source=fixed-debug-summary target=20 completed=20 form_warnings=2 duration_sec=48\n");
          ret = velafit_coach_run(api_key, &summary, &coach);
          c6_wifi_secure_zero(api_key, sizeof(api_key));
          printf("[COACH-TEST] %s advice_http=%d advice_ms=%lu advice_bytes=%lu tts_first_pcm_ms=%lu pcm_bytes=%lu\n",
                 ret == 0 ? "PASS" : "FAIL", coach.http_status,
                 coach.advice_latency_ms, (unsigned long)coach.advice_bytes,
                 coach.tts_first_pcm_ms, (unsigned long)coach.pcm_bytes);
          return ret < 0 ? 1 : 0;
        }
      if (strcmp(argv[1], "tts") == 0)
        {
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_ES8311_AUDIO
          ret = c6_wifi_tts_test(api_key);
#else
          ret = -ENOSYS;
#endif
          c6_wifi_secure_zero(api_key, sizeof(api_key));
          return ret < 0 ? 1 : 0;
        }
      if (strcmp(argv[1], "queue-start") == 0)
        {
          ret = c6_queue_start(api_key);
          c6_wifi_secure_zero(api_key, sizeof(api_key));
          return ret < 0 ? 1 : 0;
        }
      if (strcmp(argv[1], "mimo") == 0)
        ret = c6_mimo_chat(api_key, &mimo_result);
      else if (strcmp(argv[1], "queue-workout") == 0)
        {
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI
          struct workout_transport_s ctx = {api_key, &mimo_result};
          ret = microsd_cloud_queue_test(workout_queue_sender, &ctx, false);
#else
          ret = -ENOSYS;
#endif
        }
      else if (strcmp(argv[1], "workout") == 0)
        {
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI
          struct workout_transport_s ctx = {api_key, &mimo_result};
          velafit_mimo_prescription_t advice;
          ret = velafit_cloud_agent_submit_workout_via(
              "{\"session_id\":\"VF-FIXED-001\",\"exercise\":\"squat\","
              "\"reps\":10,\"source\":\"fixed-test-summary\"}",
              &advice, workout_transport, &ctx);
          if (ret == 0)
            printf("Workout schema PASS session=%s score=%lu playback=disabled source=fixed-summary\n",
                   advice.session_id, (unsigned long)advice.score_overall);
          else if (mimo_result.http_status == 200)
            printf("Workout fixed-fixture schema rejected content: %s\n", mimo_result.content);
#else
          ret = -ENOSYS;
#endif
        }
      else
        {
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_ES8311_AUDIO
          size_t frames = 0;
          int16_t *recorded = NULL;
          const int16_t *samples;
          if (strcmp(argv[1], "asr-fixture") == 0)
            {
              samples = velafit_audio_fixture(&frames);
              ret = samples ? 0 : -ENOENT;
              printf("ASR source=synthetic-fixture execution=P4 model=mimo-v2.5-asr\n");
            }
          else
            {
              frames = 5 * 44100;
              recorded = malloc(frames * sizeof(*recorded));
              samples = recorded;
              printf("ASR source=microphone execution=P4 model=mimo-v2.5-asr\n"
                     "ASR RECORD NOW: 5 seconds after 1 second settling; cloud upload follows\n");
              fflush(stdout);
              ret = recorded ? velafit_mic_capture_pcm(recorded, 5) : -ENOMEM;
            }
          if (ret == 0)
            ret = c6_mimo_asr(api_key, samples, frames, 44100, &mimo_result);
          if (recorded)
            {
              c6_wifi_secure_zero(recorded, frames * sizeof(*recorded));
              free(recorded);
            }
#else
          ret = -ENOSYS;
#endif
        }
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
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI
      if (strcmp(argv[1], "asr") == 0 || strcmp(argv[1], "asr-fixture") == 0)
        printf("ASR command_class=%d (0=unknown,1=wake,2=start,3=pause,4=resume,5=stop); dispatch=not-connected\n",
               (int)velafit_voice_parse(mimo_result.content));
#endif
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
