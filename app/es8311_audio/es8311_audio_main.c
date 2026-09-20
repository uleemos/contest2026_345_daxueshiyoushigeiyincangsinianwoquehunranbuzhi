/****************************************************************************
 * app/es8311_audio/es8311_audio_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <mqueue.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>
#include <nuttx/i2c/i2c_master.h>

#if defined(CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI) && __has_include("kws_acoustic_fixture.h")
#include "../velafit_ai/algo/velafit_kws_acoustic.h"
#include "kws_acoustic_fixture.h"
#define ACOUSTIC_KWS_AVAILABLE 1
#if __has_include("kws_v3_fixture.h")
#include "kws_v3_fixture.h"
#define KWS_V3_AVAILABLE 1
#if __has_include("kws_bank_fixture.h")
#include "kws_bank_fixture.h"
#define KWS_BANK_AVAILABLE 1
#if __has_include("kws_live_bank_fixture.h")
#include "kws_live_bank_fixture.h"
#define KWS_LIVE_BANK_AVAILABLE 1
#endif
#endif
#endif
#endif

#if __has_include("voice_fixture.h")
#  include "voice_fixture.h"
#else
#  define VOICE_FIXTURE_AVAILABLE 0
#  define VOICE_FIXTURE_FRAMES 0u
static const int16_t g_voice_pcm[1] = {0};
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ES8311_I2C_BUS      "/dev/i2c0"
#define ES8311_I2C_ADDR     0x18
#define ES8311_PCM_OUT      "/dev/audio/pcm0"
#define ES8311_PCM_IN       "/dev/audio/pcm_in0"

#define ES8311_REG_RESET    0x00
#define ES8311_REG_CLK_MGR1 0x01
#define ES8311_REG_ADC_DAC  0x14
#define ES8311_REG_CHIPID1  0xfd
#define ES8311_REG_CHIPID2  0xfe
#define ES8311_REG_CHIPID3  0xff

#define BUFFER_SAMPLES      1024
#define SAMPLE_RATE_DEFAULT 44100
#define CHANNELS_DEFAULT    2

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: es8311_i2c_read
 ****************************************************************************/

static int es8311_i2c_read(int fd, uint8_t reg, uint8_t *val)
{
  struct i2c_msg_s msg[2];
  struct i2c_transfer_s xfer;
  int ret;

  msg[0].frequency = 100000;
  msg[0].addr      = ES8311_I2C_ADDR;
  msg[0].flags     = 0;
  msg[0].buffer    = &reg;
  msg[0].length    = 1;

  msg[1].frequency = 100000;
  msg[1].addr      = ES8311_I2C_ADDR;
  msg[1].flags     = I2C_M_READ;
  msg[1].buffer    = val;
  msg[1].length    = 1;

  xfer.msgv = msg;
  xfer.msgc = 2;

  ret = ioctl(fd, I2CIOC_TRANSFER, (unsigned long)&xfer);
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: do_probe
 ****************************************************************************/

static int do_probe(void)
{
  int fd;
  uint8_t id1 = 0;
  uint8_t id2 = 0;
  uint8_t id3 = 0;
  uint8_t reset = 0;
  uint8_t clk = 0;
  struct stat st;
  int ret;

  printf("[ES8311] Probing I2C bus: %s at addr 0x%02x\n",
         ES8311_I2C_BUS, ES8311_I2C_ADDR);

  fd = open(ES8311_I2C_BUS, O_RDWR);
  if (fd < 0)
    {
      printf("ERROR: Failed to open %s: %d\n", ES8311_I2C_BUS, errno);
      return -errno;
    }

  ret = es8311_i2c_read(fd, ES8311_REG_CHIPID1, &id1);
  if (ret >= 0)
    {
      ret = es8311_i2c_read(fd, ES8311_REG_CHIPID2, &id2);
    }

  if (ret >= 0)
    {
      ret = es8311_i2c_read(fd, ES8311_REG_CHIPID3, &id3);
    }

  if (ret < 0)
    {
      printf("ERROR: I2C read failed from ES8311 (ret=%d, errno=%d)\n",
             ret, errno);
      close(fd);
      return ret;
    }

  es8311_i2c_read(fd, ES8311_REG_RESET, &reset);
  es8311_i2c_read(fd, ES8311_REG_CLK_MGR1, &clk);
  close(fd);

  printf("[ES8311] Chip ID: 0x%02X 0x%02X 0x%02X\n", id1, id2, id3);
  printf("[ES8311] REG00(Reset)=0x%02X, REG01(ClkMgr1)=0x%02X\n",
         reset, clk);

  if (id1 == 0x83 && id2 == 0x11)
    {
      printf("[ES8311] MATCH: Everest Semi ES8311 Codec Detected!\n");
    }
  else
    {
      printf("[ES8311] WARNING: Unexpected Chip ID!\n");
    }

  /* Check Audio Device Nodes */

  if (stat(ES8311_PCM_OUT, &st) == 0)
    {
      printf("[ES8311] Playback device node: %s [OK]\n", ES8311_PCM_OUT);
    }
  else
    {
      printf("[ES8311] Playback device node: %s [NOT FOUND]\n",
             ES8311_PCM_OUT);
    }

  if (stat(ES8311_PCM_IN, &st) == 0)
    {
      printf("[ES8311] Record device node:   %s [OK]\n", ES8311_PCM_IN);
    }
  else
    {
      printf("[ES8311] Record device node:   %s [NOT FOUND]\n",
             ES8311_PCM_IN);
    }

  return OK;
}

/****************************************************************************
 * Name: do_dump
 ****************************************************************************/

static int do_dump(void)
{
  int fd;
  uint8_t val;
  int i;
  int ret;

  fd = open(ES8311_I2C_BUS, O_RDWR);
  if (fd < 0)
    {
      printf("ERROR: Failed to open %s: %d\n", ES8311_I2C_BUS, errno);
      return -errno;
    }

  printf("\n--- ES8311 Register Dump (0x00 .. 0x47) ---\n");
  for (i = 0; i <= 0x47; i++)
    {
      if (i % 16 == 0)
        {
          printf("\n%02X: ", i);
        }

      ret = es8311_i2c_read(fd, (uint8_t)i, &val);
      if (ret < 0)
        {
          printf("XX ");
        }
      else
        {
          printf("%02X ", val);
        }
    }

  printf("\n\n--- End of Dump ---\n");
  close(fd);
  return OK;
}

/****************************************************************************
 * Name: do_tone
 ****************************************************************************/

static int do_tone(int freq, int duration_sec)
{
  int fd;
  struct audio_caps_desc_s cap_desc;
  struct ap_buffer_info_s buf_info;
  struct ap_buffer_s *apb;
  struct audio_buf_desc_s buf_desc;
  int16_t *samples;
  int total_frames;
  int frames_sent = 0;
  int i;
  double phase = 0.0;
  double phase_inc;
  int ret;

  if (freq <= 0)
    {
      freq = 1000;
    }

  if (duration_sec <= 0)
    {
      duration_sec = 2;
    }

  printf("[ES8311] Sine: %d Hz for %d sec (44.1kHz 16-bit stereo)\n",
         freq, duration_sec);

  fd = open(ES8311_PCM_OUT, O_WRONLY);
  if (fd < 0)
    {
      printf("ERROR: Failed to open %s: %d\n", ES8311_PCM_OUT, errno);
      return -errno;
    }

  /* 1. Configure audio format */

  memset(&cap_desc, 0, sizeof(cap_desc));
  cap_desc.caps.ac_len            = sizeof(struct audio_caps_s);
  cap_desc.caps.ac_type           = AUDIO_TYPE_OUTPUT;
  cap_desc.caps.ac_subtype        = AUDIO_TYPE_QUERY;
  cap_desc.caps.ac_channels       = CHANNELS_DEFAULT;
  cap_desc.caps.ac_controls.hw[0] = SAMPLE_RATE_DEFAULT;
  cap_desc.caps.ac_controls.b[2]  = 16; /* 16 bits */

  ret = ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&cap_desc);
  if (ret < 0)
    {
      printf("WARNING: AUDIOIOC_CONFIGURE returned %d\n", ret);
    }

  /* Set volume to 85% */

  cap_desc.caps.ac_subtype        = AUDIO_FU_VOLUME;
  cap_desc.caps.ac_controls.hw[0] = 850;
  ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&cap_desc);

  /* 2. Allocate buffer */

  memset(&buf_info, 0, sizeof(buf_info));
  buf_info.buffer_size = BUFFER_SAMPLES * CHANNELS_DEFAULT * sizeof(int16_t);
  buf_info.nbuffers    = 1;

  memset(&buf_desc, 0, sizeof(buf_desc));
  buf_desc.u.pbuffer   = &apb;

  ret = ioctl(fd, AUDIOIOC_ALLOCBUFFER, (unsigned long)&buf_desc);
  if (ret < 0 || apb == NULL)
    {
      printf("ERROR: AUDIOIOC_ALLOCBUFFER failed: %d\n", ret);
      close(fd);
      return ret;
    }

  /* 3. Start playback session */

  ret = ioctl(fd, AUDIOIOC_START, 0);
  if (ret < 0)
    {
      printf("WARNING: AUDIOIOC_START returned %d\n", ret);
    }

  total_frames = SAMPLE_RATE_DEFAULT * duration_sec;
  phase_inc = 2.0 * M_PI * (double)freq / (double)SAMPLE_RATE_DEFAULT;
  samples = (int16_t *)apb->samp;

  while (frames_sent < total_frames)
    {
      int chunk = BUFFER_SAMPLES;
      if (frames_sent + chunk > total_frames)
        {
          chunk = total_frames - frames_sent;
        }

      for (i = 0; i < chunk; i++)
        {
          int16_t sample = (int16_t)(sin(phase) * 20000.0);
          samples[i * 2]     = sample; /* Left */
          samples[i * 2 + 1] = sample; /* Right */
          phase += phase_inc;
          if (phase >= 2.0 * M_PI)
            {
              phase -= 2.0 * M_PI;
            }
        }

      apb->nbytes = chunk * CHANNELS_DEFAULT * sizeof(int16_t);
      buf_desc.u.buffer = apb;

      ret = ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&buf_desc);
      if (ret < 0)
        {
          printf("ERROR: Enqueue buffer failed: %d\n", ret);
          break;
        }

      frames_sent += chunk;
      usleep(chunk * 1000000UL / SAMPLE_RATE_DEFAULT);
    }

  /* 4. Stop and cleanup */

  ioctl(fd, AUDIOIOC_STOP, 0);
  ioctl(fd, AUDIOIOC_FREEBUFFER, (unsigned long)&buf_desc);
  close(fd);

  printf("[ES8311] Tone playback completed (%d frames played)\n",
         frames_sent);
  return OK;
}

