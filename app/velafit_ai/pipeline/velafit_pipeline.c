/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/pipeline/velafit_pipeline.c
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

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "esp32p4_ppa.h"
#include "velafit_pipeline.h"
#include "velafit_pose_model.h"
#include "sample_pose_frames.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: velafit_pipeline_init
 ****************************************************************************/

int velafit_pipeline_init(velafit_pipeline_t *pipe,
                          const char *exercise,
                          uint32_t canvas_w,
                          uint32_t canvas_h)
{
  if (pipe == NULL)
    {
      return -EINVAL;
    }

  memset(pipe, 0, sizeof(velafit_pipeline_t));

  if (exercise != NULL &&
      (strcmp(exercise, "jj") == 0 || strcmp(exercise, "jumping_jack") == 0))
    {
      strncpy(pipe->exercise_name, "JUMPING_JACK",
              sizeof(pipe->exercise_name) - 1);
      pipe->exercise_id = VELAFIT_EXERCISE_JUMPING_JACK;
      jumping_jack_fsm_init(&pipe->jj_fsm);
    }
  else if (exercise != NULL && strcmp(exercise, "pushup") == 0)
    {
      strncpy(pipe->exercise_name, "PUSHUP",
              sizeof(pipe->exercise_name) - 1);
      pipe->exercise_id = VELAFIT_EXERCISE_PUSHUP;
      pushup_fsm_init(&pipe->pushup_fsm);
    }
  else if (exercise != NULL && strcmp(exercise, "plank") == 0)
    {
      strncpy(pipe->exercise_name, "PLANK",
              sizeof(pipe->exercise_name) - 1);
      pipe->exercise_id = VELAFIT_EXERCISE_PLANK;
      plank_fsm_init(&pipe->plank_fsm);
    }
  else
    {
      strncpy(pipe->exercise_name, "SQUAT",
              sizeof(pipe->exercise_name) - 1);
      pipe->exercise_id = VELAFIT_EXERCISE_SQUAT;
      squat_fsm_init(&pipe->squat_fsm);
    }

  /* Initialize One-Euro Pose Filter */

  one_euro_pose_filter_init(&pipe->pose_filter, 1.0f, 0.007f, 1.0f);

  /* Allocate canvas buffer (RGB565 default) */

  if (canvas_w == 0)
    {
      canvas_w = 320;
    }

  if (canvas_h == 0)
    {
      canvas_h = 240;
    }

  pipe->canvas_buf_size = canvas_w * canvas_h * 2;
  pipe->canvas_buf = (uint8_t *)malloc(pipe->canvas_buf_size);
  if (pipe->canvas_buf == NULL)
    {
      return -ENOMEM;
    }

  velafit_canvas_init(&pipe->canvas, pipe->canvas_buf,
                      canvas_w, canvas_h, VELAFIT_PIXFMT_RGB565);

  /* Initialize session stats */

  snprintf(pipe->stats.session_id, sizeof(pipe->stats.session_id),
           "VF-SESS-20260830-%04u", (unsigned int)(rand() % 10000));
  strncpy(pipe->stats.exercise_type, pipe->exercise_name,
          sizeof(pipe->stats.exercise_type) - 1);

  pipe->last_reps = 0;
  pipe->last_quality_flags = SQUAT_QUALITY_OK;
  strncpy(pipe->last_feedback_msg, "READY, START!",
          sizeof(pipe->last_feedback_msg) - 1);

  /* Play session start audio cue */

  velafit_audio_cue_play(VELAFIT_AUDIO_CUE_START);

  return OK;
}

/****************************************************************************
 * Name: velafit_pipeline_step_image
 ****************************************************************************/

int velafit_pipeline_step_image(velafit_pipeline_t *pipe,
                                const uint8_t *model_rgb,
                                uint32_t timestamp_ms)
{
  if (pipe == NULL || model_rgb == NULL)
    {
      return -EINVAL;
    }

  pose_frame_t raw_pose;
  velafit_perf_t perf;

  int ret = velafit_pose_infer(model_rgb, &raw_pose, &perf);
  if (ret != OK)
    {
      return ret;
    }

  raw_pose.timestamp_ms = timestamp_ms;
  return velafit_pipeline_step_pose(pipe, &raw_pose);
}

