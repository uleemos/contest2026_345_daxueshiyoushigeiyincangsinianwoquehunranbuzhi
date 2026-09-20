/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>
#include <nuttx/video/fb.h>
#include <arch/board/board.h>

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>

#include <netutils/cJSON.h>
#include <netutils/netlib.h>
#include <netutils/ntpclient.h>

#include "c6_mimo_tls.h"
#include "velafit_coach_service.h"
#include "../es8311_audio/velafit_pcm_stream.h"
#include "../velafit_ai/render/velafit_render.h"

#define COACH_ADVICE_LIMIT 480

static char g_coach_api_key[160];
static char g_coach_wifi_ssid[33];
static char g_coach_wifi_password[65];
static bool g_coach_network_online;

static void coach_secure_zero(void *buffer, size_t size)
{
  volatile unsigned char *cursor = buffer;
  while (size-- > 0) *cursor++ = 0;
}

static int coach_network_connect(void)
{
  const char *ifname;
  time_t now;
  int ret;

  if (g_coach_network_online) return 0;
  if (g_coach_wifi_ssid[0] == '\0' || g_coach_wifi_password[0] == '\0')
    return -ENOKEY;

  printf("[VELAFIT] CLOUD_CONNECT phase=after-camera-release\n");
  for (unsigned int attempt = 1; attempt <= 3; attempt++)
    {
      ret = board_c6_wifi_initialize();
      if (ret == 0) break;
      printf("[VELAFIT] C6_INIT_RETRY attempt=%u ret=%d\n", attempt, ret);
      if (attempt < 3) sleep(1);
    }
  if (ret < 0) return ret;
  ret = board_c6_wifi_connect(g_coach_wifi_ssid, g_coach_wifi_password);
  if (ret < 0) return ret;
  ifname = board_c6_wifi_netdev_name();
  if (ifname == NULL) return -ENODEV;
  ret = netlib_ifup(ifname);
  if (ret < 0) return ret;
  ret = netlib_obtain_ipv4addr(ifname);
  if (ret < 0) return ret;

  now = time(NULL);
  if (now < 1704067200)
    {
      ret = ntpc_start();
      if (ret < 0) return ret;
      for (unsigned int second = 0; second < 20; second++)
        {
          sleep(1);
          if (time(NULL) >= 1704067200) break;
        }
      if (time(NULL) < 1704067200) return -ETIMEDOUT;
    }

  g_coach_network_online = true;
  coach_secure_zero(g_coach_wifi_password,
                    sizeof(g_coach_wifi_password));
  printf("[VELAFIT] CLOUD_CONNECT PASS netdev=%s\n", ifname);
  return 0;
}

static void coach_log_idle_heap(struct mallinfo before)
{
  struct mallinfo after = mallinfo();
  printf("[VELAFIT] COACH_HEAP phase=after used=%u free=%u largest=%u delta_used=%d\n",
         after.uordblks, after.fordblks, after.mxordblk,
         (int)after.uordblks - (int)before.uordblks);
  printf("[VELAFIT] wake=enabled\n");
}

static int coach_pcm(void *arg, const uint8_t *pcm, size_t bytes)
{
  return velafit_pcm_stream_write((struct velafit_pcm_stream_s *)arg,
                                  pcm, bytes);
}

static char *coach_request_json(const struct velafit_workout_summary_s *s)
{
  cJSON *root = cJSON_CreateObject();
  cJSON *messages;
  cJSON *message;
  cJSON *thinking;
  cJSON *summary;
  char *summary_text = NULL;
  char *payload = NULL;
  if (root == NULL) return NULL;

  cJSON_AddStringToObject(root, "model", "mimo-v2.5");
  cJSON_AddNumberToObject(root, "max_completion_tokens", 256);
  thinking = cJSON_AddObjectToObject(root, "thinking");
  cJSON_AddStringToObject(thinking, "type", "disabled");
  messages = cJSON_AddArrayToObject(root, "messages");
  message = cJSON_CreateObject();
  cJSON_AddStringToObject(message, "role", "system");
  cJSON_AddStringToObject(message, "content",
    "你是一名运动训练助手。请根据训练摘要用自然、友好的中文给出两到三条简短反馈，"
    "总长度控制在约100到150个中文字符，适合直接语音播报。不要使用Markdown、标题或"
    "医疗诊断，不要大段重复数据，也不要声称看到了摘要中没有的信息。"
    "form_warning_reps表示已计入completed_reps但带有姿态质量提醒的动作，不是额外失败次数。");
  cJSON_AddItemToArray(messages, message);

  summary = cJSON_CreateObject();
  cJSON_AddStringToObject(summary, "exercise", "squat");
  cJSON_AddNumberToObject(summary, "target_reps", s->target_reps);
  cJSON_AddNumberToObject(summary, "completed_reps", s->completed_reps);
  cJSON_AddNumberToObject(summary, "form_warning_reps",
                          s->form_warning_reps);
  cJSON_AddNumberToObject(summary, "shallow_warning_reps",
                          s->shallow_warning_reps);
  cJSON_AddNumberToObject(summary, "knee_caving_warning_reps",
                          s->knee_caving_warning_reps);
  cJSON_AddNumberToObject(summary, "trunk_lean_warning_reps",
                          s->trunk_lean_warning_reps);
  cJSON_AddNumberToObject(summary, "duration_sec", s->duration_sec);
  cJSON_AddNumberToObject(summary, "body_lost_count", s->body_lost_count);
  summary_text = cJSON_PrintUnformatted(summary);
  cJSON_Delete(summary);
  if (summary_text == NULL) goto out;

  message = cJSON_CreateObject();
  cJSON_AddStringToObject(message, "role", "user");
  cJSON_AddStringToObject(message, "content", summary_text);
  cJSON_AddItemToArray(messages, message);
  payload = cJSON_PrintUnformatted(root);
out:
  cJSON_free(summary_text);
  cJSON_Delete(root);
  return payload;
}

