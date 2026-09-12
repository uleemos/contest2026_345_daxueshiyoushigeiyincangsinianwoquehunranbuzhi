/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/algo/one_euro_filter.h
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

#ifndef __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_ALGO_ONE_EURO_FILTER_H
#define __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_ALGO_ONE_EURO_FILTER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>

#include "velafit_types.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct
{
  float raw_prev;
  float x_prev;
  float dx_prev;
  float t_prev;
  bool initialized;
} low_pass_filter_t;

typedef struct
{
  low_pass_filter_t x_filt;
  low_pass_filter_t y_filt;
  float min_cutoff;
  float beta;
  float d_cutoff;
} one_euro_point_t;

typedef struct
{
  one_euro_point_t points[VELAFIT_NUM_KEYPOINTS];
} one_euro_pose_filter_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

void one_euro_pose_filter_init(one_euro_pose_filter_t *filter,
                               float min_cutoff,
                               float beta,
                               float d_cutoff);

void one_euro_pose_filter_apply(one_euro_pose_filter_t *filter,
                                const pose_frame_t *raw_pose,
                                pose_frame_t *filtered_pose,
                                float timestamp_sec);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_ALGO_ONE_EURO_FILTER_H */