/****************************************************************************
 * Name: velafit_pipeline_step_camera_frame
 ****************************************************************************/

int velafit_pipeline_step_camera_frame(velafit_pipeline_t *pipe,
                                       const void *cam_frame,
                                       uint16_t cam_w,
                                       uint16_t cam_h,
                                       int color_fmt,
                                       int rotation,
                                       uint32_t timestamp_ms)
{
  if (pipe == NULL || cam_frame == NULL || cam_w == 0 || cam_h == 0)
    {
      return -EINVAL;
    }

#ifdef CONFIG_ESP32P4_PPA
  /* Keep large image workspaces off the 8 KiB application stack.  Allocate
   * two full RGB888-sized buffers so scale/rotate/CSC can always use
   * distinct input and output regions.
   */

  size_t workspace_size = VELAFIT_MODEL_INPUT_WIDTH *
                          VELAFIT_MODEL_INPUT_HEIGHT *
                          VELAFIT_MODEL_INPUT_CHANNELS;
  uint8_t *scaled_buf = (uint8_t *)malloc(workspace_size);
  uint8_t *aux_buf = (uint8_t *)malloc(workspace_size);
  if (scaled_buf == NULL || aux_buf == NULL)
    {
      free(scaled_buf);
      free(aux_buf);
      return -ENOMEM;
    }

  const void *cur_in = cam_frame;
  int cur_fmt = color_fmt;
  int ret;

  /* 1. Downscale camera frame to the selected model dimensions. */

  ret = esp32p4_ppa_scale(cur_in, cam_w, cam_h,
                          scaled_buf,
                          VELAFIT_MODEL_INPUT_WIDTH,
                          VELAFIT_MODEL_INPUT_HEIGHT,
                          cur_fmt);
  if (ret != OK)
    {
      goto out;
    }

  cur_in = scaled_buf;

  /* 2. Rotate the square model input if needed. */

  if (rotation != 0)
    {
      ret = esp32p4_ppa_rotate(cur_in,
                               VELAFIT_MODEL_INPUT_WIDTH,
                               VELAFIT_MODEL_INPUT_HEIGHT,
                               aux_buf, rotation, cur_fmt);
      if (ret != OK)
        {
          goto out;
        }

      cur_in = aux_buf;
    }

  /* 3. Convert to RGB888 for model inference if input is not RGB888 */

  if (cur_fmt != ESP32P4_PPA_COLOR_RGB888)
    {
      uint8_t *rgb_buf = cur_in == scaled_buf ? aux_buf : scaled_buf;
      ret = esp32p4_ppa_csc(cur_in, rgb_buf,
                            VELAFIT_MODEL_INPUT_WIDTH,
                            VELAFIT_MODEL_INPUT_HEIGHT,
                            cur_fmt, ESP32P4_PPA_COLOR_RGB888);
      if (ret != OK)
        {
          goto out;
        }

      cur_in = rgb_buf;
    }

  /* 4. Feed the RGB888 image into AI model & pipeline step. */

  ret = velafit_pipeline_step_image(pipe, (const uint8_t *)cur_in,
                                    timestamp_ms);

out:
  free(aux_buf);
  free(scaled_buf);
  return ret;
#else
  return -ENOSYS;
#endif
}

/****************************************************************************
 * Name: velafit_pipeline_step_pose
 ****************************************************************************/

