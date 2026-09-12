/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/algo/plank_fsm.c
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

#include <stdio.h>

#include "plank_fsm.h"
#include "geometry.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PLANK_BODY_LINE_IDEAL_MIN     160.0f
#define PLANK_BODY_LINE_SAG_THRESH    155.0f
#define PLANK_MAX_DT_MS               1000

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: plank_fsm_init
 ****************************************************************************/

void plank_fsm_init(plank_fsm_t *fsm)
{
  fsm->state = PLANK_STATE_IDLE;
  fsm->start_time_ms = 0;
  fsm->last_update_time_ms = UINT32_MAX;
  fsm->total_hold_duration_ms = 0;
  fsm->valid_hold_duration_ms = 0;
  fsm->hips_sag_duration_ms = 0;
  fsm->hips_pike_duration_ms = 0;
  fsm->head_drop_duration_ms = 0;
  fsm->last_quality_flags = PLANK_QUALITY_OK;
  fsm->curr_body_line_angle = 180.0f;
}

/****************************************************************************
 * Name: plank_fsm_reset
 ****************************************************************************/

void plank_fsm_reset(plank_fsm_t *fsm)
{
  plank_fsm_init(fsm);
}

/****************************************************************************
 * Name: plank_fsm_update
 ****************************************************************************/

bool plank_fsm_update(plank_fsm_t *fsm,
                      const pose_frame_t *pose,
                      uint32_t timestamp_ms)
{
  if (!pose || !pose->valid)
    {
      return false;
    }

  if (fsm->last_update_time_ms == UINT32_MAX)
    {
      fsm->start_time_ms = timestamp_ms;
      fsm->last_update_time_ms = timestamp_ms;
      fsm->state = PLANK_STATE_HOLDING;
      return true;
    }

  uint32_t dt = timestamp_ms - fsm->last_update_time_ms;
  if (dt > PLANK_MAX_DT_MS)
    {
      dt = 33; /* Default ~30 FPS step */
    }

  fsm->last_update_time_ms = timestamp_ms;

  float body_line = velafit_get_body_line_angle(pose);
  fsm->curr_body_line_angle = body_line;

  float mid_shoulder_y =
    (pose->kpts[KPT_LEFT_SHOULDER].y +
     pose->kpts[KPT_RIGHT_SHOULDER].y) * 0.5f;
  float mid_hip_y =
    (pose->kpts[KPT_LEFT_HIP].y +
     pose->kpts[KPT_RIGHT_HIP].y) * 0.5f;
  float mid_ankle_y =
    (pose->kpts[KPT_LEFT_ANKLE].y +
     pose->kpts[KPT_RIGHT_ANKLE].y) * 0.5f;
  float exp_hip_y = (mid_shoulder_y + mid_ankle_y) * 0.5f;

  fsm->state = PLANK_STATE_HOLDING;
  fsm->total_hold_duration_ms += dt;

  uint32_t flags = PLANK_QUALITY_OK;

  if (body_line < PLANK_BODY_LINE_SAG_THRESH)
    {
      if (mid_hip_y > exp_hip_y + 0.04f)
        {
          flags |= PLANK_QUALITY_HIPS_SAG;
          fsm->hips_sag_duration_ms += dt;
        }
      else if (mid_hip_y < exp_hip_y - 0.04f)
        {
          flags |= PLANK_QUALITY_HIPS_PIKE;
          fsm->hips_pike_duration_ms += dt;
        }
    }

  float nose_y = pose->kpts[KPT_NOSE].y;
  if (nose_y > mid_shoulder_y + 0.12f)
    {
      flags |= PLANK_QUALITY_HEAD_DROP;
      fsm->head_drop_duration_ms += dt;
    }

  if (flags == PLANK_QUALITY_OK)
    {
      fsm->valid_hold_duration_ms += dt;
    }

  fsm->last_quality_flags = flags;
  return true;
}

/****************************************************************************
 * Name: plank_fsm_get_quality_score
 ****************************************************************************/

float plank_fsm_get_quality_score(const plank_fsm_t *fsm)
{
  if (fsm->total_hold_duration_ms == 0)
    {
      return 100.0f;
    }

  return ((float)fsm->valid_hold_duration_ms * 100.0f) /
         (float)fsm->total_hold_duration_ms;
}
