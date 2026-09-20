/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/render/velafit_render.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>

#include "esp32p4_ppa.h"
#include "velafit_render.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define KPT_CONF_THRESHOLD 0.25f

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* 16 standard skeletal segments connecting 17 COCO keypoints */

static const velafit_limb_t g_skeleton_limbs[VELAFIT_NUM_LIMBS] =
{
  {
    KPT_NOSE, KPT_LEFT_EYE
  },
  {
    KPT_NOSE, KPT_RIGHT_EYE
  },
  {
    KPT_LEFT_EYE, KPT_LEFT_EAR
  },
  {
    KPT_RIGHT_EYE, KPT_RIGHT_EAR
  },
  {
    KPT_LEFT_SHOULDER, KPT_RIGHT_SHOULDER
  },
  {
    KPT_LEFT_SHOULDER, KPT_LEFT_ELBOW
  },
  {
    KPT_LEFT_ELBOW, KPT_LEFT_WRIST
  },
  {
    KPT_RIGHT_SHOULDER, KPT_RIGHT_ELBOW
  },
  {
    KPT_RIGHT_ELBOW, KPT_RIGHT_WRIST
  },
  {
    KPT_LEFT_SHOULDER, KPT_LEFT_HIP
  },
  {
    KPT_RIGHT_SHOULDER, KPT_RIGHT_HIP
  },
  {
    KPT_LEFT_HIP, KPT_RIGHT_HIP
  },
  {
    KPT_LEFT_HIP, KPT_LEFT_KNEE
  },
  {
    KPT_LEFT_KNEE, KPT_LEFT_ANKLE
  },
  {
    KPT_RIGHT_HIP, KPT_RIGHT_KNEE
  },
  {
    KPT_RIGHT_KNEE, KPT_RIGHT_ANKLE
  }
};

/* 5x7 ASCII Bitmap Font for chars 32 (' ') to 126 ('~') */

