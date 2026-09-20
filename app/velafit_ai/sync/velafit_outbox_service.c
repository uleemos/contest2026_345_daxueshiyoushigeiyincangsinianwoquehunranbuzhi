/* SPDX-License-Identifier: Apache-2.0 */
#include "velafit_outbox_service.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#define CAPACITY 4
struct entry { char id[32], summary[2049]; };
struct vf_outbox_service
{
  pthread_mutex_t mutex;
  struct entry queue[CAPACITY];
  unsigned head;
  char directory[192];
  vf_outbox_sender sender;
  void *context;
  int64_t storage_due;
  struct vf_outbox_service_stats stats;
};

struct vf_outbox_service *vf_outbox_service_create(const char *dir,
                                                   vf_outbox_sender sender,
                                                   void *context)
{
  if (!dir || !dir[0] || strlen(dir) >= 192 || !sender) return NULL;
  struct vf_outbox_service *s = calloc(1, sizeof(*s));
  if (!s) return NULL;
  if (pthread_mutex_init(&s->mutex, NULL)) {free(s); return NULL;}
  strcpy(s->directory, dir);
  s->sender = sender; s->context = context;
  /* Explicitly offline until the caller enables transport. */
  return s;
}

int vf_outbox_service_enqueue(struct vf_outbox_service *s, const char *id,
                              const char *summary)
{
  if (!s || !id || !id[0] || strlen(id) > 31 || !summary ||
      strlen(summary) > 2048) return -EINVAL;
  int ret = pthread_mutex_trylock(&s->mutex);
  if (ret) return -ret;
  if (s->stats.pending_ram == CAPACITY) ret = -EAGAIN;
  else
    {
      struct entry *e = &s->queue[(s->head + s->stats.pending_ram) % CAPACITY];
      strcpy(e->id, id); strcpy(e->summary, summary);
      s->stats.pending_ram++;
    }
  pthread_mutex_unlock(&s->mutex);
  return ret;
}

int vf_outbox_service_status(struct vf_outbox_service *s,
                             struct vf_outbox_service_stats *out)
{
  if (!s || !out) return -EINVAL;
  int ret = pthread_mutex_trylock(&s->mutex);
  if (ret) return -ret;
  *out = s->stats;
  pthread_mutex_unlock(&s->mutex);
  return 0;
}

int vf_outbox_service_online(struct vf_outbox_service *s, bool online)
{
  if (!s) return -EINVAL;
  int ret = pthread_mutex_trylock(&s->mutex);
  if (ret) return -ret;
  s->stats.online = online;
  pthread_mutex_unlock(&s->mutex);
  return 0;
}

int vf_outbox_service_step(struct vf_outbox_service *s, int64_t now)
{
  if (!s || now < 0 || now > 4102444800LL) return -EINVAL;
  struct entry entry;
  bool pending, online;
  pthread_mutex_lock(&s->mutex);
  pending = s->stats.pending_ram != 0;
  if (pending) entry = s->queue[s->head];
  online = s->stats.online;
  pthread_mutex_unlock(&s->mutex);
  int ret = 0;
  if (pending && now >= s->storage_due)
    {
      ret = vf_outbox_enqueue(s->directory, entry.id, entry.summary);
      pthread_mutex_lock(&s->mutex);
      s->stats.last_error = ret;
      if (!ret || ret == -EINVAL || ret == -EEXIST)
        {
          s->head = (s->head + 1) % CAPACITY;
          s->stats.pending_ram--;
          if (!ret) s->stats.persisted++;
          else s->stats.rejected++;
        }
      else
        {
          s->stats.storage_failures++;
          s->storage_due = now + 1;
        }
      pthread_mutex_unlock(&s->mutex);
      if (ret < 0) return ret;
    }
  if (online)
    {
      struct vf_outbox_scan scan;
      ret = vf_outbox_tick_report(s->directory, now, s->sender, s->context, &scan);
      pthread_mutex_lock(&s->mutex);
      s->stats.corrupt_records = scan.corrupt_records;
      s->stats.io_errors = scan.io_errors;
      s->stats.last_error = ret < 0 ? ret : 0;
      if (ret > 0) s->stats.sent++;
      pthread_mutex_unlock(&s->mutex);
    }
  return ret;
}

int vf_outbox_service_destroy(struct vf_outbox_service *s)
{
  if (!s) return 0;
  if (s->stats.pending_ram) return -EBUSY;
  pthread_mutex_destroy(&s->mutex);
  free(s);
  return 0;
}
