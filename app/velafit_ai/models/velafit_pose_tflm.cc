/****************************************************************************
 * app/velafit_ai/models/velafit_pose_tflm.cc
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <errno.h>
#include <malloc.h>
#include <new>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_profiler_interface.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "velafit_pose_tflm.h"

extern "C"
{
extern const unsigned char
  g_movenet_singlepose_lightning_int8_v4_model_data[];
extern const unsigned int
  g_movenet_singlepose_lightning_int8_v4_model_data_size;
extern const unsigned int g_velafit_pose_fixture_rgb192_size;
}

namespace
{

constexpr int kInputDims[] = {1, 192, 192, 3};
constexpr int kOutputDims[] = {1, 1, 17, 3};
constexpr unsigned int kOpCount = 19;
using Resolver = tflite::MicroMutableOpResolver<kOpCount>;

Resolver *g_resolver;
tflite::MicroInterpreter *g_interpreter;
void *g_arena;

uint64_t monotonic_us()
{
  struct timespec ts;
  return clock_gettime(CLOCK_MONOTONIC, &ts) == 0 ?
         static_cast<uint64_t>(ts.tv_sec) * 1000000ull +
         static_cast<uint64_t>(ts.tv_nsec) / 1000ull : 0;
}

#ifdef CONFIG_VELAFIT_POSE_OPERATOR_PROFILE
class OperatorProfiler final : public tflite::MicroProfilerInterface
{
 public:
  static constexpr unsigned int kMaxTags = 24;

  uint32_t BeginEvent(const char *tag) override
  {
    unsigned int slot = 0;
    for (; slot < used_; slot++)
      {
        if (std::strcmp(entries_[slot].tag, tag) == 0)
          {
            break;
          }
      }

    if (slot == used_)
      {
        if (used_ == kMaxTags)
          {
            overflow_++;
            return UINT32_MAX;
          }

        entries_[slot].tag = tag;
        used_++;
      }

    entries_[slot].started_us = monotonic_us();
    return slot;
  }

  void EndEvent(uint32_t handle) override
  {
    if (handle >= used_)
      {
        return;
      }

    Entry &entry = entries_[handle];
    entry.total_us += monotonic_us() - entry.started_us;
    entry.calls++;
  }

  void Reset()
  {
    std::memset(entries_, 0, sizeof(entries_));
    used_ = 0;
    overflow_ = 0;
  }

  void Report(uint64_t invoke_us) const
  {
    std::printf("[VELAFIT-PROFILE] operator,count,total_us,avg_us,invoke_pct\n");
    for (unsigned int i = 0; i < used_; i++)
      {
        const Entry &entry = entries_[i];
        const uint64_t average = entry.calls == 0 ? 0 :
                                 entry.total_us / entry.calls;
        const double percentage = invoke_us == 0 ? 0.0 :
                                  100.0 * entry.total_us / invoke_us;
        std::printf("[VELAFIT-PROFILE] %s,%lu,%llu,%llu,%.2f\n",
                    entry.tag, static_cast<unsigned long>(entry.calls),
                    static_cast<unsigned long long>(entry.total_us),
                    static_cast<unsigned long long>(average), percentage);
      }

    std::printf("[VELAFIT-PROFILE] tags=%u overflow=%u\n", used_, overflow_);
  }

 private:
  struct Entry
  {
    const char *tag;
    uint64_t started_us;
    uint64_t total_us;
    uint32_t calls;
  };

  Entry entries_[kMaxTags]{};
  unsigned int used_ = 0;
  unsigned int overflow_ = 0;
};

OperatorProfiler g_profiler;
#endif

bool shape_is(const TfLiteTensor *tensor, const int *dims, size_t count)
{
  if (tensor == nullptr || tensor->dims == nullptr ||
      tensor->dims->size != static_cast<int>(count))
    {
      return false;
    }

  for (size_t i = 0; i < count; i++)
    {
      if (tensor->dims->data[i] != dims[i])
        {
          return false;
        }
    }

  return true;
}

TfLiteStatus register_ops(Resolver &resolver)
{
#define ADD_OP(method) if (resolver.method() != kTfLiteOk) return kTfLiteError
  ADD_OP(AddAdd);
  ADD_OP(AddArgMax);
  ADD_OP(AddCast);
  ADD_OP(AddConcatenation);
  ADD_OP(AddConv2D);
  ADD_OP(AddDepthwiseConv2D);
  ADD_OP(AddDequantize);
  ADD_OP(AddDiv);
  ADD_OP(AddFloorDiv);
  ADD_OP(AddGatherNd);
  ADD_OP(AddLogistic);
  ADD_OP(AddMul);
  ADD_OP(AddPack);
  ADD_OP(AddQuantize);
  ADD_OP(AddReshape);
  ADD_OP(AddResizeBilinear);
  ADD_OP(AddSqrt);
  ADD_OP(AddSub);
  ADD_OP(AddUnpack);
#undef ADD_OP
  return kTfLiteOk;
}

void cleanup()
{
  delete g_interpreter;
  delete g_resolver;
  free(g_arena);
  g_interpreter = nullptr;
  g_resolver = nullptr;
  g_arena = nullptr;
}

}  // namespace

extern "C" int velafit_pose_tflm_init(void)
{
  if (g_interpreter != nullptr)
    {
      return OK;
    }

  if (g_velafit_pose_fixture_rgb192_size != 192u * 192u * 3u)
    {
      std::printf("[VELAFIT-TFLM] fixture size FAIL: %u\n",
                  g_velafit_pose_fixture_rgb192_size);
      return -EINVAL;
    }

  const tflite::Model *model = tflite::GetModel(
    g_movenet_singlepose_lightning_int8_v4_model_data);
  if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION)
    {
      return -EPROTO;
    }

  g_resolver = new (std::nothrow) Resolver;
  g_arena = memalign(16, CONFIG_VELAFIT_TFLM_RUNNER_ARENA_SIZE);
  if (g_resolver == nullptr || g_arena == nullptr ||
      register_ops(*g_resolver) != kTfLiteOk)
    {
      cleanup();
      return -ENOMEM;
    }

  g_interpreter = new (std::nothrow) tflite::MicroInterpreter(
    model, *g_resolver, static_cast<uint8_t *>(g_arena),
    CONFIG_VELAFIT_TFLM_RUNNER_ARENA_SIZE, nullptr,
#ifdef CONFIG_VELAFIT_POSE_OPERATOR_PROFILE
    &g_profiler
#else
    nullptr
#endif
    );
  if (g_interpreter == nullptr ||
      g_interpreter->AllocateTensors() != kTfLiteOk)
    {
      cleanup();
      return -ENOMEM;
    }

  TfLiteTensor *input = g_interpreter->input(0);
  TfLiteTensor *output = g_interpreter->output(0);
  if (input == nullptr || input->type != kTfLiteUInt8 ||
      input->bytes != 192u * 192u * 3u || !shape_is(input, kInputDims, 4) ||
      output == nullptr || output->type != kTfLiteFloat32 ||
      output->bytes != 17u * 3u * sizeof(float) ||
      !shape_is(output, kOutputDims, 4))
    {
      cleanup();
      return -EPROTO;
    }

  std::printf("[VELAFIT-TFLM] init PASS model=%u arena=%u\n",
              g_movenet_singlepose_lightning_int8_v4_model_data_size,
              static_cast<unsigned int>(CONFIG_VELAFIT_TFLM_RUNNER_ARENA_SIZE));
  return OK;
}

extern "C" void velafit_pose_tflm_deinit(void)
{
  cleanup();
}

extern "C" int velafit_pose_tflm_infer(const uint8_t *rgb192,
                                         pose_frame_t *out_pose,
                                         velafit_perf_t *perf)
{
  if (rgb192 == nullptr || out_pose == nullptr || g_interpreter == nullptr)
    {
      return -EINVAL;
    }

  TfLiteTensor *input = g_interpreter->input(0);
  TfLiteTensor *output = g_interpreter->output(0);
  const uint64_t pre_start = monotonic_us();
  std::memcpy(input->data.uint8, rgb192, input->bytes);
  const uint64_t invoke_start = monotonic_us();

#ifdef CONFIG_VELAFIT_POSE_OPERATOR_PROFILE
  g_profiler.Reset();
#endif

  if (g_interpreter->Invoke() != kTfLiteOk)
    {
      return -EIO;
    }

  const uint64_t post_start = monotonic_us();
#ifdef CONFIG_VELAFIT_POSE_OPERATOR_PROFILE
  g_profiler.Report(post_start - invoke_start);
#endif
  unsigned int confident = 0;
  std::memset(out_pose, 0, sizeof(*out_pose));

  for (unsigned int i = 0; i < VELAFIT_NUM_KEYPOINTS; i++)
    {
      const float y = output->data.f[i * 3];
      const float x = output->data.f[i * 3 + 1];
      const float score = output->data.f[i * 3 + 2];
      if (!std::isfinite(x) || !std::isfinite(y) ||
          !std::isfinite(score) || x < 0.0f || x > 1.0f ||
          y < 0.0f || y > 1.0f || score < 0.0f || score > 1.0f)
        {
          return -ERANGE;
        }

      out_pose->kpts[i].x = x;
      out_pose->kpts[i].y = y;
      out_pose->kpts[i].score = score;
      confident += score >= 0.2f;
    }

  const uint64_t ended = monotonic_us();
  out_pose->valid = confident >= 5;
  if (perf != nullptr)
    {
      std::memset(perf, 0, sizeof(*perf));
      perf->preprocess_us = static_cast<uint32_t>(invoke_start - pre_start);
      perf->infer_us = static_cast<uint32_t>(post_start - invoke_start);
      perf->postprocess_us = static_cast<uint32_t>(ended - post_start);
      perf->total_us = static_cast<uint32_t>(ended - pre_start);
      perf->fps = perf->total_us == 0 ? 0.0f :
                  1000000.0f / static_cast<float>(perf->total_us);
    }

  std::printf("[VELAFIT-TFLM] output PASS confident=%u/17 valid=%s\n",
              confident, out_pose->valid ? "yes" : "no");
  return out_pose->valid ? OK : -ENODATA;
}
