/* SPDX-License-Identifier: Apache-2.0 */
/* Host-only frozen pre-Q20 FIR oracle. Never linked into the firmware. */
#include "velafit_resample.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#define TAPS 96
#define PHASES 160
#define PI 3.14159265358979323846
struct vf_resample {
  float coeff[PHASES][TAPS];
  int16_t history[TAPS];
  unsigned head, until_output, phase;
};
struct vf_resample *vf_resample_create(void) {
  struct vf_resample *s=calloc(1,sizeof(*s));
  if(!s) return NULL;
  for(unsigned p=0;p<PHASES;p++) {
    double sum=0;
    for(unsigned k=0;k<TAPS;k++) {
      double x=(double)k+(double)p/PHASES-(TAPS-1)*.5;
      double cutoff=7000.0/44100.0;
      double h=fabs(x)<1e-12 ? 2*cutoff : sin(2*PI*cutoff*x)/(PI*x);
      double w=.42-.5*cos(2*PI*k/(TAPS-1))+.08*cos(4*PI*k/(TAPS-1));
      s->coeff[p][k]=h*w;sum+=s->coeff[p][k];
    }
    for(unsigned k=0;k<TAPS;k++) s->coeff[p][k]/=sum;
  }
  return s;
}
void vf_resample_destroy(struct vf_resample *s) { free(s); }
int vf_resample_process(struct vf_resample *s,const int16_t *input,size_t count,
                        int16_t *output,size_t capacity) {
  if(!s || (!input && count) || (!output && count) || count>INT_MAX) return -EINVAL;
  if(capacity<count) return -ENOSPC;
  unsigned produced=0;
  for(size_t i=0;i<count;i++) {
    s->history[s->head]=input[i];
    if(!s->until_output) {
      float value=0;
      for(unsigned k=0;k<TAPS;k++)
        value+=s->history[(s->head+TAPS-k)%TAPS]*s->coeff[s->phase][k];
      long v=lroundf(value);
      output[produced++]=v < -32768 ? -32768 : v > 32767 ? 32767 : v;
      unsigned next=s->phase+441;s->until_output=next/PHASES;s->phase=next%PHASES;
    }
    --s->until_output;s->head=(s->head+1)%TAPS;
  }
  return produced;
}
