/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/models/velafit_pose_model.c
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
#include <string.h>
#include <time.h>
#include <errno.h>

#include "velafit_pose_model.h"
#include "sample_pose_frames.h"

#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
#  include "velafit_pose_tflm.h"
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_model_initialized = false;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: velafit_pose_model_init
 ****************************************************************************/

int velafit_pose_model_init(void)
{
  if (g_model_initialized)
    {
      return OK;
    }

#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
  int ret = velafit_pose_tflm_init();
  if (ret == OK)
    {
      printf("[VELAFIT-POSE] Backend: TFLM MoveNet (real inference)\n");
      g_model_initialized = true;
    }

  return ret;
#elif defined(CONFIG_VELAFIT_POSE_BACKEND_SIMULATION)
  printf("[VELAFIT-POSE] Backend: SIMULATION (no image inference)\n");
  g_model_initialized = true;
  return OK;
#else
  printf("[VELAFIT-POSE] Backend unavailable: real inference not integrated\n");
  return -ENOSYS;
#endif
}

/****************************************************************************
 * Name: velafit_pose_model_deinit
 ****************************************************************************/

void velafit_pose_model_deinit(void)
{
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
  velafit_pose_tflm_deinit();
#endif
  g_model_initialized = false;
}

/****************************************************************************
 * Name: velafit_pose_model_backend_name
 ****************************************************************************/

const char *velafit_pose_model_backend_name(void)
{
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
  return "tflm-movenet-lightning-int8-v4";
#elif defined(CONFIG_VELAFIT_POSE_BACKEND_SIMULATION)
  return "simulation";
#else
  return "none";
#endif
}

/****************************************************************************
 * Name: velafit_pose_model_backend_is_real
 ****************************************************************************/

bool velafit_pose_model_backend_is_real(void)
{
#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
  return true;
#else
  return false;
#endif
}

/****************************************************************************
 * Name: velafit_pose_infer
 ****************************************************************************/

int velafit_pose_infer(const uint8_t *rgb_image,
                       pose_frame_t *out_pose,
                       velafit_perf_t *perf)
{
  if (rgb_image == NULL || out_pose == NULL)
    {
      return -EINVAL;
    }

  if (!g_model_initialized)
    {
      int ret = velafit_pose_model_init();
      if (ret != OK)
        {
          return ret;
        }
    }

#ifdef CONFIG_VELAFIT_POSE_BACKEND_TFLM
  return velafit_pose_tflm_infer(rgb_image, out_pose, perf);
#elif defined(CONFIG_VELAFIT_POSE_BACKEND_SIMULATION)
  struct timespec t0;
  struct timespec t3;

  clock_gettime(CLOCK_MONOTONIC, &t0);
  memcpy(out_pose, &g_sample_pose_squat_deep, sizeof(pose_frame_t));
  out_pose->valid = true;
  clock_gettime(CLOCK_MONOTONIC, &t3);

  if (perf)
    {
      memset(perf, 0, sizeof(*perf));
      perf->total_us      = (t3.tv_sec - t0.tv_sec) * 1000000 +
                            (t3.tv_nsec - t0.tv_nsec) / 1000;
    }

  return OK;
#else
  return -ENOSYS;
#endif
}
