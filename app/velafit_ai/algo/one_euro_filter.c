/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/algo/one_euro_filter.c
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

#include <math.h>

#include "one_euro_filter.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: calc_alpha
 ****************************************************************************/

static float calc_alpha(float rate, float cutoff)
{
  float tau = 1.0f / (2.0f * (float)M_PI * cutoff);
  float te = 1.0f / rate;
  return 1.0f / (1.0f + tau / te);
}

/****************************************************************************
 * Name: one_euro_filter_step
 ****************************************************************************/

static float one_euro_filter_step(low_pass_filter_t *xf,
                                  float val,
                                  float timestamp,
                                  float min_cutoff,
                                  float beta,
                                  float d_cutoff)
{
  if (!xf->initialized)
    {
      xf->t_prev = timestamp;
      xf->raw_prev = val;
      xf->x_prev = val;
      xf->dx_prev = 0.0f;
      xf->initialized = true;
      return val;
    }

  float dt = timestamp - xf->t_prev;
  if (dt <= 1e-4f)
    {
      dt = 1e-4f;
    }

  xf->t_prev = timestamp;

  float rate = 1.0f / dt;
  float dx = (val - xf->raw_prev) * rate;
  xf->raw_prev = val;
  float d_alpha = calc_alpha(rate, d_cutoff);
  float edx = d_alpha * dx + (1.0f - d_alpha) * xf->dx_prev;
  xf->dx_prev = edx;

  float cutoff = min_cutoff + beta * fabsf(edx);
  float alpha = calc_alpha(rate, cutoff);
  float filtered = alpha * val + (1.0f - alpha) * xf->x_prev;
  xf->x_prev = filtered;
  return filtered;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: one_euro_pose_filter_init
 ****************************************************************************/

void one_euro_pose_filter_init(one_euro_pose_filter_t *filter,
                               float min_cutoff,
                               float beta,
                               float d_cutoff)
{
  for (int i = 0; i < VELAFIT_NUM_KEYPOINTS; i++)
    {
      filter->points[i].x_filt.initialized = false;
      filter->points[i].y_filt.initialized = false;
      filter->points[i].min_cutoff = min_cutoff;
      filter->points[i].beta = beta;
      filter->points[i].d_cutoff = d_cutoff;
    }
}

/****************************************************************************
 * Name: one_euro_pose_filter_apply
 ****************************************************************************/

void one_euro_pose_filter_apply(one_euro_pose_filter_t *filter,
                                const pose_frame_t *raw_pose,
                                pose_frame_t *filtered_pose,
                                float timestamp_sec)
{
  filtered_pose->timestamp_ms = raw_pose->timestamp_ms;
  filtered_pose->valid = raw_pose->valid;

  for (int i = 0; i < VELAFIT_NUM_KEYPOINTS; i++)
    {
      one_euro_point_t *pt = &filter->points[i];
      /* A missing/non-finite observation must not poison persistent filter
       * state. Reinitialize this point when a valid observation returns. */
      if (!raw_pose->valid || !isfinite(raw_pose->kpts[i].x) ||
          !isfinite(raw_pose->kpts[i].y) ||
          !isfinite(raw_pose->kpts[i].score) || raw_pose->kpts[i].score < 0.3f)
        {
          pt->x_filt.initialized = false;
          pt->y_filt.initialized = false;
          filtered_pose->kpts[i].x = 0.0f;
          filtered_pose->kpts[i].y = 0.0f;
          filtered_pose->kpts[i].score = 0.0f;
          continue;
        }
      filtered_pose->kpts[i].x =
        one_euro_filter_step(&pt->x_filt,
                             raw_pose->kpts[i].x,
                             timestamp_sec,
                             pt->min_cutoff,
                             pt->beta,
                             pt->d_cutoff);

      filtered_pose->kpts[i].y =
        one_euro_filter_step(&pt->y_filt,
                             raw_pose->kpts[i].y,
                             timestamp_sec,
                             pt->min_cutoff,
                             pt->beta,
                             pt->d_cutoff);

      filtered_pose->kpts[i].score = raw_pose->kpts[i].score;
    }
}