/****************************************************************************
 * Name: do_record
 ****************************************************************************/

static int do_record(int duration_sec, FAR const char *filepath)
{
  int fd;
  int file_fd = -1;
  struct audio_caps_desc_s cap_desc;
  struct ap_buffer_s *apb = NULL;
  struct audio_buf_desc_s buf_desc;
  int total_frames;
  int frames_recorded = 0;
  int64_t sum_sq = 0;
  int total_samples = 0;
  int peak = 0;
  double rms;
  int ret;

  if (duration_sec <= 0)
    {
      duration_sec = 3;
    }

  printf("[ES8311] Recording from Microphone for %d seconds...\n",
         duration_sec);

  if (filepath != NULL)
    {
      file_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (file_fd >= 0)
        {
          printf("[ES8311] Saving raw PCM to: %s\n", filepath);
        }
      else
        {
          printf("WARNING: Could not open %s for saving: %d\n",
                 filepath, errno);
        }
    }

  fd = open(ES8311_PCM_IN, O_RDONLY);
  if (fd < 0)
    {
      printf("ERROR: Failed to open %s: %d\n", ES8311_PCM_IN, errno);
      if (file_fd >= 0)
        {
          close(file_fd);
        }

      return -errno;
    }

  /* 1. Configure recording format */

  memset(&cap_desc, 0, sizeof(cap_desc));
  cap_desc.caps.ac_len            = sizeof(struct audio_caps_s);
  cap_desc.caps.ac_type           = AUDIO_TYPE_INPUT;
  cap_desc.caps.ac_subtype        = AUDIO_TYPE_QUERY;
  cap_desc.caps.ac_channels       = CHANNELS_DEFAULT;
  cap_desc.caps.ac_controls.hw[0] = SAMPLE_RATE_DEFAULT;
  cap_desc.caps.ac_controls.b[2]  = 16;

  ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&cap_desc);

  /* 2. Allocate buffer */

  memset(&buf_desc, 0, sizeof(buf_desc));
  buf_desc.u.pbuffer = &apb;

  ret = ioctl(fd, AUDIOIOC_ALLOCBUFFER, (unsigned long)&buf_desc);
  if (ret < 0 || apb == NULL)
    {
      printf("ERROR: AUDIOIOC_ALLOCBUFFER failed for recording: %d\n", ret);
      close(fd);
      if (file_fd >= 0)
        {
          close(file_fd);
        }

      return ret;
    }

  /* 3. Start recording */

  ioctl(fd, AUDIOIOC_START, 0);
  total_frames = SAMPLE_RATE_DEFAULT * duration_sec;

  while (frames_recorded < total_frames)
    {
      int chunk = BUFFER_SAMPLES;
      int16_t *samples;
      int i;
      int n_samps;

      buf_desc.u.buffer = apb;
      ret = ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&buf_desc);
      if (ret < 0)
        {
          printf("ERROR: Receive buffer failed: %d\n", ret);
          break;
        }

      usleep(chunk * 1000000UL / SAMPLE_RATE_DEFAULT);

      samples = (int16_t *)apb->samp;
      n_samps = apb->nbytes / sizeof(int16_t);

      for (i = 0; i < n_samps; i++)
        {
          int val = abs((int)samples[i]);
          if (val > peak)
            {
              peak = val;
            }

          sum_sq += (int64_t)val * val;
        }

      total_samples += n_samps;
      frames_recorded += chunk;

      if (file_fd >= 0 && apb->nbytes > 0)
        {
          write(file_fd, apb->samp, apb->nbytes);
        }
    }

  ioctl(fd, AUDIOIOC_STOP, 0);
  ioctl(fd, AUDIOIOC_FREEBUFFER, (unsigned long)&buf_desc);
  close(fd);

  if (file_fd >= 0)
    {
      close(file_fd);
    }

  rms = (total_samples > 0) ? sqrt((double)sum_sq / total_samples) : 0.0;
  printf("[ES8311] Recorded frames: %d, Peak: %d, RMS: %.1f\n",
         frames_recorded, peak, rms);
  if (rms > 50.0)
    {
      printf("[ES8311] Microphone Input Signal Verified [OK]\n");
    }
  else
    {
      printf("[ES8311] Low signal level detected (check MIC bias/gain)\n");
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 ****************************************************************************/

static int voice_stats(void)
{
  uint32_t hash = 2166136261u;
  unsigned int nonzero = 0;
  unsigned int peak = 0;
  uint64_t squares = 0;

  for (unsigned int i = 0; i < VOICE_FIXTURE_FRAMES; i++)
    {
      int value = g_voice_pcm[i];
      unsigned int magnitude = abs(value);
      uint16_t bits = (uint16_t)value;
      hash = (hash ^ (bits & 255)) * 16777619u;
      hash = (hash ^ (bits >> 8)) * 16777619u;
      if (magnitude != 0) nonzero++;
      if (magnitude > peak) peak = magnitude;
      squares += (int64_t)value * value;
    }

  printf("[VOICE-STATS] frames=%u nonzero=%u peak=%u rms=%.1f fnv32=%08lx\n",
         VOICE_FIXTURE_FRAMES, nonzero, peak,
         VOICE_FIXTURE_FRAMES ? sqrt((double)squares / VOICE_FIXTURE_FRAMES) : 0,
         (unsigned long)hash);
  return nonzero ? 0 : -ENODATA;
}

/* About +3 dB for speech only; leave the reference tone and codec unchanged. */

static int16_t voice_sample(unsigned int frame)
{
  int32_t value = (int32_t)g_voice_pcm[frame] * 181 / 128;
  if (value > INT16_MAX) value = INT16_MAX;
  if (value < INT16_MIN) value = INT16_MIN;
  return value;
}

struct mic_level_s
{
  uint64_t squares[2];
  int64_t sum[2];
  unsigned int peak[2];
  unsigned int nonzero[2];
  unsigned int clipped[2];
  unsigned int frames;
};

static void mic_level_add(struct mic_level_s *level,
                          const int16_t *samples, unsigned int frames)
{
  for (unsigned int i = 0; i < frames; i++)
    for (unsigned int c = 0; c < 2; c++)
      {
        int value = samples[2 * i + c];
        unsigned int magnitude = abs(value);
        level->sum[c] += value;
        level->squares[c] += (int64_t)value * value;
        if (magnitude > level->peak[c]) level->peak[c] = magnitude;
        if (value != 0) level->nonzero[c]++;
        if (value == INT16_MIN || value == INT16_MAX) level->clipped[c]++;
      }
  level->frames += frames;
}

static void mic_level_print(struct mic_level_s *level, unsigned int completed)
{
  if (level->frames == 0) return;
  for (unsigned int c = 0; c < 2; c++)
    printf("[MIC-LEVEL] end_frame=%u channel=%u frames=%u peak=%u rms=%.1f mean=%.1f nonzero=%u clipped=%u\n",
           completed, c, level->frames, level->peak[c],
           sqrt((double)level->squares[c] / level->frames),
           (double)level->sum[c] / level->frames,
           level->nonzero[c], level->clipped[c]);
  memset(level, 0, sizeof(*level));
}

struct local_pcm_s
{
  int16_t *samples;
  unsigned int frames;
  unsigned int captured;
  void (*consume)(void *arg, const int16_t *stereo, unsigned int frames,
                  unsigned int mono_slot);
  void *arg;
  bool (*stop_requested)(void *arg);
};

static int queue_voice(int fd, struct ap_buffer_s *apb, unsigned int *sent,
                       bool control, const struct local_pcm_s *pcm)
{
  struct audio_buf_desc_s desc = {0};
  unsigned int lead = control ? SAMPLE_RATE_DEFAULT : 0;
  unsigned int speech_frames = pcm ? pcm->frames : VOICE_FIXTURE_FRAMES;
  unsigned int n = speech_frames + 2 * lead - *sent;
  int16_t *samples = (int16_t *)apb->samp;

  if (n > 512) n = 512;
  apb->curbyte = 0;
  apb->flags = 0;
  apb->nbytes = n * 4;
  for (unsigned int i = 0; i < n; i++)
    {
      unsigned int pos = *sent + i;
      bool tone = pos < lead || pos >= lead + speech_frames;
      samples[2 * i] = tone ?
        1875 * sin(2.0 * M_PI * 440 * pos / SAMPLE_RATE_DEFAULT) :
        (pcm ? pcm->samples[pos - lead] : voice_sample(pos - lead));
      samples[2 * i + 1] = samples[2 * i];
    }

  desc.u.buffer = apb;
  desc.numbytes = apb->nbytes;
  if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&desc) < 0)
    return -1;
  *sent += n;
  return n;
}