static int coach_lcd_begin(const struct velafit_workout_summary_s *s,
                           const char *status, const char *advice)
{
  struct fb_videoinfo_s vinfo = {0};
  struct fb_planeinfo_s pinfo = {0};
  velafit_canvas_t canvas;
  char line[80];
  int fd = open("/dev/fb0", O_RDWR);
  if (fd < 0) return -errno;
  if (ioctl(fd, FBIOGET_VIDEOINFO, (unsigned long)(uintptr_t)&vinfo) < 0 ||
      ioctl(fd, FBIOGET_PLANEINFO, (unsigned long)(uintptr_t)&pinfo) < 0)
    { int ret = -errno; close(fd); return ret; }
  if (vinfo.fmt != FB_FMT_RGB16_565 || pinfo.bpp != 16 || pinfo.fbmem == NULL)
    { close(fd); return -ENOTSUP; }

  velafit_canvas_init(&canvas, pinfo.fbmem, vinfo.xres, vinfo.yres,
                      VELAFIT_PIXFMT_RGB565);
  velafit_draw_rect_filled(&canvas, 0, 0, vinfo.xres, vinfo.yres,
                           VELAFIT_COLOR_DARKGRAY);
  velafit_draw_string(&canvas, 110, 55, "WORKOUT COMPLETE",
                      VELAFIT_COLOR_GREEN, VELAFIT_COLOR_DARKGRAY, 4);
  snprintf(line, sizeof(line), "%u SQUATS     %u SEC",
           s->completed_reps, s->duration_sec);
  velafit_draw_string(&canvas, 220, 145, line, VELAFIT_COLOR_WHITE,
                      VELAFIT_COLOR_DARKGRAY, 3);
  velafit_draw_string(&canvas, 330, 225, "AI COACH",
                      VELAFIT_COLOR_YELLOW, VELAFIT_COLOR_DARKGRAY, 4);
  velafit_draw_string(&canvas, 350, 290, status,
                      VELAFIT_COLOR_WHITE, VELAFIT_COLOR_DARKGRAY, 3);

  /* The current built-in 5x7 font has no CJK glyphs. Keep the real Chinese
   * advice for TTS and serial evidence instead of rendering mojibake. */
  (void)advice;
  close(fd);
  return 0;
}

int velafit_coach_run(const char *api_key,
                      const struct velafit_workout_summary_s *summary,
                      struct velafit_coach_result_s *result)
{
  struct c6_mimo_result_s advice = {0};
  struct c6_mimo_tts_result_s tts = {0};
  struct velafit_pcm_stream_s *player = NULL;
  struct velafit_pcm_stream_stats_s playback = {0};
  char *payload = NULL;
  struct mallinfo heap_before;
  int ret;

