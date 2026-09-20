// SPDX-License-Identifier: Apache-2.0
// Board streaming CONTROL model only, not the target phrase or microphone.
#include <cstdio>
#include <cstring>
#include <ctime>
#include <malloc.h>
#include <errno.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
extern "C" const unsigned char g_velafit_mww_control[];
extern "C" const unsigned char g_velafit_mww_control_end[];
extern "C" const unsigned char g_velafit_mww_development[] __attribute__((weak));
extern "C" const unsigned char g_velafit_mww_development_end[] __attribute__((weak));

static uint64_t now_us()
{
  timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
  return uint64_t(t.tv_sec)*1000000 + t.tv_nsec/1000;
}

static int probe(void *arena, unsigned count, const unsigned char *data,
                 size_t bytes, const char *label)
{
  flatbuffers::Verifier verify(data, bytes);
  if (!tflite::VerifyModelBuffer(verify)) return -EINVAL;
  auto *model = tflite::GetModel(data);
  if (model->version() != TFLITE_SCHEMA_VERSION) return -EINVAL;
  tflite::MicroMutableOpResolver<13> ops;
#define OP(name) if (ops.name() != kTfLiteOk) return -ENOSYS
  OP(AddCallOnce); OP(AddVarHandle); OP(AddReshape); OP(AddReadVariable);
  OP(AddConcatenation); OP(AddStridedSlice); OP(AddAssignVariable);
  OP(AddConv2D); OP(AddDepthwiseConv2D); OP(AddSplitV);
  OP(AddFullyConnected); OP(AddLogistic); OP(AddQuantize);
#undef OP
  auto *allocator = tflite::MicroAllocator::Create((uint8_t *)arena, 128*1024);
  if (!allocator) return -ENOMEM;
  auto *vars = tflite::MicroResourceVariables::Create(allocator,32);
  if (!vars) return -ENOMEM;
  tflite::MicroInterpreter interpreter(model,ops,allocator,vars);
  if (interpreter.AllocateTensors()!=kTfLiteOk) return -ENOMEM;
  auto *in=interpreter.input(0);
  auto *out=interpreter.output(0);
  if (in->type!=kTfLiteInt8 || in->bytes!=120 ||
      out->type!=kTfLiteUInt8 || out->bytes!=1) return -EINVAL;
  uint64_t start=now_us(), maximum=0;
  for (unsigned i=0;i<count;i++)
    {
      memset(in->data.int8,in->params.zero_point,in->bytes);
      uint64_t t=now_us();
      if (interpreter.Invoke()!=kTfLiteOk) return -EIO;
      t=now_us()-t;
      if (t>maximum) maximum=t;
      if ((i+1)%100==0) printf("MWW %s progress=%u/%u\n",label,i+1,count);
    }
  uint64_t elapsed=now_us()-start;
  printf("P4 MWW %s invokes=%u arena=%zu elapsed_us=%llu max_us=%llu\n",
    label,count,interpreter.arena_used_bytes(),(unsigned long long)elapsed,
    (unsigned long long)maximum);
  uint8_t reference[100];
  for (int pass=0;pass<2;pass++)
    {
      if (interpreter.Reset()!=kTfLiteOk) return -EIO;
      for (int frame=0;frame<100;frame++)
        {
          for (size_t j=0;j<in->bytes;j++)
            in->data.int8[j]=int((j*17+frame*31)%256)-128;
          if (interpreter.Invoke()!=kTfLiteOk) return -EIO;
          if (!pass) reference[frame]=out->data.uint8[0];
          else if (reference[frame]!=out->data.uint8[0]) return -EILSEQ;
        }
    }
  printf("P4 MWW %s reset/replay PASS frames=100 passes=2; no wake acceptance\n",label);
  return 0;
}

extern "C" int velafit_mww_probe(unsigned count)
{
  if (!count || count>1000) return -EINVAL;
  void *arena=memalign(16,128*1024);
  if (!arena) return -ENOMEM;
  int ret=probe(arena,count,g_velafit_mww_control,
                g_velafit_mww_control_end-g_velafit_mww_control,"CONTROL");
  free(arena);
  printf("P4 MWW CONTROL result=%d\n",ret);
  return ret;
}

extern "C" int velafit_mww_development_probe(unsigned count)
{
  if (!count || count>1000) return -EINVAL;
  if (!g_velafit_mww_development || !g_velafit_mww_development_end)
    return -ENOENT;
  void *arena=memalign(16,128*1024);
  if (!arena) return -ENOMEM;
  int ret=probe(arena,count,g_velafit_mww_development,
    g_velafit_mww_development_end-g_velafit_mww_development,"DEVELOPMENT_NOT_ACCEPTED");
  free(arena);
  printf("P4 MWW DEVELOPMENT_NOT_ACCEPTED result=%d\n",ret);
  return ret;
}

extern "C" int velafit_mww_chain_test(const uint8_t *model,size_t bytes);
extern "C" int velafit_mww_chain_probe(void)
{
  return velafit_mww_chain_test(g_velafit_mww_control,
    g_velafit_mww_control_end-g_velafit_mww_control);
}
