/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>

#include <nuttx/audio/audio.h>

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "velafit_pcm_stream.h"

#define PCM_DEVICE          "/dev/audio/pcm0"
#define PCM_RING_BYTES      (64 * 1024)
#define PCM_FRAMES_PER_APB  512
#define PCM_APB_COUNT       3
#define PCM_PREBUFFER_BYTES (12 * 1024)
#define PCM_WAIT_SECONDS    5
#define PCM_CODEC_RATE      44100

struct velafit_pcm_stream_s
{
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t can_read;
  pthread_cond_t can_write;
  pthread_cond_t ready_cond;
  uint8_t *ring;
  size_t read_pos;
  size_t write_pos;
  size_t used;
  size_t input_bytes;
  size_t output_bytes;
  size_t played_frames;
  unsigned int rate;
  uint32_t phase;
  int16_t previous;
  bool have_previous;
  unsigned int underflows;
  int result;
  bool ready;
  bool eof;
  bool abort;
};

static void deadline_after(struct timespec *ts, unsigned int seconds)
{
  clock_gettime(CLOCK_REALTIME, ts);
  ts->tv_sec += seconds;
}

static size_t ring_read_locked(struct velafit_pcm_stream_s *s,
                               uint8_t *dst, size_t bytes)
{
  size_t first = bytes;
  if (first > PCM_RING_BYTES - s->read_pos)
    first = PCM_RING_BYTES - s->read_pos;
  memcpy(dst, s->ring + s->read_pos, first);
  memcpy(dst + first, s->ring, bytes - first);
  s->read_pos = (s->read_pos + bytes) % PCM_RING_BYTES;
  s->used -= bytes;
  pthread_cond_broadcast(&s->can_write);
  return bytes;
}

static int fill_apb(struct velafit_pcm_stream_s *s, struct ap_buffer_s *apb,
                    unsigned int *queued_frames)
{
  uint8_t mono[PCM_FRAMES_PER_APB * 2];
  size_t bytes;

  pthread_mutex_lock(&s->lock);
  while (s->used < 2 && !s->eof && !s->abort)
    {
      s->underflows++;
      pthread_cond_wait(&s->can_read, &s->lock);
    }

  if (s->abort)
    {
      pthread_mutex_unlock(&s->lock);
      return -ECANCELED;
    }

  bytes = s->used;
  if (bytes > sizeof(mono)) bytes = sizeof(mono);
  bytes &= ~(size_t)1;
  if (bytes == 0)
    {
      pthread_mutex_unlock(&s->lock);
      return 0;
    }

  ring_read_locked(s, mono, bytes);
  pthread_mutex_unlock(&s->lock);

  int16_t *stereo = (int16_t *)apb->samp;
  unsigned int frames = bytes / 2;
  for (unsigned int i = 0; i < frames; i++)
    {
      /* Decode explicitly so behavior is independent of CPU alignment. */
      int16_t sample = (int16_t)((uint16_t)mono[2 * i] |
                                 (uint16_t)mono[2 * i + 1] << 8);
      stereo[2 * i] = sample;
      stereo[2 * i + 1] = sample;
    }

  apb->curbyte = 0;
  apb->flags = 0;
  apb->nbytes = frames * 4;
  *queued_frames = frames;
  return 1;
}

