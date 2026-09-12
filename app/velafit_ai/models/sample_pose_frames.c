/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/models/sample_pose_frames.c
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

#include "sample_pose_frames.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* 1. Standing upright */

const pose_frame_t g_sample_pose_standing =
{
  {
    {0.50f, 0.15f, 0.95f},
    {0.48f, 0.13f, 0.94f},
    {0.52f, 0.13f, 0.94f},
    {0.45f, 0.14f, 0.90f},
    {0.55f, 0.14f, 0.90f},
    {0.40f, 0.25f, 0.96f},
    {0.60f, 0.25f, 0.96f},
    {0.37f, 0.40f, 0.92f},
    {0.63f, 0.40f, 0.92f},
    {0.35f, 0.55f, 0.88f},
    {0.65f, 0.55f, 0.88f},
    {0.43f, 0.50f, 0.98f},
    {0.57f, 0.50f, 0.98f},
    {0.43f, 0.72f, 0.95f},
    {0.57f, 0.72f, 0.95f},
    {0.43f, 0.92f, 0.92f},
    {0.57f, 0.92f, 0.92f}
  },
  0,
  true
};

/* 2. Standard Deep Squat */

const pose_frame_t g_sample_pose_squat_deep =
{
  {
    {0.50f, 0.35f, 0.95f},
    {0.48f, 0.33f, 0.94f},
    {0.52f, 0.33f, 0.94f},
    {0.45f, 0.34f, 0.90f},
    {0.55f, 0.34f, 0.90f},
    {0.40f, 0.45f, 0.96f},
    {0.60f, 0.45f, 0.96f},
    {0.35f, 0.52f, 0.92f},
    {0.65f, 0.52f, 0.92f},
    {0.38f, 0.58f, 0.88f},
    {0.62f, 0.58f, 0.88f},
    {0.42f, 0.68f, 0.98f},
    {0.58f, 0.68f, 0.98f},
    {0.35f, 0.68f, 0.95f},
    {0.65f, 0.68f, 0.95f},
    {0.40f, 0.92f, 0.92f},
    {0.60f, 0.92f, 0.92f}
  },
  1000,
  true
};

/* 3. Shallow Squat */

const pose_frame_t g_sample_pose_squat_shallow =
{
  {
    {0.50f, 0.25f, 0.95f},
    {0.48f, 0.23f, 0.94f},
    {0.52f, 0.23f, 0.94f},
    {0.45f, 0.24f, 0.90f},
    {0.55f, 0.24f, 0.90f},
    {0.40f, 0.35f, 0.96f},
    {0.60f, 0.35f, 0.96f},
    {0.37f, 0.45f, 0.92f},
    {0.63f, 0.45f, 0.92f},
    {0.38f, 0.50f, 0.88f},
    {0.62f, 0.50f, 0.88f},
    {0.43f, 0.58f, 0.98f},
    {0.57f, 0.58f, 0.98f},
    {0.36f, 0.70f, 0.95f},
    {0.64f, 0.70f, 0.95f},
    {0.42f, 0.92f, 0.92f},
    {0.58f, 0.92f, 0.92f}
  },
  1000,
  true
};

/* 4. Knee Caving Squat */

const pose_frame_t g_sample_pose_squat_valgus =
{
  {
    {0.50f, 0.35f, 0.95f},
    {0.48f, 0.33f, 0.94f},
    {0.52f, 0.33f, 0.94f},
    {0.45f, 0.34f, 0.90f},
    {0.55f, 0.34f, 0.90f},
    {0.40f, 0.45f, 0.96f},
    {0.60f, 0.45f, 0.96f},
    {0.35f, 0.52f, 0.92f},
    {0.65f, 0.52f, 0.92f},
    {0.38f, 0.58f, 0.88f},
    {0.62f, 0.58f, 0.88f},
    {0.42f, 0.68f, 0.98f},
    {0.58f, 0.68f, 0.98f},
    {0.46f, 0.72f, 0.95f},
    {0.54f, 0.72f, 0.95f},
    {0.38f, 0.92f, 0.92f},
    {0.62f, 0.92f, 0.92f}
  },
  1000,
  true
};

/* 5. Push-up Plank (Top extended support) */

