/****************************************************************************
 * app/velafit_ai/models/velafit_tiny_pose_tflm.cc
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <errno.h>
#include <malloc.h>
#include <new>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "velafit_tiny_pose_tflm.h"
#include "velafit_tiny_pose_preprocess.h"

#ifdef VELAFIT_ESP_NN_TEST
TFLMRegistration velafit_register_conv_pie();
#endif

extern "C"
{
extern const unsigned char g_velafit_tiny_pose_model[];
extern const unsigned int g_velafit_tiny_pose_model_size;
extern const int8_t g_velafit_tiny_pose_fixture[];
extern const unsigned int g_velafit_tiny_pose_fixture_size;
extern const int8_t g_velafit_tiny_pose_oracle[];
extern const unsigned int g_velafit_tiny_pose_oracle_size;
}

namespace
{

constexpr size_t kArenaBytes = 512 * 1024;
constexpr unsigned int kWarmupRounds = 5;
constexpr unsigned int kMaximumRounds = 1000;
constexpr unsigned int kInputBytes = 96 * 96 * 3;
constexpr unsigned int kOutputBytes = 8 * 3;
constexpr int kInputDims[] = {1, 96, 96, 3};
constexpr int kOutputDims[] = {1, 24};
using Resolver = tflite::MicroMutableOpResolver<3>;

uint64_t monotonic_us();

void *g_runtime_arena;
Resolver *g_runtime_resolver;
tflite::MicroInterpreter *g_runtime_interpreter;

float clamp_unit(float value)
{
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

void decode_pose(const TfLiteTensor *output, pose_frame_t *pose)
{
  static const int mapping[8] =
    {
      KPT_LEFT_SHOULDER, KPT_RIGHT_SHOULDER,
      KPT_LEFT_HIP, KPT_RIGHT_HIP,
      KPT_LEFT_KNEE, KPT_RIGHT_KNEE,
      KPT_LEFT_ANKLE, KPT_RIGHT_ANKLE
    };
  unsigned int confident = 0;

  std::memset(pose, 0, sizeof(*pose));
  for (unsigned int index = 0; index < 8; index++)
    {
      kpt_2d_t *point = &pose->kpts[mapping[index]];
      point->x = clamp_unit((output->data.int8[index * 3] -
                             output->params.zero_point) *
                            output->params.scale);
      point->y = clamp_unit((output->data.int8[index * 3 + 1] -
                             output->params.zero_point) *
                            output->params.scale);
      point->score = clamp_unit((output->data.int8[index * 3 + 2] -
                                 output->params.zero_point) *
                                output->params.scale);
      confident += point->score >= 0.30f;
    }

  pose->timestamp_ms = static_cast<uint32_t>(monotonic_us() / 1000);
  pose->valid = confident >= 6;
}

uint64_t monotonic_us()
{
  struct timespec now;
  return clock_gettime(CLOCK_MONOTONIC, &now) == 0 ?
         static_cast<uint64_t>(now.tv_sec) * 1000000ull +
         static_cast<uint64_t>(now.tv_nsec) / 1000ull : 0;
}

uint64_t machine_cycles()
{
  uint32_t high;
  uint32_t low;
  uint32_t check;
  do
    {
      __asm__ volatile("csrr %0, 0xb80\ncsrr %1, 0xb00\ncsrr %2, 0xb80"
                       : "=r"(high), "=r"(low), "=r"(check) :: "memory");
    }
  while (high != check);

  return (static_cast<uint64_t>(high) << 32) | low;
}

bool shape_is(const TfLiteTensor *tensor, const int *dims, size_t count)
{
  if (tensor == nullptr || tensor->dims == nullptr ||
      tensor->dims->size != static_cast<int>(count))
    {
      return false;
    }

  for (size_t index = 0; index < count; index++)
    {
      if (tensor->dims->data[index] != dims[index])
        {
          return false;
        }
    }

  return true;
}

void print_heap(const char *stage)
{
  const struct mallinfo info = mallinfo();
  std::printf("TINYPOSE-HEAP stage=%s arena=%d used=%d free=%d maxfree=%d\n",
              stage, info.arena, info.uordblks, info.fordblks,
              info.mxordblk);
}

unsigned int output_differences(const TfLiteTensor *output)
{
  unsigned int different = 0;
  for (unsigned int index = 0; index < kOutputBytes; index++)
    {
      different += output->data.int8[index] != g_velafit_tiny_pose_oracle[index];
    }

  return different;
}

unsigned int byte_differences(const int8_t *left, const int8_t *right)
{
  unsigned int different = 0;
  for (unsigned int index = 0; index < kOutputBytes; index++)
    {
      different += left[index] != right[index];
    }

  return different;
}

void print_output(const char *label, const int8_t *values)
{
  std::printf("TINYPOSE-OUTPUT %s=", label);
  for (unsigned int index = 0; index < kOutputBytes; index++)
    {
      std::printf("%s%d", index == 0 ? "" : ",",
                  static_cast<int>(values[index]));
    }

  std::printf("\n");
}

uint32_t percentile(const uint32_t *sorted, unsigned int count,
                    unsigned int percentage)
{
  const unsigned int rank = (count * percentage + 99) / 100;
  return sorted[rank == 0 ? 0 : rank - 1];
}

}  // namespace

extern "C" int velafit_tiny_pose_init(void)
{
  if (g_runtime_interpreter != nullptr)
    {
      return 0;
    }

  g_runtime_resolver = new (std::nothrow) Resolver;
  g_runtime_arena = memalign(16, kArenaBytes);
  if (g_runtime_resolver == nullptr || g_runtime_arena == nullptr)
    {
      velafit_tiny_pose_deinit();
      return -ENOMEM;
    }

  if (
#ifdef VELAFIT_ESP_NN_TEST
      g_runtime_resolver->AddConv2D(velafit_register_conv_pie()) != kTfLiteOk ||
#else
      g_runtime_resolver->AddConv2D() != kTfLiteOk ||
#endif
      g_runtime_resolver->AddReshape() != kTfLiteOk ||
      g_runtime_resolver->AddFullyConnected() != kTfLiteOk)
    {
      velafit_tiny_pose_deinit();
      return -ENOTSUP;
    }

  const tflite::Model *model = tflite::GetModel(g_velafit_tiny_pose_model);
  if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION)
    {
      velafit_tiny_pose_deinit();
      return -EPROTO;
    }

  g_runtime_interpreter = new (std::nothrow) tflite::MicroInterpreter(
    model, *g_runtime_resolver, static_cast<uint8_t *>(g_runtime_arena),
    kArenaBytes);
  if (g_runtime_interpreter == nullptr ||
      g_runtime_interpreter->AllocateTensors() != kTfLiteOk)
    {
      velafit_tiny_pose_deinit();
      return -ENOMEM;
    }

  TfLiteTensor *input = g_runtime_interpreter->input(0);
  TfLiteTensor *output = g_runtime_interpreter->output(0);
  if (input == nullptr || input->type != kTfLiteInt8 ||
      input->bytes != kInputBytes || !shape_is(input, kInputDims, 4) ||
      output == nullptr || output->type != kTfLiteInt8 ||
      output->bytes != kOutputBytes || !shape_is(output, kOutputDims, 2))
    {
      velafit_tiny_pose_deinit();
      return -EPROTO;
    }

  return 0;
}

extern "C" void velafit_tiny_pose_deinit(void)
{
  delete g_runtime_interpreter;
  g_runtime_interpreter = nullptr;
  delete g_runtime_resolver;
  g_runtime_resolver = nullptr;
  free(g_runtime_arena);
  g_runtime_arena = nullptr;
}

extern "C" int velafit_tiny_pose_infer_rgb192(const uint8_t *rgb192,
                                                pose_frame_t *pose,
                                                velafit_perf_t *perf)
{
  if (rgb192 == nullptr || pose == nullptr || perf == nullptr)
    {
      return -EINVAL;
    }

  std::memset(perf, 0, sizeof(*perf));
  const uint64_t total_started = monotonic_us();
  int ret = velafit_tiny_pose_init();
  if (ret < 0)
    {
      return ret;
    }

  TfLiteTensor *input = g_runtime_interpreter->input(0);
  const uint64_t preprocess_started = monotonic_us();
  velafit_tiny_pose_preprocess_rgb192(rgb192, input->data.int8,
                                      input->params.scale,
                                      input->params.zero_point);
  perf->preprocess_us = monotonic_us() - preprocess_started;

  const uint64_t infer_started = monotonic_us();
  if (g_runtime_interpreter->Invoke() != kTfLiteOk)
    {
      return -EIO;
    }
  perf->infer_us = monotonic_us() - infer_started;

  const uint64_t post_started = monotonic_us();
  decode_pose(g_runtime_interpreter->output(0), pose);
  perf->postprocess_us = monotonic_us() - post_started;
  perf->total_us = monotonic_us() - total_started;
  perf->fps = perf->total_us == 0 ? 0.0f : 1000000.0f / perf->total_us;
  return pose->valid ? 0 : -ENODATA;
}

extern "C" int velafit_tiny_pose_benchmark(unsigned int rounds)
{
  if (rounds == 0 || rounds > kMaximumRounds)
    {
      return -EINVAL;
    }

  if (g_velafit_tiny_pose_fixture_size != kInputBytes ||
      g_velafit_tiny_pose_oracle_size != kOutputBytes)
    {
      std::printf("TINYPOSE-BENCH asset_size=FAIL model=%u fixture=%u oracle=%u\n",
                  g_velafit_tiny_pose_model_size,
                  g_velafit_tiny_pose_fixture_size,
                  g_velafit_tiny_pose_oracle_size);
      return -EINVAL;
    }

  int ret = 0;
  unsigned int completed = 0;
  unsigned int mismatches = 0;
  unsigned int repeat_mismatches = 0;
  int8_t device_reference[kOutputBytes]{};
  uint64_t total = 0;
  uint32_t *samples = nullptr;
  void *arena = nullptr;
  Resolver *resolver = nullptr;
  tflite::MicroInterpreter *interpreter = nullptr;

  std::printf("TINYPOSE-BENCH scope=Invoke-only excluded=camera,resize,RGB-conversion,render,cold-init\n");
  std::printf("TINYPOSE-BENCH model=%u input=INT8[1,96,96,3] output=INT8[1,24] warmup=%u rounds=%u\n",
              g_velafit_tiny_pose_model_size, kWarmupRounds, rounds);
  print_heap("before");

  samples = static_cast<uint32_t *>(calloc(rounds, sizeof(*samples)));
  resolver = new (std::nothrow) Resolver;
  arena = memalign(16, kArenaBytes);
  if (samples == nullptr || resolver == nullptr || arena == nullptr)
    {
      ret = -ENOMEM;
      goto out;
    }

  if (
#ifdef VELAFIT_ESP_NN_TEST
      resolver->AddConv2D(velafit_register_conv_pie()) != kTfLiteOk ||
#else
      resolver->AddConv2D() != kTfLiteOk ||
#endif
      resolver->AddReshape() != kTfLiteOk ||
      resolver->AddFullyConnected() != kTfLiteOk)
    {
      std::printf("TINYPOSE-BENCH resolver=FAIL required=CONV_2D,RESHAPE,FULLY_CONNECTED\n");
      ret = -ENOTSUP;
      goto out;
    }

  {
    const tflite::Model *model =
      tflite::GetModel(g_velafit_tiny_pose_model);
    if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION)
      {
        ret = -EPROTO;
        goto out;
      }

    const uint64_t init_cycles_start = machine_cycles();
    const uint64_t init_start = monotonic_us();
    interpreter = new (std::nothrow) tflite::MicroInterpreter(
      model, *resolver, static_cast<uint8_t *>(arena), kArenaBytes);
    if (interpreter == nullptr || interpreter->AllocateTensors() != kTfLiteOk)
      {
        std::printf("TINYPOSE-BENCH init=FAIL possible_missing_op_or_arena\n");
        ret = -ENOMEM;
        goto out;
      }

    const uint64_t init_us = monotonic_us() - init_start;
    const uint64_t init_cycles = machine_cycles() - init_cycles_start;
    TfLiteTensor *input = interpreter->input(0);
    TfLiteTensor *output = interpreter->output(0);
    if (input == nullptr || input->type != kTfLiteInt8 ||
        input->bytes != kInputBytes || !shape_is(input, kInputDims, 4) ||
        output == nullptr || output->type != kTfLiteInt8 ||
        output->bytes != kOutputBytes || !shape_is(output, kOutputDims, 2))
      {
        std::printf("TINYPOSE-BENCH tensor_contract=FAIL\n");
        ret = -EPROTO;
        goto out;
      }

    std::memcpy(input->data.int8, g_velafit_tiny_pose_fixture, kInputBytes);
    std::printf("TINYPOSE-META init_wall_us=%llu init_cycles=%llu "
                "init_cycle_us_400mhz=%llu arena_alloc=%u arena_used=%u "
                "conv_backend=%s\n",
                static_cast<unsigned long long>(init_us),
                static_cast<unsigned long long>(init_cycles),
                static_cast<unsigned long long>(init_cycles / 400),
                static_cast<unsigned int>(kArenaBytes),
                static_cast<unsigned int>(interpreter->arena_used_bytes()),
#ifdef VELAFIT_ESP_NN_TEST
                "ESP_NN_PIE"
#else
                "TFLM_DEFAULT"
#endif
                );
    std::printf("TINYPOSE-META input_type=INT8 scale=%.9g zero_point=%ld bytes=%u\n",
                static_cast<double>(input->params.scale),
                static_cast<long>(input->params.zero_point),
                static_cast<unsigned int>(input->bytes));
    std::printf("TINYPOSE-META output_type=INT8 scale=%.9g zero_point=%ld bytes=%u\n",
                static_cast<double>(output->params.scale),
                static_cast<long>(output->params.zero_point),
                static_cast<unsigned int>(output->bytes));
    print_heap("after_init");

    for (unsigned int warmup = 0; warmup < kWarmupRounds; warmup++)
      {
        std::memcpy(input->data.int8, g_velafit_tiny_pose_fixture,
                    kInputBytes);
        if (interpreter->Invoke() != kTfLiteOk)
          {
            ret = -EIO;
            goto out;
          }

        mismatches += output_differences(output);
        if (warmup == 0)
          {
            std::memcpy(device_reference, output->data.int8, kOutputBytes);
            print_output("device", device_reference);
            print_output("host", g_velafit_tiny_pose_oracle);
          }
        else
          {
            repeat_mismatches += byte_differences(
              output->data.int8, device_reference);
          }
      }

    for (; completed < rounds; completed++)
      {
        std::memcpy(input->data.int8, g_velafit_tiny_pose_fixture,
                    kInputBytes);
        const uint64_t started = monotonic_us();
        if (interpreter->Invoke() != kTfLiteOk)
          {
            ret = -EIO;
            break;
          }

        const uint64_t elapsed = monotonic_us() - started;
        if (elapsed > UINT32_MAX)
          {
            ret = -EOVERFLOW;
            break;
          }

        samples[completed] = static_cast<uint32_t>(elapsed);
        total += elapsed;
        mismatches += output_differences(output);
        repeat_mismatches += byte_differences(
          output->data.int8, device_reference);
      }

    print_heap("after_invoke");
  }

out:
  const size_t arena_used = interpreter == nullptr ? 0 :
                            interpreter->arena_used_bytes();
  delete interpreter;
  delete resolver;
  free(arena);
  print_heap("after_deinit");

  if (ret == 0 && mismatches != 0)
    {
      ret = -EIO;
    }

  uint32_t minimum = 0;
  uint32_t p50 = 0;
  uint32_t p95 = 0;
  uint32_t maximum = 0;
  if (completed != 0)
    {
      std::sort(samples, samples + completed);
      minimum = samples[0];
      p50 = percentile(samples, completed, 50);
      p95 = percentile(samples, completed, 95);
      maximum = samples[completed - 1];
    }

  const bool complete = ret == 0 && completed == rounds;
  std::printf("TINYPOSE-BENCH result=%d completed=%u/%u warmup=%u "
              "host_mismatches=%u repeat_mismatches=%u arena_used=%u min_us=%lu avg_us=%llu "
              "p50_us=%lu p95_us=%lu max_us=%lu gate_150ms=%s "
              "gate_200ms=%s\n",
              ret, completed, rounds, kWarmupRounds, mismatches,
              repeat_mismatches,
              static_cast<unsigned int>(arena_used),
              static_cast<unsigned long>(minimum),
              static_cast<unsigned long long>(completed ? total / completed : 0),
              static_cast<unsigned long>(p50),
              static_cast<unsigned long>(p95),
              static_cast<unsigned long>(maximum),
              complete && p95 < 150000 ? "PASS" : "FAIL",
              complete && maximum < 200000 ? "PASS" : "FAIL");
  free(samples);
  return ret;
}