int velafit_pipeline_step_pose(velafit_pipeline_t *pipe,
                               const pose_frame_t *raw_pose)
{
  if (pipe == NULL || raw_pose == NULL || !raw_pose->valid)
    {
      return -EINVAL;
    }

  pose_frame_t filtered_pose;
  uint32_t t_ms = raw_pose->timestamp_ms;
  pipe->frame_count++;

  /* 1. Apply One-Euro Pose Filter */

  one_euro_pose_filter_apply(&pipe->pose_filter,
                             raw_pose,
                             &filtered_pose,
                             (float)t_ms / 1000.0f);

  /* 2. Update Action State Machine based on exercise type */

  uint32_t cur_reps = 0;
  uint32_t flags = SQUAT_QUALITY_OK;

  switch (pipe->exercise_id)
    {
      case VELAFIT_EXERCISE_SQUAT:
        squat_fsm_update(&pipe->squat_fsm, &filtered_pose, t_ms);
        cur_reps = pipe->squat_fsm.total_reps;
        flags = pipe->squat_fsm.last_quality_flags;

        if (cur_reps > pipe->last_reps)
          {
            pipe->stats.total_reps = cur_reps;

            if (flags == SQUAT_QUALITY_OK)
              {
                pipe->stats.valid_reps++;
                strncpy(pipe->last_feedback_msg, "GOOD REP!",
                        sizeof(pipe->last_feedback_msg) - 1);
                velafit_audio_cue_play(VELAFIT_AUDIO_CUE_REP_COUNT);
              }
            else
              {
                if (flags & SQUAT_QUALITY_SHALLOW)
                  {
                    pipe->stats.shallow_count++;
                    strncpy(pipe->last_feedback_msg, "WARN: SHALLOW SQUAT!",
                            sizeof(pipe->last_feedback_msg) - 1);
                    velafit_audio_cue_play(VELAFIT_AUDIO_CUE_WARN_SHALLOW);
                  }

                if (flags & SQUAT_QUALITY_KNEE_CAVING)
                  {
                    pipe->stats.knee_caving_count++;
                    strncpy(pipe->last_feedback_msg, "WARN: KNEE VALGUS!",
                            sizeof(pipe->last_feedback_msg) - 1);
                    velafit_audio_cue_play(VELAFIT_AUDIO_CUE_WARN_VALGUS);
                  }

                if (flags & SQUAT_QUALITY_TRUNK_LEAN)
                  {
                    pipe->stats.trunk_lean_count++;
                    strncpy(pipe->last_feedback_msg, "WARN: FORWARD LEAN!",
                            sizeof(pipe->last_feedback_msg) - 1);
                    velafit_audio_cue_play(VELAFIT_AUDIO_CUE_WARN_LEAN);
                  }
              }

            pipe->last_reps = cur_reps;
            pipe->last_quality_flags = flags;
          }
        break;

      case VELAFIT_EXERCISE_PUSHUP:
        pushup_fsm_update(&pipe->pushup_fsm, &filtered_pose, t_ms);
        cur_reps = pipe->pushup_fsm.total_reps;
        flags = pipe->pushup_fsm.last_quality_flags;

        if (cur_reps > pipe->last_reps)
          {
            pipe->stats.total_reps = cur_reps;

            if (flags == PUSHUP_QUALITY_OK)
              {
                pipe->stats.valid_reps++;
                strncpy(pipe->last_feedback_msg, "GOOD PUSHUP!",
                        sizeof(pipe->last_feedback_msg) - 1);
                velafit_audio_cue_play(VELAFIT_AUDIO_CUE_REP_COUNT);
              }
            else
              {
                if (flags & PUSHUP_QUALITY_SHALLOW)
                  {
                    pipe->stats.shallow_count++;
                    strncpy(pipe->last_feedback_msg, "WARN: CHEST LOWER!",
                            sizeof(pipe->last_feedback_msg) - 1);
                    velafit_audio_cue_play(VELAFIT_AUDIO_CUE_WARN_SHALLOW);
                  }

                if (flags & PUSHUP_QUALITY_HIPS_SAG)
                  {
                    pipe->stats.hips_sag_count++;
                    strncpy(pipe->last_feedback_msg, "WARN: HIPS SAGGING!",
                            sizeof(pipe->last_feedback_msg) - 1);
                    velafit_audio_cue_play(VELAFIT_AUDIO_CUE_WARN_SAG);
                  }

                if (flags & PUSHUP_QUALITY_HIPS_PIKE)
                  {
                    pipe->stats.hips_pike_count++;
                    strncpy(pipe->last_feedback_msg, "WARN: LOWER HIPS!",
                            sizeof(pipe->last_feedback_msg) - 1);
                    velafit_audio_cue_play(VELAFIT_AUDIO_CUE_WARN_PIKE);
                  }
              }

            pipe->last_reps = cur_reps;
            pipe->last_quality_flags = flags;
          }
        break;

      case VELAFIT_EXERCISE_PLANK:
        plank_fsm_update(&pipe->plank_fsm, &filtered_pose, t_ms);
        cur_reps = pipe->plank_fsm.total_hold_duration_ms / 1000;
        flags = pipe->plank_fsm.last_quality_flags;

        if (flags & PLANK_QUALITY_HIPS_SAG)
          {
            strncpy(pipe->last_feedback_msg, "WARN: HIPS SAGGING!",
                    sizeof(pipe->last_feedback_msg) - 1);
          }
        else if (flags & PLANK_QUALITY_HIPS_PIKE)
          {
            strncpy(pipe->last_feedback_msg, "WARN: LOWER HIPS!",
                    sizeof(pipe->last_feedback_msg) - 1);
          }
        else
          {
            strncpy(pipe->last_feedback_msg, "HOLDING PLANK!",
                    sizeof(pipe->last_feedback_msg) - 1);
          }

        pipe->stats.hold_duration_sec = cur_reps;
        pipe->last_quality_flags = flags;
        break;

      case VELAFIT_EXERCISE_JUMPING_JACK:
      default:
        jumping_jack_fsm_update(&pipe->jj_fsm, &filtered_pose, t_ms);
        cur_reps = pipe->jj_fsm.total_reps;

        if (cur_reps > pipe->last_reps)
          {
            pipe->stats.total_reps = cur_reps;
            pipe->stats.valid_reps = cur_reps;
            strncpy(pipe->last_feedback_msg, "JJ COUNT +1!",
                    sizeof(pipe->last_feedback_msg) - 1);
            velafit_audio_cue_play(VELAFIT_AUDIO_CUE_REP_COUNT);
            pipe->last_reps = cur_reps;
          }
        break;
    }

  /* 3. Extract Joint Angles for Depth Gauge */

  float cur_angle = 0.0f;
  float tgt_angle = 0.0f;

  if (pipe->exercise_id == VELAFIT_EXERCISE_SQUAT)
    {
      cur_angle = (velafit_get_knee_angle_left(&filtered_pose) +
                   velafit_get_knee_angle_right(&filtered_pose)) * 0.5f;
      tgt_angle = 90.0f;
    }
  else if (pipe->exercise_id == VELAFIT_EXERCISE_PUSHUP)
    {
      cur_angle = (velafit_get_elbow_angle_left(&filtered_pose) +
                   velafit_get_elbow_angle_right(&filtered_pose)) * 0.5f;
      tgt_angle = 90.0f;
    }
  else if (pipe->exercise_id == VELAFIT_EXERCISE_PLANK)
    {
      cur_angle = velafit_get_body_line_angle(&filtered_pose);
      tgt_angle = 180.0f;
    }

  float cur_cal = velafit_calc_calories(pipe->exercise_name,
                                        pipe->frame_count / 30,
                                        100.0f,
                                        VELAFIT_DEFAULT_USER_WEIGHT_KG);

  /* 4. Render Rich Dashboard Overlay to Canvas */

  velafit_render_dashboard(&pipe->canvas,
                           &filtered_pose,
                           pipe->exercise_name,
                           cur_reps,
                           cur_cal,
                           30.0f,
                           cur_angle,
                           tgt_angle,
                           flags,
                           pipe->last_feedback_msg);

  /* 5. Blast to Framebuffer if available */

  velafit_render_to_fb0(&pipe->canvas);

  return OK;
}

