/****************************************************************************
 * tools/models/test_sc2336_raw10.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sc2336_capture.h"
#include "sc2336_transform.h"

static void pack_group(const uint16_t pixels[4], uint8_t bytes[5])
{
  bytes[0] = pixels[0] >> 2;
  bytes[1] = pixels[1] >> 2;
  bytes[2] = pixels[2] >> 2;
  bytes[3] = pixels[3] >> 2;
  bytes[4] = (pixels[0] & 3) | ((pixels[1] & 3) << 2) |
             ((pixels[2] & 3) << 4) | ((pixels[3] & 3) << 6);
}

int main(void)
{
  enum { RAW_W = 8, RAW_H = 4, RGB_W = 8, RGB_H = 8 };
  uint8_t raw[RAW_W * RAW_H * 5 / 4];
  uint8_t rgb[RGB_W * RGB_H * 3];
  struct sc2336_rgb_capture_s result;

  for (unsigned int y = 0; y < RAW_H; y++)
    {
      for (unsigned int x = 0; x < RAW_W; x += 4)
        {
          uint16_t p[4];
          for (unsigned int lane = 0; lane < 4; lane++)
            {
              unsigned int px = x + lane;
              uint8_t value;
              if ((y & 1) == 0 && (px & 1) == 0)
                {
                  value = 40;  /* B */
                }
              else if ((y & 1) != 0 && (px & 1) != 0)
                {
                  value = 200; /* R */
                }
              else
                {
                  value = 100; /* G */
                }

              p[lane] = (uint16_t)value << 2;
            }

          pack_group(p, raw + ((y * RAW_W + x) / 4) * 5);
        }
    }

  memset(&result, 0, sizeof(result));
  assert(sc2336_raw10_bggr_letterbox(raw, sizeof(raw), RAW_W, RAW_H,
                                     rgb, sizeof(rgb), RGB_W, RGB_H,
                                     &result) == 0);
  assert(result.content_x == 2 && result.content_y == 0);
  assert(result.content_w == 4 && result.content_h == 8);
  assert(result.linear_mean_r == 200 && result.linear_mean_g == 100 &&
         result.linear_mean_b == 40);
  assert(result.mean_r > result.mean_b && result.mean_g > result.mean_b);

  for (unsigned int y = 0; y < RGB_H; y++)
    {
      assert(rgb[y * RGB_W * 3] == 0);
      assert(rgb[(y * RGB_W + RGB_W - 1) * 3] == 0);
    }

  struct sc2336_rect fit = sc2336_fit(720, 1280, 192, 192);
  struct sc2336_rect crop = sc2336_cover(720, 1280, 1024, 600);
  assert(fit.x == 42 && fit.y == 0 && fit.w == 108 && fit.h == 192);
  assert(crop.x == 0 && crop.y == 429 && crop.w == 720 && crop.h == 421);
  for (int k = 0; k < 17; k++)
    {
      /* All 17 known coordinates, including near each crop boundary. */
      float vx = (k + 0.5f) * crop.w / 17;
      float vy = crop.y + (16.5f - k) * crop.h / 17;
      float mx = (fit.x + vx * fit.w / 720) / 192;
      float my = (fit.y + vy * fit.h / 1280) / 192;
      float fx, fy;
      assert(sc2336_model_to_display(1280,720,192,192,1024,600,
                                     mx,my,&fx,&fy));
      assert(fabsf(fx - vx * 1024 / 720) < 0.001f);
      assert(fabsf(fy - (vy - crop.y) * 600 / crop.h) < 0.001f);
    }
  float fx, fy;
  assert(!sc2336_model_to_display(1280,720,192,192,1024,600,0,0,&fx,&fy));
  assert(!sc2336_model_to_display(1280,720,192,192,1024,600,.5f,0,&fx,&fy));
  assert(!sc2336_model_to_display(1280,720,192,192,1024,600,NAN,.5f,&fx,&fy));

  assert(sc2336_raw10_bggr_letterbox(raw, sizeof(raw) - 1, RAW_W, RAW_H,
                                     rgb, sizeof(rgb), RGB_W, RGB_H,
                                     &result) < 0);
  puts("SC2336 RAW10 BGGR letterbox tests: PASS");
  return 0;
}