static int checked_audio_source(bool record, bool asr_compact, int seconds,
                         const char *path, bool voice, bool buffered,
                         bool control, struct local_pcm_s *pcm)
{
  struct audio_caps_desc_s caps = {0};
  struct audio_buf_desc_s desc = {0};
  struct ap_buffer_info_s info = {0};
  struct ap_buffer_s *apb = NULL;
  struct ap_buffer_s *voice_buffers[3] = {NULL, NULL, NULL};
  unsigned int queued_frames[3] = {0, 0, 0};
  unsigned int submitted_buffers = 0;
  unsigned int returned_buffers = 0;
  unsigned int cleared_nbytes = 0;
  struct mq_attr attr = {0};
  struct audio_msg_s msg;
  struct timespec deadline;
  char mqname[40];
  mqd_t mq = (mqd_t)-1;
  int fd = -1;
  int output = -1;
  int ret = -EIO;
  const char *stage = "open";
  bool reserved = false;
  bool registered = false;
  bool started = false;
  bool stop_early = false;
  unsigned int completed = 0;
  unsigned int sent = 0;
  unsigned int kept = 0;
  unsigned int discarded = 0;
  unsigned int slot_nonzero[2] = {0, 0};
  int mono_slot = -1;
  const unsigned int rate = SAMPLE_RATE_DEFAULT;
  const unsigned int frames = 512;
  const unsigned int speech_frames = pcm ? pcm->frames : VOICE_FIXTURE_FRAMES;
  const unsigned int total_frames = voice ?
    speech_frames + (control ? 2 * rate : 0) : seconds * rate;
  const char *label = record ? "RECORD" : voice ? "VOICE" : "TONE";
  int16_t mono[512];
  struct mic_level_s mic_level = {0};
  const bool level_only = record && path == NULL;

  if (voice && !pcm && !VOICE_FIXTURE_AVAILABLE)
    {
      printf("[AUDIO-CHECK] VOICE unavailable: generate voice_fixture.h and rebuild\n");
      return -ENOENT;
    }

  const unsigned int max_seconds =
    record && pcm && pcm->consume ? 60 : 10;
  if (seconds < 1 || (unsigned int)seconds > max_seconds ||
      (level_only && asr_compact))
    {
      return -EINVAL;
    }

  fd = open(record ? ES8311_PCM_IN : ES8311_PCM_OUT,
            record ? O_RDONLY : O_WRONLY);
  if (fd < 0) return -errno;
  stage = "reserve";
  if (ioctl(fd, AUDIOIOC_RESERVE, 0) < 0) goto fail;
  reserved = true;
  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = record ? AUDIO_TYPE_INPUT : AUDIO_TYPE_OUTPUT;
  caps.caps.ac_channels = 2;
  caps.caps.ac_controls.hw[0] = rate;
  caps.caps.ac_controls.b[2] = 16;
  stage = "configure-stream";
  if (ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&caps) < 0) goto fail;
  if (!record)
    {
      caps.caps.ac_type = AUDIO_TYPE_FEATURE;
      caps.caps.ac_format.hw = AUDIO_FU_VOLUME;
      caps.caps.ac_controls.hw[0] = 200;
      stage = "configure-volume";
      if (ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&caps) < 0) goto fail;
    }

  snprintf(mqname, sizeof(mqname), "/velafit-audio-%ld", (long)getpid());
  attr.mq_maxmsg = 8;
  attr.mq_msgsize = sizeof(msg);
  mq = mq_open(mqname, O_CREAT | O_EXCL | O_RDWR, 0600, &attr);
  if (mq == (mqd_t)-1) goto fail;
  stage = "register-mq";
  if (ioctl(fd, AUDIOIOC_REGISTERMQ, (unsigned long)mq) < 0) goto fail;
  registered = true;
  stage = "get-buffer-info";
  if (ioctl(fd, AUDIOIOC_GETBUFFERINFO, (unsigned long)&info) < 0 ||
      info.buffer_size < frames * 4 || info.nbuffers == 0)
    {
      errno = EINVAL;
      goto fail;
    }

  /* RX currently uses one 12-bit-size DMA descriptor.  The advertised
   * 8192-byte codec buffer cannot fit it; request exactly one 512-frame
   * chunk.  TX already submits only this chunk's nbytes.
   */

  desc.numbytes = record ? frames * 4 : info.buffer_size;
  desc.u.pbuffer = &apb;
  stage = "allocate-buffer";
  if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (unsigned long)&desc) < 0) goto fail;
  if (apb == NULL || apb->nmaxbytes < frames * 4) goto out;
  if ((voice || level_only) && buffered)
    {
      voice_buffers[0] = apb;
      for (unsigned int i = 1; i < 3; i++)
        {
          desc.numbytes = record ? frames * 4 : info.buffer_size;
          desc.u.pbuffer = &voice_buffers[i];
          if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (unsigned long)&desc) < 0)
            goto fail;
          if (voice_buffers[i] == NULL ||
              voice_buffers[i]->nmaxbytes < frames * 4) goto out;
        }
    }
  if (record && !level_only)
    {
      output = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
      if (output < 0) goto fail;
    }

  printf("[AUDIO-CHECK] %s START frames=%u rate=%u channels=2 bits=16\n",
         label, total_frames, rate);
  if (voice && !pcm)
    printf("[VOICE-GAIN] speech=181/128 (+3.01dB) fixture_peak=1875 output_peak<=2651 codec_volume=200\n");
  if (level_only)
    printf("[MIC-LEVEL] %s; no file or sample export\n",
           pcm ? "local RAM capture, mono slot selected during warmup" : "statistics only");
  fflush(stdout);
  if (level_only && buffered)
    {
      unsigned int target_buffers = (total_frames + rate + frames - 1) / frames;
      printf("[MIC-WARMUP] discard_frames=%u keep_frames=%u\n", rate, total_frames);
      stage = "record-prime";
      for (unsigned int i = 0; i < 3 && submitted_buffers < target_buffers; i++)
        {
          voice_buffers[i]->nbytes = 0;
          voice_buffers[i]->curbyte = 0;
          voice_buffers[i]->flags = 0;
          desc.u.buffer = voice_buffers[i];
          desc.numbytes = frames * 4;
          if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&desc) < 0)
            goto fail;
          queued_frames[i] = frames;
          submitted_buffers++;
        }
      if (ioctl(fd, AUDIOIOC_START, 0) < 0) goto fail;
      started = true;
      while (returned_buffers < target_buffers)
        {
          unsigned int slot = 3;
          stage = "record-wait-dequeue";
          clock_gettime(CLOCK_REALTIME, &deadline);
          deadline.tv_sec += 3;
          ssize_t got = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL,
                                       &deadline);
          if (got < 0 && errno == EINTR) continue;
          if (got < 0) goto fail;
          if (got != sizeof(msg) || msg.msg_id == AUDIO_MSG_COMPLETE) goto out;
          if (msg.msg_id != AUDIO_MSG_DEQUEUE) continue;
          for (unsigned int i = 0; i < 3; i++)
            if (msg.u.ptr == voice_buffers[i]) slot = i;
          if (slot == 3 || queued_frames[slot] == 0) goto out;
          struct ap_buffer_s *returned = voice_buffers[slot];
          if (returned->nbytes != frames * 4) goto out;
          unsigned int skip = discarded < rate ? rate - discarded : 0;
          if (skip > frames) skip = frames;
          /* Observe only the settled half of warmup. One physical microphone
           * can arrive in either interleaved slot after capture restart.
           * This normalizes the application input, not the I2S root cause.
           */
          const int16_t *raw = (const int16_t *)returned->samp;
          for (unsigned int i = 0; i < skip; i++)
            if (discarded + i >= rate / 2)
              for (unsigned int c = 0; c < 2; c++)
                slot_nonzero[c] += raw[2 * i + c] != 0;
          discarded += skip;
          unsigned int take = frames - skip;
          if (take > total_frames - kept) take = total_frames - kept;
          const int16_t *input = (const int16_t *)returned->samp + 2 * skip;
          if (take && mono_slot < 0)
            {
              if (!!slot_nonzero[0] == !!slot_nonzero[1])
                {
                  printf("[MIC-SLOT] FAIL ambiguous warmup nonzero=%u,%u\n",
                         slot_nonzero[0], slot_nonzero[1]);
                  goto out;
                }
              mono_slot = slot_nonzero[0] ? 0 : 1;
              printf("[MIC-SLOT] selected=%d warmup_nonzero=%u,%u\n",
                     mono_slot, slot_nonzero[0], slot_nonzero[1]);
            }
          for (unsigned int i = 0; i < take; i++)
            if (input[2 * i + (1 - mono_slot)] != 0)
              {
                printf("[MIC-SLOT] FAIL inactive slot changed at frame=%u\n",
                       kept + i);
                goto out;
              }
          mic_level_add(&mic_level, input, take);
          if (pcm && pcm->samples)
            for (unsigned int i = 0; i < take; i++)
              pcm->samples[kept + i] = input[2 * i + mono_slot];
          if (take && pcm && pcm->consume)
            pcm->consume(pcm->arg, input, take, mono_slot);
          kept += take;
          if (take && pcm && pcm->stop_requested &&
              pcm->stop_requested(pcm->arg))
            {
              pcm->captured = kept;
              /* Do not release driver-owned buffers. Stop refilling and
               * drain the buffers that were already submitted first. */
              stop_early = true;
              target_buffers = submitted_buffers;
            }
          completed += frames;
          returned_buffers++;
          queued_frames[slot] = 0;
          if (!stop_early && submitted_buffers < target_buffers)
            {
              returned->nbytes = 0;
              returned->curbyte = 0;
              returned->flags = 0;
              desc.u.buffer = returned;
              desc.numbytes = frames * 4;
              if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&desc) < 0)
                goto fail;
              queued_frames[slot] = frames;
              submitted_buffers++;
            }
        }
      if (!stop_early && kept != total_frames) goto out;
      if (pcm) pcm->captured = kept;
      ret = 0;
      goto out;
    }
  if (voice && buffered)
    {
      if (control)
        printf("[VOICE-CONTROL] tone=1s voice=%.3fs tone=1s volume=200 tone_peak=1875\n",
               (double)speech_frames / rate);
      stage = "voice-prime";
      int queued = queue_voice(fd, voice_buffers[0], &sent, control, pcm);
      if (queued < 0) goto fail;
      queued_frames[0] = queued;
      submitted_buffers++;
      if (ioctl(fd, AUDIOIOC_START, 0) < 0) goto fail;
      started = true;
      for (unsigned int i = 1; i < 3 && sent < total_frames; i++)
        {
          queued = queue_voice(fd, voice_buffers[i], &sent, control, pcm);
          if (queued < 0) goto fail;
          queued_frames[i] = queued;
          submitted_buffers++;
        }

      while (completed < total_frames)
        {
          struct ap_buffer_s *returned = NULL;
          unsigned int slot = 3;
          stage = "voice-wait-dequeue";
          clock_gettime(CLOCK_REALTIME, &deadline);
          deadline.tv_sec += 3;
          ssize_t got = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL,
                                       &deadline);
          if (got < 0 && errno == EINTR) continue;
          if (got < 0) goto fail;
          if (got != sizeof(msg) || msg.msg_id == AUDIO_MSG_COMPLETE) goto out;
          if (msg.msg_id != AUDIO_MSG_DEQUEUE) continue;
          for (unsigned int i = 0; i < 3; i++)
            if (msg.u.ptr == voice_buffers[i])
              {
                returned = voice_buffers[i];
                slot = i;
              }
          if (returned == NULL || queued_frames[slot] == 0) goto out;
          /* audio_dequeuebuffer clears nbytes before notifying us.
           * Account using the size saved before submission instead.
           */
          completed += queued_frames[slot];
          queued_frames[slot] = 0;
          returned_buffers++;
          if (returned->nbytes == 0) cleared_nbytes++;
          if (sent < total_frames)
            {
              stage = "voice-refill";
              queued = queue_voice(fd, returned, &sent, control, pcm);
              if (queued < 0) goto fail;
              queued_frames[slot] = queued;
              submitted_buffers++;
            }
        }

      /* DMA completion precedes the last samples leaving the I2S FIFO.
       * Allow one chunk to drain before powering down the codec.
       */

      usleep(20000);
      ret = 0;
      goto out;
    }

  while (completed < total_frames)
    {
      unsigned int n = total_frames - completed;
      if (n > frames) n = frames;
      apb->curbyte = 0;
      apb->flags = 0;
      apb->nbytes = record ? 0 : n * 4;
      if (!record)
        {
          int16_t *samples = (int16_t *)apb->samp;
          for (unsigned int i = 0; i < n; i++)
            {
              int16_t value = voice ? voice_sample(completed + i) :
                1875 * sin(2.0 * M_PI * 440 * (completed + i) / rate);
              samples[2 * i] = value;
              samples[2 * i + 1] = value;
            }
        }

      desc.u.buffer = apb;
      desc.numbytes = n * 4;
      stage = "enqueue-buffer";
      if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&desc) < 0) goto fail;
      if (!started)
        {
          stage = "start";
          if (ioctl(fd, AUDIOIOC_START, 0) < 0) goto fail;
          started = true;
        }

      clock_gettime(CLOCK_REALTIME, &deadline);
      deadline.tv_sec += 3;
      do
        {
          ssize_t got = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL,
                                       &deadline);
          if (got < 0 && errno == EINTR) continue;
          if (got < 0)
            {
              stage = "wait-dequeue";
              goto fail;
            }
          if (got != sizeof(msg)) goto out;
          if (msg.msg_id == AUDIO_MSG_DEQUEUE && msg.u.ptr == apb) break;
          if (msg.msg_id == AUDIO_MSG_COMPLETE) goto out;
        }
      while (true);

      if (!record && !voice && completed == 0)
        {
          /* Read only after the first completed buffer, while the codec
           * is still started.  A post-close dump shows the stop/reset
           * state and cannot diagnose playback mute or power settings.
           */

          printf("[AUDIO-CHECK] Active playback codec snapshot\n");
          do_dump();
        }

      if (record)
        {
          if (apb->nbytes == 0 || apb->nbytes > apb->nmaxbytes ||
              (apb->nbytes % 4) != 0) goto out;
          size_t bytes = apb->nbytes < n * 4 ? apb->nbytes : n * 4;
          unsigned int input_frames = bytes / 4;

          stage = "write-output";
          if (level_only)
            {
              mic_level_add(&mic_level, (const int16_t *)apb->samp,
                            input_frames);
              if (mic_level.frames >= rate)
                mic_level_print(&mic_level, completed + input_frames);
            }
          else if (asr_compact)
            {
              FAR int16_t *samples = (FAR int16_t *)apb->samp;
              unsigned int output_frames = 0;

              for (unsigned int i = 0; i < input_frames; i++)
                {
                  if (((completed + i) % 3) == 0)
                    {
                      mono[output_frames++] = samples[2 * i];
                    }
                }

              if (write(output, mono, output_frames * sizeof(int16_t)) !=
                  output_frames * sizeof(int16_t))
                {
                  goto fail;
                }
            }
          else if (write(output, apb->samp, bytes) != bytes)
            {
              goto fail;
            }

          completed += input_frames;
        }
      else completed += n;
    }

  ret = 0;
  goto out;