/****************************************************************************
 * Name: velafit_pipeline_finish
 ****************************************************************************/

int velafit_pipeline_finish(velafit_pipeline_t *pipe,
                            char *json_buf,
                            size_t json_max_len)
{
  if (pipe == NULL)
    {
      return -EINVAL;
    }

  /* Play workout finish fanfare */

  velafit_audio_cue_play(VELAFIT_AUDIO_CUE_FINISH);

  pipe->stats.duration_sec = pipe->frame_count / 30;
  if (pipe->stats.duration_sec == 0)
    {
      pipe->stats.duration_sec = 1;
    }

  if (pipe->exercise_id == VELAFIT_EXERCISE_SQUAT)
    {
      pipe->stats.avg_knee_angle_min =
        pipe->squat_fsm.min_knee_angle_curr;
    }
  else if (pipe->exercise_id == VELAFIT_EXERCISE_PUSHUP)
    {
      pipe->stats.min_elbow_angle =
        pipe->pushup_fsm.min_elbow_angle_curr;
    }

  float acc = 0.0f;
  if (pipe->stats.total_reps > 0)
    {
      acc = (float)pipe->stats.valid_reps * 100.0f /
            (float)pipe->stats.total_reps;
    }
  else if (pipe->exercise_id == VELAFIT_EXERCISE_PLANK)
    {
      acc = plank_fsm_get_quality_score(&pipe->plank_fsm);
    }

  pipe->stats.accuracy_pct = acc;
  pipe->stats.calories_kcal =
    velafit_calc_calories(pipe->stats.exercise_type,
                          pipe->stats.duration_sec,
                          acc,
                          VELAFIT_DEFAULT_USER_WEIGHT_KG);
  pipe->stats.sync_status = VELAFIT_SYNC_PENDING;

  if (json_buf != NULL && json_max_len > 0)
    {
      snprintf(json_buf, json_max_len,
               "{\n"
               "  \"version\": \"1.0.0\",\n"
               "  \"session_id\": \"%s\",\n"
               "  \"device\": \"ESP32-P4-Function-EV-Board-V1.8\",\n"
               "  \"exercise_type\": \"%s\",\n"
               "  \"total_reps\": %lu,\n"
               "  \"valid_reps\": %lu,\n"
               "  \"hold_duration_sec\": %lu,\n"
               "  \"accuracy_rate_pct\": %.1f,\n"
               "  \"duration_sec\": %lu,\n"
               "  \"calories_kcal\": %.2f,\n"
               "  \"sync_status\": \"PENDING\",\n"
               "  \"fault_breakdown\": {\n"
               "    \"shallow_depth\": %lu,\n"
               "    \"knee_caving_valgus\": %lu,\n"
               "    \"hips_sagging\": %lu,\n"
               "    \"hips_piking\": %lu,\n"
               "    \"trunk_lean\": %lu\n"
               "  },\n"
               "  \"metrics\": {\n"
               "    \"min_knee_angle_deg\": %.1f,\n"
               "    \"min_elbow_angle_deg\": %.1f,\n"
               "    \"total_frames_processed\": %lu\n"
               "  },\n"
               "  \"status\": \"COMPLETED\"\n"
               "}\n",
               pipe->stats.session_id,
               pipe->stats.exercise_type,
               (unsigned long)pipe->stats.total_reps,
               (unsigned long)pipe->stats.valid_reps,
               (unsigned long)pipe->stats.hold_duration_sec,
               acc,
               (unsigned long)pipe->stats.duration_sec,
               pipe->stats.calories_kcal,
               (unsigned long)pipe->stats.shallow_count,
               (unsigned long)pipe->stats.knee_caving_count,
               (unsigned long)pipe->stats.hips_sag_count,
               (unsigned long)pipe->stats.hips_pike_count,
               (unsigned long)pipe->stats.trunk_lean_count,
               pipe->stats.avg_knee_angle_min,
               pipe->stats.min_elbow_angle,
               (unsigned long)pipe->frame_count);

      /* Persist session record to local storage */

      velafit_session_record_t rec;
      memset(&rec, 0, sizeof(rec));
      strncpy(rec.session_id, pipe->stats.session_id,
              sizeof(rec.session_id) - 1);
      strncpy(rec.exercise_type, pipe->stats.exercise_type,
              sizeof(rec.exercise_type) - 1);
      rec.timestamp = 0;
      rec.total_reps = pipe->stats.total_reps;
      rec.valid_reps = pipe->stats.valid_reps;
      rec.duration_sec = pipe->stats.duration_sec;
      rec.calories_kcal = pipe->stats.calories_kcal;
      rec.accuracy_pct = pipe->stats.accuracy_pct;
      rec.sync_status = VELAFIT_SYNC_PENDING;

      velafit_storage_save_session(&rec, json_buf);
    }

  return OK;
}

