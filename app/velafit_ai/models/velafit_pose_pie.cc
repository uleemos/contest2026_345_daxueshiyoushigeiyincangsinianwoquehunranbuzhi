/* SPDX-License-Identifier: Apache-2.0
 * MoveNet-local registration: existing TFLM/KWS kernels are not replaced.
 */
#include <nuttx/config.h>
#include "tensorflow/lite/micro/kernels/conv.h"
#include "tensorflow/lite/micro/kernels/depthwise_conv.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/micro_log.h"
#include <cstring>
#include <ctime>
#include <pthread.h>
#include "velafit_channel_pack.h"
#ifdef VELAFIT_QACC_CONV
#include "velafit_qacc_conv.h"
#endif

#ifdef VELAFIT_ESP_NN_TEST
extern "C" bool velafit_pose_profile_enabled(void);
extern "C" {
#include <esp_nn_riscv_pie.h>
int velafit_esp_nn_lock(void);
void velafit_esp_nn_unlock(void);
}

namespace {
struct ConvData
{
  void *original;
  int scratch_index;
  int scratch_bytes;
  data_dims_t input;
  data_dims_t filter;
  data_dims_t output;
  conv_params_t params;
#ifdef VELAFIT_QACC_CONV
  int8_t *packed;
  int32_t *combined;
#endif
};

void *Init(TfLiteContext *ctx, const char *buffer, size_t length)
{
  auto *data = static_cast<ConvData *>(ctx->AllocatePersistentBuffer(ctx, sizeof(ConvData)));
  if (!data) return nullptr;
  std::memset(data, 0, sizeof(*data));
  data->scratch_index = -1;
  data->original = tflite::Register_CONV_2D().init(ctx, buffer, length);
  return data->original ? data : nullptr;
}

TfLiteStatus Prepare(TfLiteContext *ctx, TfLiteNode *node)
{
  auto *data = static_cast<ConvData *>(node->user_data);
  if (!data || !data->original) return kTfLiteError;
  node->user_data = data->original;
  auto status = tflite::Register_CONV_2D().prepare(ctx, node);
  node->user_data = data;
  if (status != kTfLiteOk) return status;
  auto *in = tflite::micro::GetEvalInput(ctx, node, 0);
  auto *filter = tflite::micro::GetEvalInput(ctx, node, 1);
  auto *out = tflite::micro::GetEvalOutput(ctx, node, 0);
  auto *params = static_cast<TfLiteConvParams *>(node->builtin_data);
  auto *base = static_cast<tflite::OpDataConv *>(data->original);
  if (in->type != kTfLiteInt8 || filter->type != kTfLiteInt8 || out->type != kTfLiteInt8 ||
      in->dims->size != 4 || filter->dims->size != 4 || out->dims->size != 4 ||
      in->dims->data[0] != 1 || out->dims->data[0] != 1 ||
      params->dilation_width_factor != 1 || params->dilation_height_factor != 1 ||
      base->filter_zero_point != 0) return kTfLiteOk;  // reference fallback
  data->input = {in->dims->data[2], in->dims->data[1], in->dims->data[3], 1};
  data->filter = {filter->dims->data[2], filter->dims->data[1], filter->dims->data[3], 1};
  data->output = {out->dims->data[2], out->dims->data[1], out->dims->data[3], 1};
  data->params = {-base->input_zero_point, base->output_zero_point,
                 {params->stride_width, params->stride_height},
                 {base->padding.width, base->padding.height}, {1, 1},
                 {base->output_activation_min, base->output_activation_max}};
  const auto &f = data->filter;
  const auto &p = data->params;
#ifdef VELAFIT_QACC_CONV
  const int64_t elements = static_cast<int64_t>(f.width) * f.height * data->input.channels;
  const int64_t padded = (elements + 15) & ~15ll;
  const int64_t outputs = (static_cast<int64_t>(data->output.channels) + 15) & ~15ll;
  if (elements <= 0 || padded * outputs > 4 * 1024 * 1024) return kTfLiteError;
  data->packed = static_cast<int8_t *>(ctx->AllocatePersistentBuffer(ctx, padded * outputs));
  data->combined = static_cast<int32_t *>(ctx->AllocatePersistentBuffer(ctx, data->output.channels * sizeof(int32_t)));
  if (!data->packed || !data->combined) return kTfLiteError;
  auto *bias = node->inputs->size > 2 && node->inputs->data[2] != kTfLiteOptionalTensor ?
               tflite::micro::GetEvalInput(ctx, node, 2) : nullptr;
  vf_qacc_pack(filter->data.int8, bias ? bias->data.i32 : nullptr, data->packed,
               data->combined, elements, data->output.channels, p.in_offset);
  data->scratch_bytes = padded;
  return ctx->RequestScratchBufferInArena(ctx, data->scratch_bytes + 32, &data->scratch_index);
#endif
  // Exclude upstream undersized tiled scratch for tiny padded windows.
  const int64_t window = static_cast<int64_t>(f.width) * f.height * data->input.channels;
  if (window < 16 && !(f.width == 1 && f.height == 1 &&
      p.padding.width == 0 && p.padding.height == 0 &&
      p.stride.width == 1 && p.stride.height == 1)) return kTfLiteOk;
  data->scratch_bytes = esp_nn_get_conv_scratch_size_riscv_pie(
      &data->input, &data->filter, &data->output, &data->params);
  if (data->scratch_bytes <= 0 || data->scratch_bytes > 1024 * 1024) return kTfLiteError;
  return ctx->RequestScratchBufferInArena(ctx, data->scratch_bytes + 32, &data->scratch_index);
}

TfLiteStatus Eval(TfLiteContext *ctx, TfLiteNode *node)
{
  auto *data = static_cast<ConvData *>(node->user_data);
  if (data->scratch_index < 0)
    {
      node->user_data = data->original;
      auto status = tflite::Register_CONV_2D().invoke(ctx, node);
      node->user_data = data;
      return status;
    }
  auto *in = tflite::micro::GetEvalInput(ctx, node, 0);
  auto *filter = tflite::micro::GetEvalInput(ctx, node, 1);
  auto *bias = node->inputs->size > 2 && node->inputs->data[2] != kTfLiteOptionalTensor ?
               tflite::micro::GetEvalInput(ctx, node, 2) : nullptr;
  auto *out = tflite::micro::GetEvalOutput(ctx, node, 0);
  auto *scratch = static_cast<uint8_t *>(ctx->GetScratchBuffer(ctx, data->scratch_index));
  auto *base = static_cast<tflite::OpDataConv *>(data->original);
  if (!scratch || velafit_esp_nn_lock() != 0) return kTfLiteError;
  std::memset(scratch + data->scratch_bytes, 0xa5, 32);
  const quant_data_t quant = {base->per_channel_output_shift, base->per_channel_output_multiplier};
  __asm__ volatile("csrwi 0x7f1, 1" ::: "memory");
#ifdef CONFIG_VELAFIT_POSE_OPERATOR_PROFILE
  struct timespec started, stopped;
  clock_gettime(CLOCK_MONOTONIC, &started);
#endif
  esp_nn_set_conv_scratch_buf_riscv_pie(scratch);
#ifdef VELAFIT_QACC_CONV
  vf_qacc_conv(&data->input, in->data.int8, &data->filter, data->packed,
              data->combined, &data->output, out->data.int8, &data->params, &quant,
              reinterpret_cast<int8_t *>(scratch));
#else
  esp_nn_conv_s8_riscv_pie(&data->input, in->data.int8, &data->filter, filter->data.int8,
                         bias ? bias->data.i32 : nullptr, &data->output, out->data.int8,
                         &data->params, &quant);
#endif
  esp_nn_set_conv_scratch_buf_riscv_pie(nullptr);
  __asm__ volatile("csrwi 0x7f2, 0\ncsrwi 0x7f1, 0" ::: "memory");
  bool good = true;
  for (int i = 0; i < 32; i++) good &= scratch[data->scratch_bytes + i] == 0xa5;
  velafit_esp_nn_unlock();
#ifdef CONFIG_VELAFIT_POSE_OPERATOR_PROFILE
  clock_gettime(CLOCK_MONOTONIC, &stopped);
  const int64_t elapsed = (stopped.tv_sec - started.tv_sec) * 1000000ll +
                          (stopped.tv_nsec - started.tv_nsec) / 1000;
  if (velafit_pose_profile_enabled() && elapsed >= 50000)
    MicroPrintf("PIE-CONV input=%dx%dx%d filter=%dx%d out_ch=%d us=%lu",
                data->input.width, data->input.height, data->input.channels,
                data->filter.width, data->filter.height, data->output.channels,
                static_cast<unsigned long>(elapsed));
#endif
  if (!good) MicroPrintf("PIE conv scratch guard FAIL");
  return good ? kTfLiteOk : kTfLiteError;
}
} // namespace