fail:
  ret = -errno;
out:
  if (started) ioctl(fd, AUDIOIOC_STOP, 0);
  if (level_only) mic_level_print(&mic_level, completed);
  if (level_only && buffered)
    printf("[MIC-QUEUE] submitted=%u returned=%u actual_frames=%u kept_frames=%u discarded_frames=%u requested_frames=%u\n",
           submitted_buffers, returned_buffers, completed, kept, discarded, total_frames);
  if (voice && buffered)
    printf("[VOICE-QUEUE] submitted=%u returned=%u cleared_nbytes=%u sent_frames=%u completed_frames=%u\n",
           submitted_buffers, returned_buffers, cleared_nbytes, sent, completed);
  for (unsigned int i = 1; i < 3; i++)
    {
      if (voice_buffers[i] != NULL)
        {
          desc.u.buffer = voice_buffers[i];
          ioctl(fd, AUDIOIOC_FREEBUFFER, (unsigned long)&desc);
        }
    }
  if (apb != NULL)
    {
      desc.u.buffer = apb;
      ioctl(fd, AUDIOIOC_FREEBUFFER, (unsigned long)&desc);
    }
  if (registered) ioctl(fd, AUDIOIOC_UNREGISTERMQ, (unsigned long)mq);
  if (reserved) ioctl(fd, AUDIOIOC_RELEASE, 0);
  if (mq != (mqd_t)-1)
    {
      mq_close(mq);
      mq_unlink(mqname);
    }
  if (output >= 0) close(output);
  close(fd);
  printf("[AUDIO-CHECK] %s %s stage=%s completed_frames=%u ret=%d\n",
         label, ret == 0 ? "TRANSFER-PASS" : "FAIL",
         stage, completed, ret);
  return ret;
}

