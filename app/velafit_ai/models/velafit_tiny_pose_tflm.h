/****************************************************************************
 * app/velafit_ai/models/velafit_tiny_pose_tflm.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_VELAFIT_AI_MODELS_VELAFIT_TINY_POSE_TFLM_H
#define __APP_VELAFIT_AI_MODELS_VELAFIT_TINY_POSE_TFLM_H

#include <stdint.h>

#include "velafit_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

int velafit_tiny_pose_benchmark(unsigned int rounds);
int velafit_tiny_pose_init(void);
void velafit_tiny_pose_deinit(void);
int velafit_tiny_pose_infer_rgb192(const uint8_t *rgb192,
                                   pose_frame_t *pose,
                                   velafit_perf_t *perf);

#ifdef __cplusplus
}
#endif

#endif
