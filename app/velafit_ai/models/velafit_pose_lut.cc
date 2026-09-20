/* SPDX-License-Identifier: Apache-2.0
 * Exact byte-domain lookup tables, evaluated by the original TFLM operator.
 * No approximate sigmoid, altered rounding, or cached model outputs.
 */
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/quantize.h"
#include "tensorflow/lite/micro/kernels/dequantize.h"
#include "tensorflow/lite/micro/kernels/logistic.h"
#include "tensorflow/lite/micro/micro_utils.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include <cstring>
#include <cstdlib>

namespace {
struct Data { void *original; void *table; bool ready; unsigned int width; };
using Factory = TFLMRegistration (*)();

template<Factory factory>
void *Init(TfLiteContext *ctx, const char *buffer, size_t length)
{
  auto *d = static_cast<Data *>(ctx->AllocatePersistentBuffer(ctx, sizeof(Data)));
  if (!d) return nullptr;
  std::memset(d, 0, sizeof(*d));
  d->original = factory().init(ctx, buffer, length);
  return d->original ? d : nullptr;
}

template<Factory factory>
TfLiteStatus Prepare(TfLiteContext *ctx, TfLiteNode *node)
{
  auto *d = static_cast<Data *>(node->user_data);
  if (!d) return kTfLiteError;
  node->user_data = d->original;
  auto status = factory().prepare(ctx, node);
  node->user_data = d;
  if (status != kTfLiteOk) return status;
  auto *in = tflite::micro::GetEvalInput(ctx, node, 0);
  auto *out = tflite::micro::GetEvalOutput(ctx, node, 0);
  if (in->type != kTfLiteInt8 && in->type != kTfLiteUInt8) return kTfLiteOk;
  if (out->type == kTfLiteInt8 || out->type == kTfLiteUInt8) d->width = 1;
  else if (out->type == kTfLiteFloat32) d->width = 4;
  else return kTfLiteOk;
  d->table = ctx->AllocatePersistentBuffer(ctx, 256 * d->width);
  return d->table ? kTfLiteOk : kTfLiteError;
}

template<Factory factory>
TfLiteStatus Eval(TfLiteContext *ctx, TfLiteNode *node)
{
  auto *d = static_cast<Data *>(node->user_data);
  if (!d->table)
    {
      node->user_data = d->original;
      auto status = factory().invoke(ctx, node);
      node->user_data = d;
      return status;
    }
  auto *in = const_cast<TfLiteEvalTensor *>(tflite::micro::GetEvalInput(ctx, node, 0));
  auto *out = tflite::micro::GetEvalOutput(ctx, node, 0);
  const int count = tflite::micro::GetTensorShape(in).FlatSize();
  if (count != tflite::micro::GetTensorShape(out).FlatSize()) return kTfLiteError;
  if (!d->ready)
    {
      // Only unary per-tensor reference operators are wrapped. They determine
      // loop extents at invoke time and need no scratch buffers. Restore both
      // eval tensors even if the reference invocation fails.
      uint8_t values[256];
      for (int i = 0; i < 256; i++) values[i] = i;
      struct { int size; int data[1]; } shape = {1, {256}};
      const auto saved_in = *in;
      const auto saved_out = *out;
      in->dims = reinterpret_cast<TfLiteIntArray *>(&shape);
      out->dims = in->dims;
      in->data.uint8 = values;
      out->data.raw = static_cast<char *>(d->table);
      node->user_data = d->original;
      const auto status = factory().invoke(ctx, node);
      node->user_data = d;
      *in = saved_in;
      *out = saved_out;
      if (status != kTfLiteOk) return status;
      d->ready = true;
    }
  const auto *input = in->data.uint8;
  if (d->width == 1)
    {
      const auto *table = static_cast<const uint8_t *>(d->table);
      for (int i = 0; i < count; i++) out->data.uint8[i] = table[input[i]];
    }
  else
    {
      const auto *table = static_cast<const float *>(d->table);
      for (int i = 0; i < count; i++) out->data.f[i] = table[input[i]];
    }
  return kTfLiteOk;
}

template<Factory factory>
TFLMRegistration Registration()
{
  auto op = factory();
  op.init = Init<factory>;
  op.prepare = Prepare<factory>;
  op.invoke = Eval<factory>;
  return op;
}
}

TFLMRegistration velafit_register_quantize_lut()
{ return Registration<tflite::Register_QUANTIZE>(); }
TFLMRegistration velafit_register_dequantize_lut()
{ return Registration<tflite::Register_DEQUANTIZE>(); }
TFLMRegistration velafit_register_logistic_lut()
{ return Registration<tflite::Register_LOGISTIC>(); }

