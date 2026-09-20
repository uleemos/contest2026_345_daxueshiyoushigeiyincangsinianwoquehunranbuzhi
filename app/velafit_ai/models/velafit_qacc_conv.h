/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <esp_nn_riscv_pie.h>
void vf_qacc_pack(const int8_t *weights, const int32_t *bias, int8_t *packed,
                  int32_t *combined, int elements, int outputs, int input_offset);
void vf_qacc_conv(const data_dims_t *input, const int8_t *data,
                 const data_dims_t *filter, const int8_t *packed,
                 const int32_t *combined, const data_dims_t *output,
                 int8_t *result, const conv_params_t *params,
                 const quant_data_t *quant, int8_t *scratch);
#ifdef __cplusplus
}
#endif
