/* SPDX-License-Identifier: Apache-2.0 */
#include "velafit_resample.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

#define TAPS 96
#define PHASES 160
#define PI 3.14159265358979323846
struct vf_resample {
  int32_t coeff[PHASES][TAPS]; /* Q20; RV32 integer MAC, not emulated float */
  int16_t history[TAPS];
  unsigned head;
  unsigned until_output;
  unsigned phase;
};

struct vf_resample *vf_resample_create(void) {
  struct vf_resample *s = calloc(1, sizeof(*s));
  if (!s) return NULL;
  for (unsigned p = 0; p < PHASES; ++p) {
    double sum = 0;
    float coeff[TAPS];
    for (unsigned k = 0; k < TAPS; ++k) {
      double x = (double)k + (double)p / PHASES - (TAPS-1)*0.5;
      double cutoff = 7000.0 / 44100.0;
      double h = fabs(x) < 1e-12 ? 2*cutoff : sin(2*PI*cutoff*x)/(PI*x);
      double window = 0.42 - 0.5*cos(2*PI*k/(TAPS-1))
                      + 0.08*cos(4*PI*k/(TAPS-1));
      coeff[k] = h * window;
      sum += coeff[k];
    }
    for (unsigned k = 0; k < TAPS; ++k)
      s->coeff[p][k] = (int32_t)lround((double)(float)(coeff[k]/sum)*1048576.0);
  }
  return s;
}

void vf_resample_destroy(struct vf_resample *s) { free(s); }

int vf_resample_process(struct vf_resample *s, const int16_t *input,
                        size_t count, int16_t *output, size_t capacity) {
  if (!s || (!input && count) || (!output && count) || count > INT_MAX)
    return -EINVAL;
  /* Conservative capacity gate before mutating streaming state. */
  if (capacity < count) return -ENOSPC;
  unsigned produced = 0;
  for (size_t i = 0; i < count; ++i) {
    s->history[s->head] = input[i];
    if (s->until_output == 0) {
      int64_t value = 0;
      unsigned index = s->head;
      for (unsigned k = 0; k < TAPS; ++k) {
        value += (int64_t)s->history[index] * s->coeff[s->phase][k];
        index = index ? index-1 : TAPS-1;
      }
      /* Symmetric round-away-from-zero, avoiding implementation-defined
       * signed right shift. int64 safely covers the sum of 96 Q20 products. */
      int64_t rounded = value >= 0 ? (value+524288)/1048576 : -((-value+524288)/1048576);
      output[produced++] = rounded < -32768 ? -32768 : rounded > 32767 ? 32767 : rounded;
      unsigned next = s->phase + 441;
      s->until_output = next / PHASES;
      s->phase = next % PHASES;
    }
    --s->until_output;
    s->head = (s->head+1)%TAPS;
  }
  return produced;
}
