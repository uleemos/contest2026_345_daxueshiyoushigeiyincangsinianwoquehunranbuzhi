/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SC2336_TRANSFORM_H
#define SC2336_TRANSFORM_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

/* Pixel-edge coordinates: source centers are i+0.5. Sensor -> view is
 * CCW90: (x,y) -> (y, raw_w-x). No anatomical left/right permutation.
 * Rectangles deliberately use the SAME integer rounding as the rasterizer.
 */
struct sc2336_rect { uint16_t x, y, w, h; };

static inline struct sc2336_rect sc2336_fit(uint16_t sw, uint16_t sh,
                                           uint16_t dw, uint16_t dh)
{
  struct sc2336_rect r = {0, 0, 0, 0};
  if (!sw || !sh || !dw || !dh) return r;
  if ((uint32_t)dw * sh <= (uint32_t)dh * sw)
    { r.w = dw; r.h = (uint32_t)sh * dw / sw; }
  else
    { r.h = dh; r.w = (uint32_t)sw * dh / sh; }
  r.x = (dw - r.w) / 2;
  r.y = (dh - r.h) / 2;
  return r;
}

static inline struct sc2336_rect sc2336_cover(uint16_t sw, uint16_t sh,
                                             uint16_t dw, uint16_t dh)
{
  struct sc2336_rect r = {0, 0, sw, sh};
  if (!sw || !sh || !dw || !dh)
    { r.w = r.h = 0; return r; }
  if ((uint32_t)sw * dh > (uint32_t)sh * dw)
    r.w = (uint32_t)sh * dw / dh;
  else
    r.h = (uint32_t)sw * dh / dw;
  r.x = (sw - r.w) / 2;
  r.y = (sh - r.h) / 2;
  return r;
}

/* Map normalized model output to native framebuffer pixels. Return false
 * for padding/cropped points: never clamp them into a misleading border bone.
 * The panel's physical orientation is not another software rotation here.
 */
static inline bool sc2336_model_to_display(uint16_t rw, uint16_t rh,
    uint16_t mw, uint16_t mh, uint16_t fw, uint16_t fh,
    float mx, float my, float *fx, float *fy)
{
  struct sc2336_rect fit = sc2336_fit(rh, rw, mw, mh);
  struct sc2336_rect crop = sc2336_cover(rh, rw, fw, fh);
  float x = mx * mw - fit.x;
  float y = my * mh - fit.y;
  if (!fx || !fy || !fit.w || !fit.h || !crop.w || !crop.h ||
      !isfinite(mx) || !isfinite(my) || x < 0 || y < 0 ||
      x >= fit.w || y >= fit.h) return false;
  x = x * rh / fit.w;
  y = y * rw / fit.h;
  if (x < crop.x || y < crop.y || x >= crop.x + crop.w ||
      y >= crop.y + crop.h) return false;
  *fx = (x - crop.x) * fw / crop.w;
  *fy = (y - crop.y) * fh / crop.h;
  return true;
}
#endif
