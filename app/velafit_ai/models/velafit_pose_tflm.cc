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
#include "velafit_pose_oracle.h"
#ifdef VELAFIT_POSE_LUT
TFLMRegistration velafit_register_quantize_lut();
TFLMRegistration velafit_register_dequantize_lut();
TFLMRegistration velafit_register_logistic_lut();
TFLMRegistration velafit_register_add_lut();
TFLMRegistration velafit_register_sub_lut();
TFLMRegistration velafit_register_mul_lut();
#endif
#ifdef VELAFIT_ESP_NN_TEST
TFLMRegistration velafit_register_conv_pie();
TFLMRegistration velafit_register_depthwise_pie();
#endif

extern "C"
{
extern const unsigned char
  g_movenet_singlepose_lightning_int8_v4_model_data[];
extern const unsigned int
  g_movenet_singlepose_lightning_int8_v4_model_data_size;
extern const unsigned int g_velafit_pose_fixture_rgb192_size;
extern const unsigned char g_velafit_pose_fixture_rgb192[];
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
bool g_use_pie = false;
bool g_copy_weights = false;
bool g_profile_enabled = true;
void *g_model_copy;

uint64_t monotonic_us()
{
  struct timespec ts;
  return clock_gettime(CLOCK_MONOTONIC, &ts) == 0 ?
         static_cast<uint64_t>(ts.tv_sec) * 1000000ull +
         static_cast<uint64_t>(ts.tv_nsec) / 1000ull : 0;
}

uint64_t machine_cycles()
{
  uint32_t high, low, check;
  do
    {
      __asm__ volatile("csrr %0, 0xb80\ncsrr %1, 0xb00\ncsrr %2, 0xb80"
                       : "=r"(high), "=r"(low), "=r"(check) :: "memory");
    }
  while (high != check);
  return (static_cast<uint64_t>(high) << 32) | low;
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
#ifdef VELAFIT_POSE_BINARY_LUT
  if (resolver.AddAdd(g_use_pie ? velafit_register_add_lut() : tflite::Register_ADD()) != kTfLiteOk)
    return kTfLiteError;
#else
  ADD_OP(AddAdd);
#endif
  ADD_OP(AddArgMax);
  ADD_OP(AddCast);
  ADD_OP(AddConcatenation);
#ifdef VELAFIT_ESP_NN_TEST
  if (g_use_pie)
    {
      if (resolver.AddConv2D(velafit_register_conv_pie()) != kTfLiteOk)
        return kTfLiteError;
    }
  else
#endif
    {
      ADD_OP(AddConv2D);
    }
#ifdef VELAFIT_ESP_NN_TEST
  if (g_use_pie)
    {
      if (resolver.AddDepthwiseConv2D(velafit_register_depthwise_pie()) != kTfLiteOk)
        return kTfLiteError;
    }
  else
#endif
    {
      ADD_OP(AddDepthwiseConv2D);
    }
#ifdef VELAFIT_POSE_LUT
  if (resolver.AddDequantize(g_use_pie ? velafit_register_dequantize_lut() : tflite::Register_DEQUANTIZE()) != kTfLiteOk)
    return kTfLiteError;
#else
  ADD_OP(AddDequantize);
#endif
  ADD_OP(AddDiv);
  ADD_OP(AddFloorDiv);
  ADD_OP(AddGatherNd);
#ifdef VELAFIT_POSE_LUT
  ADD_OP(AddLogistic);
  if (g_use_pie)
    {
      // This baseline exposes no AddLogistic(registration) overload. The
      // registration belongs to our mutable resolver; retain opcode/parser.
      auto *op = const_cast<TFLMRegistration *>(resolver.FindOp(tflite::BuiltinOperator_LOGISTIC));
      if (!op) return kTfLiteError;
      const auto replacement = velafit_register_logistic_lut();
      op->init = replacement.init;
      op->prepare = replacement.prepare;
      op->invoke = replacement.invoke;
    }
#else
  ADD_OP(AddLogistic);
#endif
#ifdef VELAFIT_POSE_BINARY_LUT
  if (resolver.AddMul(g_use_pie ? velafit_register_mul_lut() : tflite::Register_MUL()) != kTfLiteOk)
    return kTfLiteError;
#else
  ADD_OP(AddMul);
#endif
  ADD_OP(AddPack);
#ifdef VELAFIT_POSE_LUT
  if (resolver.AddQuantize(g_use_pie ? velafit_register_quantize_lut() : tflite::Register_QUANTIZE()) != kTfLiteOk)
    return kTfLiteError;
#else
  ADD_OP(AddQuantize);
#endif
  ADD_OP(AddReshape);
  ADD_OP(AddResizeBilinear);
  ADD_OP(AddSqrt);
#ifdef VELAFIT_POSE_BINARY_LUT
  if (resolver.AddSub(g_use_pie ? velafit_register_sub_lut() : tflite::Register_SUB()) != kTfLiteOk)
    return kTfLiteError;
#else
  ADD_OP(AddSub);
#endif
  ADD_OP(AddUnpack);
#undef ADD_OP
  return kTfLiteOk;
}

void cleanup()
{
  delete g_interpreter;
  delete g_resolver;
  free(g_arena);
  free(g_model_copy);
  g_model_copy = nullptr;
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

  const unsigned char *model_bytes = g_movenet_singlepose_lightning_int8_v4_model_data;
  if (g_use_pie && g_copy_weights)
    {
      g_model_copy = memalign(16, g_movenet_singlepose_lightning_int8_v4_model_data_size);
      if (!g_model_copy) return -ENOMEM;
      std::memcpy(g_model_copy, model_bytes, g_movenet_singlepose_lightning_int8_v4_model_data_size);
      model_bytes = static_cast<const unsigned char *>(g_model_copy);
      std::printf("[VELAFIT-TFLM] private model copy=%p bytes=%u\n", g_model_copy,
                  g_movenet_singlepose_lightning_int8_v4_model_data_size);
    }
  const tflite::Model *model = tflite::GetModel(model_bytes);
  if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION)
    {
      cleanup();
      return -EPROTO;
    }

  size_t arena_bytes = CONFIG_VELAFIT_TFLM_RUNNER_ARENA_SIZE;
#ifdef VELAFIT_QACC_CONV
  if (g_use_pie) arena_bytes = 8 * 1024 * 1024;
#endif
#ifdef VELAFIT_POSE_BINARY_LUT
  if (g_use_pie) arena_bytes = 12 * 1024 * 1024;
#endif
  g_resolver = new (std::nothrow) Resolver;
  g_arena = memalign(16, arena_bytes);
  if (g_resolver == nullptr || g_arena == nullptr ||
      register_ops(*g_resolver) != kTfLiteOk)
    {
      cleanup();
      return -ENOMEM;
    }

  g_interpreter = new (std::nothrow) tflite::MicroInterpreter(
    model, *g_resolver, static_cast<uint8_t *>(g_arena),
    arena_bytes, nullptr,
#ifdef CONFIG_VELAFIT_POSE_OPERATOR_PROFILE
    g_profile_enabled ? &g_profiler : nullptr
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
              static_cast<unsigned int>(arena_bytes));
  return OK;
}

