/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <limits.h>

/* Exact gemmlowp two-stage rounding. For signed product p,
 * SRDHM = floor(p / 2^31) + bit30(p), except INT_MIN*INT_MIN saturation.
 * Keep the remainder-based second rounding: the usual add-half shortcut can
 * overflow near INT_MAX. Shifts are the TFLM range [-31, 30].
 */
static inline __attribute__((always_inline))
int32_t vf_requant_exact(int32_t x, int32_t multiplier, int32_t shift)
{
  const int left = shift > 0 ? shift : 0;
  const int right = shift < 0 ? -shift : 0;
  const int32_t a = (int32_t)((uint32_t)x << left);
  const uint64_t bits = (uint64_t)((int64_t)a * multiplier);
  int32_t high = (int32_t)((uint32_t)(bits >> 31) + ((uint32_t)(bits >> 30) & 1u));
  if (a == INT32_MIN && multiplier == INT32_MIN) high = INT32_MAX;
  const uint32_t mask = (1u << right) - 1u;
  const uint32_t remainder = (uint32_t)high & mask;
  const uint32_t threshold = (mask >> 1) + (high < 0);
  return (high >> right) + (remainder > threshold);
}