TFLMRegistration velafit_register_conv_pie()
{
  auto registration = tflite::Register_CONV_2D();
  registration.init = Init;
  registration.prepare = Prepare;
  registration.invoke = Eval;
  return registration;
}

namespace {
TfLiteStatus DepthwiseEval(TfLiteContext *ctx, TfLiteNode *node)
{
  auto *in = tflite::micro::GetEvalInput(ctx, node, 0);
  auto *filter = tflite::micro::GetEvalInput(ctx, node, 1);
  auto *out = tflite::micro::GetEvalOutput(ctx, node, 0);
  auto *params = static_cast<TfLiteDepthwiseConvParams *>(node->builtin_data);
  auto *base = static_cast<tflite::OpDataConv *>(node->user_data);
  if (in->type != kTfLiteInt8 || filter->type != kTfLiteInt8 || out->type != kTfLiteInt8 ||
      in->dims->size != 4 || filter->dims->size != 4 || out->dims->size != 4 ||
      in->dims->data[0] != 1 || out->dims->data[0] != 1 || in->dims->data[3] < 16 ||
      in->dims->data[3] > 256 ||
      params->depth_multiplier != 1 || params->dilation_width_factor != 1 ||
      params->dilation_height_factor != 1 || base->filter_zero_point != 0)
    // Upstream combined_offset is NULL above256 channels, dropping bias and
    // input offset for vector lanes. Keep those nodes on TFLM reference.
    return tflite::Register_DEPTHWISE_CONV_2D().invoke(ctx, node);
  const data_dims_t input = {in->dims->data[2], in->dims->data[1], in->dims->data[3], 1};
  const data_dims_t weights = {filter->dims->data[2], filter->dims->data[1], filter->dims->data[3], 1};
  const data_dims_t output = {out->dims->data[2], out->dims->data[1], out->dims->data[3], 1};
  const dw_conv_params_t p = {-base->input_zero_point, base->output_zero_point, 1,
                             {params->stride_width, params->stride_height},
                             {base->padding.width, base->padding.height}, {1, 1},
                             {base->output_activation_min, base->output_activation_max}};
  const quant_data_t quant = {base->per_channel_output_shift, base->per_channel_output_multiplier};
  auto *bias = node->inputs->size > 2 && node->inputs->data[2] != kTfLiteOptionalTensor ?
               tflite::micro::GetEvalInput(ctx, node, 2) : nullptr;
  if (velafit_esp_nn_lock() != 0) return kTfLiteError;
  __asm__ volatile("csrwi 0x7f1, 1" ::: "memory");
  esp_nn_depthwise_conv_s8_riscv_pie(&input, in->data.int8, &weights, filter->data.int8,
      bias ? bias->data.i32 : nullptr, &output, out->data.int8, &p, &quant);
  __asm__ volatile("csrwi 0x7f2, 0\ncsrwi 0x7f1, 0" ::: "memory");
  velafit_esp_nn_unlock();
  return kTfLiteOk;
}
}

