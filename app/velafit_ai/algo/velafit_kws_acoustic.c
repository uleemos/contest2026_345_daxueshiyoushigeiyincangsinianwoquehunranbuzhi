/* SPDX-License-Identifier: Apache-2.0
 * Single-speaker acoustic-template KWS prototype. No cloud or ASR dependency.
 * MFCC + subsequence DTW; thresholds require independent speech/noise tests.
 */
#include "velafit_kws_acoustic.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846f
#define INF 1e20f

struct vf_kws_frontend
{
  int16_t fir[63], history[63], window[400], signal[400];
  int32_t re[512], im[512];
  int16_t tw_re[256], tw_im[256];
  float dct[VF_KWS_DIM][26];
  unsigned int bins[28], ring, phase, fill;
};

struct vf_kws_matcher
{
  int16_t model[VF_KWS_MAX_FRAMES * VF_KWS_DIM];
  unsigned int frames, tick;
  float prev[VF_KWS_MAX_FRAMES + 1], curr[VF_KWS_MAX_FRAMES + 1];
  unsigned int start[VF_KWS_MAX_FRAMES + 1], next[VF_KWS_MAX_FRAMES + 1];
  unsigned int steps[VF_KWS_MAX_FRAMES + 1], nsteps[VF_KWS_MAX_FRAMES + 1];
  /* D=diagonal, H=one repeated input, V=one repeated model frame.
   * H/V must follow D: no arbitrary collapse of a complete phrase.
   * Experimental, not enabled by the production application build.
   */
  struct { float cost; unsigned int age, steps; }
    path[2][VF_KWS_MAX_FRAMES + 1][3];
  unsigned int row;
  unsigned int bounded, quiet, wall;
};

struct vf_kws_frontend *vf_kws_frontend_create(void)
{
  struct vf_kws_frontend *c = calloc(1, sizeof(*c));
  if (!c) return NULL;
  float sum = 0;
  for (int i = 0; i < 63; i++)
    {
      int n = i - 31;
      float value = n ? sinf(2 * PI * 7000 / 44100 * n) / (PI * n) : 14000.0f / 44100;
      c->fir[i] = (int16_t)(32767 * value * (0.54f - 0.46f * cosf(2 * PI * i / 62)));
      sum += c->fir[i];
    }
  for (int i = 0; i < 63; i++) c->fir[i] = (int16_t)(c->fir[i] * 32767 / sum);
  for (int i = 0; i < 400; i++) c->window[i] = (int16_t)(32767 * (0.54f - 0.46f * cosf(2 * PI * i / 399)));
  for (int i = 0; i < 256; i++)
    { c->tw_re[i] = (int16_t)(32767 * cosf(-2 * PI * i / 512)); c->tw_im[i] = (int16_t)(32767 * sinf(-2 * PI * i / 512)); }
  float low = 2595 * log10f(1 + 80.0f / 700);
  float high = 2595 * log10f(1 + 7000.0f / 700);
  for (int i = 0; i < 28; i++)
    c->bins[i] = (unsigned int)(513 * 700 * (powf(10, (low + (high - low) * i / 27) / 2595) - 1) / 16000);
  for (int k = 0; k < VF_KWS_DIM; k++)
    for (int j = 0; j < 26; j++) c->dct[k][j] = cosf(PI * (k + 1) * (j + 0.5f) / 26);
  return c;
}

void vf_kws_frontend_free(struct vf_kws_frontend *c) { free(c); }

