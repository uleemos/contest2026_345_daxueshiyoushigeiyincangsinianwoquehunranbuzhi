/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/render/velafit_render.h
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

#ifndef __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_RENDER_VELAFIT_RENDER_H
#define __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_RENDER_VELAFIT_RENDER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "velafit_types.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VELAFIT_COLOR_RGB(r, g, b)  ((velafit_color_t){ (r), (g), (b) })

#define VELAFIT_COLOR_BLACK         VELAFIT_COLOR_RGB(0, 0, 0)
#define VELAFIT_COLOR_WHITE         VELAFIT_COLOR_RGB(255, 255, 255)
#define VELAFIT_COLOR_RED           VELAFIT_COLOR_RGB(255, 23, 68)
#define VELAFIT_COLOR_GREEN         VELAFIT_COLOR_RGB(0, 230, 118)
#define VELAFIT_COLOR_BLUE          VELAFIT_COLOR_RGB(41, 121, 255)
#define VELAFIT_COLOR_YELLOW        VELAFIT_COLOR_RGB(255, 234, 0)
#define VELAFIT_COLOR_CYAN          VELAFIT_COLOR_RGB(0, 229, 255)
#define VELAFIT_COLOR_ORANGE        VELAFIT_COLOR_RGB(255, 145, 0)
#define VELAFIT_COLOR_MAGENTA       VELAFIT_COLOR_RGB(255, 64, 129)
#define VELAFIT_COLOR_PURPLE        VELAFIT_COLOR_RGB(170, 0, 255)
#define VELAFIT_COLOR_GRAY          VELAFIT_COLOR_RGB(97, 97, 97)
#define VELAFIT_COLOR_DARKGRAY      VELAFIT_COLOR_RGB(33, 33, 33)

#define VELAFIT_NUM_LIMBS           16

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum
{
  VELAFIT_PIXFMT_RGB565 = 0,
  VELAFIT_PIXFMT_RGB888,
  VELAFIT_PIXFMT_ARGB8888
} velafit_pixfmt_t;

typedef enum
{
  VELAFIT_THEME_CYBERPUNK = 0,
  VELAFIT_THEME_SPORT_CLASSIC,
  VELAFIT_THEME_HIGH_CONTRAST,
  VELAFIT_THEME_MAX
} velafit_theme_t;

typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} velafit_color_t;

typedef struct
{
  void *buffer;
  uint32_t width;
  uint32_t height;
  uint32_t stride_bytes;
  velafit_pixfmt_t format;
  velafit_theme_t theme;
} velafit_canvas_t;

typedef struct
{
  kpt_id_t start_kpt;
  kpt_id_t end_kpt;
} velafit_limb_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

void velafit_canvas_init(velafit_canvas_t *canvas,
                         void *buffer,
                         uint32_t width,
                         uint32_t height,
                         velafit_pixfmt_t format);

void velafit_canvas_set_theme(velafit_canvas_t *canvas,
                              velafit_theme_t theme);

void velafit_canvas_clear(velafit_canvas_t *canvas,
                          velafit_color_t color);

void velafit_draw_pixel(velafit_canvas_t *canvas,
                        int x,
                        int y,
                        velafit_color_t color);

void velafit_draw_line(velafit_canvas_t *canvas,
                       int x0,
                       int y0,
                       int x1,
                       int y1,
                       velafit_color_t color,
                       int thickness);

void velafit_draw_arrow(velafit_canvas_t *canvas,
                        int x0,
                        int y0,
                        int x1,
                        int y1,
                        int arrow_size,
                        velafit_color_t color,
                        int thickness);

void velafit_draw_circle_filled(velafit_canvas_t *canvas,
                                int cx,
                                int cy,
                                int radius,
                                velafit_color_t color);

void velafit_draw_rect(velafit_canvas_t *canvas,
                       int x,
                       int y,
                       int w,
                       int h,
                       velafit_color_t color);

void velafit_draw_rect_filled(velafit_canvas_t *canvas,
                              int x,
                              int y,
                              int w,
                              int h,
                              velafit_color_t color);

void velafit_draw_char(velafit_canvas_t *canvas,
                       int x,
                       int y,
                       char c,
                       velafit_color_t fg,
                       velafit_color_t bg,
                       int scale);

void velafit_draw_string(velafit_canvas_t *canvas,
                         int x,
                         int y,
                         const char *str,
                         velafit_color_t fg,
                         velafit_color_t bg,
                         int scale);

/* Product mounting policy: strings are rasterized CCW90 in framebuffer
 * coordinates. This does not rotate camera preview or pose coordinates. */

/* SC2336 CCW90 + 192-square letterbox output -> center-cover preview.
 * Only display coordinates change. Never feed these cropped coordinates to
 * angle computation. Canvas dimensions are native framebuffer dimensions.
 */
void velafit_render_sc2336_skeleton(velafit_canvas_t *canvas,
                                   const pose_frame_t *model_pose,
                                   uint16_t raw_w, uint16_t raw_h,
                                   uint32_t quality_flags);

void velafit_render_skeleton(velafit_canvas_t *canvas,
                             const pose_frame_t *pose,
                             uint32_t quality_flags);

void velafit_render_guidance(velafit_canvas_t *canvas,
                             const pose_frame_t *pose,
                             uint32_t quality_flags,
                             const char *exercise);

void velafit_render_depth_gauge(velafit_canvas_t *canvas,
                                int x,
                                int y,
                                int w,
                                int h,
                                float current_angle,
                                float target_angle,
                                const char *label);

void velafit_render_hud(velafit_canvas_t *canvas,
                        const char *exercise,
                        uint32_t reps,
                        float fps,
                        uint32_t quality_flags,
                        const char *feedback_msg);

void velafit_render_dashboard(velafit_canvas_t *canvas,
                              const pose_frame_t *pose,
                              const char *exercise,
                              uint32_t reps,
                              float calories_kcal,
                              float fps,
                              float current_angle,
                              float target_angle,
                              uint32_t quality_flags,
                              const char *feedback_msg);

int velafit_render_save_ppm(const velafit_canvas_t *canvas,
                            const char *filename);

void velafit_render_session_status(velafit_canvas_t *canvas,
                                   const char *state, uint32_t seconds,
                                   bool awake, bool online);

int velafit_render_to_fb0(const velafit_canvas_t *canvas);

int velafit_render_blend_background(velafit_canvas_t *canvas,
                                    const void *bg_image,
                                    uint8_t alpha);

int velafit_render_scale_to_fb0(const velafit_canvas_t *canvas,
                                void *fb_mem,
                                uint16_t fb_w,
                                uint16_t fb_h);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_RENDER_VELAFIT_RENDER_H */
