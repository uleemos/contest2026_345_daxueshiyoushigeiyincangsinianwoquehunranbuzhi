/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_TINY_POSE_PREPROCESS_H
#define VELAFIT_TINY_POSE_PREPROCESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void velafit_tiny_pose_preprocess_rgb192(const uint8_t *source,
                                         int8_t *destination,
                                         float scale,
                                         int zero_point);

/* Export the exact geometric input to TinyPose before quantization as an
 * RGB96 diagnostic image. Four-pixel area averages are rounded to uint8. */
void velafit_tiny_pose_debug_rgb96(const uint8_t *source,
                                   uint8_t *destination);

#ifdef __cplusplus
}
#endif
#endif