static void features(struct vf_kws_frontend *c, float *out, float *rms)
{
  int32_t mean = 0;
  int64_t energy = 0;
  for (int i = 0; i < 400; i++) mean += c->signal[i];
  mean /= 400;
  memset(c->re, 0, sizeof(c->re));
  memset(c->im, 0, sizeof(c->im));
  for (int i = 0; i < 400; i++)
    {
      int32_t sample = c->signal[i] - mean;
      energy += (int64_t)sample * sample;
      c->re[i] = (sample * c->window[i]) / 32768;
    }
  *rms = sqrtf(energy / 400);
  for (unsigned int i = 1, j = 0; i < 512; i++)
    {
      unsigned int bit = 256;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) { int32_t t = c->re[i]; c->re[i] = c->re[j]; c->re[j] = t; }
    }
  for (unsigned int len = 2; len <= 512; len <<= 1)
    for (unsigned int base = 0; base < 512; base += len)
      for (unsigned int j = 0; j < len / 2; j++)
        {
          unsigned int a = base + j, b = a + len / 2, t = j * 512 / len;
          int32_t re = ((int64_t)c->re[b] * c->tw_re[t] - (int64_t)c->im[b] * c->tw_im[t]) / 32768;
          int32_t im = ((int64_t)c->re[b] * c->tw_im[t] + (int64_t)c->im[b] * c->tw_re[t]) / 32768;
          c->re[b] = c->re[a] - re; c->im[b] = c->im[a] - im;
          c->re[a] += re; c->im[a] += im;
        }
  float mel[26];
  for (unsigned int m = 0; m < 26; m++)
    {
      float power = 0;
      unsigned int a = c->bins[m], b = c->bins[m + 1], d = c->bins[m + 2];
      for (unsigned int k = a; k < d; k++)
        {
          float weight = k < b ? (float)(k - a) / (b - a) : (float)(d - k) / (d - b);
          power += weight * ((float)c->re[k] * c->re[k] + (float)c->im[k] * c->im[k]);
        }
      mel[m] = logf(fmaxf(power, 1.0f));
    }
  float norm = 0;
  for (int k = 0; k < VF_KWS_DIM; k++)
    {
      out[k] = 0;
      for (int j = 0; j < 26; j++) out[k] += mel[j] * c->dct[k][j];
      norm += out[k] * out[k];
    }
  norm = sqrtf(fmaxf(norm, 1e-12f));
  for (int k = 0; k < VF_KWS_DIM; k++) out[k] /= norm;
}

int vf_kws_frontend_sample(struct vf_kws_frontend *c, int16_t sample,
                           float *out, float *rms)
{
  if (!c || !out || !rms) return -1;
  c->history[c->ring] = sample;
  c->ring = (c->ring + 1) % 63;
  c->phase += 16000;
  if (c->phase < 44100) return 0;
  c->phase -= 44100;
  int64_t filtered = 0;
  for (unsigned int j = 0; j < 63; j++)
    filtered += (int32_t)c->fir[j] * c->history[(c->ring + 62 - j) % 63];
  filtered /= 32768;
  if (filtered > 32767) filtered = 32767;
  if (filtered < -32768) filtered = -32768;
  c->signal[c->fill++] = filtered;
  if (c->fill < 400) return 0;
  features(c, out, rms);
  memmove(c->signal, c->signal + 160, 240 * sizeof(*c->signal));
  c->fill = 240;
  return 1;
}

struct vf_kws_matcher *vf_kws_matcher_create(const float *model, size_t frames)
{
  if (!model || frames < 20 || frames > VF_KWS_MAX_FRAMES) return NULL;
  for (size_t i = 0; i < frames * VF_KWS_DIM; i++)
    if (!isfinite(model[i]) || fabsf(model[i]) > 1.01f) return NULL;
  struct vf_kws_matcher *c = calloc(1, sizeof(*c));
  if (!c) return NULL;
  for (size_t i = 0; i < frames * VF_KWS_DIM; i++) c->model[i] = model[i] * 4096;
  c->frames = frames;
  for (size_t i = 1; i <= frames; i++) c->prev[i] = INF;
  for (unsigned int r = 0; r < 2; r++)
    for (unsigned int j = 0; j <= frames; j++)
      for (unsigned int s = 0; s < 3; s++) c->path[r][j][s].cost = INF;
  return c;
}

struct vf_kws_matcher *vf_kws_matcher_create_bounded(const float *model, size_t frames)
{
  struct vf_kws_matcher *c = vf_kws_matcher_create(model, frames);
  if (c) c->bounded = 1;
  return c;
}

static void matcher_reset(struct vf_kws_matcher *c)
{
  c->tick = c->row = c->wall = c->quiet = 0;
  memset(c->start, 0, sizeof(c->start));
  memset(c->steps, 0, sizeof(c->steps));
  for (unsigned int j = 0; j <= c->frames; j++)
    {
      c->prev[j] = c->curr[j] = INF;
      for (unsigned int r = 0; r < 2; r++)
        for (unsigned int s = 0; s < 3; s++)
          {
            c->path[r][j][s].cost = INF;
            c->path[r][j][s].age = c->path[r][j][s].steps = 0;
          }
    }
}