extern "C" int velafit_pose_pie_ram_compare(void)
{
  g_copy_weights = true;
  int ret = velafit_pose_pie_compare();
  g_copy_weights = false;
  return ret;
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
  if (g_profile_enabled) g_profiler.Reset();
#endif

  if (g_interpreter->Invoke() != kTfLiteOk)
    {
      return -EIO;
    }

  const uint64_t post_start = monotonic_us();
#ifdef CONFIG_VELAFIT_POSE_OPERATOR_PROFILE
  if (g_profile_enabled) g_profiler.Report(post_start - invoke_start);
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

extern "C" int velafit_pose_pie_compare(void)
{
#ifdef VELAFIT_ESP_NN_TEST
  if (g_interpreter != nullptr) return -EBUSY;
  pose_frame_t reference{};
  pose_frame_t accelerated{};
  velafit_perf_t refperf{};
  velafit_perf_t fastperf{};
  g_use_pie = false;
  int ret = velafit_pose_tflm_init();
  if (ret == 0) ret = velafit_pose_tflm_infer(g_velafit_pose_fixture_rgb192, &reference, &refperf);
  cleanup();
  if (ret != 0) return ret;
  g_use_pie = true;
  ret = velafit_pose_tflm_init();
  if (ret == 0) ret = velafit_pose_tflm_infer(g_velafit_pose_fixture_rgb192, &accelerated, &fastperf);
  cleanup();
  g_use_pie = false;
  if (ret != 0) return ret;
  unsigned int different = 0;
  float max_error = 0;
  for (unsigned int i = 0; i < VELAFIT_NUM_KEYPOINTS; i++)
    {
      const float expected[] = {reference.kpts[i].x, reference.kpts[i].y, reference.kpts[i].score};
      const float actual[] = {accelerated.kpts[i].x, accelerated.kpts[i].y, accelerated.kpts[i].score};
      for (unsigned int j = 0; j < 3; j++)
        {
          uint32_t a, b;
          std::memcpy(&a, &expected[j], sizeof(a));
          std::memcpy(&b, &actual[j], sizeof(b));
          different += a != b;
          float error = std::fabs(expected[j] - actual[j]);
          if (error > max_error) max_error = error;
          std::printf("POSE-COMPARE k=%u component=%u ref=%08lx pie=%08lx\n", i, j,
                      static_cast<unsigned long>(a), static_cast<unsigned long>(b));
        }
    }
  std::printf("POSE-COMPARE different=%u/51 max_abs=%.9f reference_us=%lu pie_us=%lu "
              "profile=configuration-dependent conv+depthwise\n", different, static_cast<double>(max_error),
              static_cast<unsigned long>(refperf.infer_us), static_cast<unsigned long>(fastperf.infer_us));
  return different ? -EIO : 0;
#else
  return -ENOTSUP;
#endif
}

extern "C" bool velafit_pose_profile_enabled(void)
{
  return g_profile_enabled;
}

extern "C" void velafit_pose_profile_set(bool enabled)
{
  g_profile_enabled = enabled;
}

static int pose_benchmark(unsigned int rounds, bool saved_oracle)
{
#ifdef VELAFIT_ESP_NN_TEST
  if (rounds == 0 || rounds > 1000) return -EINVAL;
  if (g_interpreter) return -EBUSY;
  pose_frame_t expected{}, actual{};
  velafit_perf_t perf{};
  g_profile_enabled = false;
  g_use_pie = false;
  int ret = 0;
  if (saved_oracle)
    {
      for (unsigned int k=0; k<VELAFIT_NUM_KEYPOINTS; k++)
        {
          std::memcpy(&expected.kpts[k].x, &vf_pose_oracle[k][0], sizeof(float));
          std::memcpy(&expected.kpts[k].y, &vf_pose_oracle[k][1], sizeof(float));
          std::memcpy(&expected.kpts[k].score, &vf_pose_oracle[k][2], sizeof(float));
        }
      std::printf("POSE-BENCH oracle=archived-reference same-model-and-fixture-only\n");
    }
  else
    {
      ret = velafit_pose_tflm_init();
      if (!ret) ret = velafit_pose_tflm_infer(g_velafit_pose_fixture_rgb192, &expected, &perf);
      cleanup();
    }
  uint64_t total = 0;
  uint32_t minimum = UINT32_MAX, maximum = 0;
  unsigned int completed = 0;
  g_use_pie = true;
  g_copy_weights = true;
  const uint64_t init_start = monotonic_us();
  if (!ret) ret = velafit_pose_tflm_init();
  const uint64_t init_us = monotonic_us() - init_start;
  while (!ret && completed < rounds)
    {
      const uint64_t cycles_start = machine_cycles();
      const uint64_t wall_start = monotonic_us();
      ret = velafit_pose_tflm_infer(g_velafit_pose_fixture_rgb192, &actual, &perf);
      const uint64_t cycles = machine_cycles() - cycles_start;
      const uint64_t wall_us = monotonic_us() - wall_start;
      for (unsigned int k = 0; !ret && k < VELAFIT_NUM_KEYPOINTS; k++)
        {
          // Compare only the 51 float components, not structure padding.
          if (std::memcmp(&actual.kpts[k].x, &expected.kpts[k].x, sizeof(float)) ||
              std::memcmp(&actual.kpts[k].y, &expected.kpts[k].y, sizeof(float)) ||
              std::memcmp(&actual.kpts[k].score, &expected.kpts[k].score, sizeof(float)))
            ret = -EIO;
        }
      if (ret) break;
      completed++;
      total += perf.infer_us;
      if (perf.infer_us < minimum) minimum = perf.infer_us;
      if (perf.infer_us > maximum) maximum = perf.infer_us;
      std::printf("POSE-BENCH round=%u/%u us=%lu identical=51/51\n", completed, rounds,
                  static_cast<unsigned long>(perf.infer_us));
      std::printf("POSE-CLOCK cycles=%llu wall_us=%llu cycles_per_us=%llu\n",
                  static_cast<unsigned long long>(cycles),
                  static_cast<unsigned long long>(wall_us),
                  static_cast<unsigned long long>(wall_us ? cycles / wall_us : 0));
    }
  cleanup();
  g_use_pie = false;
  g_copy_weights = false;
  g_profile_enabled = true;
  std::printf("POSE-BENCH result=%d completed=%u/%u init_us=%llu min_us=%lu "
              "mean_us=%llu max_us=%lu gate_2s=%s gate_200ms=%s profiler=off fixed-only\n",
              ret, completed, rounds, static_cast<unsigned long long>(init_us),
              static_cast<unsigned long>(completed ? minimum : 0),
              static_cast<unsigned long long>(completed ? total / completed : 0),
              static_cast<unsigned long>(maximum),
              !ret && completed == rounds && maximum <= 2000000 ? "PASS" : "FAIL",
              !ret && completed == rounds && maximum <= 200000 ? "PASS" : "FAIL");
  return ret;
#else
  return -ENOTSUP;
#endif
}

extern "C" int velafit_pose_pie_benchmark(unsigned int rounds)
{
  return pose_benchmark(rounds, false);
}

extern "C" int velafit_pose_pie_fast_benchmark(unsigned int rounds)
{
  return pose_benchmark(rounds, true);
}