static void *pcm_worker(void *arg)
{
  struct velafit_pcm_stream_s *s = arg;
  struct audio_caps_desc_s caps = {0};
  struct audio_buf_desc_s desc = {0};
  struct ap_buffer_info_s info = {0};
  struct ap_buffer_s *buffers[PCM_APB_COUNT] = {0};
  unsigned int queued[PCM_APB_COUNT] = {0};
  struct mq_attr attr = {0};
  struct audio_msg_s msg;
  struct timespec deadline;
  char mqname[48];
  mqd_t mq = (mqd_t)-1;
  bool reserved = false;
  bool registered = false;
  bool started = false;
  int fd = -1;
  int ret = -EIO;
  const char *stage = "open";

  fd = open(PCM_DEVICE, O_WRONLY);
  if (fd < 0)
    {
      ret = -errno;
      goto out;
    }

  stage = "reserve";
  if (ioctl(fd, AUDIOIOC_RESERVE, 0) < 0) goto fail;
  reserved = true;

  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = AUDIO_TYPE_OUTPUT;
  caps.caps.ac_channels = 2;
  caps.caps.ac_controls.hw[0] = PCM_CODEC_RATE;
  caps.caps.ac_controls.b[2] = 16;
  stage = "configure-stream";
  if (ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&caps) < 0) goto fail;

  caps.caps.ac_type = AUDIO_TYPE_FEATURE;
  caps.caps.ac_format.hw = AUDIO_FU_VOLUME;
  caps.caps.ac_controls.hw[0] = 200;
  stage = "configure-volume";
  if (ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&caps) < 0) goto fail;

  snprintf(mqname, sizeof(mqname), "/velafit-pcm-%ld", (long)getpid());
  attr.mq_maxmsg = 8;
  attr.mq_msgsize = sizeof(msg);
  mq = mq_open(mqname, O_CREAT | O_EXCL | O_RDWR, 0600, &attr);
  if (mq == (mqd_t)-1) goto fail;
  stage = "register-mq";
  if (ioctl(fd, AUDIOIOC_REGISTERMQ, (unsigned long)mq) < 0) goto fail;
  registered = true;

  stage = "buffer-info";
  if (ioctl(fd, AUDIOIOC_GETBUFFERINFO, (unsigned long)&info) < 0 ||
      info.buffer_size < PCM_FRAMES_PER_APB * 4) goto fail;

  for (unsigned int i = 0; i < PCM_APB_COUNT; i++)
    {
      desc.numbytes = info.buffer_size;
      desc.u.pbuffer = &buffers[i];
      stage = "allocate-buffer";
      if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (unsigned long)&desc) < 0 ||
          buffers[i] == NULL ||
          buffers[i]->nmaxbytes < PCM_FRAMES_PER_APB * 4) goto fail;
    }

  pthread_mutex_lock(&s->lock);
  s->ready = true;
  pthread_cond_broadcast(&s->ready_cond);
  while (s->used < PCM_PREBUFFER_BYTES && !s->eof && !s->abort)
    pthread_cond_wait(&s->can_read, &s->lock);
  pthread_mutex_unlock(&s->lock);

  if (s->abort)
    {
      ret = -ECANCELED;
      goto out;
    }

  unsigned int active = 0;
  for (unsigned int i = 0; i < PCM_APB_COUNT; i++)
    {
      int filled = fill_apb(s, buffers[i], &queued[i]);
      if (filled < 0) { ret = filled; goto out; }
      if (filled == 0) break;
      desc.u.buffer = buffers[i];
      desc.numbytes = buffers[i]->nbytes;
      stage = "prime";
      if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&desc) < 0)
        goto fail;
      active++;
    }

  if (active == 0)
    {
      ret = -ENODATA;
      goto out;
    }

  stage = "start";
  if (ioctl(fd, AUDIOIOC_START, 0) < 0) goto fail;
  started = true;
  printf("[PCM-STREAM] PLAYBACK_START input_rate=%u codec_rate=%u resample=linear bits=16 input_channels=1 output_channels=2 prebuffer=%u\n",
         s->rate, PCM_CODEC_RATE, PCM_PREBUFFER_BYTES);

  while (active > 0)
    {
      deadline_after(&deadline, PCM_WAIT_SECONDS);
      stage = "wait-dequeue";
      ssize_t got;
      do
        got = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL, &deadline);
      while (got < 0 && errno == EINTR);
      if (got < 0) goto fail;
      if (got != sizeof(msg)) { ret = -EPROTO; goto out; }
      if (msg.msg_id == AUDIO_MSG_COMPLETE) continue;
      if (msg.msg_id != AUDIO_MSG_DEQUEUE) continue;

      unsigned int slot;
      for (slot = 0; slot < PCM_APB_COUNT; slot++)
        if (msg.u.ptr == buffers[slot]) break;
      if (slot == PCM_APB_COUNT || queued[slot] == 0)
        { ret = -EPROTO; goto out; }

      s->played_frames += queued[slot];
      queued[slot] = 0;
      active--;
      int filled = fill_apb(s, buffers[slot], &queued[slot]);
      if (filled < 0) { ret = filled; goto out; }
      if (filled > 0)
        {
          desc.u.buffer = buffers[slot];
          desc.numbytes = buffers[slot]->nbytes;
          stage = "refill";
          if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&desc) < 0)
            goto fail;
          active++;
        }
    }

  usleep(30000);
  ret = 0;
  goto out;

fail:
  ret = errno ? -errno : -EIO;
out:
  if (started) ioctl(fd, AUDIOIOC_STOP, 0);
  if (fd >= 0)
    {
      for (unsigned int i = 0; i < PCM_APB_COUNT; i++)
        if (buffers[i] != NULL)
          {
            desc.u.buffer = buffers[i];
            ioctl(fd, AUDIOIOC_FREEBUFFER, (unsigned long)&desc);
          }
      if (registered) ioctl(fd, AUDIOIOC_UNREGISTERMQ, (unsigned long)mq);
      if (reserved) ioctl(fd, AUDIOIOC_RELEASE, 0);
      close(fd);
    }
  if (mq != (mqd_t)-1)
    {
      mq_close(mq);
      mq_unlink(mqname);
    }

  pthread_mutex_lock(&s->lock);
  s->result = ret;
  s->ready = true;
  s->abort = s->abort || ret < 0;
  pthread_cond_broadcast(&s->ready_cond);
  pthread_cond_broadcast(&s->can_write);
  pthread_cond_broadcast(&s->can_read);
  pthread_mutex_unlock(&s->lock);
  printf("[PCM-STREAM] %s stage=%s input_bytes=%lu output_bytes=%lu played_frames=%lu underflows=%u ret=%d\n",
         ret == 0 ? "PLAYBACK_DONE" : "PLAYBACK_FAIL", stage,
         (unsigned long)s->input_bytes, (unsigned long)s->output_bytes,
         (unsigned long)s->played_frames,
         s->underflows, ret);
  return NULL;
}