static int checked_audio(bool record, bool asr_compact, int seconds,
                         const char *path, bool voice, bool buffered,
                         bool control)
{
  return checked_audio_source(record, asr_compact, seconds, path, voice,
                              buffered, control, NULL);
}

static int export_pcm(const int16_t *samples, unsigned int frames)
{
  const char digits[] = "0123456789abcdef";
  uint32_t hash = 2166136261u;
  unsigned int bytes = frames * 2;
  printf("[PCM-BEGIN] rate=44100 frames=%u bytes=%u\n", frames, bytes);
  for (unsigned int offset = 0; offset < bytes; offset += 128)
    {
      char hex[257];
      unsigned int n = bytes - offset;
      if (n > 128) n = 128;
      for (unsigned int i = 0; i < n; i++)
        {
          unsigned int pos = offset + i;
          uint16_t value = (uint16_t)samples[pos / 2];
          uint8_t byte = (value >> ((pos % 2) * 8)) & 255;
          hash = (hash ^ byte) * 16777619u;
          hex[2 * i] = digits[byte >> 4];
          hex[2 * i + 1] = digits[byte & 15];
        }
      hex[2 * n] = 0;
      if (printf("[PCM] %08x %s\n", offset, hex) < 0) return -EIO;
    }
  printf("[PCM-END] fnv32=%08lx\n", (unsigned long)hash);
  fflush(stdout);
  return ferror(stdout) ? -EIO : 0;
}

