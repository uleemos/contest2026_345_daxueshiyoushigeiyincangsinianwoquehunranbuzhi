/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "c6_queue_service.h"
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include "c6_mimo_tls.h"
#include "../velafit_ai/sync/velafit_outbox_service.h"
#include "../velafit_ai/sync/velafit_cloud_agent.h"
int microsd_outbox_open(void);
int microsd_outbox_close(void);
static pthread_mutex_t g_control = PTHREAD_MUTEX_INITIALIZER;
static struct vf_outbox_service *g_service;
static struct vf_outbox_service_stats g_last;
static char g_key[160];
static int g_state; /* 0 idle, 1 starting, 2 active, 3 stopping, 4 stopped */
static int g_error;
static bool g_stop;

static void erase_key(void)
{
  volatile char *p = g_key;
  for (unsigned i = 0; i < sizeof(g_key); i++) p[i] = 0;
}

static bool network_up(void)
{
  struct ifreq req = {0};
  strcpy(req.ifr_name, "eth0");
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return false;
  bool ready = ioctl(fd, SIOCGIFFLAGS, &req) == 0 &&
               (req.ifr_flags & IFF_UP) && (req.ifr_flags & IFF_RUNNING);
  close(fd);
  return ready;
}

static int transport(void *context, const char *request, char *out, size_t cap)
{
  struct c6_mimo_result_s *result = context;
  int ret = c6_mimo_completion(g_key, request, result);
  if (ret < 0) return ret;
  if (strlen(result->content) >= cap) return -EOVERFLOW;
  strcpy(out, result->content);
  return 0;
}

static int send_summary(void *context, const char *summary)
{
  (void)context;
  struct c6_mimo_result_s result = {0};
  velafit_mimo_prescription_t advice;
  int ret = velafit_cloud_agent_submit_workout_via(summary, &advice,
                                                 transport, &result);
  printf("OUTBOX real MiMo ret=%d HTTP=%d latency_ms=%lu schema=%s playback=disabled\n",
         ret, result.http_status, result.elapsed_ms, ret == 0 ? "PASS" : "FAIL");
  if (ret == 0)
    printf("OUTBOX advice session=%s score=%lu\n", advice.session_id,
           (unsigned long)advice.score_overall);
  return ret;
}

static int service_task(int argc, char **argv)
{
  (void)argc; (void)argv;
  bool mounted = false;
  int ret = microsd_outbox_open();
  if (ret < 0) goto done;
  mounted = true;
  struct vf_outbox_service *service = vf_outbox_service_create(
      "/sdcard/velafit-outbox", send_summary, NULL);
  if (!service) {ret = -ENOMEM; goto done;}
  pthread_mutex_lock(&g_control);
  g_service = service; g_state = g_stop ? 3 : 2;
  pthread_mutex_unlock(&g_control);
  printf("OUTBOX scheduler ready: persistent=SD capacity=4 playback=disabled\n");
  for (;;)
    {
      pthread_mutex_lock(&g_control);
      bool stop = g_stop;
      vf_outbox_service_status(service, &g_last);
      unsigned pending = g_last.pending_ram;
      pthread_mutex_unlock(&g_control);
      if (stop && !pending) break;
      bool online = !stop && network_up() && time(NULL) >= 1704067200;
      vf_outbox_service_online(service, online);
      vf_outbox_service_step(service, time(NULL));
      usleep(200000);
    }
  pthread_mutex_lock(&g_control);
  vf_outbox_service_status(service, &g_last);
  ret = vf_outbox_service_destroy(service);
  g_service = NULL;
  pthread_mutex_unlock(&g_control);
done:
  if (mounted)
    {
      int close_ret = microsd_outbox_close();
      if (ret == 0) ret = close_ret;
    }
  pthread_mutex_lock(&g_control);
  erase_key(); g_error = ret; g_state = 4;
  pthread_mutex_unlock(&g_control);
  printf("OUTBOX scheduler stopped ret=%d key_cleared=yes\n", ret);
  return ret < 0 ? 1 : 0;
}

int c6_queue_start(const char *key)
{
  if (!key || strncmp(key, "sk-", 3) || strlen(key) >= sizeof(g_key)) return -EINVAL;
  pthread_mutex_lock(&g_control);
  if (g_state >= 1 && g_state <= 3)
    {pthread_mutex_unlock(&g_control); return -EALREADY;}
  strcpy(g_key, key); g_stop = false; g_state = 1; g_error = 0;
  memset(&g_last, 0, sizeof(g_last));
  int pid = task_create("vf-cloud", 80, 32768, service_task, NULL);
  if (pid < 0) {g_error = -errno; g_state = 4; erase_key();}
  pthread_mutex_unlock(&g_control);
  printf("OUTBOX scheduler start pid=%d; key RAM-only, not stored on SD\n", pid);
  return pid < 0 ? g_error : 0;
}

int c6_queue_submit(const char *id, const char *summary)
{
  int ret = pthread_mutex_trylock(&g_control);
  if (ret) return -ret;
  ret = g_state == 2 && g_service && !g_stop ?
        vf_outbox_service_enqueue(g_service, id, summary) : -ESHUTDOWN;
  pthread_mutex_unlock(&g_control);
  return ret;
}

int c6_queue_stop(void)
{
  pthread_mutex_lock(&g_control);
  g_stop = true;
  if (g_state == 1 || g_state == 2) g_state = 3;
  pthread_mutex_unlock(&g_control);
  return 0;
}

int c6_queue_status(void)
{
  pthread_mutex_lock(&g_control);
  if (g_service) vf_outbox_service_status(g_service, &g_last);
  printf("OUTBOX status state=%d ram=%u persisted=%u sent=%u rejected=%u storage_failures=%u corrupt=%u io_errors=%u online=%d error=%d service_error=%d\n",
         g_state, g_last.pending_ram, g_last.persisted, g_last.sent,
         g_last.rejected, g_last.storage_failures, g_last.corrupt_records,
         g_last.io_errors, g_last.online, g_last.last_error, g_error);
  pthread_mutex_unlock(&g_control);
  return 0;
}
#else
#include <errno.h>
int c6_queue_start(const char *key) {(void)key; return -ENOSYS;}
int c6_queue_submit(const char *id,const char *s) {(void)id;(void)s;return -ENOSYS;}
int c6_queue_stop(void) {return -ENOSYS;}
int c6_queue_status(void) {return -ENOSYS;}
#endif