static const uint8_t g_font5x7[95][5] =
{
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* ' ' (32) */
  {0x00, 0x00, 0x5f, 0x00, 0x00}, /* '!' */
  {0x00, 0x07, 0x00, 0x07, 0x00}, /* '"' */
  {0x14, 0x7f, 0x14, 0x7f, 0x14}, /* '#' */
  {0x24, 0x2a, 0x7f, 0x2a, 0x12}, /* '$' */
  {0x23, 0x13, 0x08, 0x64, 0x62}, /* '%' */
  {0x36, 0x49, 0x55, 0x22, 0x50}, /* '&' */
  {0x00, 0x05, 0x03, 0x00, 0x00}, /* '\'' */
  {0x00, 0x1c, 0x22, 0x41, 0x00}, /* '(' */
  {0x00, 0x41, 0x22, 0x1c, 0x00}, /* ')' */
  {0x14, 0x08, 0x3e, 0x08, 0x14}, /* '*' */
  {0x08, 0x08, 0x3e, 0x08, 0x08}, /* '+' */
  {0x00, 0x50, 0x30, 0x00, 0x00}, /* ',' */
  {0x08, 0x08, 0x08, 0x08, 0x08}, /* '-' */
  {0x00, 0x60, 0x60, 0x00, 0x00}, /* '.' */
  {0x20, 0x10, 0x08, 0x04, 0x02}, /* '/' */
  {0x3e, 0x51, 0x49, 0x45, 0x3e}, /* '0' */
  {0x00, 0x42, 0x7f, 0x40, 0x00}, /* '1' */
  {0x42, 0x61, 0x51, 0x49, 0x46}, /* '2' */
  {0x21, 0x41, 0x45, 0x4b, 0x31}, /* '3' */
  {0x18, 0x14, 0x12, 0x7f, 0x10}, /* '4' */
  {0x27, 0x45, 0x45, 0x45, 0x39}, /* '5' */
  {0x3c, 0x4a, 0x49, 0x49, 0x30}, /* '6' */
  {0x01, 0x71, 0x09, 0x05, 0x03}, /* '7' */
  {0x36, 0x49, 0x49, 0x49, 0x36}, /* '8' */
  {0x06, 0x49, 0x49, 0x29, 0x1e}, /* '9' */
  {0x00, 0x36, 0x36, 0x00, 0x00}, /* ':' */
  {0x00, 0x56, 0x36, 0x00, 0x00}, /* ';' */
  {0x08, 0x14, 0x22, 0x41, 0x00}, /* '<' */
  {0x14, 0x14, 0x14, 0x14, 0x14}, /* '=' */
  {0x00, 0x41, 0x22, 0x14, 0x08}, /* '>' */
  {0x02, 0x01, 0x51, 0x09, 0x06}, /* '?' */
  {0x32, 0x49, 0x79, 0x41, 0x3e}, /* '@' */
  {0x7e, 0x11, 0x11, 0x11, 0x7e}, /* 'A' */
  {0x7f, 0x49, 0x49, 0x49, 0x36}, /* 'B' */
  {0x3e, 0x41, 0x41, 0x41, 0x22}, /* 'C' */
  {0x7f, 0x41, 0x41, 0x22, 0x1c}, /* 'D' */
  {0x7f, 0x49, 0x49, 0x49, 0x41}, /* 'E' */
  {0x7f, 0x09, 0x09, 0x09, 0x01}, /* 'F' */
  {0x3e, 0x41, 0x49, 0x49, 0x7a}, /* 'G' */
  {0x7f, 0x08, 0x08, 0x08, 0x7f}, /* 'H' */
  {0x00, 0x41, 0x7f, 0x41, 0x00}, /* 'I' */
  {0x20, 0x40, 0x41, 0x3f, 0x01}, /* 'J' */
  {0x7f, 0x08, 0x14, 0x22, 0x41}, /* 'K' */
  {0x7f, 0x40, 0x40, 0x40, 0x40}, /* 'L' */
  {0x7f, 0x02, 0x0c, 0x02, 0x7f}, /* 'M' */
  {0x7f, 0x04, 0x08, 0x10, 0x7f}, /* 'N' */
  {0x3e, 0x41, 0x41, 0x41, 0x3e}, /* 'O' */
  {0x7f, 0x09, 0x09, 0x09, 0x06}, /* 'P' */
  {0x3e, 0x41, 0x51, 0x21, 0x5e}, /* 'Q' */
  {0x7f, 0x09, 0x19, 0x29, 0x46}, /* 'R' */
  {0x46, 0x49, 0x49, 0x49, 0x31}, /* 'S' */
  {0x01, 0x01, 0x7f, 0x01, 0x01}, /* 'T' */
  {0x3f, 0x40, 0x40, 0x40, 0x3f}, /* 'U' */
  {0x1f, 0x20, 0x40, 0x20, 0x1f}, /* 'V' */
  {0x3f, 0x40, 0x38, 0x40, 0x3f}, /* 'W' */
  {0x63, 0x14, 0x08, 0x14, 0x63}, /* 'X' */
  {0x07, 0x08, 0x70, 0x08, 0x07}, /* 'Y' */
  {0x61, 0x51, 0x49, 0x45, 0x43}, /* 'Z' */
  {0x00, 0x7f, 0x41, 0x41, 0x00}, /* '[' */
  {0x02, 0x04, 0x08, 0x10, 0x20}, /* '\\' */
  {0x00, 0x41, 0x41, 0x7f, 0x00}, /* ']' */
  {0x04, 0x02, 0x01, 0x02, 0x04}, /* '^' */
  {0x40, 0x40, 0x40, 0x40, 0x40}, /* '_' */
  {0x00, 0x01, 0x02, 0x04, 0x00}, /* '`' */
  {0x20, 0x54, 0x54, 0x54, 0x78}, /* 'a' */
  {0x7f, 0x48, 0x44, 0x44, 0x38}, /* 'b' */
  {0x38, 0x44, 0x44, 0x44, 0x20}, /* 'c' */
  {0x38, 0x44, 0x44, 0x48, 0x7f}, /* 'd' */
  {0x38, 0x54, 0x54, 0x54, 0x18}, /* 'e' */
  {0x08, 0x7e, 0x09, 0x01, 0x02}, /* 'f' */
  {0x0c, 0x52, 0x52, 0x52, 0x3e}, /* 'g' */
  {0x7f, 0x08, 0x04, 0x04, 0x78}, /* 'h' */
  {0x00, 0x44, 0x7d, 0x40, 0x00}, /* 'i' */
  {0x20, 0x40, 0x44, 0x3d, 0x00}, /* 'j' */
  {0x7f, 0x10, 0x28, 0x44, 0x00}, /* 'k' */
  {0x00, 0x41, 0x7f, 0x40, 0x00}, /* 'l' */
  {0x7c, 0x04, 0x18, 0x04, 0x78}, /* 'm' */
  {0x7c, 0x08, 0x04, 0x04, 0x78}, /* 'n' */
  {0x38, 0x44, 0x44, 0x44, 0x38}, /* 'o' */
  {0x7c, 0x14, 0x14, 0x14, 0x08}, /* 'p' */
  {0x08, 0x14, 0x14, 0x18, 0x7c}, /* 'q' */
  {0x7c, 0x08, 0x04, 0x04, 0x08}, /* 'r' */
  {0x48, 0x54, 0x54, 0x54, 0x20}, /* 's' */
  {0x04, 0x3f, 0x44, 0x40, 0x20}, /* 't' */
  {0x3c, 0x40, 0x40, 0x20, 0x7c}, /* 'u' */
  {0x1c, 0x20, 0x40, 0x20, 0x1c}, /* 'v' */
  {0x3c, 0x40, 0x30, 0x40, 0x3c}, /* 'w' */
  {0x44, 0x28, 0x10, 0x28, 0x44}, /* 'x' */
  {0x0c, 0x50, 0x50, 0x50, 0x3c}, /* 'y' */
  {0x44, 0x64, 0x54, 0x4c, 0x44}, /* 'z' */
  {0x00, 0x08, 0x36, 0x41, 0x00}, /* '{' */
  {0x00, 0x00, 0x7f, 0x00, 0x00}, /* '|' */
  {0x00, 0x41, 0x36, 0x08, 0x00}, /* '}' */
  {0x08, 0x08, 0x2a, 0x1c, 0x08}, /* '~' */
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: velafit_canvas_init
 ****************************************************************************/

void velafit_canvas_init(velafit_canvas_t *canvas,
                         void *buffer,
                         uint32_t width,
                         uint32_t height,
                         velafit_pixfmt_t format)
{
  if (canvas == NULL)
    {
      return;
    }

  canvas->buffer = buffer;
  canvas->width = width;
  canvas->height = height;
  canvas->format = format;
  canvas->theme = VELAFIT_THEME_CYBERPUNK;

  uint32_t bpp = 2;
  if (format == VELAFIT_PIXFMT_RGB888)
    {
      bpp = 3;
    }
  else if (format == VELAFIT_PIXFMT_ARGB8888)
    {
      bpp = 4;
    }

  canvas->stride_bytes = width * bpp;
}

/****************************************************************************
 * Name: velafit_canvas_set_theme
 ****************************************************************************/

void velafit_canvas_set_theme(velafit_canvas_t *canvas,
                              velafit_theme_t theme)
{
  if (canvas != NULL)
    {
      canvas->theme = theme;
    }
}

/****************************************************************************
 * Name: velafit_canvas_clear
 ****************************************************************************/

void velafit_canvas_clear(velafit_canvas_t *canvas,
                          velafit_color_t color)
{
  if (canvas == NULL || canvas->buffer == NULL)
    {
      return;
    }

  uint32_t w = canvas->width;
  uint32_t h = canvas->height;

#ifdef CONFIG_ESP32P4_PPA
  int ppa_fmt = ESP32P4_PPA_COLOR_RGB565;
  uint32_t ppa_color = 0;

  if (canvas->format == VELAFIT_PIXFMT_RGB565)
    {
      ppa_fmt = ESP32P4_PPA_COLOR_RGB565;
      ppa_color = ((color.r >> 3) << 11) |
                  ((color.g >> 2) << 5) |
                  (color.b >> 3);
    }
  else if (canvas->format == VELAFIT_PIXFMT_RGB888)
    {
      ppa_fmt = ESP32P4_PPA_COLOR_RGB888;
      ppa_color = ((uint32_t)color.r) |
                  ((uint32_t)color.g << 8) |
                  ((uint32_t)color.b << 16);
    }

  if (esp32p4_ppa_fill(canvas->buffer, w, h, ppa_color, ppa_fmt) == OK)
    {
      return;
    }
#endif

  if (canvas->format == VELAFIT_PIXFMT_RGB565)
    {
      uint16_t c565 = ((color.r >> 3) << 11) |
                      ((color.g >> 2) << 5) |
                      (color.b >> 3);
      uint16_t *dst = (uint16_t *)canvas->buffer;
      size_t total = w * h;
      for (size_t i = 0; i < total; i++)
        {
          dst[i] = c565;
        }
    }
  else if (canvas->format == VELAFIT_PIXFMT_RGB888)
    {
      uint8_t *dst = (uint8_t *)canvas->buffer;
      size_t total = w * h;
      for (size_t i = 0; i < total; i++)
        {
          *dst++ = color.r;
          *dst++ = color.g;
          *dst++ = color.b;
        }
    }
}

/****************************************************************************
 * Name: velafit_draw_pixel
 ****************************************************************************/

void velafit_draw_pixel(velafit_canvas_t *canvas,
                        int x,
                        int y,
                        velafit_color_t color)
{
  if (canvas == NULL || canvas->buffer == NULL)
    {
      return;
    }

  if (x < 0 || x >= (int)canvas->width || y < 0 || y >= (int)canvas->height)
    {
      return;
    }

  if (canvas->format == VELAFIT_PIXFMT_RGB565)
    {
      uint16_t c565 = ((color.r >> 3) << 11) |
                      ((color.g >> 2) << 5) |
                      (color.b >> 3);
      uint16_t *buf = (uint16_t *)canvas->buffer;
      buf[y * canvas->width + x] = c565;
    }
  else if (canvas->format == VELAFIT_PIXFMT_RGB888)
    {
      uint8_t *buf = (uint8_t *)canvas->buffer;
      size_t offset = (y * canvas->width + x) * 3;
      buf[offset]     = color.r;
      buf[offset + 1] = color.g;
      buf[offset + 2] = color.b;
    }
}

/****************************************************************************
 * Name: velafit_draw_line
 ****************************************************************************/

void velafit_draw_line(velafit_canvas_t *canvas,
                       int x0,
                       int y0,
                       int x1,
                       int y1,
                       velafit_color_t color,
                       int thickness)
{
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx - dy;

  while (1)
    {
      if (thickness <= 1)
        {
          velafit_draw_pixel(canvas, x0, y0, color);
        }
      else
        {
          int half = thickness / 2;
          for (int ox = -half; ox <= half; ox++)
            {
              for (int oy = -half; oy <= half; oy++)
                {
                  velafit_draw_pixel(canvas, x0 + ox, y0 + oy, color);
                }
            }
        }

      if (x0 == x1 && y0 == y1)
        {
          break;
        }

      int e2 = 2 * err;
      if (e2 > -dy)
        {
          err -= dy;
          x0 += sx;
        }

      if (e2 < dx)
        {
          err += dx;
          y0 += sy;
        }
    }
}

/****************************************************************************
 * Name: velafit_draw_arrow
 ****************************************************************************/

void velafit_draw_arrow(velafit_canvas_t *canvas,
                        int x0,
                        int y0,
                        int x1,
                        int y1,
                        int arrow_size,
                        velafit_color_t color,
                        int thickness)
{
  velafit_draw_line(canvas, x0, y0, x1, y1, color, thickness);

  float angle = atan2f((float)(y1 - y0), (float)(x1 - x0));
  float wing_angle = 0.5236f; /* ~30 deg */

  int x_wing1 = x1 - (int)((float)arrow_size * cosf(angle - wing_angle));
  int y_wing1 = y1 - (int)((float)arrow_size * sinf(angle - wing_angle));

  int x_wing2 = x1 - (int)((float)arrow_size * cosf(angle + wing_angle));
  int y_wing2 = y1 - (int)((float)arrow_size * sinf(angle + wing_angle));

  velafit_draw_line(canvas, x1, y1, x_wing1, y_wing1, color, thickness);
  velafit_draw_line(canvas, x1, y1, x_wing2, y_wing2, color, thickness);
}

/****************************************************************************
 * Name: velafit_draw_circle_filled
 ****************************************************************************/

void velafit_draw_circle_filled(velafit_canvas_t *canvas,
                                int cx,
                                int cy,
                                int radius,
                                velafit_color_t color)
{
  int r2 = radius * radius;
  for (int dy = -radius; dy <= radius; dy++)
    {
      for (int dx = -radius; dx <= radius; dx++)
        {
          if (dx * dx + dy * dy <= r2)
            {
              velafit_draw_pixel(canvas, cx + dx, cy + dy, color);
            }
        }
    }
}

/****************************************************************************
 * Name: velafit_draw_rect
 ****************************************************************************/

void velafit_draw_rect(velafit_canvas_t *canvas,
                       int x,
                       int y,
                       int w,
                       int h,
                       velafit_color_t color)
{
  velafit_draw_line(canvas, x, y, x + w - 1, y, color, 1);
  velafit_draw_line(canvas, x, y + h - 1, x + w - 1, y + h - 1, color, 1);
  velafit_draw_line(canvas, x, y, x, y + h - 1, color, 1);
  velafit_draw_line(canvas, x + w - 1, y, x + w - 1, y + h - 1, color, 1);
}

/****************************************************************************
 * Name: velafit_draw_rect_filled
 ****************************************************************************/

void velafit_draw_rect_filled(velafit_canvas_t *canvas,
                              int x,
                              int y,
                              int w,
                              int h,
                              velafit_color_t color)
{
  for (int j = 0; j < h; j++)
    {
      for (int i = 0; i < w; i++)
        {
          velafit_draw_pixel(canvas, x + i, y + j, color);
        }
    }
}

/****************************************************************************
 * Name: velafit_draw_char
 ****************************************************************************/

void velafit_draw_char(velafit_canvas_t *canvas,
                       int x,
                       int y,
                       char c,
                       velafit_color_t fg,
                       velafit_color_t bg,
                       int scale)
{
  if (c < 32 || c > 126)
    {
      c = '?';
    }

  int font_idx = c - 32;
  if (scale <= 0)
    {
      scale = 1;
    }

  for (int col = 0; col < 5; col++)
    {
      uint8_t line = g_font5x7[font_idx][col];
      for (int row = 0; row < 7; row++)
        {
          velafit_color_t color = (line & (1 << row)) ? fg : bg;
          if (scale == 1)
            {
              if (line & (1 << row))
                {
                  velafit_draw_pixel(canvas, x + col, y + row, color);
                }
            }
          else
            {
              for (int sx = 0; sx < scale; sx++)
                {
                  for (int sy = 0; sy < scale; sy++)
                    {
                      if (line & (1 << row))
                        {
                          velafit_draw_pixel(canvas, x + col * scale + sx,
                                             y + row * scale + sy, color);
                        }
                    }
                }
            }
        }
    }
}

/****************************************************************************
 * Name: velafit_draw_string
 ****************************************************************************/

void velafit_draw_string(velafit_canvas_t *canvas,
                         int x,
                         int y,
                         const char *str,
                         velafit_color_t fg,
                         velafit_color_t bg,
                         int scale)
{
  size_t length;
  int source_width;
  int source_height;
  int rotated_x;
  int rotated_y;

  if (str == NULL)
    {
      return;
    }

  if (scale <= 0)
    {
      scale = 1;
    }

  length = strlen(str);
  if (length == 0)
    {
      return;
    }

  /* The product LCD is physically mounted 90 degrees clockwise relative to
   * framebuffer coordinates. Rotate every text raster CCW90 around the
   * caller's original text-box center. Camera pixels and model coordinates
   * remain unchanged. */

  source_width = (int)(length * 6 - 1) * scale;
  source_height = 7 * scale;
  rotated_x = x + (source_width - source_height) / 2;
  rotated_y = y + (source_height - source_width) / 2;
  if (rotated_x < 2) rotated_x = 2;
  if (rotated_y < 2) rotated_y = 2;
  if (canvas != NULL)
    {
      if (rotated_x + source_height > (int)canvas->width - 2)
        rotated_x = (int)canvas->width - source_height - 2;
      if (rotated_y + source_width > (int)canvas->height - 2)
        rotated_y = (int)canvas->height - source_width - 2;
      if (rotated_x < 2) rotated_x = 2;
      if (rotated_y < 2) rotated_y = 2;
    }

  for (size_t index = 0; index < length; index++)
    {
      unsigned char c = (unsigned char)str[index];
      int font_idx;
      if (c < 32 || c > 126) c = '?';
      font_idx = c - 32;
      for (int col = 0; col < 5; col++)
        {
          uint8_t bits = g_font5x7[font_idx][col];
          for (int row = 0; row < 7; row++)
            {
              if ((bits & (1 << row)) == 0) continue;
              for (int sx = 0; sx < scale; sx++)
                for (int sy = 0; sy < scale; sy++)
                  {
                    int source_x = ((int)index * 6 + col) * scale + sx;
                    int source_y = row * scale + sy;
                    velafit_draw_pixel(canvas,
                                       rotated_x + source_y,
                                       rotated_y + source_width - 1 - source_x,
                                       fg);
                  }
            }
        }
    }

  (void)bg;
}

/****************************************************************************
 * Name: velafit_render_skeleton
 ****************************************************************************/

#include "../../sc2336_probe/sc2336_transform.h"

void velafit_render_sc2336_skeleton(velafit_canvas_t *canvas,
                                   const pose_frame_t *model_pose,
                                   uint16_t raw_w,uint16_t raw_h,
                                   uint32_t quality_flags)
{
  if (!canvas || !model_pose || !canvas->width || !canvas->height ||
      canvas->width>UINT16_MAX || canvas->height>UINT16_MAX) return;
  pose_frame_t display=*model_pose;
  for(unsigned i=0;i<VELAFIT_NUM_KEYPOINTS;i++)
    {
      float x,y;
      if(!isfinite(display.kpts[i].score) ||
         !sc2336_model_to_display(raw_w,raw_h,192,192,
            canvas->width,canvas->height,display.kpts[i].x,display.kpts[i].y,&x,&y))
        {display.kpts[i].x=display.kpts[i].y=0;display.kpts[i].score=0;}
      else
        {display.kpts[i].x=x/canvas->width;display.kpts[i].y=y/canvas->height;}
    }
  velafit_render_skeleton(canvas,&display,quality_flags);
}

void velafit_render_skeleton(velafit_canvas_t *canvas,
                             const pose_frame_t *pose,
                             uint32_t quality_flags)
{
  if (canvas == NULL || pose == NULL || !pose->valid)
    {
      return;
    }

  int w = (int)canvas->width;
  int h = (int)canvas->height;

  /* 1. Render Limbs */

  for (int i = 0; i < VELAFIT_NUM_LIMBS; i++)
    {
      kpt_id_t k1 = g_skeleton_limbs[i].start_kpt;
      kpt_id_t k2 = g_skeleton_limbs[i].end_kpt;

      if (pose->kpts[k1].score < KPT_CONF_THRESHOLD ||
          pose->kpts[k2].score < KPT_CONF_THRESHOLD)
        {
          continue;
        }

      int x0 = (int)(pose->kpts[k1].x * (float)w);
      int y0 = (int)(pose->kpts[k1].y * (float)h);
      int x1 = (int)(pose->kpts[k2].x * (float)w);
      int y1 = (int)(pose->kpts[k2].y * (float)h);

      velafit_color_t limb_color =
        (canvas->theme == VELAFIT_THEME_CYBERPUNK) ?
        VELAFIT_COLOR_CYAN : VELAFIT_COLOR_BLUE;

      /* Check quality flags for specific limb error highlight */

      if ((quality_flags & (SQUAT_QUALITY_KNEE_CAVING |
                            PUSHUP_QUALITY_ELBOW_FLARE)) &&
          (k1 == KPT_LEFT_HIP || k1 == KPT_RIGHT_HIP ||
           k1 == KPT_LEFT_KNEE || k1 == KPT_RIGHT_KNEE))
        {
          limb_color = VELAFIT_COLOR_RED;
        }
      else if ((quality_flags & (SQUAT_QUALITY_TRUNK_LEAN |
                                 PUSHUP_QUALITY_HIPS_SAG |
                                 PUSHUP_QUALITY_HIPS_PIKE |
                                 PLANK_QUALITY_HIPS_SAG |
                                 PLANK_QUALITY_HIPS_PIKE)) &&
               (k1 == KPT_LEFT_SHOULDER || k1 == KPT_RIGHT_SHOULDER ||
                k1 == KPT_LEFT_HIP || k1 == KPT_RIGHT_HIP))
        {
          limb_color = VELAFIT_COLOR_ORANGE;
        }
      else if (quality_flags & (SQUAT_QUALITY_SHALLOW |
                                PUSHUP_QUALITY_SHALLOW))
        {
          limb_color = VELAFIT_COLOR_YELLOW;
        }

      velafit_draw_line(canvas, x0, y0, x1, y1, limb_color, 2);
    }

  /* 2. Render Joint Keypoints */

  for (int i = 0; i < VELAFIT_NUM_KEYPOINTS; i++)
    {
      if (pose->kpts[i].score < KPT_CONF_THRESHOLD)
        {
          continue;
        }

      int cx = (int)(pose->kpts[i].x * (float)w);
      int cy = (int)(pose->kpts[i].y * (float)h);

      velafit_color_t kpt_color = VELAFIT_COLOR_GREEN;

      if ((quality_flags & (SQUAT_QUALITY_KNEE_CAVING |
                            PUSHUP_QUALITY_ELBOW_FLARE)) &&
          (i == KPT_LEFT_KNEE || i == KPT_RIGHT_KNEE ||
           i == KPT_LEFT_ANKLE || i == KPT_RIGHT_ANKLE))
        {
          kpt_color = VELAFIT_COLOR_RED;
        }
      else if ((quality_flags & (SQUAT_QUALITY_TRUNK_LEAN |
                                 PUSHUP_QUALITY_HIPS_SAG |
                                 PUSHUP_QUALITY_HIPS_PIKE |
                                 PLANK_QUALITY_HIPS_SAG |
                                 PLANK_QUALITY_HIPS_PIKE)) &&
               (i == KPT_LEFT_SHOULDER || i == KPT_RIGHT_SHOULDER ||
                i == KPT_LEFT_HIP || i == KPT_RIGHT_HIP))
        {
          kpt_color = VELAFIT_COLOR_ORANGE;
        }

      velafit_draw_circle_filled(canvas, cx, cy, 3, kpt_color);
      velafit_draw_circle_filled(canvas, cx, cy, 1, VELAFIT_COLOR_WHITE);
    }
}

/****************************************************************************
 * Name: velafit_render_guidance
 ****************************************************************************/

void velafit_render_guidance(velafit_canvas_t *canvas,
                             const pose_frame_t *pose,
                             uint32_t quality_flags,
                             const char *exercise)
{
  if (canvas == NULL || pose == NULL || !pose->valid)
    {
      return;
    }

  int w = (int)canvas->width;
  int h = (int)canvas->height;

  /* 1. Knee Valgus Guidance (Push Knees Outward) */

  if (quality_flags & SQUAT_QUALITY_KNEE_CAVING)
    {
      int lkx = (int)(pose->kpts[KPT_LEFT_KNEE].x * (float)w);
      int lky = (int)(pose->kpts[KPT_LEFT_KNEE].y * (float)h);
      int rkx = (int)(pose->kpts[KPT_RIGHT_KNEE].x * (float)w);
      int rky = (int)(pose->kpts[KPT_RIGHT_KNEE].y * (float)h);

      velafit_draw_arrow(canvas, lkx, lky, lkx - 16, lky, 6,
                         VELAFIT_COLOR_YELLOW, 2);
      velafit_draw_arrow(canvas, rkx, rky, rkx + 16, rky, 6,
                         VELAFIT_COLOR_YELLOW, 2);
      velafit_draw_string(canvas, lkx - 24, lky - 12, "PUSH OUT",
                          VELAFIT_COLOR_YELLOW, VELAFIT_COLOR_BLACK, 1);
    }

  /* 2. Trunk Lean Guidance (Chest Up) */

  if (quality_flags & SQUAT_QUALITY_TRUNK_LEAN)
    {
      int shx = (int)(pose->kpts[KPT_LEFT_SHOULDER].x * (float)w);
      int shy = (int)(pose->kpts[KPT_LEFT_SHOULDER].y * (float)h);

      velafit_draw_arrow(canvas, shx, shy, shx, shy - 20, 6,
                         VELAFIT_COLOR_ORANGE, 2);
      velafit_draw_string(canvas, shx - 18, shy - 30, "CHEST UP",
                          VELAFIT_COLOR_ORANGE, VELAFIT_COLOR_BLACK, 1);
    }

  /* 3. Hips Sagging Guidance (Lift Hips) */

  if (quality_flags & (PUSHUP_QUALITY_HIPS_SAG | PLANK_QUALITY_HIPS_SAG))
    {
      int hx = (int)(pose->kpts[KPT_LEFT_HIP].x * (float)w);
      int hy = (int)(pose->kpts[KPT_LEFT_HIP].y * (float)h);

      velafit_draw_arrow(canvas, hx, hy, hx, hy - 20, 6,
                         VELAFIT_COLOR_ORANGE, 2);
      velafit_draw_string(canvas, hx - 18, hy - 30, "LIFT HIPS",
                          VELAFIT_COLOR_ORANGE, VELAFIT_COLOR_BLACK, 1);
    }

  /* 4. Hips Piking Guidance (Lower Hips) */

  if (quality_flags & (PUSHUP_QUALITY_HIPS_PIKE | PLANK_QUALITY_HIPS_PIKE))
    {
      int hx = (int)(pose->kpts[KPT_LEFT_HIP].x * (float)w);
      int hy = (int)(pose->kpts[KPT_LEFT_HIP].y * (float)h);

      velafit_draw_arrow(canvas, hx, hy, hx, hy + 20, 6,
                         VELAFIT_COLOR_ORANGE, 2);
      velafit_draw_string(canvas, hx - 18, hy + 22, "LOWER HIPS",
                          VELAFIT_COLOR_ORANGE, VELAFIT_COLOR_BLACK, 1);
    }
}

/****************************************************************************
 * Name: velafit_render_depth_gauge
 ****************************************************************************/

void velafit_render_depth_gauge(velafit_canvas_t *canvas,
                                int x,
                                int y,
                                int w,
                                int h,
                                float current_angle,
                                float target_angle,
                                const char *label)
{
  if (canvas == NULL)
    {
      return;
    }

  /* Background Box */

  velafit_draw_rect_filled(canvas, x, y, w, h, VELAFIT_COLOR_DARKGRAY);
  velafit_draw_rect(canvas, x, y, w, h, VELAFIT_COLOR_GRAY);

  /* Calculate Normalized Depth (180 deg = 0%, 90 deg = 100%) */

  float depth_pct = (180.0f - current_angle) / (180.0f - target_angle);
  if (depth_pct < 0.0f)
    {
      depth_pct = 0.0f;
    }

  if (depth_pct > 1.2f)
    {
      depth_pct = 1.2f;
    }

  int fill_h = (int)((float)(h - 4) * depth_pct);
  if (fill_h > h - 4)
    {
      fill_h = h - 4;
    }

  velafit_color_t bar_color = VELAFIT_COLOR_CYAN;
  if (depth_pct >= 0.95f)
    {
      bar_color = VELAFIT_COLOR_GREEN;
    }
  else if (depth_pct >= 0.60f)
    {
      bar_color = VELAFIT_COLOR_YELLOW;
    }

  if (fill_h > 0)
    {
      velafit_draw_rect_filled(canvas, x + 2, y + h - 2 - fill_h,
                               w - 4, fill_h, bar_color);
    }

  /* Target Depth Threshold Marker Line */

  int target_y = y + 2;
  velafit_draw_line(canvas, x - 2, target_y, x + w + 1, target_y,
                    VELAFIT_COLOR_WHITE, 2);

  /* Draw Value & Label */

  char val_buf[16];
  snprintf(val_buf, sizeof(val_buf), "%d*", (int)current_angle);
  velafit_draw_string(canvas, x - 18, y + h + 4, val_buf,
                      VELAFIT_COLOR_WHITE, VELAFIT_COLOR_BLACK, 1);

  if (label != NULL)
    {
      velafit_draw_string(canvas, x - 18, y - 10, label,
                          VELAFIT_COLOR_CYAN, VELAFIT_COLOR_BLACK, 1);
    }
}

/****************************************************************************
 * Name: velafit_render_hud
 ****************************************************************************/

void velafit_render_hud(velafit_canvas_t *canvas,
                        const char *exercise,
                        uint32_t reps,
                        float fps,
                        uint32_t quality_flags,
                        const char *feedback_msg)
{
  if (canvas == NULL)
    {
      return;
    }

  int w = (int)canvas->width;
  int h = (int)canvas->height;

  /* 1. Top Status Banner */

  velafit_draw_rect_filled(canvas, 0, 0, w, 24, VELAFIT_COLOR_DARKGRAY);

  char buf[64];
  snprintf(buf, sizeof(buf), "VELAFIT: %s  REPS:%lu  FPS:%.1f",
           (exercise != NULL) ? exercise : "WORKOUT",
           (unsigned long)reps,
           fps);
  velafit_draw_string(canvas, 8, 8, buf,
                      VELAFIT_COLOR_WHITE,
                      VELAFIT_COLOR_DARKGRAY,
                      1);

  /* 2. Bottom Coaching / Feedback Panel */

  velafit_color_t panel_color = VELAFIT_COLOR_DARKGRAY;
  velafit_color_t text_color = VELAFIT_COLOR_GREEN;

  if (quality_flags != SQUAT_QUALITY_OK)
    {
      panel_color = VELAFIT_COLOR_DARKGRAY;
      text_color = VELAFIT_COLOR_ORANGE;
    }

  velafit_draw_rect_filled(canvas, 0, h - 28, w, 28, panel_color);
  velafit_draw_line(canvas, 0, h - 28, w, h - 28, text_color, 2);

  const char *msg = (feedback_msg != NULL) ? feedback_msg : "READY";
  velafit_draw_string(canvas, 8, h - 20, msg,
                      text_color,
                      panel_color,
                      1);
}

/****************************************************************************
 * Name: velafit_render_dashboard
 ****************************************************************************/

void velafit_render_session_status(velafit_canvas_t *canvas,
                                   const char *state, uint32_t seconds,
                                   bool awake, bool online)
{
  if (!canvas || !canvas->buffer || canvas->width < 16 || canvas->height < 80)
    return;
  char line[96];
  snprintf(line,sizeof(line),"%s %lu:%02lu WAKE:%s NET:%s",
           state ? state : "IDLE", (unsigned long)(seconds/60),
           (unsigned long)(seconds%60), awake ? "YES":"NO",
           online ? "ON":"OFF");
  /* Existing bitmap glyphs advance six pixels. Clip at whole glyphs,
   * reserve a separate status row below the count banner. */
  size_t columns=(canvas->width-16)/6;
  if (columns<sizeof(line)) line[columns]=0;
  velafit_draw_rect_filled(canvas,0,24,canvas->width,16,VELAFIT_COLOR_DARKGRAY);
  velafit_draw_string(canvas,8,28,line,VELAFIT_COLOR_WHITE,
                      VELAFIT_COLOR_DARKGRAY,1);
}

void velafit_render_dashboard(velafit_canvas_t *canvas,
                              const pose_frame_t *pose,
                              const char *exercise,
                              uint32_t reps,
                              float calories_kcal,
                              float fps,
                              float current_angle,
                              float target_angle,
                              uint32_t quality_flags,
                              const char *feedback_msg)
{
  if (canvas == NULL)
    {
      return;
    }

  int w = (int)canvas->width;
  int h = (int)canvas->height;

  /* 1. Clear Canvas */

  velafit_canvas_clear(canvas, VELAFIT_COLOR_BLACK);

  /* 2. Render Skeleton & Correction Guidance */

  if (pose != NULL && pose->valid)
    {
      velafit_render_skeleton(canvas, pose, quality_flags);
      velafit_render_guidance(canvas, pose, quality_flags, exercise);
    }

  /* 3. Top Banner Overlay */

  velafit_draw_rect_filled(canvas, 0, 0, w, 24, VELAFIT_COLOR_DARKGRAY);
  char buf[64];
  snprintf(buf, sizeof(buf), "VELAFIT: %s  REPS:%lu  CAL:%.1f kcal",
           (exercise != NULL) ? exercise : "WORKOUT",
           (unsigned long)reps,
           calories_kcal);
  velafit_draw_string(canvas, 8, 8, buf,
                      VELAFIT_COLOR_WHITE,
                      VELAFIT_COLOR_DARKGRAY,
                      1);

  /* 4. Render Right-side Depth / Angle Gauge */

  if (target_angle > 0.0f)
    {
      velafit_render_depth_gauge(canvas, w - 24, 36, 12, h - 76,
                                 current_angle, target_angle, "DEPTH");
    }

  /* 5. Bottom Feedback Panel */

  velafit_color_t text_color = (quality_flags == SQUAT_QUALITY_OK) ?
                               VELAFIT_COLOR_GREEN : VELAFIT_COLOR_ORANGE;

  velafit_draw_rect_filled(canvas, 0, h - 28, w, 28,
                           VELAFIT_COLOR_DARKGRAY);
  velafit_draw_line(canvas, 0, h - 28, w, h - 28, text_color, 2);

  const char *msg = (feedback_msg != NULL) ? feedback_msg : "PERFECT FORM";
  velafit_draw_string(canvas, 8, h - 20, msg,
                      text_color,
                      VELAFIT_COLOR_DARKGRAY,
                      1);
}

/****************************************************************************
 * Name: velafit_render_save_ppm
 ****************************************************************************/

int velafit_render_save_ppm(const velafit_canvas_t *canvas,
                            const char *filename)
{
  if (canvas == NULL || canvas->buffer == NULL || filename == NULL)
    {
      return -EINVAL;
    }

  FILE *fp = fopen(filename, "wb");
  if (fp == NULL)
    {
      return -errno;
    }

  fprintf(fp, "P6\n%lu %lu\n255\n",
          (unsigned long)canvas->width,
          (unsigned long)canvas->height);

  uint32_t w = canvas->width;
  uint32_t h = canvas->height;

  if (canvas->format == VELAFIT_PIXFMT_RGB565)
    {
      const uint16_t *src = (const uint16_t *)canvas->buffer;
      for (uint32_t i = 0; i < w * h; i++)
        {
          uint16_t p = src[i];
          uint8_t r = ((p >> 11) & 0x1f) << 3;
          uint8_t g = ((p >> 5) & 0x3f) << 2;
          uint8_t b = (p & 0x1f) << 3;
          fputc(r, fp);
          fputc(g, fp);
          fputc(b, fp);
        }
    }
  else if (canvas->format == VELAFIT_PIXFMT_RGB888)
    {
      fwrite(canvas->buffer, 1, w * h * 3, fp);
    }

  fclose(fp);
  return OK;
}

/****************************************************************************
 * Name: velafit_render_to_fb0
 ****************************************************************************/

int velafit_render_to_fb0(const velafit_canvas_t *canvas)
{
  if (canvas == NULL || canvas->buffer == NULL)
    {
      return -EINVAL;
    }

  int fd = open("/dev/fb0", O_RDWR);
  if (fd < 0)
    {
      return -errno;
    }

  size_t bytes_to_write = canvas->width * canvas->height * 2;
  ssize_t written = write(fd, canvas->buffer, bytes_to_write);
  close(fd);

  if (written < (ssize_t)bytes_to_write)
    {
      return -EIO;
    }

  return OK;
}

/****************************************************************************
 * Name: velafit_render_blend_background
 ****************************************************************************/

int velafit_render_blend_background(velafit_canvas_t *canvas,
                                    const void *bg_image,
                                    uint8_t alpha)
{
  if (canvas == NULL || canvas->buffer == NULL || bg_image == NULL)
    {
      return -EINVAL;
    }

#ifdef CONFIG_ESP32P4_PPA
  int ppa_fmt = (canvas->format == VELAFIT_PIXFMT_RGB888) ?
                ESP32P4_PPA_COLOR_RGB888 : ESP32P4_PPA_COLOR_RGB565;

  return esp32p4_ppa_blend(bg_image, canvas->buffer, canvas->buffer,
                           canvas->width, canvas->height, alpha, ppa_fmt);
#else
  return -ENOSYS;
#endif
}

/****************************************************************************
 * Name: velafit_render_scale_to_fb0
 ****************************************************************************/

int velafit_render_scale_to_fb0(const velafit_canvas_t *canvas,
                                void *fb_mem,
                                uint16_t fb_w,
                                uint16_t fb_h)
{
  if (canvas == NULL || canvas->buffer == NULL || fb_mem == NULL ||
      fb_w == 0 || fb_h == 0)
    {
      return -EINVAL;
    }

#ifdef CONFIG_ESP32P4_PPA
  int ppa_fmt = (canvas->format == VELAFIT_PIXFMT_RGB888) ?
                ESP32P4_PPA_COLOR_RGB888 : ESP32P4_PPA_COLOR_RGB565;

  return esp32p4_ppa_scale(canvas->buffer, canvas->width, canvas->height,
                           fb_mem, fb_w, fb_h, ppa_fmt);
#else
  return -ENOSYS;
#endif
}