int velafit_mic_capture_pcm(int16_t *samples, unsigned int seconds)
{
  struct local_pcm_s pcm = {0};
  if (!samples || seconds < 1 || seconds > 5) return -EINVAL;
  pcm.samples = samples;
  pcm.frames = seconds * SAMPLE_RATE_DEFAULT;
  int ret = checked_audio_source(true, false, seconds, NULL, false, true,
                                 false, &pcm);
  return ret == 0 && pcm.captured != pcm.frames ? -EIO : ret;
}

const int16_t *velafit_audio_fixture(size_t *frames)
{
  *frames = VOICE_FIXTURE_FRAMES;
  return VOICE_FIXTURE_AVAILABLE ? g_voice_pcm : NULL;
}

static int mic_replay(int seconds, bool export_only)
{
  struct local_pcm_s pcm = {0};
  if (seconds < 1 || seconds > 5) return -EINVAL;
  pcm.frames = seconds * SAMPLE_RATE_DEFAULT;
  pcm.samples = malloc(pcm.frames * sizeof(*pcm.samples));
  if (!pcm.samples) return -ENOMEM;
  printf("[MIC-REPLAY] capture locally, then %s; no device file/network\n",
         export_only ? "authorized serial export" : "speaker playback");
  int ret = checked_audio_source(true, false, seconds, NULL, false, true,
                                 false, &pcm);
  if (ret == 0 && pcm.captured == pcm.frames)
    {
      if (export_only)
        {
          ret = export_pcm(pcm.samples, pcm.frames);
          goto release;
        }
      int64_t sum = 0;
      int peak = 0;
      for (unsigned int i = 0; i < pcm.frames; i++) sum += pcm.samples[i];
      int mean = sum / (int64_t)pcm.frames;
      for (unsigned int i = 0; i < pcm.frames; i++)
        {
          int magnitude = abs((int)pcm.samples[i] - mean);
          if (magnitude > peak) peak = magnitude;
        }
      /* Remove DC and attenuate only, never amplify uncertain mic noise. */
      int divisor = peak > 2651 ? peak : 2651;
      for (unsigned int i = 0; i < pcm.frames; i++)
        pcm.samples[i] = ((int32_t)pcm.samples[i] - mean) * 2651 / divisor;
      printf("[MIC-REPLAY] frames=%u dc=%d input_ac_peak=%d output_peak<=2651\n",
             pcm.frames, mean, peak);
      if (peak == 0) ret = -ENODATA;
      else ret = checked_audio_source(false, false, seconds, NULL, true,
                                       true, true, &pcm);
    }
release:
  memset(pcm.samples, 0, pcm.frames * sizeof(*pcm.samples));
  free(pcm.samples);
  return ret;
}

#ifdef ACOUSTIC_KWS_AVAILABLE
struct acoustic_run
{
  struct vf_kws_frontend *front;
  struct vf_kws_matcher *match;
  struct vf_kws_matcher *extra[7];
  unsigned int extra_count;
  unsigned int samples, frames, events, last_event;
  unsigned int speech_segments, last_active_frame;
  bool saw_active;
  unsigned long max_block_us;
  float best;
  float threshold;
  bool gated;
  bool report_active;
  struct acoustic_feature *exported;
  unsigned int export_capacity, export_count;
};

struct acoustic_feature
{
  uint32_t frame;
  float rms;
  float value[VF_KWS_DIM];
};

static unsigned long acoustic_us(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (unsigned long)t.tv_sec * 1000000ul + t.tv_nsec / 1000;
}

static void acoustic_sample(struct acoustic_run *run, int16_t sample)
{
  float feature[VF_KWS_DIM], rms;
  unsigned int duration;
  run->samples++;
  if (vf_kws_frontend_sample(run->front, sample, feature, &rms) != 1) return;
  run->frames++;
  if (rms >= 80.0f)
    {
      if (!run->saw_active || run->frames - run->last_active_frame > 70)
        run->speech_segments++;
      run->last_active_frame = run->frames;
      run->saw_active = true;
    }
  if (run->exported && run->export_count < run->export_capacity)
    {
      struct acoustic_feature *saved = &run->exported[run->export_count++];
      saved->frame = run->frames;
      saved->rms = rms;
      memcpy(saved->value, feature, sizeof(feature));
    }
  if (!run->match) return;
  float score = run->gated ?
    vf_kws_matcher_feed_gated(run->match, feature, rms, &duration) :
    vf_kws_matcher_feed(run->match, feature, &duration);
  unsigned int winner = 0;
  for (unsigned int i = 0; i < run->extra_count; i++)
    if (run->extra[i])
      {
        unsigned int other_duration;
        float other = vf_kws_matcher_feed_gated(run->extra[i], feature, rms,
                                               &other_duration);
        if (other < score)
          {
            score = other;
            duration = other_duration;
            winner = i + 1;
          }
      }
  if (rms >= 80 && score < run->best) run->best = score;
  if (rms >= 80 && score < run->threshold &&
      (!run->events || run->frames - run->last_event >= 200))
    {
      run->events++;
      run->last_event = run->frames;
      printf("[LOCAL-KWS] template=%u\n", winner);
      printf("[LOCAL-KWS] candidate=%u audio_ms=%lu distance=%.5f duration_ms=%u (uncalibrated)\n",
             run->events, (unsigned long)run->samples * 1000 / 44100,
             (double)score, duration * 10);
      if (run->report_active)
        printf("[VOICE-STATE] %s wake=nihao_openvela event=%u\n",
               run->events == 1 ? "LISTENING -> ACTIVE" :
                                  "ACTIVE wake-confirmed",
               run->events);
    }
}

static void acoustic_consume(void *arg, const int16_t *stereo, unsigned int frames,
                             unsigned int mono_slot)
{
  struct acoustic_run *run = arg;
  unsigned long start = acoustic_us();
  for (unsigned int i = 0; i < frames; i++)
    acoustic_sample(run, stereo[2 * i + mono_slot]);
  unsigned long elapsed = acoustic_us() - start;
  if (elapsed > run->max_block_us) run->max_block_us = elapsed;
}

static bool acoustic_event_observed(void *arg)
{
  const struct acoustic_run *run = arg;
  return run && run->events > 0;
}