int velafit_pcm_stream_start(struct velafit_pcm_stream_s **out,
                             unsigned int sample_rate)
{
  struct velafit_pcm_stream_s *s;
  pthread_attr_t attr;
  int ret;

  if (out == NULL || sample_rate != 24000) return -EINVAL;
  *out = NULL;
  s = calloc(1, sizeof(*s));
  if (s == NULL) return -ENOMEM;
  s->ring = malloc(PCM_RING_BYTES);
  if (s->ring == NULL) { free(s); return -ENOMEM; }
  s->rate = sample_rate;
  pthread_mutex_init(&s->lock, NULL);
  pthread_cond_init(&s->can_read, NULL);
  pthread_cond_init(&s->can_write, NULL);
  pthread_cond_init(&s->ready_cond, NULL);
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 12288);
  ret = pthread_create(&s->thread, &attr, pcm_worker, s);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      free(s->ring);
      pthread_cond_destroy(&s->ready_cond);
      pthread_cond_destroy(&s->can_write);
      pthread_cond_destroy(&s->can_read);
      pthread_mutex_destroy(&s->lock);
      free(s);
      return -ret;
    }

  pthread_mutex_lock(&s->lock);
  while (!s->ready) pthread_cond_wait(&s->ready_cond, &s->lock);
  ret = s->abort ? s->result : 0;
  pthread_mutex_unlock(&s->lock);
  if (ret < 0)
    {
      pthread_join(s->thread, NULL);
      free(s->ring);
      pthread_cond_destroy(&s->ready_cond);
      pthread_cond_destroy(&s->can_write);
      pthread_cond_destroy(&s->can_read);
      pthread_mutex_destroy(&s->lock);
      free(s);
      return ret;
    }

  *out = s;
  return 0;
}

int velafit_pcm_stream_write(struct velafit_pcm_stream_s *s,
                             const uint8_t *pcm, size_t bytes)
{
  if (s == NULL || pcm == NULL || bytes == 0 || (bytes & 1)) return -EINVAL;
  pthread_mutex_lock(&s->lock);
  s->input_bytes += bytes;
  for (size_t offset = 0; offset < bytes; offset += 2)
    {
      int16_t current = (int16_t)((uint16_t)pcm[offset] |
                                  (uint16_t)pcm[offset + 1] << 8);
      if (!s->have_previous)
        {
          s->previous = current;
          s->have_previous = true;
          continue;
        }

      while (s->phase < PCM_CODEC_RATE)
        {
          int32_t delta = (int32_t)current - s->previous;
          int16_t sample = (int16_t)(s->previous +
                           delta * (int32_t)s->phase / PCM_CODEC_RATE);
          while (PCM_RING_BYTES - s->used < 2 && !s->abort)
            pthread_cond_wait(&s->can_write, &s->lock);
          if (s->abort)
            {
              int ret = s->result < 0 ? s->result : -ECANCELED;
              pthread_mutex_unlock(&s->lock);
              return ret;
            }
          s->ring[s->write_pos] = sample & 0xff;
          s->ring[(s->write_pos + 1) % PCM_RING_BYTES] =
            ((uint16_t)sample >> 8) & 0xff;
          s->write_pos = (s->write_pos + 2) % PCM_RING_BYTES;
          s->used += 2;
          s->output_bytes += 2;
          s->phase += s->rate;
        }
      s->phase -= PCM_CODEC_RATE;
      s->previous = current;
      pthread_cond_broadcast(&s->can_read);
    }
  pthread_mutex_unlock(&s->lock);
  return 0;
}

static int stream_close(struct velafit_pcm_stream_s *s, bool abort,
                        struct velafit_pcm_stream_stats_s *stats)
{
  if (s == NULL) return -EINVAL;
  pthread_mutex_lock(&s->lock);
  s->eof = true;
  s->abort = s->abort || abort;
  pthread_cond_broadcast(&s->can_read);
  pthread_cond_broadcast(&s->can_write);
  pthread_mutex_unlock(&s->lock);
  pthread_join(s->thread, NULL);
  if (stats != NULL)
    {
      stats->input_bytes = s->input_bytes;
      stats->played_frames = s->played_frames;
      stats->underflows = s->underflows;
    }
  int ret = s->result;
  free(s->ring);
  pthread_cond_destroy(&s->ready_cond);
  pthread_cond_destroy(&s->can_write);
  pthread_cond_destroy(&s->can_read);
  pthread_mutex_destroy(&s->lock);
  free(s);
  return ret;
}

int velafit_pcm_stream_finish(struct velafit_pcm_stream_s *s,
                              struct velafit_pcm_stream_stats_s *stats)
{
  return stream_close(s, false, stats);
}

void velafit_pcm_stream_abort(struct velafit_pcm_stream_s *s)
{
  if (s != NULL) (void)stream_close(s, true, NULL);
}
