/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/algo/squat_fsm.c
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
#include <math.h>

#include "squat_fsm.h"
#include "geometry.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SQUAT_ANGLE_STAND_THRESH     155.0f
#define SQUAT_ANGLE_DESCEND_THRESH   140.0f
#define SQUAT_ANGLE_DEEP_TARGET       95.0f
#define SQUAT_ANGLE_SHALLOW_LIMIT    105.0f
#define SQUAT_ANGLE_ASCEND_THRESH    115.0f
#define SQUAT_VALGUS_RATIO_LIMIT       0.72f
#define SQUAT_TRUNK_LEAN_LIMIT        45.0f
#define SQUAT_MIN_REP_DURATION_MS    500

static const squat_fsm_config_t g_squat_default_config =
{
  SQUAT_ANGLE_STAND_THRESH, SQUAT_ANGLE_DESCEND_THRESH,
  SQUAT_ANGLE_DEEP_TARGET, SQUAT_ANGLE_SHALLOW_LIMIT,
  SQUAT_ANGLE_ASCEND_THRESH, SQUAT_VALGUS_RATIO_LIMIT,
  SQUAT_TRUNK_LEAN_LIMIT, SQUAT_MIN_REP_DURATION_MS
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: squat_fsm_init
 ****************************************************************************/

void squat_fsm_init(squat_fsm_t *fsm)
{
  fsm->state = SQUAT_STATE_STAND;
  fsm->total_reps = 0;
  fsm->valid_reps = 0;
  fsm->shallow_count = 0;
  fsm->knee_caving_count = 0;
  fsm->trunk_lean_count = 0;
  fsm->min_knee_angle_curr = 180.0f;
  fsm->min_knee_ankle_ratio_curr = 1.0f;
  fsm->max_trunk_lean_curr = 0.0f;
  fsm->rep_start_time_ms = 0;
  fsm->last_rep_complete_time_ms = 0;
  fsm->last_quality_flags = SQUAT_QUALITY_OK;
  fsm->last_sample_ms = 0;
  fsm->have_sample = false;
  fsm->armed = false;
}

void squat_fsm_interrupt(squat_fsm_t *fsm)
{
  if (!fsm) return;
  fsm->state = SQUAT_STATE_STAND;
  fsm->min_knee_angle_curr = 180.0f;
  fsm->min_knee_ankle_ratio_curr = 1.0f;
  fsm->max_trunk_lean_curr = 0.0f;
  fsm->have_sample = false;
  fsm->armed = false;
}

void squat_fsm_adaptive_init(squat_fsm_adaptive_t *tracker)
{
  if (!tracker) return;
  squat_fsm_init(&tracker->fsm);
  tracker->standing_angle = 0.0f;
  tracker->standing_valid = false;
}

void squat_fsm_adaptive_begin_capture(squat_fsm_adaptive_t *tracker)
{
  if (!tracker) return;
  squat_fsm_init(&tracker->fsm);
  tracker->fsm.armed = tracker->standing_valid;
}

/****************************************************************************
 * Name: squat_fsm_reset_counters
 ****************************************************************************/

void squat_fsm_reset_counters(squat_fsm_t *fsm)
{
  squat_fsm_init(fsm);
}

/****************************************************************************
 * Name: squat_fsm_update
 ****************************************************************************/

bool squat_fsm_update(squat_fsm_t *fsm,
                      const pose_frame_t *pose,
                      uint32_t timestamp_ms)
{
  return squat_fsm_update_config(fsm, pose, timestamp_ms, 0.3f,
                                 &g_squat_default_config);
}

bool squat_fsm_update_confidence(squat_fsm_t *fsm,
                                 const pose_frame_t *pose,
                                 uint32_t timestamp_ms,
                                 float minimum_score)
{
  return squat_fsm_update_config(fsm, pose, timestamp_ms, minimum_score,
                                 &g_squat_default_config);
}

bool squat_fsm_update_config(squat_fsm_t *fsm,
                             const pose_frame_t *pose,
                             uint32_t timestamp_ms,
                             float minimum_score,
                             const squat_fsm_config_t *config)
{
  const squat_fsm_config_t *settings = config != NULL ? config :
                                        &g_squat_default_config;
  if (!fsm) return false;
  if (!pose || !pose->valid)
    {
      squat_fsm_interrupt(fsm);
      return false;
    }

  const int required[] = {KPT_LEFT_SHOULDER, KPT_RIGHT_SHOULDER,
    KPT_LEFT_HIP, KPT_RIGHT_HIP, KPT_LEFT_KNEE, KPT_RIGHT_KNEE,
    KPT_LEFT_ANKLE, KPT_RIGHT_ANKLE};
  for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); i++)
    {
      const kpt_2d_t *p = &pose->kpts[required[i]];
      if (!isfinite(p->score) || p->score < minimum_score ||
          !isfinite(p->x) || !isfinite(p->y))
        { squat_fsm_interrupt(fsm); return false; }
    }

  /* Conservative continuity limit: do not count movements hidden in a
   * long inference gap. Duplicate/out-of-order timestamps are not evidence.
   * uint32 subtraction permits normal millisecond clock wraparound.
   */
  if (fsm->have_sample)
    {
      uint32_t dt = timestamp_ms - fsm->last_sample_ms;
      if (dt == 0 || dt > 0x7fffffffu) return false;
      if (dt > 1000) squat_fsm_interrupt(fsm);
    }
  fsm->last_sample_ms = timestamp_ms;
  fsm->have_sample = true;

  float knee_l = velafit_get_knee_angle_left(pose);
  float knee_r = velafit_get_knee_angle_right(pose);
  float avg_knee = (knee_l + knee_r) * 0.5f;

  float knee_ankle_ratio = velafit_get_knee_to_ankle_ratio(pose);
  float trunk_lean = velafit_get_trunk_lean_angle(pose);

  bool rep_finished = false;

  if (!fsm->armed)
    {
      if (avg_knee > settings->stand_angle) fsm->armed = true;
      return false;
    }

  /* Track the full repetition, including descent after a small reversal. */
  if (fsm->state != SQUAT_STATE_STAND)
    {
      if (avg_knee < fsm->min_knee_angle_curr)
        fsm->min_knee_angle_curr = avg_knee;
      if (knee_ankle_ratio < fsm->min_knee_ankle_ratio_curr)
        fsm->min_knee_ankle_ratio_curr = knee_ankle_ratio;
      if (trunk_lean > fsm->max_trunk_lean_curr)
        fsm->max_trunk_lean_curr = trunk_lean;
    }

  switch (fsm->state)
    {
      case SQUAT_STATE_STAND:
        if (avg_knee < settings->descend_angle)
          {
            fsm->state = SQUAT_STATE_DESCENDING;
            fsm->rep_start_time_ms = timestamp_ms;
            fsm->min_knee_angle_curr = avg_knee;
            fsm->min_knee_ankle_ratio_curr = knee_ankle_ratio;
            fsm->max_trunk_lean_curr = trunk_lean;
          }
        break;

      case SQUAT_STATE_DESCENDING:
        if (avg_knee < fsm->min_knee_angle_curr)
          {
            fsm->min_knee_angle_curr = avg_knee;
          }

        if (knee_ankle_ratio < fsm->min_knee_ankle_ratio_curr)
          {
            fsm->min_knee_ankle_ratio_curr = knee_ankle_ratio;
          }

        if (trunk_lean > fsm->max_trunk_lean_curr)
          {
            fsm->max_trunk_lean_curr = trunk_lean;
          }

        if (avg_knee <= settings->deep_angle)
          {
            fsm->state = SQUAT_STATE_BOTTOM;
          }
        else if (avg_knee > settings->ascend_angle &&
                 avg_knee > fsm->min_knee_angle_curr + 5.0f)
          {
            /* Turnaround early without reaching target depth */

            fsm->state = SQUAT_STATE_ASCENDING;
          }
        else
          {
            break;
          }

      case SQUAT_STATE_BOTTOM:
        if (avg_knee < fsm->min_knee_angle_curr)
          {
            fsm->min_knee_angle_curr = avg_knee;
          }

        if (knee_ankle_ratio < fsm->min_knee_ankle_ratio_curr)
          {
            fsm->min_knee_ankle_ratio_curr = knee_ankle_ratio;
          }

        if (trunk_lean > fsm->max_trunk_lean_curr)
          {
            fsm->max_trunk_lean_curr = trunk_lean;
          }

        if (avg_knee > settings->ascend_angle)
          {
            fsm->state = SQUAT_STATE_ASCENDING;
          }
        else
          {
            break;
          }

      case SQUAT_STATE_ASCENDING:
        if (avg_knee > settings->stand_angle)
          {
            uint32_t duration = timestamp_ms - fsm->rep_start_time_ms;
            if (duration >= settings->minimum_rep_ms)
              {
                fsm->total_reps++;
                fsm->last_quality_flags = SQUAT_QUALITY_OK;

                /* Quality audit */

                if (fsm->min_knee_angle_curr > settings->shallow_angle)
                  {
                    fsm->last_quality_flags |= SQUAT_QUALITY_SHALLOW;
                    fsm->shallow_count++;
                  }

                if (fsm->min_knee_ankle_ratio_curr <
                    settings->valgus_ratio)
                  {
                    fsm->last_quality_flags |= SQUAT_QUALITY_KNEE_CAVING;
                    fsm->knee_caving_count++;
                  }

                if (fsm->max_trunk_lean_curr > settings->trunk_lean_angle)
                  {
                    fsm->last_quality_flags |= SQUAT_QUALITY_TRUNK_LEAN;
                    fsm->trunk_lean_count++;
                  }

                if (fsm->last_quality_flags == SQUAT_QUALITY_OK)
                  {
                    fsm->valid_reps++;
                  }

                fsm->last_rep_complete_time_ms = timestamp_ms;
                rep_finished = true;
              }

            fsm->state = SQUAT_STATE_STAND;
            fsm->min_knee_angle_curr = 180.0f;
          }
        break;
    }

  return rep_finished;
}