static int acoustic_check(bool live, bool negative, unsigned int gated,
                          unsigned int seconds, unsigned int required_events,
                          bool report_active)
{
  struct acoustic_run run = {0};
  run.best = 1e20f;
  run.threshold = report_active ? 0.17f : 0.08f;
  run.gated = gated;
  run.report_active = report_active;
  run.front = vf_kws_frontend_create();
#ifdef KWS_V3_AVAILABLE
  if (gated)
    run.match = vf_kws_matcher_create_bounded(g_kws_v3_template, KWS_V3_FRAMES);
  else
#endif
  run.match = vf_kws_matcher_create(g_kws_template, VF_KWS_TEMPLATE_FRAMES);
  int ret = -ENOMEM;
#ifdef KWS_BANK_AVAILABLE
  if (gated == 2)
    {
      vf_kws_matcher_free(run.match);
      run.match = vf_kws_matcher_create_bounded(g_kws_bank[0], g_kws_bank_frames[0]);
      for (unsigned int i = 0; i < 2; i++)
        {
          run.extra[run.extra_count] =
            vf_kws_matcher_create_bounded(g_kws_bank[i + 1],
                                          g_kws_bank_frames[i + 1]);
          if (!run.extra[run.extra_count++]) goto out;
        }
#ifdef KWS_LIVE_BANK_AVAILABLE
      for (unsigned int i = 0;
           i < KWS_LIVE_BANK_COUNT &&
           run.extra_count < sizeof(run.extra) / sizeof(run.extra[0]); i++)
        {
          run.extra[run.extra_count] =
            vf_kws_matcher_create_bounded(g_kws_live_bank[i],
                                          g_kws_live_bank_frames[i]);
          if (!run.extra[run.extra_count++]) goto out;
        }
#endif
    }
#endif
  if (!run.front || !run.match) goto out;
  printf("[LOCAL-KWS] variant=%s duration_units=%s\n",
         gated == 2 ? "v4-three-template-bank" : gated ? "v3-bounded-energy-gate" : "v2",
         gated ? "retained_feature_ms_not_latency" : "feature_ms");
  printf("[LOCAL-KWS] MFCC-DTW phrase=nihao_openvela threshold=%.2f PROTOTYPE network=none source=%s\n",
         (double)run.threshold,
         live ? "microphone" : gated ? "separate-development-fixture" :
         negative ? "non-target-fixture" : "enrollment-and-repeat-fixture");
  unsigned long start = acoustic_us();
  if (live)
    {
      struct local_pcm_s pcm = {0};
      pcm.consume = acoustic_consume;
      pcm.arg = &run;
      if (required_events == 1 && report_active)
        pcm.stop_requested = acoustic_event_observed;
      ret = checked_audio_source(true, false, seconds, NULL, false, true, false, &pcm);
    }
  else
    {
      const int16_t *samples = negative ? g_voice_pcm : g_kws_test_pcm;
      unsigned int count = negative ? VOICE_FIXTURE_FRAMES : VF_KWS_TEST_SAMPLES;
#ifdef KWS_V3_AVAILABLE
      if (gated)
        {
          samples = negative ? g_kws_v3_negative : g_kws_v3_positive;
          count = negative ? sizeof(g_kws_v3_negative) / sizeof(int16_t) :
                             sizeof(g_kws_v3_positive) / sizeof(int16_t);
          printf("[LOCAL-KWS] fixture=separate-development-recording not-fresh-validation\n");
        }
#endif
      for (unsigned int base = 0; base < count; base += 512)
        {
          unsigned long block = acoustic_us();
          for (unsigned int i = base; i < count && i < base + 512; i++)
            acoustic_sample(&run, samples[i]);
          unsigned long elapsed = acoustic_us() - block;
          if (elapsed > run.max_block_us) run.max_block_us = elapsed;
        }
      ret = 0;
    }
  printf("[LOCAL-KWS] result=%d samples=%u features=%u speech_segments=%u candidates=%u elapsed_us=%lu max_512_block_us=%lu best_distance=%.5f\n",
         ret, run.samples, run.frames, run.speech_segments, run.events,
         acoustic_us() - start,
         run.max_block_us, (double)run.best);
  if (ret == 0 && required_events && run.events < required_events)
    {
      printf("[VOICE-STATE] FAIL state=LISTENING events=%u required=%u\n",
             run.events, required_events);
      ret = -ENOMSG;
    }
  else if (ret == 0 && required_events)
    printf("[VOICE-STATE] PASS state=ACTIVE events=%u required=%u\n",
           run.events, required_events);
out:
  vf_kws_frontend_free(run.front);
  vf_kws_matcher_free(run.match);
  for (unsigned int i = 0; i < run.extra_count; i++)
    vf_kws_matcher_free(run.extra[i]);
  return ret;
}

int velafit_wake_wait_once(unsigned int max_seconds)
{
  if (max_seconds < 1 || max_seconds > 60) return -EINVAL;
  printf("[VELAFIT] IDLE listening wake=nihao_openvela timeout_s=%u\n",
         max_seconds);
  fflush(stdout);
  return acoustic_check(true, false, 2, max_seconds, 1, true);
}

static int acoustic_active_accept(unsigned int trials)
{
  if (trials < 1 || trials > 3) return -EINVAL;
  printf("[VOICE-ACCEPT] START trials=%u phrase=nihao_openvela "
         "window_s=10 threshold=0.17 cooldown_s=2\n", trials);
  printf("[VOICE-ACCEPT] state=LISTENING SPEAK %u TIMES AFTER WARMUP\n",
         trials);
  fflush(stdout);
  int ret = acoustic_check(true, false, 2, 10, trials, true);
  printf("[VOICE-ACCEPT] %s events_required=%u final_state=%s\n",
         ret ? "FAIL" : "PASS", trials, ret ? "LISTENING" : "ACTIVE");
  return ret;
}

static int acoustic_monitor_accept(unsigned int seconds,
                                   unsigned int spoken_count)
{
  if (seconds < 15 || seconds > 60 || spoken_count < 1 || spoken_count > 20)
    return -EINVAL;
  printf("[VOICE-MONITOR] START seconds=%u spoken_count=%u phrase=nihao_openvela "
         "threshold=0.17 cooldown_s=2\n", seconds, spoken_count);
  printf("[VOICE-MONITOR] continuous=YES timing_schedule=NONE; "
         "speak naturally any time after warmup\n");
  fflush(stdout);
  int ret = acoustic_check(true, false, 2, seconds, spoken_count, true);
  printf("[VOICE-MONITOR] %s spoken_count=%u final_state=%s\n",
         ret ? "FAIL" : "PASS", spoken_count,
         ret ? "LISTENING_OR_PARTIAL" : "ACTIVE");
  return ret;
}

static int acoustic_feature_export(unsigned int seconds)
{
  if (seconds < 15 || seconds > 60) return -EINVAL;
  struct acoustic_run run = {0};
  run.front = vf_kws_frontend_create();
  run.export_capacity = seconds * 100 + 16;
  run.exported = calloc(run.export_capacity, sizeof(*run.exported));
  if (!run.front || !run.exported)
    {
      vf_kws_frontend_free(run.front);
      free(run.exported);
      return -ENOMEM;
    }
  struct local_pcm_s pcm = {0};
  pcm.consume = acoustic_consume;
  pcm.arg = &run;
  printf("[KWS-ENROLL] START seconds=%u storage=private-host-features raw_pcm=not-exported\n",
         seconds);
  printf("[KWS-ENROLL] continuous=YES timing_schedule=NONE; speak phrase 5 times naturally\n");
  fflush(stdout);
  int ret = checked_audio_source(true, false, seconds, NULL, false, true,
                                 false, &pcm);
  unsigned int active = 0;
  if (!ret)
    {
      printf("[KWS-FEATURE-BEGIN] frames=%u dim=%u rate_hz=100 rms_gate=80\n",
             run.export_count, VF_KWS_DIM);
      for (unsigned int i = 0; i < run.export_count; i++)
        if (run.exported[i].rms >= 80.0f)
          {
            uint32_t rms_bits;
            memcpy(&rms_bits, &run.exported[i].rms, sizeof(rms_bits));
            printf("[KWS-FEATURE] %lu %08lx",
                   (unsigned long)run.exported[i].frame,
                   (unsigned long)rms_bits);
            for (unsigned int j = 0; j < VF_KWS_DIM; j++)
              {
                uint32_t bits;
                memcpy(&bits, &run.exported[i].value[j], sizeof(bits));
                printf(" %08lx", (unsigned long)bits);
              }
            putchar('\n');
            active++;
          }
      printf("[KWS-FEATURE-END] total=%u active=%u truncated=%s\n",
             run.export_count, active,
             run.export_count == run.export_capacity ? "yes" : "no");
    }
  memset(run.exported, 0,
         run.export_capacity * sizeof(*run.exported));
  free(run.exported);
  vf_kws_frontend_free(run.front);
  return ret;
}
#endif

