/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/models/sample_pose_frames.h
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

#ifndef __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_MODELS_SAMPLE_POSE_FRAMES_H
#define __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_MODELS_SAMPLE_POSE_FRAMES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "velafit_types.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

extern const pose_frame_t g_sample_pose_standing;
extern const pose_frame_t g_sample_pose_squat_deep;
extern const pose_frame_t g_sample_pose_squat_shallow;
extern const pose_frame_t g_sample_pose_squat_valgus;
extern const pose_frame_t g_sample_pose_squat_lean;

extern const pose_frame_t g_sample_pose_pushup_plank;
extern const pose_frame_t g_sample_pose_pushup_bottom_deep;
extern const pose_frame_t g_sample_pose_pushup_shallow;
extern const pose_frame_t g_sample_pose_pushup_sag;

extern const pose_frame_t g_sample_pose_plank_perfect;
extern const pose_frame_t g_sample_pose_plank_sag;
extern const pose_frame_t g_sample_pose_plank_pike;

extern const pose_frame_t g_sample_pose_jj_closed;
extern const pose_frame_t g_sample_pose_jj_open;

/* Sample 160x160x3 RGB test image byte buffer */

extern const uint8_t g_sample_test_image_160x160[160 * 160 * 3];

#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
extern const uint8_t g_velafit_pose_fixture_rgb192[192 * 192 * 3];
extern const uint32_t g_velafit_pose_fixture_rgb192_size;
#endif

#ifdef __cplusplus
}
#endif

#endif /* __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_MODELS_SAMPLE_POSE_FRAMES_H */