/****************************************************************************
 * Name: velafit_pipeline_deinit
 ****************************************************************************/

void velafit_pipeline_deinit(velafit_pipeline_t *pipe)
{
  if (pipe == NULL)
    {
      return;
    }

  if (pipe->canvas_buf != NULL)
    {
      free(pipe->canvas_buf);
      pipe->canvas_buf = NULL;
    }
}

/****************************************************************************
 * Name: velafit_pipeline_step_sample
 *
 * Description:
 *   Feed deterministic samples with the simulation clock.  Sample fixtures
 *   carry illustrative timestamps and must not be reused as live timestamps;
 *   doing so makes the One-Euro filter receive zero/negative time deltas.
 ****************************************************************************/

static int velafit_pipeline_step_sample(velafit_pipeline_t *pipe,
                                        const pose_frame_t *sample,
                                        uint32_t timestamp_ms)
{
  pose_frame_t pose = *sample;
  pose.timestamp_ms = timestamp_ms;
  return velafit_pipeline_step_pose(pipe, &pose);
}

/****************************************************************************
 * Name: velafit_pipeline_run_simulation
 ****************************************************************************/

int velafit_pipeline_run_simulation(const char *exercise,
                                    int cycles,
                                    const char *ppm_out)
{
  printf("\n=======================================================\n");
  printf("  VelaFit Multi-Media Pipeline Simulation: [%s]\n",
         (exercise != NULL) ? exercise : "squat");
  printf("=======================================================\n");

  if (cycles <= 0)
    {
      cycles = 1;
    }

  velafit_pipeline_t pipe;
  int ret = velafit_pipeline_init(&pipe, exercise, 320, 240);
  if (ret != OK)
    {
      printf("ERROR: Pipeline init failed! (code: %d)\n", ret);
      return ret;
    }

  printf("[1/5] Pipeline Init: Exercise=%s, "
         "Canvas=320x240 RGB565 [OK]\n",
         pipe.exercise_name);

  uint32_t t_ms = 0;

  if (pipe.exercise_id == VELAFIT_EXERCISE_PUSHUP)
    {
      for (int c = 0; c < cycles; c++)
        {
          printf("\n>>> --- Push-up Cycle %d/%d ---\n", c + 1, cycles);

          /* Standard Deep Pushup */

          printf("  [Step 1] Standard Deep Pushup...\n");
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_pushup_plank,
                                       t_ms);
          t_ms += 200;
          velafit_pipeline_step_sample(&pipe,
                                       &g_sample_pose_pushup_bottom_deep,
                                       t_ms);
          t_ms += 400;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_pushup_plank,
                                       t_ms);
          t_ms += 300;

          /* Shallow Pushup */

          printf("  [Step 2] Shallow Pushup Fault...\n");
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_pushup_plank,
                                       t_ms);
          t_ms += 200;
          velafit_pipeline_step_sample(&pipe,
                                       &g_sample_pose_pushup_shallow,
                                       t_ms);
          t_ms += 400;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_pushup_plank,
                                       t_ms);
          t_ms += 300;

          /* Sagging Hips Pushup */

          printf("  [Step 3] Sagging Hips Fault...\n");
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_pushup_plank,
                                       t_ms);
          t_ms += 200;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_pushup_sag,
                                       t_ms);
          t_ms += 400;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_pushup_plank,
                                       t_ms);
          t_ms += 300;
        }
    }
  else if (pipe.exercise_id == VELAFIT_EXERCISE_PLANK)
    {
      printf("\n>>> --- Plank Hold Simulation (15 seconds) ---\n");

      /* 5 sec Perfect */

      printf("  [Phase 1] 5s Standard Horizontal Plank...\n");
      for (int i = 0; i < 5; i++)
        {
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_plank_perfect,
                                       t_ms);
          t_ms += 1000;
        }

      /* 5 sec Sagging */

      printf("  [Phase 2] 5s Sagging Hips Fault...\n");
      for (int i = 0; i < 5; i++)
        {
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_plank_sag,
                                       t_ms);
          t_ms += 1000;
        }

      /* 5 sec Piking */

      printf("  [Phase 3] 5s Piking Hips Fault...\n");
      for (int i = 0; i < 5; i++)
        {
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_plank_pike,
                                       t_ms);
          t_ms += 1000;
        }
    }
  else
    {
      for (int c = 0; c < cycles; c++)
        {
          printf("\n>>> --- Squat Cycle %d/%d ---\n", c + 1, cycles);

          /* Standard Deep Squat */

          printf("  [Step 1] Standard Squat (Stand->Bottom->Stand)...\n");
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_standing, t_ms);
          t_ms += 200;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_squat_shallow,
                                       t_ms);
          t_ms += 300;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_squat_deep,
                                       t_ms);
          t_ms += 300;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_squat_shallow,
                                       t_ms);
          t_ms += 300;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_standing, t_ms);
          t_ms += 200;

          /* Shallow Fault */

          printf("  [Step 2] Shallow Squat (Partial Squat)...\n");
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_standing, t_ms);
          t_ms += 200;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_squat_shallow,
                                       t_ms);
          t_ms += 600;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_standing, t_ms);
          t_ms += 500;

          /* Knee Valgus Fault */

          printf("  [Step 3] Knee Valgus Fault (Inward Caving)...\n");
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_standing, t_ms);
          t_ms += 200;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_squat_valgus,
                                       t_ms);
          t_ms += 600;
          velafit_pipeline_step_sample(&pipe, &g_sample_pose_standing, t_ms);
          t_ms += 500;
        }
    }

  /* Save final rendered frame to PPM if path specified */

  if (ppm_out != NULL && ppm_out[0] != '\0')
    {
      printf("\n[4/5] Exporting OSD Frame to: %s ...\n", ppm_out);
      int save_ret = velafit_render_save_ppm(&pipe.canvas, ppm_out);
      if (save_ret == OK)
        {
          printf("      PPM Image Exported (320x240 RGB) [OK]\n");
        }
      else
        {
          printf("      PPM Export Failed: %d\n", save_ret);
        }
    }
  else
    {
      int fb_ret = velafit_render_to_fb0(&pipe.canvas);
      if (fb_ret == OK)
        {
          printf("\n[4/5] Rendered to /dev/fb0 [OK]\n");
        }
      else
        {
          printf("\n[4/5] /dev/fb0 unavailable: %d "
                 "[DISPLAY NOT VERIFIED]\n", fb_ret);
        }
    }

  /* Generate Edge-Cloud JSON Report */

  char json_report[1024];
  velafit_pipeline_finish(&pipe, json_report, sizeof(json_report));

  printf("\n[5/5] Edge-Cloud Workout JSON Report:\n");
  printf("-------------------------------------------------------\n");
  printf("%s", json_report);
  printf("-------------------------------------------------------\n");

  velafit_pipeline_deinit(&pipe);
  printf("\n>>> [Pipeline Simulation: COMPLETED]\n\n");
  return OK;
}
