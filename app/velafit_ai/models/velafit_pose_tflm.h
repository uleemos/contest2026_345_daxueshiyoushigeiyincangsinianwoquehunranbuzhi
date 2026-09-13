/****************************************************************************
 * app/velafit_ai/models/velafit_pose_tflm.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_VELAFIT_AI_MODELS_VELAFIT_POSE_TFLM_H
#define __APP_VELAFIT_AI_MODELS_VELAFIT_POSE_TFLM_H

#include "velafit_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

int velafit_pose_tflm_init(void);
void velafit_pose_tflm_deinit(void);
int velafit_pose_tflm_infer(const uint8_t *rgb192,
                            pose_frame_t *out_pose,
                            velafit_perf_t *perf);

#ifdef __cplusplus
}
#endif

#endif
