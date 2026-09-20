/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>

#include "sc2336_model_preview.h"

#define MODEL_SIDE 96u

static uint16_t rgb565(const uint8_t *rgb)
{
  return (uint16_t)(((uint16_t)(rgb[0] >> 3) << 11) |
                    ((uint16_t)(rgb[1] >> 2) << 5) |
                    (rgb[2] >> 3));
}

int sc2336_model_preview_blit_ccw_rgb565(const uint8_t *rgb96,
                                         size_t rgb96_len,
                                         uint16_t *framebuffer,
                                         size_t framebuffer_len,
                                         uint16_t width,
                                         uint16_t height,
                                         uint16_t stride,
                                         uint8_t scale)
{
  const uint16_t shown = MODEL_SIDE * scale;
  uint16_t origin_x;
  uint16_t origin_y;

  if (!rgb96 || !framebuffer || rgb96_len < MODEL_SIDE * MODEL_SIDE * 3 ||
      scale == 0 || scale > 6 || stride < width || shown > width ||
      shown > height ||
      framebuffer_len < (size_t)stride * height * sizeof(uint16_t))
    {
      return -EINVAL;
    }

  memset(framebuffer, 0, (size_t)stride * height * sizeof(uint16_t));
  origin_x = (width - shown) / 2;
  origin_y = (height - shown) / 2;

  for (uint16_t source_y = 0; source_y < MODEL_SIDE; source_y++)
    {
      for (uint16_t source_x = 0; source_x < MODEL_SIDE; source_x++)
        {
          /* Forward CCW90 in raster coordinates: (x,y)->(y,95-x). */
          const uint16_t rotated_x = source_y;
          const uint16_t rotated_y = MODEL_SIDE - 1 - source_x;
          const uint16_t color = rgb565(&rgb96[
            ((size_t)source_y * MODEL_SIDE + source_x) * 3]);
          for (uint8_t dy = 0; dy < scale; dy++)
            for (uint8_t dx = 0; dx < scale; dx++)
              framebuffer[(size_t)(origin_y + rotated_y * scale + dy) *
                          stride + origin_x + rotated_x * scale + dx] = color;
        }
    }

  return 0;
}
