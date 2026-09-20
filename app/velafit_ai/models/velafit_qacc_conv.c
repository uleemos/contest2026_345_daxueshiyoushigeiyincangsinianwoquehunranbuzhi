/* SPDX-License-Identifier: MIT
 * Vector inner loop adapted from Espressif ESP-DL:
 * esp-dl/dl/base/isa/esp32p4/dl_esp32p4_s8_conv2d.S
 * commit b7e9d88a570948d1d6cf9ef064c8cb8e04bda99e.
 * Copyright (c) 2021 Espressif Systems (Shanghai) Co., Ltd.
 * See LICENSE.esp-dl in this directory for the retained MIT notice.
 *
 * VelaFit adapter: OHWI weights -> blocks of 16 output channels; preserve
 * TFLM asymmetric input offset and per-channel quantization using ESP-NN.
 * Requires caller-held PIE lock and saved HWLP/PIE task context.
 */
#include "velafit_qacc_conv.h"
#include "velafit_requant.h"
#include <common_functions.h>
#include <string.h>

void vf_qacc_pack(const int8_t *weights, const int32_t *bias, int8_t *packed,
                  int32_t *combined, int elements, int outputs, int input_offset)
{
  const int padded = (elements + 15) & ~15;
  for (int block = 0; block < outputs; block += 16)
    for (int lane = 0; lane < 16; lane++)
      {
        const int oc = block + lane;
        int sum = 0;
        for (int k = 0; k < padded; k++)
          {
            const int8_t w = oc < outputs && k < elements ? weights[oc * elements + k] : 0;
            packed[block * padded + k * 16 + lane] = w;
            sum += w;
          }
        if (oc < outputs) combined[oc] = sum * input_offset + (bias ? bias[oc] : 0);
      }
}

#define VF_MAC(N, A, B) "esp.vsmulas.s8.qacc.ld.incp " A ", x31, " B ", q0, " #N "\n"
#define VF_BODY \
  VF_MAC(0, "q1", "q1") VF_MAC(1, "q2", "q2") \
  VF_MAC(2, "q1", "q1") VF_MAC(3, "q2", "q2") \
  VF_MAC(4, "q1", "q1") VF_MAC(5, "q2", "q2") \
  VF_MAC(6, "q1", "q1") VF_MAC(7, "q2", "q2") \
  VF_MAC(8, "q1", "q1") VF_MAC(9, "q2", "q2") \
  VF_MAC(10, "q1", "q1") VF_MAC(11, "q2", "q2") \
  VF_MAC(12, "q1", "q1") VF_MAC(13, "q2", "q2")

static void dot16(const int8_t *input, const int8_t *filter, int count,
                  int32_t *result)
{
  const int loops = count / 16 - 1;
  __asm__ volatile(
    "mv x30, %[input]\nmv x31, %[filter]\nesp.zero.qacc\n"
    "esp.vld.128.ip q0, x30, 16\n"
    "esp.vld.128.ip q1, x31, 16\nesp.vld.128.ip q2, x31, 16\n"
    "beqz %[loops], 2f\nesp.lp.setup 0, %[loops], 1f\n"
    VF_BODY VF_MAC(14, "q1", "q1")
    "esp.vsmulas.s8.qacc.ld.incp q0, x30, q2, q0, 15\n"
    "1: esp.vld.128.ip q2, x31, 16\n"
    "2:\n" VF_BODY
    "esp.vsmulas.s8.qacc q1, q0, 14\nesp.vsmulas.s8.qacc q2, q0, 15\n"
    :: [input] "r"(input), [filter] "r"(filter), [loops] "r"(loops)
    : "x30", "x31", "memory");
  ESP_NN_QACC_EXTRACT_S32(result);
}

void vf_qacc_conv(const data_dims_t *input, const int8_t *data,
                 const data_dims_t *filter, const int8_t *packed,
                 const int32_t *combined, const data_dims_t *output,
                 int8_t *result, const conv_params_t *params,
                 const quant_data_t *quant, int8_t *scratch)
{
  const int elements = filter->width * filter->height * input->channels;
  const int padded = (elements + 15) & ~15;
  ESP_NN_PIE_ENABLE();
  /* Bound the strided output working set while retaining reuse of each
   * packed filter block. Large whole-image channel passes repeatedly
   * evict partially written output cache lines on PSRAM. */
  const int pixels = output->height * output->width;
  for (int tile = 0; tile < pixels; tile += 32)
  for (int oc = 0; oc < output->channels; oc += 16)
    for (int pixel = tile; pixel < pixels && pixel < tile + 32; pixel++)
      {
        const int y = pixel / output->width;
        const int x = pixel % output->width;
        const int8_t *patch = scratch;
        if (filter->width == 1 && filter->height == 1 &&
            params->padding.width == 0 && params->padding.height == 0 &&
            elements == padded)
          patch = data + ((y * params->stride.height) * input->width +
                          x * params->stride.width) * input->channels;
        else
          {
            memset(scratch, 0, padded);
            int offset = 0;
            for (int ky = 0; ky < filter->height; ky++)
              for (int kx = 0; kx < filter->width; kx++)
                {
                  const int iy = y * params->stride.height + ky - params->padding.height;
                  const int ix = x * params->stride.width + kx - params->padding.width;
                  if (iy < 0 || iy >= input->height || ix < 0 || ix >= input->width)
                    memset(scratch + offset, -params->in_offset, input->channels);
                  else
                    memcpy(scratch + offset, data + (iy * input->width + ix) * input->channels,
                           input->channels);
                  offset += input->channels;
                }
          }
        int8_t *dst = result + (y * output->width + x) * output->channels;
        {
            int32_t sums[16] __attribute__((aligned(16)));
            dot16(patch, packed + oc * padded, padded, sums);
            for (int lane = 0; lane < 16 && oc + lane < output->channels; lane++)
              {
                const int c = oc + lane;
                int32_t r = vf_requant_exact(sums[lane] + combined[c], quant->mult[c], quant->shift[c]);
                r += params->out_offset;
                r = r < params->activation.min ? params->activation.min : r;
                r = r > params->activation.max ? params->activation.max : r;
                dst[c] = r;
              }
          }
      }
}
