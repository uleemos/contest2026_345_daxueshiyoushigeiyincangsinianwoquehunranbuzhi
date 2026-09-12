/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/models/velafit_pose_model.h
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

#ifndef __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_MODELS_VELAFIT_POSE_MODEL_H
#define __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_MODELS_VELAFIT_POSE_MODEL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "velafit_types.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

int velafit_pose_model_init(void);

void velafit_pose_model_deinit(void);

const char *velafit_pose_model_backend_name(void);

bool velafit_pose_model_backend_is_real(void);

int velafit_pose_infer(const uint8_t *rgb_image,
                       pose_frame_t *out_pose,
                       velafit_perf_t *perf);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_MODELS_VELAFIT_POSE_MODEL_H */