bool squat_fsm_adaptive_update(squat_fsm_adaptive_t *tracker,
                               const pose_frame_t *pose,
                               uint32_t timestamp_ms,
                               float minimum_score)
{
  if (!tracker || !pose || !pose->valid)
    {
      if (tracker) squat_fsm_interrupt(&tracker->fsm);
      return false;
    }

  float knee = (velafit_get_knee_angle_left(pose) +
                velafit_get_knee_angle_right(pose)) * 0.5f;
  if (!isfinite(knee))
    {
      squat_fsm_interrupt(&tracker->fsm);
      return false;
    }

  /* Lightweight pose models compress the absolute knee-angle range and the
   * offset changes with framing. Learn the standing maximum, then express
   * transitions as motion relative to that session baseline. */
  if (!tracker->standing_valid)
    {
      tracker->standing_angle = knee;
      tracker->standing_valid = true;
    }
  else if (tracker->fsm.state == SQUAT_STATE_STAND)
    {
      if (knee > tracker->standing_angle)
        {
          tracker->standing_angle = knee;
        }
      else if (knee >= tracker->standing_angle - 3.0f)
        {
          tracker->standing_angle = tracker->standing_angle * 0.98f +
                                    knee * 0.02f;
        }
    }

  const float baseline = tracker->standing_angle;
  float return_angle = baseline - 3.0f;
  float ascend_angle = baseline - 10.0f;

  /* Once a repetition has started, also express recovery relative to that
   * repetition's observed minimum. Tiny models can shift every absolute
   * angle after a large movement even while all keypoints stay confident.
   * A 70 percent recovery is enough to close the repetition; the recovered
   * pose becomes the next baseline and continues tracking upward in STAND. */
  if (tracker->fsm.state != SQUAT_STATE_STAND &&
      tracker->fsm.min_knee_angle_curr < 180.0f)
    {
      float excursion = baseline - tracker->fsm.min_knee_angle_curr;
      if (excursion > 0.0f)
        {
          float relative_ascend = tracker->fsm.min_knee_angle_curr +
                                  fmaxf(4.0f, excursion * 0.40f);
          float relative_return = tracker->fsm.min_knee_angle_curr +
                                  fmaxf(6.0f, excursion * 0.70f);
          if (relative_ascend < ascend_angle)
            {
              ascend_angle = relative_ascend;
            }
          if (relative_return < return_angle)
            {
              return_angle = relative_return;
            }
        }
    }

  const squat_fsm_config_t relative =
    {
      return_angle,
      baseline - 7.0f,  /* deliberate motion, outside standing jitter */
      baseline - 18.0f, /* full-depth excursion measured on real sessions */
      baseline - 18.0f, /* every excursion short of target is shallow */
      ascend_angle,
      0.72f,
      45.0f,
      500
    };
  bool finished = squat_fsm_update_config(&tracker->fsm, pose, timestamp_ms,
                                           minimum_score, &relative);
  if (finished)
    {
      tracker->standing_angle = knee;
    }

  return finished;
}
