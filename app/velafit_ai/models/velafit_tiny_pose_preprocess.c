/* SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include "velafit_tiny_pose_preprocess.h"

static int8_t quantize_pixel(float value, float scale, int zero_point)
{
  float normalized = value / 127.5f - 1.0f;
  long result = (long)nearbyintf(normalized / scale + zero_point);
  if (result < -128) result = -128;
  if (result > 127) result = 127;
  return (int8_t)result;
}

void velafit_tiny_pose_preprocess_rgb192(const uint8_t *source,
                                         int8_t *destination,
                                         float scale,
                                         int zero_point)
{
  /* With the camera fixed in its clockwise-90 physical mounting, the capture
   * helper's portrait RGB192 is already upright. Preserve that orientation
   * and perform only a 2x2 area resize. This produces a 54x96 portrait image
   * with left/right letterbox bars in the square model tensor. */
  for (unsigned int y = 0; y < 96; y++)
    {
      for (unsigned int x = 0; x < 96; x++)
        {
          for (unsigned int channel = 0; channel < 3; channel++)
            {
              unsigned int sum = 0;
              for (unsigned int dy = 0; dy < 2; dy++)
                {
                  for (unsigned int dx = 0; dx < 2; dx++)
                    {
                      unsigned int source_y = 2 * y + dy;
                      unsigned int source_x = 2 * x + dx;
                      sum += source[(source_y * 192 + source_x) * 3 + channel];
                    }
                }
              destination[(y * 96 + x) * 3 + channel] =
                quantize_pixel((float)sum * 0.25f, scale, zero_point);
            }
        }
    }
}

void velafit_tiny_pose_debug_rgb96(const uint8_t *source,
                                   uint8_t *destination)
{
  if (!source || !destination) return;
  for (unsigned int y = 0; y < 96; y++)
    {
      for (unsigned int x = 0; x < 96; x++)
        {
          for (unsigned int channel = 0; channel < 3; channel++)
            {
              unsigned int sum = 0;
              for (unsigned int dy = 0; dy < 2; dy++)
                for (unsigned int dx = 0; dx < 2; dx++)
                  {
                    unsigned int source_y = 2 * y + dy;
                    unsigned int source_x = 2 * x + dx;
                    sum += source[(source_y * 192 + source_x) * 3 + channel];
                  }
              destination[(y * 96 + x) * 3 + channel] =
                (uint8_t)((sum + 2) / 4);
            }
        }
    }
}
