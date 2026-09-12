/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/audio/velafit_audio_cue.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>

#ifdef CONFIG_AUDIO
#  include <nuttx/audio/audio.h>
#endif

#include "velafit_audio_cue.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VELAFIT_PCM_DEVICE       "/dev/audio/pcm0"
#define VELAFIT_AUDIO_SAMPLERATE 44100
#define VELAFIT_AUDIO_CHANNELS   2
#define VELAFIT_AUDIO_CHUNK_SAMP 512
#define VELAFIT_MAX_NOTES        5

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct
{
  int freq_hz;
  int duration_ms;
} audio_note_t;

typedef struct
{
  const char *name;
  const char *desc;
  int num_notes;
  audio_note_t notes[VELAFIT_MAX_NOTES];
} cue_profile_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const cue_profile_t g_cue_profiles[VELAFIT_AUDIO_CUE_MAX] =
{
  {
    "START",
    "Session Starting Prompt",
    2,
    {
      {
        440, 80
      },
      {
        880, 120
      }
    }
  },
  {
    "COUNT_REP",
    "Valid Repetition Count Chime",
    3,
    {
      {
        523, 60
      },
      {
        659, 60
      },
      {
        784, 100
      }
    }
  },
  {
    "WARN_SHALLOW",
    "Shallow Squat Alert (Squat Deeper)",
    2,
    {
      {
        330, 90
      },
      {
        260, 140
      }
    }
  },
  {
    "WARN_VALGUS",
    "Knee Valgus Alert (Avoid Inward Caving)",
    3,
    {
      {
        600, 70
      },
      {
        350, 70
      },
      {
        600, 90
      }
    }
  },
  {
    "WARN_LEAN",
    "Forward Lean Alert (Keep Chest Up)",
    2,
    {
      {
        400, 90
      },
      {
        300, 140
      }
    }
  },
  {
    "WARN_SAG",
    "Hips Sagging Alert (Engage Core)",
    2,
    {
      {
        260, 90
      },
      {
        200, 140
      }
    }
  },
  {
    "WARN_PIKE",
    "Hips Piking Alert (Lower Hips)",
    2,
    {
      {
        320, 90
      },
      {
        480, 120
      }
    }
  },
  {
    "COUNTDOWN",
    "3-2-1 Countdown Beep",
    1,
    {
      {
        880, 60
      }
    }
  },
  {
    "REST",
    "Rest Interval Transition Tone",
    2,
    {
      {
        440, 80
      },
      {
        330, 120
      }
    }
  },
  {
    "WHISTLE",
    "Work Interval Start Whistle",
    2,
    {
      {
        587, 70
      },
      {
        880, 120
      }
    }
  },
  {
    "WAKEUP",
    "Voice Assistant Wakeup Tone",
    2,
    {
      {
        1046, 70
      },
      {
        1318, 120
      }
    }
  },
  {
    "FINISH",
    "Workout Complete Victory Fanfare",
    4,
    {
      {
        523, 90
      },
      {
        659, 90
      },
      {
        784, 90
      },
      {
        1046, 220
      }
    }
  }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: play_notes_hardware
 ****************************************************************************/

static int play_notes_hardware(const audio_note_t *notes,
                               int num_notes,
                               int volume_pct)
{
#ifdef CONFIG_AUDIO
  int fd = open(VELAFIT_PCM_DEVICE, O_WRONLY);
  if (fd < 0)
    {
      return -errno;
    }

  struct audio_caps_desc_s cap_desc;
  memset(&cap_desc, 0, sizeof(cap_desc));
  cap_desc.caps.ac_len            = sizeof(struct audio_caps_s);
  cap_desc.caps.ac_type           = AUDIO_TYPE_OUTPUT;
  cap_desc.caps.ac_subtype        = AUDIO_TYPE_QUERY;
  cap_desc.caps.ac_channels       = VELAFIT_AUDIO_CHANNELS;
  cap_desc.caps.ac_controls.hw[0] = VELAFIT_AUDIO_SAMPLERATE;
  cap_desc.caps.ac_controls.b[2]  = 16;

  int ret = ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&cap_desc);
  if (ret < 0)
    {
      /* Non-fatal warning */
    }

  /* Set volume */

  if (volume_pct <= 0)
    {
      volume_pct = 80;
    }

  if (volume_pct > 100)
    {
      volume_pct = 100;
    }

  cap_desc.caps.ac_subtype        = AUDIO_FU_VOLUME;
  cap_desc.caps.ac_controls.hw[0] = (uint16_t)(volume_pct * 10);
  ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)&cap_desc);

  /* Allocate buffer */

  struct ap_buffer_info_s buf_info;
  struct ap_buffer_s *apb = NULL;
  struct audio_buf_desc_s buf_desc;

  memset(&buf_info, 0, sizeof(buf_info));
  buf_info.buffer_size = VELAFIT_AUDIO_CHUNK_SAMP *
                         VELAFIT_AUDIO_CHANNELS * sizeof(int16_t);
  buf_info.nbuffers    = 1;

  memset(&buf_desc, 0, sizeof(buf_desc));
  buf_desc.u.pbuffer   = &apb;

  ret = ioctl(fd, AUDIOIOC_ALLOCBUFFER, (unsigned long)&buf_desc);
  if (ret < 0 || apb == NULL)
    {
      close(fd);
      return ret;
    }

  ioctl(fd, AUDIOIOC_START, 0);

  int16_t *samples = (int16_t *)apb->samp;

  for (int n = 0; n < num_notes; n++)
    {
      int freq = notes[n].freq_hz;
      int dur_ms = notes[n].duration_ms;
      int total_samples = (VELAFIT_AUDIO_SAMPLERATE * dur_ms) / 1000;
      double phase = 0.0;
      double phase_inc = (2.0 * M_PI * (double)freq) /
                         (double)VELAFIT_AUDIO_SAMPLERATE;
      int sent = 0;

      while (sent < total_samples)
        {
          int chunk = VELAFIT_AUDIO_CHUNK_SAMP;
          if (sent + chunk > total_samples)
            {
              chunk = total_samples - sent;
            }

          for (int i = 0; i < chunk; i++)
            {
              int global_idx = sent + i;
              double env = 1.0;

              /* 10% attack and decay envelope ramp */

              int ramp_len = total_samples / 10;
              if (ramp_len < 1)
                {
                  ramp_len = 1;
                }

              if (global_idx < ramp_len)
                {
                  env = (double)global_idx / (double)ramp_len;
                }
              else if (global_idx > total_samples - ramp_len)
                {
                  env = (double)(total_samples - global_idx) /
                        (double)ramp_len;
                }

              int16_t val = (int16_t)(sin(phase) * 18000.0 * env);
              samples[i * 2]     = val;
              samples[i * 2 + 1] = val;

              phase += phase_inc;
              if (phase >= 2.0 * M_PI)
                {
                  phase -= 2.0 * M_PI;
                }
            }

          apb->nbytes = chunk * VELAFIT_AUDIO_CHANNELS * sizeof(int16_t);
          buf_desc.u.buffer = apb;

          ret = ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&buf_desc);
          if (ret < 0)
            {
              break;
            }

          sent += chunk;
          usleep(chunk * 1000000UL / VELAFIT_AUDIO_SAMPLERATE);
        }
    }

  ioctl(fd, AUDIOIOC_STOP, 0);
  ioctl(fd, AUDIOIOC_FREEBUFFER, (unsigned long)&buf_desc);
  close(fd);
  return OK;
