/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "velafit_channel_pack.h"
int main(void)
{
  const size_t cases[] = {16, 24, 256, 320, 512, 960};
  for (size_t k = 0; k < sizeof(cases)/sizeof(cases[0]); k++)
    {
      size_t channels = cases[k], positions = 73;
      size_t size = channels * positions;
      int8_t *source = malloc(size);
      int8_t *output = malloc(size + 32);
      int8_t *packed = malloc(positions * 256 + 32);
      assert(source && output && packed);
      for (size_t i = 0; i < size; i++) source[i] = (int8_t)(i * 29 + i / channels);
      memset(output, 0xa5, size + 32);
      for (size_t offset = 0; offset < channels; offset += 256)
        {
          size_t count = channels - offset < 256 ? channels - offset : 256;
          memset(packed, 0xa5, positions * 256 + 32);
          vf_pack_channels(packed, source, positions, channels, offset, count);
          for (size_t i = 0; i < positions; i++)
            for (size_t c = 0; c < count; c++)
              assert(packed[i * count + c] == source[i * channels + offset + c]);
          for (size_t i = positions * count; i < positions * 256 + 32; i++)
            assert((uint8_t)packed[i] == 0xa5);
          vf_scatter_channels(output, packed, positions, channels, offset, count);
        }
      assert(memcmp(source, output, size) == 0);
      for (size_t i = size; i < size + 32; i++) assert((uint8_t)output[i] == 0xa5);
      free(source); free(output); free(packed);
    }
  puts("HOST channel packing 6 layouts, tail/roundtrip/guards PASS (not PIE execution)");
}
