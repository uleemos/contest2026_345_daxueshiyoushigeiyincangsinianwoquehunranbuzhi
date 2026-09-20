/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/pipeline/velafit_pipeline.h
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

#ifndef __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_PIPELINE_VELAFIT_PIPELINE_H
#define __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_PIPELINE_VELAFIT_PIPELINE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "velafit_types.h"
#include "geometry.h"
#include "one_euro_filter.h"
#include "squat_fsm.h"
#include "jumping_jack_fsm.h"
#include "pushup_fsm.h"
#include "plank_fsm.h"
#include "calorie_calc.h"
#include "velafit_render.h"
#include "velafit_audio_cue.h"
#include "velafit_storage.h"
#include "velafit_sync.h"
#include "velafit_session.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum
{
  VELAFIT_EXERCISE_SQUAT = 0,
  VELAFIT_EXERCISE_JUMPING_JACK,
  VELAFIT_EXERCISE_PUSHUP,
  VELAFIT_EXERCISE_PLANK,
  VELAFIT_EXERCISE_MAX
} velafit_exercise_type_t;

typedef struct
{
  char exercise_name[16];
  velafit_exercise_type_t exercise_id;
  squat_fsm_t squat_fsm;
  jumping_jack_fsm_t jj_fsm;
  pushup_fsm_t pushup_fsm;
  plank_fsm_t plank_fsm;
  one_euro_pose_filter_t pose_filter;
  velafit_canvas_t canvas;
  uint8_t *canvas_buf;
  size_t canvas_buf_size;
  velafit_session_stats_t stats;
  uint32_t frame_count;
  uint32_t last_reps;
  uint32_t last_quality_flags;
  char last_feedback_msg[64];
  velafit_session_t session;
  bool controlled;
  bool network_online;
  uint32_t first_frame_ms;
  uint32_t last_frame_ms;
  float measured_fps;
  uint32_t pose_after_ms;
  bool summary_saved;
} velafit_pipeline_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

int velafit_pipeline_init(velafit_pipeline_t *pipe,
                          const char *exercise,
                          uint32_t canvas_w,
                          uint32_t canvas_h);

int velafit_pipeline_step_image(velafit_pipeline_t *pipe,
                                const uint8_t *model_rgb,
                                uint32_t timestamp_ms);

int velafit_pipeline_step_camera_frame(velafit_pipeline_t *pipe,
                                       const void *cam_frame,
                                       uint16_t cam_w,
                                       uint16_t cam_h,
                                       int color_fmt,
                                       int rotation,
                                       uint32_t timestamp_ms);

int velafit_pipeline_step_pose(velafit_pipeline_t *pipe,
                               const pose_frame_t *raw_pose);

int velafit_pipeline_event(velafit_pipeline_t *pipe,
                           enum velafit_voice_command event, uint64_t now_ms);
int velafit_pipeline_tick(velafit_pipeline_t *pipe, uint64_t now_ms);

int velafit_pipeline_finish(velafit_pipeline_t *pipe,
                            char *json_buf,
                            size_t json_max_len);

void velafit_pipeline_deinit(velafit_pipeline_t *pipe);

int velafit_pipeline_run_simulation(const char *exercise,
                                    int cycles,
                                    const char *ppm_out);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_PACKAGES_DEMOS_CONTEST2026_345_VELAFIT_AI_PIPELINE_VELAFIT_PIPELINE_H */