const pose_frame_t g_sample_pose_pushup_plank =
{
  {
    {0.20f, 0.38f, 0.95f},
    {0.18f, 0.36f, 0.94f},
    {0.22f, 0.36f, 0.94f},
    {0.16f, 0.37f, 0.90f},
    {0.24f, 0.37f, 0.90f},
    {0.30f, 0.40f, 0.96f},
    {0.30f, 0.40f, 0.96f},
    {0.30f, 0.55f, 0.92f},
    {0.30f, 0.55f, 0.92f},
    {0.30f, 0.70f, 0.88f},
    {0.30f, 0.70f, 0.88f},
    {0.55f, 0.42f, 0.98f},
    {0.55f, 0.42f, 0.98f},
    {0.72f, 0.44f, 0.95f},
    {0.72f, 0.44f, 0.95f},
    {0.88f, 0.46f, 0.92f},
    {0.88f, 0.46f, 0.92f}
  },
  0,
  true
};

/* 6. Push-up Bottom Deep (Standard chest to floor, elbow ~85 deg) */

const pose_frame_t g_sample_pose_pushup_bottom_deep =
{
  {
    {0.20f, 0.63f, 0.95f},
    {0.18f, 0.61f, 0.94f},
    {0.22f, 0.61f, 0.94f},
    {0.16f, 0.62f, 0.90f},
    {0.24f, 0.62f, 0.90f},
    {0.30f, 0.65f, 0.96f},
    {0.30f, 0.65f, 0.96f},
    {0.24f, 0.55f, 0.92f},
    {0.24f, 0.55f, 0.92f},
    {0.30f, 0.70f, 0.88f},
    {0.30f, 0.70f, 0.88f},
    {0.55f, 0.66f, 0.98f},
    {0.55f, 0.66f, 0.98f},
    {0.72f, 0.68f, 0.95f},
    {0.72f, 0.68f, 0.95f},
    {0.88f, 0.70f, 0.92f},
    {0.88f, 0.70f, 0.92f}
  },
  600,
  true
};

/* 7. Push-up Shallow (Incomplete depth, elbow ~122 deg) */

const pose_frame_t g_sample_pose_pushup_shallow =
{
  {
    {0.20f, 0.48f, 0.95f},
    {0.18f, 0.46f, 0.94f},
    {0.22f, 0.46f, 0.94f},
    {0.16f, 0.47f, 0.90f},
    {0.24f, 0.47f, 0.90f},
    {0.30f, 0.50f, 0.96f},
    {0.30f, 0.50f, 0.96f},
    {0.27f, 0.58f, 0.92f},
    {0.27f, 0.58f, 0.92f},
    {0.34f, 0.67f, 0.88f},
    {0.34f, 0.67f, 0.88f},
    {0.55f, 0.52f, 0.98f},
    {0.55f, 0.52f, 0.98f},
    {0.72f, 0.54f, 0.95f},
    {0.72f, 0.54f, 0.95f},
    {0.88f, 0.56f, 0.92f},
    {0.88f, 0.56f, 0.92f}
  },
  600,
  true
};

/* 8. Push-up Sagging Hips */

const pose_frame_t g_sample_pose_pushup_sag =
{
  {
    {0.20f, 0.38f, 0.95f},
    {0.18f, 0.36f, 0.94f},
    {0.22f, 0.36f, 0.94f},
    {0.16f, 0.37f, 0.90f},
    {0.24f, 0.37f, 0.90f},
    {0.30f, 0.40f, 0.96f},
    {0.30f, 0.40f, 0.96f},
    {0.30f, 0.55f, 0.92f},
    {0.30f, 0.55f, 0.92f},
    {0.30f, 0.70f, 0.88f},
    {0.30f, 0.70f, 0.88f},
    {0.55f, 0.60f, 0.98f},
    {0.55f, 0.60f, 0.98f},
    {0.72f, 0.52f, 0.95f},
    {0.72f, 0.52f, 0.95f},
    {0.88f, 0.46f, 0.92f},
    {0.88f, 0.46f, 0.92f}
  },
  600,
  true
};

/* 9. Plank Perfect (Standard horizontal plank) */

const pose_frame_t g_sample_pose_plank_perfect =
{
  {
    {0.20f, 0.48f, 0.95f},
    {0.18f, 0.46f, 0.94f},
    {0.22f, 0.46f, 0.94f},
    {0.16f, 0.47f, 0.90f},
    {0.24f, 0.47f, 0.90f},
    {0.30f, 0.50f, 0.96f},
    {0.30f, 0.50f, 0.96f},
    {0.30f, 0.65f, 0.92f},
    {0.30f, 0.65f, 0.92f},
    {0.38f, 0.65f, 0.88f},
    {0.38f, 0.65f, 0.88f},
    {0.55f, 0.52f, 0.98f},
    {0.55f, 0.52f, 0.98f},
    {0.72f, 0.54f, 0.95f},
    {0.72f, 0.54f, 0.95f},
    {0.88f, 0.56f, 0.92f},
    {0.88f, 0.56f, 0.92f}
  },
  0,
  true
};