namespace {
struct DepthwiseData { void *original; int scratch; size_t bytes; };

void *DepthwiseInit(TfLiteContext *ctx, const char *buffer, size_t length)
{
  auto *data = static_cast<DepthwiseData *>(ctx->AllocatePersistentBuffer(ctx, sizeof(DepthwiseData)));
  if (!data) return nullptr;
  data->scratch = -1;
  data->bytes = 0;
  data->original = tflite::Register_DEPTHWISE_CONV_2D().init(ctx, buffer, length);
  return data->original ? data : nullptr;
}

TfLiteStatus DepthwisePrepare(TfLiteContext *ctx, TfLiteNode *node)
{
  auto *data = static_cast<DepthwiseData *>(node->user_data);
  if (!data || !data->original) return kTfLiteError;
  node->user_data = data->original;
  auto status = tflite::Register_DEPTHWISE_CONV_2D().prepare(ctx, node);
  node->user_data = data;
  if (status != kTfLiteOk) return status;
  auto *in = tflite::micro::GetEvalInput(ctx, node, 0);
  auto *filter = tflite::micro::GetEvalInput(ctx, node, 1);
  auto *out = tflite::micro::GetEvalOutput(ctx, node, 0);
  auto *p = static_cast<TfLiteDepthwiseConvParams *>(node->builtin_data);
  auto *base = static_cast<tflite::OpDataConv *>(data->original);
  if (in->type != kTfLiteInt8 || filter->type != kTfLiteInt8 || out->type != kTfLiteInt8 ||
      in->dims->size != 4 || filter->dims->size != 4 || out->dims->size != 4 ||
      in->dims->data[0] != 1 || out->dims->data[0] != 1 ||
#ifdef VELAFIT_ESP_NN_V132
      in->dims->data[3] < 8 ||
#else
      in->dims->data[3] <= 256 || in->dims->data[3] % 16 ||
#endif
      p->depth_multiplier != 1 || p->dilation_width_factor != 1 ||
      p->dilation_height_factor != 1 || base->filter_zero_point != 0) return kTfLiteOk;
#ifdef VELAFIT_ESP_NN_V132
  const data_dims_t input = {in->dims->data[2], in->dims->data[1], in->dims->data[3], 1};
  const data_dims_t weights = {filter->dims->data[2], filter->dims->data[1], filter->dims->data[3], 1};
  const data_dims_t output = {out->dims->data[2], out->dims->data[1], out->dims->data[3], 1};
  const dw_conv_params_t params = {-base->input_zero_point, base->output_zero_point, 1,
      {p->stride_width, p->stride_height}, {base->padding.width, base->padding.height},
      {1, 1}, {base->output_activation_min, base->output_activation_max}};
  const int bytes = esp_nn_get_depthwise_conv_scratch_size_riscv_pie(&input, &weights, &output, &params);
  if (bytes <= 0 || bytes > 1024 * 1024) return kTfLiteError;
  data->bytes = bytes;
#else
  const int64_t positions = static_cast<int64_t>(in->dims->data[1]) * in->dims->data[2] +
                            static_cast<int64_t>(filter->dims->data[1]) * filter->dims->data[2] +
                            static_cast<int64_t>(out->dims->data[1]) * out->dims->data[2];
  if (positions <= 0 || positions > (1024 * 1024 - 32) / 256) return kTfLiteOk;
  data->bytes = positions * 256;
#endif
  return ctx->RequestScratchBufferInArena(ctx, data->bytes + 32, &data->scratch);
}

TfLiteStatus DepthwisePackedEval(TfLiteContext *ctx, TfLiteNode *node)
{
  auto *data = static_cast<DepthwiseData *>(node->user_data);
  if (data->scratch < 0)
    {
      node->user_data = data->original;
      auto status = DepthwiseEval(ctx, node);
      node->user_data = data;
      return status;
    }
  auto *in = tflite::micro::GetEvalInput(ctx, node, 0);
  auto *filter = tflite::micro::GetEvalInput(ctx, node, 1);
  auto *out = tflite::micro::GetEvalOutput(ctx, node, 0);
  auto *bias = node->inputs->size > 2 && node->inputs->data[2] != kTfLiteOptionalTensor ?
               tflite::micro::GetEvalInput(ctx, node, 2) : nullptr;
  auto *params = static_cast<TfLiteDepthwiseConvParams *>(node->builtin_data);
  auto *base = static_cast<tflite::OpDataConv *>(data->original);
  auto *scratch = static_cast<int8_t *>(ctx->GetScratchBuffer(ctx, data->scratch));
  if (!scratch) return kTfLiteError;
  const int channels = in->dims->data[3];
  const int input_positions = in->dims->data[1] * in->dims->data[2];
  const int filter_positions = filter->dims->data[1] * filter->dims->data[2];
  const int output_positions = out->dims->data[1] * out->dims->data[2];
#ifndef VELAFIT_ESP_NN_V132
  int8_t *packed_filter = scratch + input_positions * 256;
  int8_t *packed_output = packed_filter + filter_positions * 256;
#endif
  const dw_conv_params_t p = {-base->input_zero_point, base->output_zero_point, 1,
                             {params->stride_width, params->stride_height},
                             {base->padding.width, base->padding.height}, {1, 1},
                             {base->output_activation_min, base->output_activation_max}};
  if (!scratch || velafit_esp_nn_lock() != 0) return kTfLiteError;
  std::memset(scratch + data->bytes, 0xa5, 32);
  __asm__ volatile("csrwi 0x7f1, 1" ::: "memory");
#ifdef VELAFIT_ESP_NN_V132
  const data_dims_t input = {in->dims->data[2], in->dims->data[1], channels, 1};
  const data_dims_t weights = {filter->dims->data[2], filter->dims->data[1], channels, 1};
  const data_dims_t output = {out->dims->data[2], out->dims->data[1], channels, 1};
  const quant_data_t quant = {base->per_channel_output_shift, base->per_channel_output_multiplier};
  esp_nn_set_depthwise_conv_scratch_buf_riscv_pie(scratch);
  esp_nn_depthwise_conv_s8_riscv_pie(&input, in->data.int8, &weights, filter->data.int8,
      bias ? bias->data.i32 : nullptr, &output, out->data.int8, &p, &quant);
  esp_nn_set_depthwise_conv_scratch_buf_riscv_pie(nullptr);
#else
  for (int offset = 0; offset < channels; offset += 256)
    {
      const int count = channels - offset < 256 ? channels - offset : 256;
      vf_pack_channels(scratch, in->data.int8, input_positions, channels, offset, count);
      vf_pack_channels(packed_filter, filter->data.int8, filter_positions, channels, offset, count);
      const data_dims_t input = {in->dims->data[2], in->dims->data[1], count, 1};
      const data_dims_t weights = {filter->dims->data[2], filter->dims->data[1], count, 1};
      const data_dims_t output = {out->dims->data[2], out->dims->data[1], count, 1};
      const quant_data_t quant = {base->per_channel_output_shift + offset,
                                 base->per_channel_output_multiplier + offset};
      esp_nn_depthwise_conv_s8_riscv_pie(&input, scratch, &weights, packed_filter,
          bias ? bias->data.i32 + offset : nullptr, &output, packed_output, &p, &quant);
      vf_scatter_channels(out->data.int8, packed_output, output_positions, channels, offset, count);
    }
#endif
  __asm__ volatile("csrwi 0x7f2, 0\ncsrwi 0x7f1, 0" ::: "memory");
  bool good = true;
  for (int i = 0; i < 32; i++) good &= static_cast<uint8_t>(scratch[data->bytes + i]) == 0xa5;
  velafit_esp_nn_unlock();
  if (!good) MicroPrintf("PIE packed depthwise scratch guard FAIL");
  return good ? kTfLiteOk : kTfLiteError;
}
}

TFLMRegistration velafit_register_depthwise_pie()
{
  auto registration = tflite::Register_DEPTHWISE_CONV_2D();
  registration.init = DepthwiseInit;
  registration.prepare = DepthwisePrepare;
  registration.invoke = DepthwisePackedEval;
  return registration;
}
#endif
