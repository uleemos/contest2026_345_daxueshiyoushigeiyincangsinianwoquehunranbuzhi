/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SC2336_MODEL_PREVIEW_H
#define SC2336_MODEL_PREVIEW_H

#include <stddef.h>
#include <stdint.h>

/* Draw the final RGB96 model image counterclockwise relative to framebuffer
 * text coordinates. Integer nearest-neighbor scaling preserves every model
 * pixel and the image is centered without stretching or cover-cropping. */
int sc2336_model_preview_blit_ccw_rgb565(const uint8_t *rgb96,
                                         size_t rgb96_len,
                                         uint16_t *framebuffer,
                                         size_t framebuffer_len,
                                         uint16_t width,
                                         uint16_t height,
                                         uint16_t stride,
                                         uint8_t scale);

#endif
