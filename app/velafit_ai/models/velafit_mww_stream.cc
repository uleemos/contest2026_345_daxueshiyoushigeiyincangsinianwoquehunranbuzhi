// SPDX-License-Identifier: Apache-2.0
#include "velafit_mww_stream.h"
#include "velafit_resample.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <new>
#include <malloc.h>
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"

struct vf_mww_stream
{
  tflite::MicroMutableOpResolver<13> ops;
  void *arena = nullptr;
  tflite::MicroInterpreter *interpreter = nullptr;
  vf_resample *resample = nullptr;
  FrontendState frontend{};
  unsigned pending = 0;
  uint32_t frames = 0;
  bool failed = false;
};

extern "C" void vf_mww_stream_destroy(vf_mww_stream *s)
{
  if (!s) return;
  delete s->interpreter;
  FrontendFreeStateContents(&s->frontend);
  vf_resample_destroy(s->resample);
  free(s->arena);
  delete s;
}

extern "C" int vf_mww_stream_reset(vf_mww_stream *s)
{
  if (!s || !s->interpreter) return -EINVAL;
  s->failed = true;
  FrontendFreeStateContents(&s->frontend);
  s->frontend = FrontendState{};
  vf_resample_destroy(s->resample);
  s->resample = vf_resample_create();
  if (!s->resample) return -ENOMEM;
  FrontendConfig c{};
  FrontendFillConfigWithDefaults(&c);
  c.window.size_ms=30; c.window.step_size_ms=10;
  c.filterbank.num_channels=40;
  c.filterbank.lower_band_limit=125; c.filterbank.upper_band_limit=7500;
  c.noise_reduction.smoothing_bits=10;
  c.noise_reduction.even_smoothing=.025f;
  c.noise_reduction.odd_smoothing=.06f;
  c.noise_reduction.min_signal_remaining=.05f;
  c.pcan_gain_control.enable_pcan=1; c.pcan_gain_control.strength=.95f;
  c.pcan_gain_control.offset=80; c.pcan_gain_control.gain_bits=21;
  c.log_scale.enable_log=1; c.log_scale.scale_shift=6;
  if (!FrontendPopulateState(&c,&s->frontend,16000)) return -ENOMEM;
  if (s->interpreter->Reset()!=kTfLiteOk) return -EIO;
  s->pending=0; s->frames=0; s->failed=false;
  return 0;
}

extern "C" vf_mww_stream *vf_mww_stream_create(const uint8_t *data,size_t bytes)
{
  if (!data || bytes<16 || bytes>1024*1024) return nullptr;
  flatbuffers::Verifier verify(data,bytes);
  if (!tflite::VerifyModelBuffer(verify)) return nullptr;
  auto model=tflite::GetModel(data);
  if (model->version()!=TFLITE_SCHEMA_VERSION) return nullptr;
  auto s=new(std::nothrow) vf_mww_stream;
  if (!s) return nullptr;
#define OP(name) if(s->ops.name()!=kTfLiteOk) {vf_mww_stream_destroy(s);return nullptr;}
  OP(AddCallOnce); OP(AddVarHandle); OP(AddReshape); OP(AddReadVariable);
  OP(AddConcatenation); OP(AddStridedSlice); OP(AddAssignVariable);
  OP(AddConv2D); OP(AddDepthwiseConv2D); OP(AddSplitV);
  OP(AddFullyConnected); OP(AddLogistic); OP(AddQuantize);
#undef OP
  s->arena=memalign(16,128*1024);
  if (!s->arena) {vf_mww_stream_destroy(s);return nullptr;}
  auto allocator=tflite::MicroAllocator::Create((uint8_t *)s->arena,128*1024);
  if (!allocator) {vf_mww_stream_destroy(s);return nullptr;}
  auto vars=tflite::MicroResourceVariables::Create(allocator,32);
  if (!vars) {vf_mww_stream_destroy(s);return nullptr;}
  s->interpreter=new(std::nothrow) tflite::MicroInterpreter(model,s->ops,allocator,vars);
  if (!s->interpreter || s->interpreter->AllocateTensors()!=kTfLiteOk)
    {vf_mww_stream_destroy(s);return nullptr;}
  auto in=s->interpreter->input(0); auto out=s->interpreter->output(0);
  if(in->type!=kTfLiteInt8 || in->bytes!=120 || out->type!=kTfLiteUInt8 ||
     out->bytes!=1 || !std::isfinite(in->params.scale) || in->params.scale<=0 ||
     vf_mww_stream_reset(s)) {vf_mww_stream_destroy(s);return nullptr;}
  return s;
}

extern "C" size_t vf_mww_stream_arena(const vf_mww_stream *s)
{ return s && s->interpreter ? s->interpreter->arena_used_bytes() : 0; }

extern "C" int vf_mww_stream_process(vf_mww_stream *s,const int16_t *pcm,
                                     size_t samples,vf_mww_score score,void *ctx)
{
  if(!s || (!pcm && samples) || !score) return -EINVAL;
  if(s->failed) return -EIO;
  auto in=s->interpreter->input(0);
  while(samples)
    {
      size_t n=samples>441 ? 441 : samples;
      int16_t converted[441];
      int count=vf_resample_process(s->resample,pcm,n,converted,441);
      if(count<0) {s->failed=true;return count;}
      pcm+=n; samples-=n;
      size_t pos=0;
      while(pos<(size_t)count)
        {
          size_t used=0;
          auto out=FrontendProcessSamples(&s->frontend,converted+pos,count-pos,&used);
          if(!used || used>(size_t)count-pos || (out.size && out.size!=40))
            {s->failed=true;return -EIO;}
          pos+=used;
          if(!out.size) continue;
          ++s->frames;
          for(unsigned i=0;i<40;i++)
            {
              // Training float features are raw frontend uint16 * 0.0390625.
              float q=std::nearbyint(out.values[i]*.0390625f/in->params.scale)+in->params.zero_point;
              in->data.int8[s->pending*40+i]=(int8_t)(q < -128 ? -128 : q > 127 ? 127 : q);
            }
          if(++s->pending==3)
            {
              s->pending=0;
              if(s->interpreter->Invoke()!=kTfLiteOk) {s->failed=true;return -EIO;}
              score(ctx,s->interpreter->output(0)->data.uint8[0],s->frames);
            }
        }
    }
  return 0;
}
