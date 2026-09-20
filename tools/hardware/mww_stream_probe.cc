// SPDX-License-Identifier: Apache-2.0
// Host TFLM streaming control-model probe. No microphone or wake dispatch.
#include <cstdio>
#include <cstring>
#include <vector>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
extern "C" int velafit_mww_chain_test(const uint8_t *,size_t);

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 2;
  fseek(f, 0, SEEK_END);
  long length = ftell(f);
  rewind(f);
  if (length <= 0 || length > 1024*1024) { fclose(f); return 2; }
  std::vector<uint8_t> bytes(length);
  size_t got = fread(bytes.data(), 1, length, f);
  fclose(f);
  if (got != size_t(length)) return 2;
  flatbuffers::Verifier verifier(bytes.data(), bytes.size());
  if (!tflite::VerifyModelBuffer(verifier)) return 3;
  const auto *model = tflite::GetModel(bytes.data());
  if (model->version() != TFLITE_SCHEMA_VERSION) return 3;
  tflite::MicroMutableOpResolver<13> ops;
#define OP(name) if (ops.name() != kTfLiteOk) return 4
  OP(AddCallOnce); OP(AddVarHandle); OP(AddReshape); OP(AddReadVariable);
  OP(AddConcatenation); OP(AddStridedSlice); OP(AddAssignVariable);
  OP(AddConv2D); OP(AddDepthwiseConv2D); OP(AddSplitV);
  OP(AddFullyConnected); OP(AddLogistic); OP(AddQuantize);
#undef OP
  alignas(16) static uint8_t arena[128*1024];
  auto *allocator = tflite::MicroAllocator::Create(arena, sizeof(arena));
  if (!allocator) return 5;
  auto *variables = tflite::MicroResourceVariables::Create(allocator, 32);
  if (!variables) return 5;
  tflite::MicroInterpreter interpreter(model, ops, allocator, variables);
  if (interpreter.AllocateTensors() != kTfLiteOk) return 6;
  auto *input = interpreter.input(0);
  auto *output = interpreter.output(0);
  if (input->type != kTfLiteInt8 || input->bytes != 120 ||
      output->type != kTfLiteUInt8 || output->bytes != 1) return 7;
  for (int i = 0; i < 1000; ++i) {
    memset(input->data.int8, input->params.zero_point, input->bytes);
    if (interpreter.Invoke() != kTfLiteOk) return 8;
  }
  printf("HOST TFLM control PASS invocations=1000 arena_used=%zu output_raw=%u\n",
         interpreter.arena_used_bytes(), unsigned(output->data.uint8[0]));
  std::vector<uint8_t> reference;
  for (int pass = 0; pass < 2; ++pass) {
    if (interpreter.Reset() != kTfLiteOk) return 9;
    for (int frame = 0; frame < 100; ++frame) {
      for (size_t j = 0; j < input->bytes; ++j)
        input->data.int8[j] = int((j*17 + frame*31)%256)-128;
      if (interpreter.Invoke() != kTfLiteOk) return 10;
      if (!pass) reference.push_back(output->data.uint8[0]);
      else if (reference[frame] != output->data.uint8[0]) return 11;
    }
  }
  printf("HOST TFLM reset/replay PASS frames=100 passes=2\n");
  return velafit_mww_chain_test(bytes.data(),bytes.size()) ? 12 : 0;
}
