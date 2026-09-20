/****************************************************************************
 * app/sc2336_probe/sc2336_raw10.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fused MIPI packed-RAW10 unpack, BGGR bilinear demosaic, and aspect-ratio
 * preserving resize.  Only source pixels needed by the model image are read;
 * no full-resolution RGB framebuffer is allocated.
 ****************************************************************************/

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sc2336_capture.h"
#include "sc2336_transform.h"

static uint16_t raw10_at(const uint8_t *raw, uint16_t width,
                         uint16_t x, uint16_t y)
{
  size_t stride = (size_t)width * 5 / 4;
  const uint8_t *group = raw + (size_t)y * stride + (x / 4) * 5;
  unsigned int lane = x & 3;

  return ((uint16_t)group[lane] << 2) |
         ((group[4] >> (lane * 2)) & 3);
}

static uint8_t raw8_at(const uint8_t *raw, uint16_t width,
                       uint16_t x, uint16_t y)
{
  return (uint8_t)(raw10_at(raw, width, x, y) >> 2);
}

static uint8_t average2(uint8_t a, uint8_t b)
{
  return (uint8_t)(((unsigned int)a + b + 1) / 2);
}

static uint8_t average4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
  return (uint8_t)(((unsigned int)a + b + c + d + 2) / 4);
}

static void demosaic_bggr(const uint8_t *raw, uint16_t width,
                          uint16_t x, uint16_t y, uint8_t *rgb)
{
  uint8_t c = raw8_at(raw, width, x, y);
  uint8_t l = raw8_at(raw, width, x - 1, y);
  uint8_t r = raw8_at(raw, width, x + 1, y);
  uint8_t u = raw8_at(raw, width, x, y - 1);
  uint8_t d = raw8_at(raw, width, x, y + 1);
  uint8_t ul = raw8_at(raw, width, x - 1, y - 1);
  uint8_t ur = raw8_at(raw, width, x + 1, y - 1);
  uint8_t dl = raw8_at(raw, width, x - 1, y + 1);
  uint8_t dr = raw8_at(raw, width, x + 1, y + 1);

  if ((y & 1) == 0 && (x & 1) == 0)       /* B */
    {
      rgb[0] = average4(ul, ur, dl, dr);
      rgb[1] = average4(l, r, u, d);
      rgb[2] = c;
    }
  else if ((y & 1) == 0)                  /* G on B row */
    {
      rgb[0] = average2(u, d);
      rgb[1] = c;
      rgb[2] = average2(l, r);
    }
  else if ((x & 1) == 0)                  /* G on R row */
    {
      rgb[0] = average2(l, r);
      rgb[1] = c;
      rgb[2] = average2(u, d);
    }
  else                                    /* R */
    {
      rgb[0] = c;
      rgb[1] = average4(l, r, u, d);
      rgb[2] = average4(ul, ur, dl, dr);
    }
}