  if (api_key == NULL || summary == NULL || result == NULL) return -EINVAL;
  memset(result, 0, sizeof(*result));
  heap_before = mallinfo();
  printf("[VELAFIT] COACH_HEAP phase=before used=%u free=%u largest=%u\n",
         heap_before.uordblks, heap_before.fordblks, heap_before.mxordblk);
  printf("[VELAFIT] FINISHED -> AI_ANALYSIS\n");
  coach_lcd_begin(summary, "THINKING...", NULL);
  payload = coach_request_json(summary);
  if (payload == NULL)
    {
      coach_lcd_begin(summary, "AI SERVICE UNAVAILABLE", NULL);
      coach_log_idle_heap(heap_before);
      return -ENOMEM;
    }
  ret = c6_mimo_completion(api_key, payload, &advice);
  cJSON_free(payload);
  result->http_status = advice.http_status;
  result->advice_latency_ms = advice.elapsed_ms;
  if (ret < 0)
    {
      coach_lcd_begin(summary, "AI SERVICE UNAVAILABLE", NULL);
      printf("[VELAFIT] AI_ANALYSIS_FAIL http=%d ret=%d\n",
             advice.http_status, ret);
      coach_log_idle_heap(heap_before);
      return ret;
    }
  size_t length = strlen(advice.content);
  if (length == 0)
    {
      coach_lcd_begin(summary, "AI SERVICE UNAVAILABLE", NULL);
      printf("[VELAFIT] AI_ANALYSIS_FAIL http=%d ret=%d reason=empty\n",
             advice.http_status, -EBADMSG);
      coach_log_idle_heap(heap_before);
      return -EBADMSG;
    }
  if (length > COACH_ADVICE_LIMIT) length = COACH_ADVICE_LIMIT;
  memcpy(result->advice, advice.content, length);
  result->advice[length] = '\0';
  result->advice_bytes = length;
  printf("[VELAFIT] AI_ANALYSIS -> AI_RESULT http=%d latency_ms=%lu response_bytes=%lu\n",
         result->http_status, result->advice_latency_ms,
         (unsigned long)result->advice_bytes);
  printf("[VELAFIT] AI advice: %s\n", result->advice);
  coach_lcd_begin(summary, "", result->advice);

  printf("[VELAFIT] AI_RESULT -> AI_PLAYBACK\n");
  ret = velafit_pcm_stream_start(&player, 24000);
  if (ret < 0) goto tts_fail;
  ret = c6_mimo_tts_stream(api_key, result->advice, "mimo_default",
                           coach_pcm, player, &tts);
  if (ret < 0)
    {
      velafit_pcm_stream_abort(player);
      player = NULL;
      goto tts_fail;
    }
  ret = velafit_pcm_stream_finish(player, &playback);
  player = NULL;
  if (ret < 0) goto tts_fail;
  result->tts_first_pcm_ms = tts.first_pcm_ms;
  result->tts_elapsed_ms = tts.elapsed_ms;
  result->pcm_bytes = tts.pcm_bytes;
  printf("[VELAFIT] PLAYBACK_DONE -> IDLE first_pcm_ms=%lu pcm_bytes=%lu played_frames=%lu\n",
         result->tts_first_pcm_ms, (unsigned long)result->pcm_bytes,
         (unsigned long)playback.played_frames);
  coach_log_idle_heap(heap_before);
  return 0;

tts_fail:
  /* The LCD keeps the real advice.  Audio failure is non-fatal to result. */
  if (player != NULL) velafit_pcm_stream_abort(player);
  printf("[VELAFIT] TTS_FAIL ret=%d; AI_RESULT retained; -> IDLE\n", ret);
  coach_log_idle_heap(heap_before);
  return 0;
}

int velafit_coach_runtime_set_key(const char *api_key)
{
  if (api_key == NULL || strncmp(api_key, "sk-", 3) != 0 ||
      strpbrk(api_key, "\r\n") != NULL ||
      strlen(api_key) >= sizeof(g_coach_api_key)) return -EINVAL;
  memset(g_coach_api_key, 0, sizeof(g_coach_api_key));
  strlcpy(g_coach_api_key, api_key, sizeof(g_coach_api_key));
  return 0;
}

int velafit_coach_runtime_configure(const char *ssid, const char *password,
                                    const char *api_key)
{
  size_t ssid_len;
  size_t password_len;
  int ret;

  if (ssid == NULL || password == NULL || api_key == NULL) return -EINVAL;
  ssid_len = strlen(ssid);
  password_len = strlen(password);
  if (ssid_len == 0 || ssid_len >= sizeof(g_coach_wifi_ssid) ||
      password_len == 0 || password_len >= sizeof(g_coach_wifi_password))
    return -EINVAL;
  ret = velafit_coach_runtime_set_key(api_key);
  if (ret < 0) return ret;
  strlcpy(g_coach_wifi_ssid, ssid, sizeof(g_coach_wifi_ssid));
  strlcpy(g_coach_wifi_password, password,
          sizeof(g_coach_wifi_password));
  g_coach_network_online = false;
  printf("[VELAFIT] CLOUD_CONFIG ready=yes connect=deferred\n");
  return 0;
}

bool velafit_coach_runtime_ready(void)
{
  return strncmp(g_coach_api_key, "sk-", 3) == 0;
}

int velafit_coach_run_runtime(
                      const struct velafit_workout_summary_s *summary,
                      struct velafit_coach_result_s *result)
{
  int ret;
  if (!velafit_coach_runtime_ready()) return -ENOKEY;
  ret = coach_network_connect();
  if (ret < 0)
    {
      printf("[VELAFIT] CLOUD_CONNECT FAIL ret=%d; -> IDLE\n", ret);
      printf("[VELAFIT] wake=enabled\n");
      return ret;
    }
  return velafit_coach_run(g_coach_api_key, summary, result);
}
