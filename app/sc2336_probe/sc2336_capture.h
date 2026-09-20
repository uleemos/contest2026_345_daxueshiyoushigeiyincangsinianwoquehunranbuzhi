/****************************************************************************
 * app/sc2336_probe/sc2336_capture.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_SC2336_PROBE_SC2336_CAPTURE_H
#define __APP_SC2336_PROBE_SC2336_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

struct sc2336_rgb_capture_s
{
  uint32_t raw_crc32;
  uint32_t rgb_crc32;
  uint64_t capture_us;
  uint64_t convert_us;
  uint16_t content_x;
  uint16_t content_y;
  uint16_t content_w;
  uint16_t content_h;
  uint8_t linear_mean_r;
  uint8_t linear_mean_g;
  uint8_t linear_mean_b;
  uint8_t mean_r;
  uint8_t mean_g;
  uint8_t mean_b;
  uint16_t wb_gain_r_q8;
  uint16_t wb_gain_g_q8;
  uint16_t wb_gain_b_q8;
};

int sc2336_raw10_bggr_letterbox(const uint8_t *raw, size_t raw_len,
                                uint16_t raw_w, uint16_t raw_h,
                                uint8_t *rgb, size_t rgb_len,
                                uint16_t rgb_w, uint16_t rgb_h,
                                struct sc2336_rgb_capture_s *result);

int sc2336_raw10_bggr_preview_rgb565(const uint8_t *raw, size_t raw_len,
                                     uint16_t raw_w, uint16_t raw_h,
                                     uint16_t *fb, size_t fb_len,
                                     uint16_t fb_w, uint16_t fb_h,
                                     uint16_t fb_stride);

int sc2336_capture_rgb888_letterbox(uint8_t *rgb, size_t rgb_len,
                                    uint16_t rgb_w, uint16_t rgb_h,
                                    struct sc2336_rgb_capture_s *result);

#endif