static uint32_t crc32_le(const uint8_t *data, size_t len)
{
  uint32_t crc = UINT32_MAX;

  while (len-- > 0)
    {
      crc ^= *data++;
      for (unsigned int bit = 0; bit < 8; bit++)
        {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }

  return ~crc;
}

static uint8_t sqrt_u16(uint16_t value)
{
  uint16_t root = 0;
  uint16_t bit = 1u << 14;

  while (bit > value)
    {
      bit >>= 2;
    }

  while (bit != 0)
    {
      if (value >= root + bit)
        {
          value -= root + bit;
          root = (root >> 1) + bit;
        }
      else
        {
          root >>= 1;
        }

      bit >>= 2;
    }

  return (uint8_t)root;
}

static uint16_t wb_gain_q8(uint16_t target, uint8_t linear_mean)
{
  uint16_t adjusted = linear_mean > 16 ? linear_mean - 16 : 1;
  uint32_t gain = (uint32_t)target * 256 / adjusted;

  if (gain < 128)
    {
      gain = 128;
    }
  else if (gain > 1024)
    {
      gain = 1024;
    }

  return (uint16_t)gain;
}

static uint8_t correct_channel(uint8_t value, uint16_t gain,
                               const uint8_t *gamma_lut)
{
  uint32_t linear = value > 16 ? value - 16 : 0;
  uint32_t corrected = linear * gain / 256;

  if (corrected > 255)
    {
      corrected = 255;
    }

  return gamma_lut[corrected];
}

/* Counterclockwise image rotation compensates the module's clockwise roll
 * as viewed by a person facing the lens. Demosaic in SENSOR coordinates so
 * rotating the image does not change the Bayer color interpretation.
 */

static void portrait_sample(const uint8_t *raw, uint16_t raw_w,
                            uint16_t raw_h, uint16_t x, uint16_t y,
                            uint8_t *rgb)
{
  uint16_t sx = raw_w - 1 - y;
  uint16_t sy = x;

  if (sx == 0) sx = 1;
  if (sx >= raw_w - 1) sx = raw_w - 2;
  if (sy == 0) sy = 1;
  if (sy >= raw_h - 1) sy = raw_h - 2;
  demosaic_bggr(raw, raw_w, sx, sy, rgb);
}

int sc2336_raw10_bggr_preview_rgb565(const uint8_t *raw, size_t raw_len,
                                     uint16_t raw_w, uint16_t raw_h,
                                     uint16_t *fb, size_t fb_len,
                                     uint16_t fb_w, uint16_t fb_h,
                                     uint16_t fb_stride)
{
  uint32_t sum_r = 0;
  uint32_t sum_g = 0;
  uint32_t sum_b = 0;
  uint16_t sample_count = 0;
  uint16_t gain_r;
  uint16_t gain_g;
  uint16_t gain_b;
  uint16_t target;
  uint16_t preview_w;
  uint16_t preview_h;
  uint16_t offset_x;
  uint16_t offset_y;
  uint16_t crop_w;
  uint16_t crop_h;
  uint16_t red565[256];
  uint16_t green565[256];
  uint16_t blue565[256];
  uint8_t gamma_lut[256];
  uint8_t rgb[3];
  /* Visual feedback requires CCW90 relative to the native preview.
   * Cover the display with a centered crop, preserving aspect ratio.
   */
  const uint16_t view_w = raw_h;
  const uint16_t view_h = raw_w;

  if (raw == NULL || fb == NULL || raw_w < 4 || raw_h < 3 ||
      (raw_w & 3) != 0 || fb_w < 2 || fb_h < 2 ||
      (fb_w & 1) != 0 || (fb_h & 1) != 0 || fb_stride < fb_w ||
      raw_len < (size_t)raw_w * raw_h * 5 / 4 ||
      fb_len < (size_t)fb_stride * fb_h * sizeof(uint16_t))
    {
      return -EINVAL;
    }

  /* Render to every 2x2 display block. Crop in rotated source coordinates:
   * native 1024x600 uses 720x421 of the 720x1280 rotated view. On the
   * physically upright panel this removes left/right, not head/feet.
   */

  preview_w = fb_w / 2;
  preview_h = fb_h / 2;
  struct sc2336_rect crop = sc2336_cover(view_w, view_h, fb_w, fb_h);
  crop_w = crop.w;
  crop_h = crop.h;

  if (crop_w == 0 || crop_h == 0)
    {
      return -EINVAL;
    }

  offset_x = (view_w - crop_w) / 2;
  offset_y = (view_h - crop_h) / 2;

  /* Estimate gray-world white balance from a sparse 16x9 grid. */

  for (uint16_t gy = 0; gy < 9; gy++)
    {
      uint16_t sy = 1 + (uint16_t)(((uint32_t)(2 * gy + 1) *
                                    (view_h - 2)) / 18);
      for (uint16_t gx = 0; gx < 16; gx++)
        {
          uint16_t sx = 1 + (uint16_t)(((uint32_t)(2 * gx + 1) *
                                        (view_w - 2)) / 32);
          portrait_sample(raw, raw_w, raw_h, sx, sy, rgb);
          sum_r += rgb[0];
          sum_g += rgb[1];
          sum_b += rgb[2];
          sample_count++;
        }
    }

  target = (uint16_t)(((sum_r / sample_count > 16 ?
                        sum_r / sample_count - 16 : 1) +
                       (sum_g / sample_count > 16 ?
                        sum_g / sample_count - 16 : 1) +
                       (sum_b / sample_count > 16 ?
                        sum_b / sample_count - 16 : 1)));
  if (target > 192)
    {
      target = 192;
    }

  gain_r = wb_gain_q8(target, (uint8_t)(sum_r / sample_count));
  gain_g = wb_gain_q8(target, (uint8_t)(sum_g / sample_count));
  gain_b = wb_gain_q8(target, (uint8_t)(sum_b / sample_count));
  for (uint16_t value = 0; value < 256; value++)
    {
      gamma_lut[value] = sqrt_u16((uint16_t)(value * 255));
    }

  memset(fb, 0, (size_t)fb_stride * fb_h * sizeof(uint16_t));
  for (uint16_t value = 0; value < 256; value++)
    {
      red565[value] = (correct_channel(value, gain_r, gamma_lut) & 0xf8) << 8;
      green565[value] = (correct_channel(value, gain_g, gamma_lut) & 0xfc) << 3;
      blue565[value] = correct_channel(value, gain_b, gamma_lut) >> 3;
    }

  for (uint16_t dy = 0; dy < preview_h; dy++)
    {
      uint16_t sy = offset_y +
        (uint16_t)(((uint32_t)(2 * dy + 1) * crop_h) / (2 * preview_h));
      uint16_t *line0 = fb + (size_t)(dy * 2) * fb_stride;
      uint16_t *line1 = line0 + fb_stride;

      if (sy == 0)
        {
          sy = 1;
        }
      else if (sy >= view_h - 1)
        {
          sy = view_h - 2;
        }

      for (uint16_t dx = 0; dx < preview_w; dx++)
        {
          uint16_t sx = offset_x +
            (uint16_t)(((uint32_t)(2 * dx + 1) * crop_w) / (2 * preview_w));
          uint16_t pixel;

          if (sx == 0)
            {
              sx = 1;
            }
          else if (sx >= view_w - 1)
            {
              sx = view_w - 2;
            }

          portrait_sample(raw, raw_w, raw_h, sx, sy, rgb);
          pixel = red565[rgb[0]] | green565[rgb[1]] | blue565[rgb[2]];
          line0[dx * 2] = pixel;
          line0[dx * 2 + 1] = pixel;
          line1[dx * 2] = pixel;
          line1[dx * 2 + 1] = pixel;
        }
    }
  return 0;
}

int sc2336_raw10_bggr_letterbox(const uint8_t *raw, size_t raw_len,
                                uint16_t raw_w, uint16_t raw_h,
                                uint8_t *rgb, size_t rgb_len,
                                uint16_t rgb_w, uint16_t rgb_h,
                                struct sc2336_rgb_capture_s *result)
{
  size_t expected_raw;
  size_t expected_rgb;
  uint32_t sum_r = 0;
  uint32_t sum_g = 0;
  uint32_t sum_b = 0;
  uint8_t gamma_lut[256];
  uint32_t content_pixels;
  uint16_t content_w;
  uint16_t content_h;
  uint16_t offset_x;
  uint16_t offset_y;

  const uint16_t view_w = raw_h;
  const uint16_t view_h = raw_w;

  if (raw == NULL || rgb == NULL || result == NULL || raw_w < 4 ||
      raw_h < 3 || rgb_w == 0 || rgb_h == 0 || (raw_w & 3) != 0)
    {
      return -EINVAL;
    }

  expected_raw = (size_t)raw_w * raw_h * 5 / 4;
  expected_rgb = (size_t)rgb_w * rgb_h * 3;
  if (raw_len < expected_raw || rgb_len < expected_rgb)
    {
      return -EMSGSIZE;
    }

  struct sc2336_rect fit = sc2336_fit(view_w, view_h, rgb_w, rgb_h);
  content_w = fit.w;
  content_h = fit.h;

  if (content_w == 0 || content_h == 0)
    {
      return -ERANGE;
    }

  offset_x = (rgb_w - content_w) / 2;
  offset_y = (rgb_h - content_h) / 2;
  memset(rgb, 0, expected_rgb);

  for (uint16_t dy = 0; dy < content_h; dy++)
    {
      uint16_t sy = (uint16_t)(((uint32_t)(2 * dy + 1) * view_h) /
                               (2 * content_h));
      if (sy == 0) sy = 1;
      if (sy >= view_h - 1) sy = view_h - 2;

      for (uint16_t dx = 0; dx < content_w; dx++)
        {
          uint16_t sx = (uint16_t)(((uint32_t)(2 * dx + 1) * view_w) /
                                   (2 * content_w));
          uint8_t *pixel = rgb +
                           ((size_t)(offset_y + dy) * rgb_w + offset_x + dx) * 3;
          if (sx == 0) sx = 1;
          if (sx >= view_w - 1) sx = view_w - 2;
          portrait_sample(raw, raw_w, raw_h, sx, sy, pixel);
          sum_r += pixel[0];
          sum_g += pixel[1];
          sum_b += pixel[2];
        }
    }

  content_pixels = (uint32_t)content_w * content_h;
  result->content_x = offset_x;
  result->content_y = offset_y;
  result->content_w = content_w;
  result->content_h = content_h;
  result->linear_mean_r = (uint8_t)(sum_r / content_pixels);
  result->linear_mean_g = (uint8_t)(sum_g / content_pixels);
  result->linear_mean_b = (uint8_t)(sum_b / content_pixels);

  /* SC2336 delivers linear sensor samples, while MoveNet was trained with
   * display-like RGB.  Apply a cheap gray-world AWB, remove the typical
   * 10-bit sensor black pedestal after conversion to 8-bit, and approximate
   * gamma 2.0.  The capped gains keep a strongly colored scene from causing
   * an unstable or overflowing model input.
   */

  uint16_t target = (uint16_t)
    ((result->linear_mean_r > 16 ? result->linear_mean_r - 16 : 1) +
     (result->linear_mean_g > 16 ? result->linear_mean_g - 16 : 1) +
     (result->linear_mean_b > 16 ? result->linear_mean_b - 16 : 1));
  if (target > 192)
    {
      target = 192;
    }

  result->wb_gain_r_q8 = wb_gain_q8(target, result->linear_mean_r);
  result->wb_gain_g_q8 = wb_gain_q8(target, result->linear_mean_g);
  result->wb_gain_b_q8 = wb_gain_q8(target, result->linear_mean_b);
  for (uint16_t value = 0; value < 256; value++)
    {
      gamma_lut[value] = sqrt_u16((uint16_t)(value * 255));
    }

  sum_r = 0;
  sum_g = 0;
  sum_b = 0;
  for (uint16_t dy = 0; dy < content_h; dy++)
    {
      for (uint16_t dx = 0; dx < content_w; dx++)
        {
          uint8_t *pixel = rgb +
                           ((size_t)(offset_y + dy) * rgb_w + offset_x + dx) * 3;
          const uint16_t gains[3] =
            {
              result->wb_gain_r_q8,
              result->wb_gain_g_q8,
              result->wb_gain_b_q8
            };

          for (unsigned int channel = 0; channel < 3; channel++)
            {
              uint32_t linear = pixel[channel] > 16 ? pixel[channel] - 16 : 0;
              uint32_t corrected = linear * gains[channel] / 256;
              if (corrected > 255)
                {
                  corrected = 255;
                }

              pixel[channel] = gamma_lut[corrected];
            }

          sum_r += pixel[0];
          sum_g += pixel[1];
          sum_b += pixel[2];
        }
    }

  result->mean_r = (uint8_t)(sum_r / content_pixels);
  result->mean_g = (uint8_t)(sum_g / content_pixels);
  result->mean_b = (uint8_t)(sum_b / content_pixels);
  result->rgb_crc32 = crc32_le(rgb, expected_rgb);
  return 0;
}
