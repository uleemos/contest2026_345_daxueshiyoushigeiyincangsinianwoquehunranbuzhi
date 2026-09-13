/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/include/velafit_types.h
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

#ifndef __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_INCLUDE_VELAFIT_TYPES_H
#define __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_INCLUDE_VELAFIT_TYPES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VELAFIT_NUM_KEYPOINTS        17
#define VELAFIT_MODEL_INPUT_WIDTH    192
#define VELAFIT_MODEL_INPUT_HEIGHT   192
#define VELAFIT_MODEL_INPUT_CHANNELS 3

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* COCO 17 Human Keypoint Topology */

typedef enum
{
  KPT_NOSE = 0,
  KPT_LEFT_EYE,
  KPT_RIGHT_EYE,
  KPT_LEFT_EAR,
  KPT_RIGHT_EAR,
  KPT_LEFT_SHOULDER,
  KPT_RIGHT_SHOULDER,
  KPT_LEFT_ELBOW,
  KPT_RIGHT_ELBOW,
  KPT_LEFT_WRIST,
  KPT_RIGHT_WRIST,
  KPT_LEFT_HIP,
  KPT_RIGHT_HIP,
  KPT_LEFT_KNEE,
  KPT_RIGHT_KNEE,
  KPT_LEFT_ANKLE,
  KPT_RIGHT_ANKLE,
  KPT_MAX
} kpt_id_t;

/* 2D Keypoint with confidence score */

typedef struct
{
  float x;       /* Normalized [0.0, 1.0] */
  float y;       /* Normalized [0.0, 1.0] */
  float score;   /* Confidence [0.0, 1.0] */
} kpt_2d_t;

/* Pose Frame */

typedef struct
{
  kpt_2d_t kpts[VELAFIT_NUM_KEYPOINTS];
  uint32_t timestamp_ms;
  bool valid;
} pose_frame_t;

/* Squat State Machine States */

typedef enum
{
  SQUAT_STATE_STAND = 0,
  SQUAT_STATE_DESCENDING,
  SQUAT_STATE_BOTTOM,
  SQUAT_STATE_ASCENDING
} squat_state_t;

/* Squat Quality Audit Flags */

typedef enum
{
  SQUAT_QUALITY_OK = 0,
  SQUAT_QUALITY_SHALLOW      = (1 << 0), /* Partial squat */
  SQUAT_QUALITY_KNEE_CAVING  = (1 << 1), /* Valgus / knee collapse */
  SQUAT_QUALITY_TRUNK_LEAN   = (1 << 2)  /* Forward lean */
} squat_quality_flags_t;

/* Push-up States */

typedef enum
{
  PUSHUP_STATE_PLANK = 0,
  PUSHUP_STATE_DESCENDING,
  PUSHUP_STATE_BOTTOM,
  PUSHUP_STATE_ASCENDING
} pushup_state_t;

/* Push-up Quality Audit Flags */

typedef enum
{
  PUSHUP_QUALITY_OK = 0,
  PUSHUP_QUALITY_SHALLOW      = (1 << 0), /* Incomplete elbow flexion */
  PUSHUP_QUALITY_HIPS_SAG     = (1 << 1), /* Pelvic sag / back arch */
  PUSHUP_QUALITY_HIPS_PIKE    = (1 << 2), /* Butt sticking up */
  PUSHUP_QUALITY_ELBOW_FLARE  = (1 << 3)  /* Elbows flared > 75 deg */
} pushup_quality_flags_t;

/* Plank Timer States */

typedef enum
{
  PLANK_STATE_IDLE = 0,
  PLANK_STATE_HOLDING,
  PLANK_STATE_PAUSED
} plank_state_t;

/* Plank Quality Audit Flags */

typedef enum
{
  PLANK_QUALITY_OK = 0,
  PLANK_QUALITY_HIPS_SAG     = (1 << 0), /* Pelvic sag */
  PLANK_QUALITY_HIPS_PIKE    = (1 << 1), /* Butt too high */
  PLANK_QUALITY_HEAD_DROP    = (1 << 2)  /* Head sagging */
} plank_quality_flags_t;

/* Jumping Jack States */

typedef enum
{
  JJ_STATE_CLOSED = 0,
  JJ_STATE_OPENING,
  JJ_STATE_OPENED,
  JJ_STATE_CLOSING
} jumping_jack_state_t;

/* Sync Status for Edge-Cloud Upload */

typedef enum
{
  VELAFIT_SYNC_PENDING = 0,
  VELAFIT_SYNC_SYNCING,
  VELAFIT_SYNC_SYNCED,
  VELAFIT_SYNC_FAILED
} velafit_sync_status_t;

/* Performance Profile */

typedef struct
{
  uint32_t preprocess_us;
  uint32_t infer_us;
  uint32_t postprocess_us;
  uint32_t fsm_us;
  uint32_t total_us;
  float fps;
} velafit_perf_t;

/* Session Structured Metrics for Edge-Cloud Sync */

typedef struct
{
  char session_id[32];
  char exercise_type[16];
  uint32_t total_reps;
  uint32_t valid_reps;
  uint32_t duration_sec;
  uint32_t hold_duration_sec;
  float calories_kcal;
  float accuracy_pct;
  float avg_knee_angle_min;
  float min_elbow_angle;
  uint32_t knee_caving_count;
  uint32_t shallow_count;
  uint32_t trunk_lean_count;
  uint32_t hips_sag_count;
  uint32_t hips_pike_count;
  uint32_t elbow_flare_count;
  velafit_sync_status_t sync_status;
} velafit_session_stats_t;

/* Compact Session Record for Index Table */

typedef struct
{
  char session_id[32];
  char exercise_type[16];
  uint32_t timestamp;
  uint32_t total_reps;
  uint32_t valid_reps;
  uint32_t duration_sec;
  float calories_kcal;
  float accuracy_pct;
  velafit_sync_status_t sync_status;
} velafit_session_record_t;

#endif /* __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_INCLUDE_VELAFIT_TYPES_H */