#else
  (void)notes;
  (void)num_notes;
  (void)volume_pct;
  return -ENODEV;
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: velafit_audio_cue_init
 ****************************************************************************/

int velafit_audio_cue_init(void)
{
  return OK;
}

/****************************************************************************
 * Name: velafit_audio_cue_deinit
 ****************************************************************************/

void velafit_audio_cue_deinit(void)
{
}

/****************************************************************************
 * Name: velafit_audio_cue_name
 ****************************************************************************/

const char *velafit_audio_cue_name(velafit_audio_cue_type_t cue)
{
  if (cue >= VELAFIT_AUDIO_CUE_MAX)
    {
      return "UNKNOWN";
    }

  return g_cue_profiles[cue].name;
}

/****************************************************************************
 * Name: velafit_audio_cue_desc
 ****************************************************************************/

const char *velafit_audio_cue_desc(velafit_audio_cue_type_t cue)
{
  if (cue >= VELAFIT_AUDIO_CUE_MAX)
    {
      return "Unknown Cue";
    }

  return g_cue_profiles[cue].desc;
}

/****************************************************************************
 * Name: velafit_audio_cue_play
 ****************************************************************************/

int velafit_audio_cue_play(velafit_audio_cue_type_t cue)
{
  if (cue >= VELAFIT_AUDIO_CUE_MAX)
    {
      return -EINVAL;
    }

  const cue_profile_t *prof = &g_cue_profiles[cue];

  int ret = play_notes_hardware(prof->notes, prof->num_notes, 85);
  if (ret != OK)
    {
      /* Fallback to console emulation log if hardware audio unavailable */

      printf("[VELAFIT-AUDIO] Cue: [%s] -> %s (Emulated)\n",
             prof->name, prof->desc);
      return OK;
    }

  return OK;
}

/****************************************************************************
 * Name: velafit_audio_cue_play_custom
 ****************************************************************************/

int velafit_audio_cue_play_custom(int freq_hz,
                                  int duration_ms,
                                  int volume_pct)
{
  if (freq_hz <= 0 || duration_ms <= 0)
    {
      return -EINVAL;
    }

  audio_note_t note;
  note.freq_hz = freq_hz;
  note.duration_ms = duration_ms;

  int ret = play_notes_hardware(&note, 1, volume_pct);
  if (ret != OK)
    {
      printf("[VELAFIT-AUDIO] Custom Tone: %d Hz for %d ms (Emulated)\n",
             freq_hz, duration_ms);
      return OK;
    }

  return OK;
}
