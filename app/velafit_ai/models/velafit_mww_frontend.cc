// SPDX-License-Identifier: Apache-2.0
// Same feature contract as the training parity test, no microphone access.
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <errno.h>
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"

extern "C" int velafit_mww_frontend_probe(void)
{
  FrontendConfig config{};
  FrontendFillConfigWithDefaults(&config);
  config.window.size_ms=30; config.window.step_size_ms=10;
  config.filterbank.num_channels=40;
  config.filterbank.lower_band_limit=125;
  config.filterbank.upper_band_limit=7500;
  config.noise_reduction.smoothing_bits=10;
  config.noise_reduction.even_smoothing=.025f;
  config.noise_reduction.odd_smoothing=.06f;
  config.noise_reduction.min_signal_remaining=.05f;
  config.pcan_gain_control.enable_pcan=1;
  config.pcan_gain_control.strength=.95f;
  config.pcan_gain_control.offset=80;
  config.pcan_gain_control.gain_bits=21;
  config.log_scale.enable_log=1; config.log_scale.scale_shift=6;
  uint32_t reference=0;
  for (unsigned pass=0;pass<2;pass++)
    {
      FrontendState state{};
      if (!FrontendPopulateState(&config,&state,16000))
        { FrontendFreeStateContents(&state); return -ENOMEM; }
      unsigned frames=0, position=0;
      uint32_t hash=2166136261u;
      int16_t pcm[441];
      int ret=0;
      timespec start{},finish{};
      clock_gettime(CLOCK_MONOTONIC,&start);
      while (position<16000 && !ret)
        {
          unsigned n=pass?441:160;
          if (n>16000-position) n=16000-position;
          for (unsigned i=0;i<n;i++) pcm[i]=int((position+i)%1024)*16-8000;
          position+=n;
          size_t pos=0;
          while (pos<n)
            {
              size_t used=0;
              auto out=FrontendProcessSamples(&state,pcm+pos,n-pos,&used);
              if (!used || used>n-pos || (out.size && out.size!=40))
                { ret=-EIO; break; }
              pos+=used;
              if (out.size) frames++;
              for (size_t i=0;i<out.size;i++)
                {
                  hash=(hash^(out.values[i]&255))*16777619u;
                  hash=(hash^(out.values[i]>>8))*16777619u;
                }
            }
        }
      clock_gettime(CLOCK_MONOTONIC,&finish);
      int64_t us=(int64_t(finish.tv_sec)-start.tv_sec)*1000000+
                  (finish.tv_nsec-start.tv_nsec)/1000;
      FrontendFreeStateContents(&state);
      printf("MWW FRONTEND pass=%u samples=16000 frames=%u hash=%08lx us=%lld\n",
             pass,frames,(unsigned long)hash,(long long)us);
      if (ret) return ret;
      if (frames!=98 || (pass && hash!=reference)) return -EILSEQ;
      reference=hash;
    }
  puts("MWW FRONTEND chunk/reset PASS synthetic PCM only");
  return 0;
}

#ifdef VELAFIT_FRONTEND_HOST_MAIN
int main() { return velafit_mww_frontend_probe() ? 1 : 0; }
#endif