#ifndef ACOUSTIC_KWS_AVAILABLE
int velafit_wake_wait_once(unsigned int max_seconds)
{
  (void)max_seconds;
  return -ENOSYS;
}
#endif

int main(int argc, FAR char *argv[])
{
#ifdef ACOUSTIC_KWS_AVAILABLE
  if (argc == 2 && strcmp(argv[1], "kws-bench") == 0) return acoustic_check(false, false, false, 0, 0, false);
  if (argc == 2 && strcmp(argv[1], "kws-negative") == 0) return acoustic_check(false, true, false, 0, 0, false);
  if (argc == 2 && strcmp(argv[1], "check-kws") == 0) return acoustic_check(true, false, false, 10, 0, false);
#ifdef KWS_V3_AVAILABLE
  if (argc == 2 && strcmp(argv[1], "kws-v3-bench") == 0) return acoustic_check(false, false, true, 0, 0, false);
  if (argc == 2 && strcmp(argv[1], "kws-v3-negative") == 0) return acoustic_check(false, true, true, 0, 0, false);
  if (argc == 2 && strcmp(argv[1], "check-kws-v3") == 0) return acoustic_check(true, false, true, 10, 0, false);
#ifdef KWS_BANK_AVAILABLE
  if (argc == 2 && strcmp(argv[1], "kws-v4-bench") == 0) return acoustic_check(false, false, 2, 0, 0, false);
  if (argc == 2 && strcmp(argv[1], "kws-v4-negative") == 0) return acoustic_check(false, true, 2, 0, 0, false);
  if (argc == 2 && strcmp(argv[1], "check-kws-v4") == 0) return acoustic_check(true, false, 2, 10, 0, false);
  if ((argc == 2 || argc == 3) && strcmp(argv[1], "wake-active") == 0)
    return acoustic_active_accept(argc == 3 ? atoi(argv[2]) : 3);
  if (argc >= 2 && argc <= 4 && strcmp(argv[1], "wake-monitor") == 0)
    return acoustic_monitor_accept(argc >= 3 ? atoi(argv[2]) : 30,
                                   argc == 4 ? atoi(argv[3]) : 5);
  if ((argc == 2 || argc == 3) && strcmp(argv[1], "kws-enroll-export") == 0)
    return acoustic_feature_export(argc == 3 ? atoi(argv[2]) : 30);
#endif
#endif
#endif
  if (argc == 2 && strcmp(argv[1], "voice-stats") == 0)
    return voice_stats();
  if (argc >= 2 && strcmp(argv[1], "check-tone") == 0)
    return checked_audio(false, false, argc == 3 ? atoi(argv[2]) : 1, NULL, false, false, false);
  if (argc == 2 && strcmp(argv[1], "check-voice") == 0)
    return checked_audio(false, false, 1, NULL, true, false, false);
  if (argc == 2 && strcmp(argv[1], "check-voice-buffered") == 0)
    return checked_audio(false, false, 1, NULL, true, true, false);
  if (argc == 2 && strcmp(argv[1], "check-voice-control") == 0)
    return checked_audio(false, false, 1, NULL, true, true, true);
  if (argc == 3 && strcmp(argv[1], "check-mic-level") == 0)
    return checked_audio(true, false, atoi(argv[2]), NULL, false, false, false);
  if (argc == 3 && strcmp(argv[1], "check-mic-buffered") == 0)
    return checked_audio(true, false, atoi(argv[2]), NULL, false, true, false);
  if (argc == 3 && strcmp(argv[1], "check-mic-replay") == 0)
    return mic_replay(atoi(argv[2]), false);
  if (argc == 3 && strcmp(argv[1], "check-mic-export") == 0)
    return mic_replay(atoi(argv[2]), true);
  if (argc == 2 && strcmp(argv[1], "export-voice-fixture") == 0)
    return VOICE_FIXTURE_AVAILABLE ? export_pcm(g_voice_pcm, VOICE_FIXTURE_FRAMES) : -ENOENT;
  if (argc == 4 && strcmp(argv[1], "check-record") == 0)
    return checked_audio(true, false, atoi(argv[2]), argv[3], false, false, false);
  if (argc == 4 && strcmp(argv[1], "check-record-asr") == 0)
    return checked_audio(true, true, atoi(argv[2]), argv[3], false, false, false);
  if (argc < 2)
    {
      printf("Usage: es8311_audio <command> [args]\n");
      printf("Commands:\n");
      printf("  probe                  Probe ES8311 chip and audio nodes\n");
      printf("  dump                   Dump ES8311 hardware registers\n");
      printf("  check-tone [sec]       Low-level 440Hz tone (1..10s)\n");
      printf("  check-voice            Play local synthetic Chinese fixture\n");
      printf("  voice-stats            Silent embedded PCM integrity check\n");
      printf("  check-voice-buffered   Three-buffer speech with queue accounting\n");
      printf("  check-voice-control    Same stream: tone, speech, tone\n");
      printf("  check-mic-level <sec>  Local L/R statistics only (1..10s)\n");
      printf("  check-mic-buffered <sec> Three-buffer L/R statistics (1..10s)\n");
      printf("  check-mic-replay <sec>  Local mono recording and replay (1..5s)\n");
      printf("  check-mic-export <sec>  Authorized serial PCM export (1..5s)\n");
#ifdef KWS_BANK_AVAILABLE
      printf("  wake-active [trials]    Real microphone repeated ACTIVE acceptance\n");
      printf("  wake-monitor [sec] [spoken]  Continuous free-timing wake count\n");
      printf("  kws-enroll-export [sec]  Private feature enrollment; no raw PCM\n");
#endif
      printf("  export-voice-fixture    Synthetic PCM export protocol test\n");
      printf("  tone [freq] [sec]      Play tone (default 1000Hz 2s)\n");
      printf("  record [sec] [file]    Record PCM from mic (default 3s)\n");
      printf("  check-record-asr <sec> <file>  Record 14.7kHz mono PCM\n");
      return 1;
    }

  if (strcmp(argv[1], "probe") == 0)
    {
      return do_probe();
    }
  else if (strcmp(argv[1], "dump") == 0)
    {
      return do_dump();
    }
  else if (strcmp(argv[1], "tone") == 0)
    {
      int freq = (argc > 2) ? atoi(argv[2]) : 1000;
      int sec  = (argc > 3) ? atoi(argv[3]) : 2;
      return do_tone(freq, sec);
    }
  else if (strcmp(argv[1], "record") == 0)
    {
      int sec = (argc > 2) ? atoi(argv[2]) : 3;
      FAR const char *f = (argc > 3) ? argv[3] : NULL;
      return do_record(sec, f);
    }
  else
    {
      printf("Unknown command: %s\n", argv[1]);
      return 1;
    }
}