float vf_kws_matcher_feed_gated(struct vf_kws_matcher *c, const float *f,
                               float rms, unsigned int *duration)
{
  if (!c || !duration || !isfinite(rms) || rms < 0) return INF;
  *duration = 0;
  if (++c->wall >= 400) matcher_reset(c);
  if (rms < 80)
    {
      if (++c->quiet >= 80) matcher_reset(c);
      return INF;
    }
  c->quiet = 0;
  return vf_kws_matcher_feed(c, f, duration);
}

void vf_kws_matcher_free(struct vf_kws_matcher *c) { free(c); }

float vf_kws_matcher_feed(struct vf_kws_matcher *c, const float *f,
                          unsigned int *duration)
{
  if (!c || !f || !duration) return INF;
  *duration = 0;
  int16_t quantized[VF_KWS_DIM];
  for (int k = 0; k < VF_KWS_DIM; k++)
    {
      if (!isfinite(f[k]) || fabsf(f[k]) > 1.01f) return INF;
      quantized[k] = f[k] * 4096;
    }
  c->tick++;
  if (c->bounded)
  {
  unsigned int prev = c->row, curr = 1 - prev;
  for (unsigned int s = 0; s < 3; s++)
    {
      c->path[prev][0][s].cost = s ? INF : 0;
      c->path[prev][0][s].age = c->path[prev][0][s].steps = 0;
      c->path[curr][0][s].cost = INF;
    }
  for (unsigned int j = 1; j <= c->frames; j++)
    {
      uint32_t distance = 0;
      for (int k = 0; k < VF_KWS_DIM; k++)
        {
          int32_t d = quantized[k] - c->model[(j - 1) * VF_KWS_DIM + k];
          distance += d * d;
        }
      float cost = (float)distance / 16777216.0f;
      unsigned int best = 0;
      for (unsigned int s = 1; s < 3; s++)
        if (c->path[prev][j - 1][s].cost < c->path[prev][j - 1][best].cost)
          best = s;
      c->path[curr][j][0] = c->path[prev][j - 1][best];
      c->path[curr][j][0].age++;
      c->path[curr][j][1] = c->path[curr][j - 1][0];
      c->path[curr][j][2] = c->path[prev][j][0];
      c->path[curr][j][2].age++;
      for (unsigned int s = 0; s < 3; s++)
        {
          c->path[curr][j][s].cost += cost;
          c->path[curr][j][s].steps++;
        }
    }
  c->row = curr;
  float answer = INF;
  for (unsigned int s = 0; s < 3; s++)
    if (c->path[curr][c->frames][s].cost < INF / 2)
      {
        float value = c->path[curr][c->frames][s].cost /
                      c->path[curr][c->frames][s].steps;
        if (value < answer)
          {
            answer = value;
            *duration = c->path[curr][c->frames][s].age;
          }
      }
  return answer;
  }
  c->prev[0] = 0; c->start[0] = c->tick; c->steps[0] = 0;
  c->curr[0] = INF;
  for (unsigned int j = 1; j <= c->frames; j++)
    {
      uint32_t distance = 0;
      for (int k = 0; k < VF_KWS_DIM; k++)
        { int32_t d = quantized[k] - c->model[(j - 1) * VF_KWS_DIM + k]; distance += d * d; }
      float cost = (float)distance / 16777216.0f;
      float best = c->prev[j - 1];
      unsigned int start = c->start[j - 1], steps = c->steps[j - 1];
      if (c->prev[j] < best)
        { best = c->prev[j]; start = c->start[j]; steps = c->steps[j]; }
      if (c->curr[j - 1] < best)
        { best = c->curr[j - 1]; start = c->next[j - 1]; steps = c->nsteps[j - 1]; }
      c->curr[j] = best + cost;
      c->next[j] = start; c->nsteps[j] = steps + 1;
    }
  unsigned int age = c->tick - c->next[c->frames] + 1;
  float score = c->curr[c->frames] / c->nsteps[c->frames];
  memcpy(c->prev, c->curr, sizeof(c->prev));
  memcpy(c->start, c->next, sizeof(c->start));
  memcpy(c->steps, c->nsteps, sizeof(c->steps));
  *duration = age;
  /* Reject implausible time compression/extension, even if spectra match. */
  return age * 100 < c->frames * 85 || age > c->frames * 2 ? INF : score;
}
