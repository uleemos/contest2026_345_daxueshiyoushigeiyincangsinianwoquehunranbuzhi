// SPDX-License-Identifier: Apache-2.0
// Deterministic synthetic 44.1kHz PCM -> C FIR -> features -> TFLM.
// No microphone or wake acceptance. Same source runs on host and P4.
#include "velafit_mww_stream.h"
#include <cstdio>
#include <ctime>
#include <cerrno>
struct result { uint32_t hash=2166136261u; unsigned calls=0; uint32_t frame=0; };
static void on_score(void *ctx,uint8_t value,uint32_t frame)
{
  auto r=(result *)ctx;
  r->hash=(r->hash^value)*16777619u; ++r->calls; r->frame=frame;
}
extern "C" int velafit_mww_chain_test(const uint8_t *model,size_t bytes)
{
  auto s=vf_mww_stream_create(model,bytes);
  if(!s) return -ENOMEM;
  uint32_t reference=0;
  int ret=0;
  for(unsigned pass=0;pass<3 && !ret;pass++)
    {
      ret=vf_mww_stream_reset(s);
      result r;
      timespec a{},b{};
      clock_gettime(CLOCK_MONOTONIC,&a);
      for(unsigned pos=0;pos<44100 && !ret;)
        {
          unsigned n=pass==0 ? 441 : pass==1 ? 127 : 1;
          if(n>44100-pos) n=44100-pos;
          int16_t pcm[441];
          for(unsigned i=0;i<n;i++) pcm[i]=int((pos+i)%1024)*16-8000;
          ret=vf_mww_stream_process(s,pcm,n,on_score,&r);
          pos+=n;
        }
      clock_gettime(CLOCK_MONOTONIC,&b);
      long long us=(long long)(b.tv_sec-a.tv_sec)*1000000+(b.tv_nsec-a.tv_nsec)/1000;
      printf("MWW CHAIN synthetic pass=%u input=44100 calls=%u frame=%lu hash=%08lx arena=%zu us=%lld\n",
        pass,r.calls,(unsigned long)r.frame,(unsigned long)r.hash,vf_mww_stream_arena(s),us);
      if(r.calls!=32 || r.frame!=96 || (pass && reference!=r.hash)) ret=-EILSEQ;
      reference=r.hash;
    }
  vf_mww_stream_destroy(s);
  printf("MWW CHAIN result=%d; synthetic only, no wake acceptance\n",ret);
  return ret;
}