/* 10. Plank Sagging */

const pose_frame_t g_sample_pose_plank_sag =
{
  {
    {0.20f, 0.48f, 0.95f},
    {0.18f, 0.46f, 0.94f},
    {0.22f, 0.46f, 0.94f},
    {0.16f, 0.47f, 0.90f},
    {0.24f, 0.47f, 0.90f},
    {0.30f, 0.50f, 0.96f},
    {0.30f, 0.50f, 0.96f},
    {0.30f, 0.65f, 0.92f},
    {0.30f, 0.65f, 0.92f},
    {0.38f, 0.65f, 0.88f},
    {0.38f, 0.65f, 0.88f},
    {0.55f, 0.68f, 0.98f},
    {0.55f, 0.68f, 0.98f},
    {0.72f, 0.60f, 0.95f},
    {0.72f, 0.60f, 0.95f},
    {0.88f, 0.56f, 0.92f},
    {0.88f, 0.56f, 0.92f}
  },
  1000,
  true
};

/* 11. Plank Piking */

const pose_frame_t g_sample_pose_plank_pike =
{
  {
    {0.20f, 0.48f, 0.95f},
    {0.18f, 0.46f, 0.94f},
    {0.22f, 0.46f, 0.94f},
    {0.16f, 0.47f, 0.90f},
    {0.24f, 0.47f, 0.90f},
    {0.30f, 0.50f, 0.96f},
    {0.30f, 0.50f, 0.96f},
    {0.30f, 0.65f, 0.92f},
    {0.30f, 0.65f, 0.92f},
    {0.38f, 0.65f, 0.88f},
    {0.38f, 0.65f, 0.88f},
    {0.55f, 0.35f, 0.98f},
    {0.55f, 0.35f, 0.98f},
    {0.72f, 0.46f, 0.95f},
    {0.72f, 0.46f, 0.95f},
    {0.88f, 0.56f, 0.92f},
    {0.88f, 0.56f, 0.92f}
  },
  1000,
  true
};

/* 12. Jumping Jack Closed */

const pose_frame_t g_sample_pose_jj_closed =
{
  {
    {0.50f, 0.15f, 0.95f},
    {0.48f, 0.13f, 0.94f},
    {0.52f, 0.13f, 0.94f},
    {0.45f, 0.14f, 0.90f},
    {0.55f, 0.14f, 0.90f},
    {0.42f, 0.25f, 0.96f},
    {0.58f, 0.25f, 0.96f},
    {0.40f, 0.40f, 0.92f},
    {0.60f, 0.40f, 0.92f},
    {0.41f, 0.55f, 0.88f},
    {0.59f, 0.55f, 0.88f},
    {0.45f, 0.50f, 0.98f},
    {0.55f, 0.50f, 0.98f},
    {0.46f, 0.72f, 0.95f},
    {0.54f, 0.72f, 0.95f},
    {0.47f, 0.92f, 0.92f},
    {0.53f, 0.92f, 0.92f}
  },
  0,
  true
};

/* 13. Jumping Jack Open */

const pose_frame_t g_sample_pose_jj_open =
{
  {
    {0.50f, 0.15f, 0.95f},
    {0.48f, 0.13f, 0.94f},
    {0.52f, 0.13f, 0.94f},
    {0.45f, 0.14f, 0.90f},
    {0.55f, 0.14f, 0.90f},
    {0.42f, 0.25f, 0.96f},
    {0.58f, 0.25f, 0.96f},
    {0.28f, 0.16f, 0.92f},
    {0.72f, 0.16f, 0.92f},
    {0.20f, 0.08f, 0.88f},
    {0.80f, 0.08f, 0.88f},
    {0.45f, 0.50f, 0.98f},
    {0.55f, 0.50f, 0.98f},
    {0.35f, 0.72f, 0.95f},
    {0.65f, 0.72f, 0.95f},
    {0.25f, 0.92f, 0.92f},
    {0.75f, 0.92f, 0.92f}
  },
  500,
  true
};

const uint8_t g_sample_test_image_160x160[160 * 160 * 3] =
{
  0
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
