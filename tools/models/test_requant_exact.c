/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../../app/velafit_ai/models/velafit_requant.h"

static int32_t reference(int32_t x, int32_t m, int shift)
{
  const int32_t a = (int32_t)((uint32_t)x << (shift > 0 ? shift : 0));
  const int64_t product = (int64_t)a * m;
  const int64_t nudge = product >= 0 ? (1ll << 30) : 1 - (1ll << 30);
  const int32_t high = a == INT32_MIN && m == INT32_MIN ? INT32_MAX :
                       (int32_t)((product + nudge) / (1ll << 31));
  const int right = shift < 0 ? -shift : 0;
  // Independent quotient/remainder form of rounding ties away from zero.
  const int64_t divisor = 1ll << right;
  int64_t q = high / divisor;
  const int64_t remainder = high % divisor;
  if (remainder * 2 >= divisor) q++;
  if (-remainder * 2 >= divisor) q--;
  return q;
}

int main(void)
{
  const int32_t edges[] = {INT32_MIN, INT32_MIN+1, -1073741824, -3, -2, -1,
                           0, 1, 2, 3, 1073741824, INT32_MAX-1, INT32_MAX};
  unsigned long cases = 0;
  for (unsigned int i=0; i<sizeof(edges)/sizeof(edges[0]); i++)
    for (unsigned int j=0; j<sizeof(edges)/sizeof(edges[0]); j++)
      for (int s=-31; s<=30; s++,cases++)
        assert(vf_requant_exact(edges[i],edges[j],s) == reference(edges[i],edges[j],s));
  uint32_t state = 0x472591u;
  for (unsigned int i=0; i<1000000; i++,cases++)
    {
      state = state*1664525u+1013904223u;
      int32_t x = state;
      state = state*1664525u+1013904223u;
      int32_t m = state;
      const int s = (int)(i % 62) - 31;
      assert(vf_requant_exact(x,m,s) == reference(x,m,s));
    }
  printf("exact requant PASS %lu cases\n", cases);
}