namespace {
struct BinaryData
{
  void *original;
  uint8_t *table;
  int shape[4];
  int stride[2][4];
};

template<Factory factory>
void *BinaryInit(TfLiteContext *ctx, const char *buffer, size_t length)
{
  auto *d = static_cast<BinaryData *>(ctx->AllocatePersistentBuffer(ctx, sizeof(BinaryData)));
  if (!d) return nullptr;
  std::memset(d, 0, sizeof(*d));
  d->original = factory().init(ctx, buffer, length);
  return d->original ? d : nullptr;
}

template<Factory factory>
TfLiteStatus BinaryPrepare(TfLiteContext *ctx, TfLiteNode *node)
{
  auto *d = static_cast<BinaryData *>(node->user_data);
  if (!d) return kTfLiteError;
  node->user_data = d->original;
  auto status = factory().prepare(ctx, node);
  node->user_data = d;
  if (status != kTfLiteOk) return status;
  auto *a = const_cast<TfLiteEvalTensor *>(tflite::micro::GetEvalInput(ctx, node, 0));
  auto *b = const_cast<TfLiteEvalTensor *>(tflite::micro::GetEvalInput(ctx, node, 1));
  auto *out = tflite::micro::GetEvalOutput(ctx, node, 0);
  if (a->type != kTfLiteInt8 || b->type != kTfLiteInt8 || out->type != kTfLiteInt8 ||
      a->dims->size > 4 || b->dims->size > 4 || out->dims->size > 4 ||
      tflite::micro::GetTensorShape(out).FlatSize() < 128) return kTfLiteOk;
  for (int axis=0; axis<4; axis++)
    d->shape[axis] = axis < 4-out->dims->size ? 1 : out->dims->data[axis-(4-out->dims->size)];
  const TfLiteEvalTensor *inputs[2] = {a,b};
  for (int input=0; input<2; input++)
    {
      int stride = 1;
      for (int axis=3; axis>=0; axis--)
        {
          int index = axis-(4-inputs[input]->dims->size);
          int size = index < 0 ? 1 : inputs[input]->dims->data[index];
          if (size != 1 && size != d->shape[axis]) return kTfLiteError;
          d->stride[input][axis] = size == 1 ? 0 : stride;
          stride *= size;
        }
    }
  d->table = static_cast<uint8_t *>(ctx->AllocatePersistentBuffer(ctx, 65536));
  auto *pairs = static_cast<uint8_t *>(std::malloc(2*65536));
  if (!d->table || !pairs) { std::free(pairs); return kTfLiteError; }
  for (unsigned int i=0; i<65536; i++) { pairs[i] = i >> 8; pairs[65536+i] = i; }
  const auto saved_a = *a, saved_b = *b, saved_out = *out;
  struct { int size; int data[1]; } shape = {1,{65536}};
  a->dims = b->dims = out->dims = reinterpret_cast<TfLiteIntArray *>(&shape);
  a->data.uint8 = pairs;
  b->data.uint8 = pairs+65536;
  out->data.uint8 = d->table;
  node->user_data = d->original;
  status = factory().invoke(ctx,node);
  node->user_data = d;
  *a = saved_a; *b = saved_b; *out = saved_out;
  std::free(pairs);
  return status;
}

template<Factory factory>
TfLiteStatus BinaryEval(TfLiteContext *ctx, TfLiteNode *node)
{
  auto *d = static_cast<BinaryData *>(node->user_data);
  if (!d->table)
    {
      node->user_data = d->original;
      auto status = factory().invoke(ctx,node);
      node->user_data = d;
      return status;
    }
  const auto *a = tflite::micro::GetEvalInput(ctx,node,0)->data.uint8;
  const auto *b = tflite::micro::GetEvalInput(ctx,node,1)->data.uint8;
  auto *out = tflite::micro::GetEvalOutput(ctx,node,0)->data.uint8;
  for (int n=0;n<d->shape[0];n++)
    for (int y=0;y<d->shape[1];y++)
      for (int x=0;x<d->shape[2];x++)
        {
          const auto *p = a+n*d->stride[0][0]+y*d->stride[0][1]+x*d->stride[0][2];
          const auto *q = b+n*d->stride[1][0]+y*d->stride[1][1]+x*d->stride[1][2];
          for (int c=0;c<d->shape[3];c++)
            {
              *out++ = d->table[(static_cast<unsigned int>(*p)<<8)|*q];
              p += d->stride[0][3]; q += d->stride[1][3];
            }
        }
  return kTfLiteOk;
}

template<Factory factory>
TFLMRegistration BinaryRegistration()
{
  auto op=factory(); op.init=BinaryInit<factory>; op.prepare=BinaryPrepare<factory>;
  op.invoke=BinaryEval<factory>; return op;
}
}
TFLMRegistration velafit_register_add_lut()
{ return BinaryRegistration<tflite::Register_ADD>(); }
TFLMRegistration velafit_register_sub_lut()
{ return BinaryRegistration<tflite::Register_SUB>(); }
TFLMRegistration velafit_register_mul_lut()
{ return BinaryRegistration<tflite::Register_MUL>(); }
