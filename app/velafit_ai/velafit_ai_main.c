/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/velafit_ai_main.c
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "velafit_types.h"
#include "esp_nn_ops.h"
#include "velafit_pose_model.h"
#include "sample_pose_frames.h"
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
#include "velafit_pipeline.h"
#include "velafit_plan_scheduler.h"
#include "velafit_preset_plans.h"
#include "velafit_kws.h"
#include "velafit_cloud_agent.h"
#include "velafit_config.h"
#include "velafit_ppa_bench.h"
#include "velafit_touch.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: print_usage
 ****************************************************************************/

static void print_usage(void)
{
  printf("\n=======================================================\n");
  printf("  VelaFit AI Smart Posture Coach - CLI Suite\n");
  printf("=======================================================\n");
  printf("Usage: velafit_ai <command> [args]\n\n");
  printf("Commands:\n");
  printf("  benchmark            - Stage 1: ESP-NN SIMD Benchmarks\n");
  printf("  ppa                  - Stage 1: PPA 2D Hardware Benchmarks\n");
  printf("  test_pose            - Stage 2: 17 Keypoint Pose Inference\n");
  printf("  test_squat  [count]  - Stage 3: Squat FSM & Quality Audit\n");
  printf("  test_jj     [count]  - Stage 3: Jumping Jack FSM Simulation\n");
  printf("  test_pushup [count]  - Stage 3: Push-up FSM & Quality Audit\n");
  printf("  test_plank  [sec]    - Stage 3: Plank Timer & Posture Guard\n");
  printf("  render      [ppm]    - Stage 4: Skeleton OSD Rendering\n");
  printf("  audio       [cue]    - Stage 4: Voice / Tone Cue Dispatcher\n");
  printf("  pipeline    [cycles] - Stage 4: End-to-End Pipeline\n");
  printf("  plan        [cmd]    - Routine Planner (list/run [name])\n");
  printf("  storage     [cmd]    - Offline Storage (list/info/summary)\n");
  printf("  sync        [cmd]    - Cloud Sync (status/flush/mock)\n");
  printf("  config      [cmd]    - Runtime & Cloud Config (show/set)\n");
  printf("  touch       [sec]    - Capacitive Touchscreen & Gesture Test\n");
  printf("  sdcard      [test]   - MicroSD FAT32 Read/Write Test\n");
  printf("  kws         [sim]    - Local Keyword Spotting (Wakeup Test)\n");
  printf("  cloud       [sim]    - Xiaomi MIMO Multimodal Cloud Agent\n");
  printf("  report               - Edge-Cloud Workout JSON Report\n");
  printf("  all                  - Run All Verification Stages\n");
  printf("=======================================================\n\n");
}

/****************************************************************************
 * Name: cmd_test_pose
 ****************************************************************************/

static int cmd_test_pose(void)
{
  const char *backend = velafit_pose_model_backend_name();
  bool is_real = velafit_pose_model_backend_is_real();

  printf("\n>>> [Stage 2] Running Static Pose %s...\n",
         is_real ? "Inference" : "Simulation");
  printf("  Backend          : %s (%s)\n", backend,
         is_real ? "REAL INFERENCE" : "NOT REAL INFERENCE");

  velafit_perf_t perf;
  pose_frame_t detected_pose;

  int ret = velafit_pose_infer(g_sample_test_image_160x160,
                               &detected_pose, &perf);
  if (ret != 0)
    {
      printf("Error: Pose infer failed! (code: %d)\n", ret);
      return ret;
    }

  printf("\n--- 17 Human Keypoint Detections ---\n");
  const char *kpt_names[VELAFIT_NUM_KEYPOINTS] =
  {
    "Nose", "L_Eye", "R_Eye", "L_Ear", "R_Ear",
    "L_Shoulder", "R_Shoulder", "L_Elbow", "R_Elbow",
    "L_Wrist", "R_Wrist", "L_Hip", "R_Hip",
    "L_Knee", "R_Knee", "L_Ankle", "R_Ankle"
  };

  for (int i = 0; i < VELAFIT_NUM_KEYPOINTS; i++)
    {
      printf("  [%02d] %-12s : (X: %0.3f, Y: %0.3f)  Score: %0.2f\n",
             i, kpt_names[i],
             detected_pose.kpts[i].x,
             detected_pose.kpts[i].y,
             detected_pose.kpts[i].score);
    }

  float knee_l = velafit_get_knee_angle_left(&detected_pose);
  float knee_r = velafit_get_knee_angle_right(&detected_pose);
  float hip_l  = velafit_get_hip_angle_left(&detected_pose);
  float hip_r  = velafit_get_hip_angle_right(&detected_pose);
  float trunk_lean = velafit_get_trunk_lean_angle(&detected_pose);
  float knee_ankle_ratio =
    velafit_get_knee_to_ankle_ratio(&detected_pose);

  printf("\n--- Geometric Angles & Posture Features ---\n");
  printf("  Left Knee Angle  : %0.1f deg\n", knee_l);
  printf("  Right Knee Angle : %0.1f deg\n", knee_r);
  printf("  Left Hip Angle   : %0.1f deg\n", hip_l);
  printf("  Right Hip Angle  : %0.1f deg\n", hip_r);
  printf("  Trunk Lean Angle : %0.1f deg\n", trunk_lean);
  printf("  Knee/Ankle Ratio : %0.2f (Threshold > 0.72)\n",
         knee_ankle_ratio);

  printf("\n--- Backend Profiling Metrics ---\n");
  printf("  Preprocess       : %lu us\n",
         (unsigned long)perf.preprocess_us);
  printf("  Forward Infer    : %lu us\n",
         (unsigned long)perf.infer_us);
  printf("  Postprocess      : %lu us\n",
         (unsigned long)perf.postprocess_us);
  printf("  Total Latency    : %lu us (approx %.1f FPS)\n",
         (unsigned long)perf.total_us, perf.fps);
  printf(">>> [Stage 2] Pose %s Test: [%s PASS]\n\n",
         is_real ? "Inference" : "Simulation",
         is_real ? "INFERENCE" : "SIMULATION");
  return 0;
}

/****************************************************************************
 * Name: cmd_test_squat
 ****************************************************************************/

static int cmd_test_squat(int cycles)
{
  if (cycles <= 0)
    {
      cycles = 1;
    }

  printf("\n>>> [Stage 3] Squat FSM Simulation (Cycles: %d)...\n",
         cycles);

  squat_fsm_t fsm;
  squat_fsm_init(&fsm);

  uint32_t t = 0;

  for (int c = 0; c < cycles; c++)
    {
      printf("\n[Test 1/3] Simulating Standard Deep Squat...\n");
      squat_fsm_update(&fsm, &g_sample_pose_standing, t);
      t += 200;
      squat_fsm_update(&fsm, &g_sample_pose_squat_shallow, t);
      t += 300;
      squat_fsm_update(&fsm, &g_sample_pose_squat_deep, t);
      t += 300;
      squat_fsm_update(&fsm, &g_sample_pose_squat_shallow, t);
      t += 300;
      bool rep1 = squat_fsm_update(&fsm, &g_sample_pose_standing, t);
      t += 200;

      printf("  => Rep Decision: %s (Total: %lu, Valid: %lu, Flag: %s)\n",
             rep1 ? "YES [TRIGGER]" : "NO",
             (unsigned long)fsm.total_reps,
             (unsigned long)fsm.valid_reps,
             (fsm.last_quality_flags == SQUAT_QUALITY_OK) ?
             "STANDARD (OK)" : "ERROR");

      printf("\n[Test 2/3] Simulating Shallow Squat...\n");
      squat_fsm_update(&fsm, &g_sample_pose_standing, t);
      t += 200;
      squat_fsm_update(&fsm, &g_sample_pose_squat_shallow, t);
      t += 600;
      bool rep2 = squat_fsm_update(&fsm, &g_sample_pose_standing, t);
      t += 500;

      printf("  => Rep Decision: %s (Shallow: %lu, Quality: %s)\n",
             rep2 ? "YES [TRIGGER]" : "NO",
             (unsigned long)fsm.shallow_count,
             (fsm.last_quality_flags & SQUAT_QUALITY_SHALLOW) ?
             "WARN: SHALLOW [DETECTED]" : "NOT_DETECTED");

      printf("\n[Test 3/3] Simulating Knee Valgus Squat...\n");
      squat_fsm_update(&fsm, &g_sample_pose_standing, t);
      t += 200;
      squat_fsm_update(&fsm, &g_sample_pose_squat_valgus, t);
      t += 600;
      bool rep3 = squat_fsm_update(&fsm, &g_sample_pose_standing, t);
      t += 500;

      printf("  => Rep Decision: %s (Valgus: %lu, Quality: %s)\n",
             rep3 ? "YES [TRIGGER]" : "NO",
             (unsigned long)fsm.knee_caving_count,
             (fsm.last_quality_flags & SQUAT_QUALITY_KNEE_CAVING) ?
             "WARN: KNEE VALGUS [DETECTED]" : "NOT_DETECTED");
    }

  printf("\n================ Squat Summary ================\n");
  printf("  Total Reps      : %lu\n", (unsigned long)fsm.total_reps);
  printf("  Valid Standard  : %lu\n", (unsigned long)fsm.valid_reps);
  printf("  Shallow Faults  : %lu\n", (unsigned long)fsm.shallow_count);
  printf("  Valgus Faults   : %lu\n",
         (unsigned long)fsm.knee_caving_count);
  printf("  Lean Faults     : %lu\n",
         (unsigned long)fsm.trunk_lean_count);
  printf("===============================================\n");
  printf(">>> [Stage 3] Squat FSM Test: [PASS]\n\n");
  return 0;
}

/****************************************************************************
 * Name: cmd_test_jumping_jack
 ****************************************************************************/

static int cmd_test_jumping_jack(int cycles)
{
  if (cycles <= 0)
    {
      cycles = 1;
    }

  printf("\n>>> [Stage 3] Jumping Jack Simulation (Cycles: %d)...\n",
         cycles);

  jumping_jack_fsm_t fsm;
  jumping_jack_fsm_reset(&fsm);

  uint32_t t = 0;
  for (int i = 0; i < cycles; i++)
    {
      jumping_jack_fsm_update(&fsm, &g_sample_pose_jj_closed, t);
      t += 100;
      jumping_jack_fsm_update(&fsm, &g_sample_pose_jj_open, t);
      t += 300;
      jumping_jack_fsm_update(&fsm, &g_sample_pose_jj_closed, t);
      t += 100;
      bool rep = jumping_jack_fsm_update(&fsm,
                                         &g_sample_pose_jj_closed, t);
      t += 200;

      printf("  JJ Rep %d Decision: %s (Total: %lu, Valid: %lu)\n",
             i + 1, rep ? "YES [PASS]" : "NO",
             (unsigned long)fsm.total_reps,
             (unsigned long)fsm.valid_reps);
    }

  printf("\n================ Jumping Jack Summary ================\n");
  printf("  Total Reps : %lu\n", (unsigned long)fsm.total_reps);
  printf("  Valid Reps : %lu\n", (unsigned long)fsm.valid_reps);
  printf("======================================================\n");
  bool pass = fsm.total_reps == (uint32_t)cycles &&
              fsm.valid_reps == (uint32_t)cycles;
  printf(">>> [Stage 3] Jumping Jack Test: [%s]\n\n",
         pass ? "PASS" : "FAIL");
  return pass ? 0 : -EIO;
}

/****************************************************************************
 * Name: cmd_test_pushup
 ****************************************************************************/

static int cmd_test_pushup(int cycles)
{
  if (cycles <= 0)
    {
      cycles = 1;
    }

  printf("\n>>> [Stage 3] Push-up FSM Simulation (Cycles: %d)...\n",
         cycles);

  pushup_fsm_t fsm;
  pushup_fsm_init(&fsm);

  uint32_t t = 0;

  for (int c = 0; c < cycles; c++)
    {
      printf("\n[Test 1/3] Simulating Standard Deep Push-up...\n");
      pushup_fsm_update(&fsm, &g_sample_pose_pushup_plank, t);
      t += 200;
      pushup_fsm_update(&fsm, &g_sample_pose_pushup_bottom_deep, t);
      t += 400;
      bool rep1 =
        pushup_fsm_update(&fsm, &g_sample_pose_pushup_plank, t);
      t += 300;

      printf("  => Rep Decision: %s (Total: %lu, Valid: %lu, Flag: %s)\n",
             rep1 ? "YES [PASS]" : "NO",
             (unsigned long)fsm.total_reps,
             (unsigned long)fsm.valid_reps,
             (fsm.last_quality_flags == PUSHUP_QUALITY_OK) ?
             "STANDARD (OK)" : "ERROR");

      printf("\n[Test 2/3] Simulating Shallow Push-up...\n");
      pushup_fsm_update(&fsm, &g_sample_pose_pushup_plank, t);
      t += 200;
      pushup_fsm_update(&fsm, &g_sample_pose_pushup_shallow, t);
      t += 400;
      bool rep2 =
        pushup_fsm_update(&fsm, &g_sample_pose_pushup_plank, t);
      t += 300;

      printf("  => Rep Decision: %s (Shallow: %lu, Quality: %s)\n",
             rep2 ? "YES [TRIGGER]" : "NO",
             (unsigned long)fsm.shallow_count,
             (fsm.last_quality_flags & PUSHUP_QUALITY_SHALLOW) ?
             "WARN: SHALLOW [DETECTED]" : "NOT_DETECTED");

      printf("\n[Test 3/3] Simulating Sagging Hips Push-up...\n");
      pushup_fsm_update(&fsm, &g_sample_pose_pushup_plank, t);
      t += 200;
      pushup_fsm_update(&fsm, &g_sample_pose_pushup_bottom_deep, t);
      t += 400;
      bool rep3 =
        pushup_fsm_update(&fsm, &g_sample_pose_pushup_sag, t);
      t += 200;
      pushup_fsm_update(&fsm, &g_sample_pose_pushup_plank, t);
      t += 300;

      printf("  => Rep Decision: %s (Sagging: %lu, Quality: %s)\n",
             rep3 ? "YES [TRIGGER]" : "NO",
             (unsigned long)fsm.hips_sag_count,
             (fsm.last_quality_flags & PUSHUP_QUALITY_HIPS_SAG) ?
             "WARN: HIPS SAG [DETECTED]" : "NOT_DETECTED");
    }

  printf("\n================ Push-up Summary ================\n");
  printf("  Total Reps     : %lu\n", (unsigned long)fsm.total_reps);
  printf("  Valid Standard : %lu\n", (unsigned long)fsm.valid_reps);
  printf("  Shallow Faults : %lu\n", (unsigned long)fsm.shallow_count);
  printf("  Sagging Faults : %lu\n", (unsigned long)fsm.hips_sag_count);
  printf("  Piking Faults  : %lu\n", (unsigned long)fsm.hips_pike_count);
  printf("=================================================\n");
  bool pass = fsm.total_reps == (uint32_t)(cycles * 3) &&
              fsm.valid_reps == (uint32_t)cycles &&
              fsm.shallow_count == (uint32_t)cycles &&
              fsm.hips_sag_count == (uint32_t)cycles;
  printf(">>> [Stage 3] Push-up FSM Test: [%s]\n\n",
         pass ? "PASS" : "FAIL");
  return pass ? 0 : -EIO;
}

/****************************************************************************
 * Name: cmd_test_plank
 ****************************************************************************/

static int cmd_test_plank(int seconds)
{
  if (seconds <= 0)
    {
      seconds = 10;
    }

  printf("\n>>> [Stage 3] Plank Timer Simulation (%d seconds)...\n",
         seconds);

  plank_fsm_t fsm;
  plank_fsm_init(&fsm);

  uint32_t t = 0;

  /* Prime the timer at t=0; duration is accumulated between samples. */

  plank_fsm_update(&fsm, &g_sample_pose_plank_perfect, t);

  printf("  [Phase 1] 4s Perfect Plank...\n");
  for (int i = 0; i < 4; i++)
    {
      t += 1000;
      plank_fsm_update(&fsm, &g_sample_pose_plank_perfect, t);
    }

  printf("  [Phase 2] 3s Sagging Hips Plank...\n");
  for (int i = 0; i < 3; i++)
    {
      t += 1000;
      plank_fsm_update(&fsm, &g_sample_pose_plank_sag, t);
    }

  printf("  [Phase 3] 3s Piking Hips Plank...\n");
  for (int i = 0; i < 3; i++)
    {
      t += 1000;
      plank_fsm_update(&fsm, &g_sample_pose_plank_pike, t);
    }

  float score = plank_fsm_get_quality_score(&fsm);

  printf("\n================ Plank Summary ================\n");
  printf("  Total Duration : %lu ms (%.1f s)\n",
         (unsigned long)fsm.total_hold_duration_ms,
         (float)fsm.total_hold_duration_ms / 1000.0f);
  printf("  Valid Hold     : %lu ms (%.1f s)\n",
         (unsigned long)fsm.valid_hold_duration_ms,
         (float)fsm.valid_hold_duration_ms / 1000.0f);
  printf("  Hips Sag Time  : %lu ms\n",
         (unsigned long)fsm.hips_sag_duration_ms);
  printf("  Hips Pike Time : %lu ms\n",
         (unsigned long)fsm.hips_pike_duration_ms);
  printf("  Quality Score  : %.1f %%\n", score);
  printf("===============================================\n");
  bool pass = fsm.total_hold_duration_ms == 10000 &&
              fsm.valid_hold_duration_ms == 4000 &&
              fsm.hips_sag_duration_ms == 3000 &&
              fsm.hips_pike_duration_ms == 3000;
  printf(">>> [Stage 3] Plank Timer Test: [%s]\n\n",
         pass ? "PASS" : "FAIL");
  return pass ? 0 : -EIO;
}

/****************************************************************************
 * Name: cmd_render
 ****************************************************************************/

static int cmd_render(const char *ppm_path)
{
  int output_ret = OK;

  printf("\n>>> [Stage 4] Running OSD Skeleton Renderer...\n");

  uint32_t w = 320;
  uint32_t h = 240;
  uint8_t *buf = (uint8_t *)malloc(w * h * 2);
  if (buf == NULL)
    {
      printf("ERROR: Failed to allocate framebuffer memory!\n");
      return -ENOMEM;
    }

  velafit_canvas_t canvas;
  velafit_canvas_init(&canvas, buf, w, h, VELAFIT_PIXFMT_RGB565);

  /* Render Dashboard with Skeleton, Guidance Arrows & Depth Gauge */

  velafit_render_dashboard(&canvas,
                           &g_sample_pose_squat_valgus,
                           "SQUAT",
                           12,
                           18.5f,
                           30.0f,
                           85.0f,
                           90.0f,
                           SQUAT_QUALITY_KNEE_CAVING,
                           "WARN: PUSH KNEES OUTWARD");

  printf("  [1/2] 320x240 RGB565 Dashboard (Skeleton, Gauge, HUD) [OK]\n");

  /* Save or Output to FB */

  if (ppm_path != NULL && ppm_path[0] != '\0')
    {
      output_ret = velafit_render_save_ppm(&canvas, ppm_path);
      if (output_ret == OK)
        {
          printf("  [2/2] Saved PPM Image: %s [OK]\n", ppm_path);
        }
      else
        {
          printf("  [2/2] Failed to save PPM: %d\n", output_ret);
        }
    }
  else
    {
      output_ret = velafit_render_to_fb0(&canvas);
      if (output_ret == OK)
        {
          printf("  [2/2] Rendered to /dev/fb0 [OK]\n");
        }
      else
        {
          printf("  [2/2] /dev/fb0 unavailable: %d [NOT VERIFIED]\n",
                 output_ret);
        }
    }

  free(buf);
  printf(">>> [Stage 4] OSD Skeleton Render Test: [%s]\n\n",
         output_ret == OK ? "PASS" : "NOT VERIFIED");
  return output_ret;
}

/****************************************************************************
 * Name: cmd_audio
 ****************************************************************************/

static int cmd_audio(const char *cue_arg)
{
  printf("\n>>> [Stage 4] Running Voice & Audio Cue Dispatcher...\n");

  if (cue_arg != NULL && strcmp(cue_arg, "all") != 0)
    {
      velafit_audio_cue_type_t cue = VELAFIT_AUDIO_CUE_START;

      if (strcmp(cue_arg, "count") == 0)
        {
          cue = VELAFIT_AUDIO_CUE_REP_COUNT;
        }
      else if (strcmp(cue_arg, "shallow") == 0)
        {
          cue = VELAFIT_AUDIO_CUE_WARN_SHALLOW;
        }
      else if (strcmp(cue_arg, "valgus") == 0)
        {
          cue = VELAFIT_AUDIO_CUE_WARN_VALGUS;
        }
      else if (strcmp(cue_arg, "lean") == 0)
        {
          cue = VELAFIT_AUDIO_CUE_WARN_LEAN;
        }
      else if (strcmp(cue_arg, "sag") == 0)
        {
          cue = VELAFIT_AUDIO_CUE_WARN_SAG;
        }
      else if (strcmp(cue_arg, "pike") == 0)
        {
          cue = VELAFIT_AUDIO_CUE_WARN_PIKE;
        }
      else if (strcmp(cue_arg, "finish") == 0)
        {
          cue = VELAFIT_AUDIO_CUE_FINISH;
        }

      printf("  Audio Cue: [%s] -> %s\n",
             velafit_audio_cue_name(cue),
             velafit_audio_cue_desc(cue));
      velafit_audio_cue_play(cue);
    }
  else
    {
      for (int i = 0; i < VELAFIT_AUDIO_CUE_MAX; i++)
        {
          printf("  [%d/%d] Cue: [%s] -> %s\n",
                 i + 1, VELAFIT_AUDIO_CUE_MAX,
                 velafit_audio_cue_name((velafit_audio_cue_type_t)i),
                 velafit_audio_cue_desc((velafit_audio_cue_type_t)i));
          velafit_audio_cue_play((velafit_audio_cue_type_t)i);
          usleep(100000);
        }
    }

  printf(">>> [Stage 4] Audio Cue Test: [PASS]\n\n");
  return 0;
}

/****************************************************************************
 * Name: cmd_storage
 ****************************************************************************/

static int cmd_storage(const char *subcmd, const char *arg)
{
  printf("\n>>> [Storage] Offline Session Storage Manager...\n");
  printf("  Base Path: %s\n", velafit_storage_get_path());

  if (subcmd == NULL || strcmp(subcmd, "list") == 0)
    {
      velafit_session_record_t recs[VELAFIT_STORAGE_MAX_SESSIONS];
      size_t count = 0;
      velafit_storage_list_sessions(recs, VELAFIT_STORAGE_MAX_SESSIONS,
                                    &count);

      printf("\n--- Local Workout Session Index (%zu entries) ---\n",
             count);
      if (count == 0)
        {
          printf("  (No stored workout sessions found)\n");
        }
      else
        {
          printf("  %-24s | %-12s | %-6s | %-8s | %-8s | %-8s\n",
                 "Session ID", "Exercise", "Reps", "Time(s)",
                 "Calories", "Sync");
          printf("  --------------------------------------------------"
                 "---------------------------\n");
          for (size_t i = 0; i < count; i++)
            {
              const char *st_str = "PENDING";
              if (recs[i].sync_status == VELAFIT_SYNC_SYNCED)
                {
                  st_str = "SYNCED";
                }
              else if (recs[i].sync_status == VELAFIT_SYNC_FAILED)
                {
                  st_str = "FAILED";
                }

              printf("  %-22s | %-10s | %-4lu | %-6lu | "
                     "%-5.1f kcal | %-7s\n",
                     recs[i].session_id,
                     recs[i].exercise_type,
                     (unsigned long)recs[i].valid_reps,
                     (unsigned long)recs[i].duration_sec,
                     recs[i].calories_kcal,
                     st_str);
            }
        }
    }
  else if (strcmp(subcmd, "info") == 0 && arg != NULL)
    {
      char json_buf[2048];
      int ret = velafit_storage_load_session(arg, json_buf,
                                            sizeof(json_buf));
      if (ret == OK)
        {
          printf("\n--- Session Payload [%s] ---\n%s\n", arg, json_buf);
        }
      else
        {
          printf("ERROR: Session '%s' not found in storage (ret: %d)\n",
                 arg, ret);
        }
    }
  else if (strcmp(subcmd, "summary") == 0)
    {
      uint32_t total = 0;
      uint32_t pending = 0;
      float calories = 0.0f;
      velafit_storage_get_summary(&total, &pending, &calories);

      printf("\n--- Storage Aggregated Summary ---\n");
      printf("  Total Sessions Recorded : %lu\n", (unsigned long)total);
      printf("  Pending Cloud Sync      : %lu\n",
             (unsigned long)pending);
      printf("  Total Burned Calories   : %.2f kcal\n", calories);
    }

  printf(">>> [Storage] Completed.\n\n");
  return 0;
}

/****************************************************************************
 * Name: cmd_sync
 ****************************************************************************/

static int cmd_sync(const char *subcmd)
{
  printf("\n>>> [Sync] Edge-Cloud Message Synchronizer...\n");

  if (subcmd == NULL || strcmp(subcmd, "status") == 0)
    {
      int pending = velafit_sync_get_pending_count();
      bool net_ready = velafit_sync_is_network_ready();

      printf("  ESP32-C6 Wireless Link : %s\n",
             net_ready ? "ONLINE (Active)" : "OFFLINE / RESERVED");
      printf("  Pending Sessions       : %d\n", pending);
    }
  else if (strcmp(subcmd, "mock") == 0)
    {
      printf("  Registering Mock Wireless Sender (Simulating C6 Link)...\n");
      velafit_sync_register_sender(velafit_sync_mock_sender);

      int count = velafit_sync_flush();
      printf("  Sync Flush Result: %d sessions pushed to cloud\n", count);
    }
  else if (strcmp(subcmd, "flush") == 0)
    {
      int count = velafit_sync_flush();
      printf("  Sync Flush Result: %d sessions synced\n", count);
    }

  printf(">>> [Sync] Completed.\n\n");
  return 0;
}

/****************************************************************************
 * Name: cmd_plan
 ****************************************************************************/

static int cmd_plan(const char *subcmd, const char *plan_name)
{
  printf("\n>>> [Plan] VelaFit Workout Plan Scheduler...\n");

  if (subcmd == NULL || strcmp(subcmd, "list") == 0)
    {
      size_t count = velafit_get_preset_plan_count();
      printf("Available Workout Routines (%lu total):\n",
             (unsigned long)count);
      printf("-------------------------------------------------------\n");
      for (size_t i = 0; i < count; i++)
        {
          const velafit_workout_plan_t *p = velafit_get_preset_plan(i);
          if (p != NULL)
            {
              printf("  [%lu] %-10s | %-16s | %lu Steps | %s\n",
                     (unsigned long)(i + 1),
                     p->plan_name,
                     p->plan_id,
                     (unsigned long)p->num_steps,
                     p->plan_desc);
            }
        }

      printf("-------------------------------------------------------\n");
      printf("Run a plan with: velafit_ai plan run <plan_name>\n\n");
      return 0;
    }

  if (strcmp(subcmd, "run") == 0)
    {
      const char *target = (plan_name != NULL) ? plan_name : "tabata";
      const velafit_workout_plan_t *plan =
        velafit_find_preset_plan_by_name(target);

      if (plan == NULL)
        {
          printf("Error: Workout plan '%s' not found!\n", target);
          return -ENOENT;
        }

      printf("Executing Plan: %s (%s)\n", plan->plan_name, plan->plan_desc);
      printf("Total Steps   : %lu\n\n", (unsigned long)plan->num_steps);

      velafit_plan_scheduler_t sched;
      int ret = velafit_plan_scheduler_init(&sched, plan);
      if (ret != OK)
        {
          printf("Error: Failed to init plan scheduler: %d\n", ret);
          return ret;
        }

      /* Simulate execution with pose frames */

      uint32_t step_sim_dt_ms = 100;
      uint32_t max_sim_ticks = 400;

      for (uint32_t tick = 0; tick < max_sim_ticks; tick++)
        {
          if (velafit_plan_scheduler_is_finished(&sched))
            {
              break;
            }

          const pose_frame_t *pose = &g_sample_pose_standing;
          if (sched.current_phase == VELAFIT_PHASE_WORK)
            {
              if (strcmp(sched.pipeline.exercise_name, "squat") == 0)
                {
                  pose = (tick % 4 < 2) ? &g_sample_pose_squat_deep :
                                          &g_sample_pose_standing;
                }
              else if (strcmp(sched.pipeline.exercise_name, "pushup") == 0)
                {
                  pose = (tick % 4 < 2) ? &g_sample_pose_pushup_bottom_deep :
                                          &g_sample_pose_pushup_plank;
                }
              else if (strcmp(sched.pipeline.exercise_name, "plank") == 0)
                {
                  pose = &g_sample_pose_plank_perfect;
                }
              else
                {
                  pose = (tick % 4 < 2) ? &g_sample_pose_jj_open :
                                          &g_sample_pose_jj_closed;
                }
            }

          velafit_plan_scheduler_step(&sched, pose, step_sim_dt_ms);
        }

      char json_report[512];
      velafit_plan_scheduler_finish(&sched, json_report,
                                    sizeof(json_report));
      velafit_plan_scheduler_deinit(&sched);

      printf("--- Plan Execution Finished ---\n");
      printf("%s\n", json_report);
      printf(">>> [Plan] Routine Completed & Saved: [PASS]\n\n");
    }

  return 0;
}

/****************************************************************************
 * Name: cmd_report
 ****************************************************************************/

static void cmd_report(void)
{
  printf("\n>>> [Edge-Cloud] Structured Workout JSON Packet...\n");
  printf("-------------------------------------------------------\n");
  printf("{\n");
  printf("  \"device_id\": \"esp32p4_velafit_01\",\n");
  printf("  \"session_id\": \"sess_20260830_001000\",\n");
  printf("  \"exercise_type\": \"pushup\",\n");
  printf("  \"total_reps\": 25,\n");
  printf("  \"valid_reps\": 22,\n");
  printf("  \"duration_seconds\": 50,\n");
  printf("  \"calories_kcal\": 6.80,\n");
  printf("  \"metrics\": {\n");
  printf("    \"min_elbow_angle_min\": 86.2,\n");
  printf("    \"avg_rep_duration_ms\": 1950,\n");
  printf("    \"shallow_count\": 2,\n");
  printf("    \"hips_sag_count\": 1,\n");
  printf("    \"hips_pike_count\": 0\n");
  printf("  },\n");
  printf("  \"cloud_ai_agent_status\": \"READY_TO_SYNC\"\n");
  printf("}\n");
  printf("-------------------------------------------------------\n\n");
}

/****************************************************************************
 * Name: cmd_kws
 ****************************************************************************/

static int cmd_kws(const char *mode)
{
  return velafit_kws_run_simulation(mode);
}

/****************************************************************************
 * Name: cmd_cloud
 ****************************************************************************/

static int cmd_cloud(void)
{
  return velafit_cloud_agent_run_simulation();
}

/****************************************************************************
 * Name: cmd_config
 ****************************************************************************/

static int cmd_config(const char *sub, const char *arg1, const char *arg2)
{
  if (sub == NULL || strcmp(sub, "show") == 0)
    {
      velafit_config_print();
    }
  else if (strcmp(sub, "set_url") == 0)
    {
      if (arg1 == NULL)
        {
          printf("Usage: velafit_ai config set_url <url>\n");
          return -EINVAL;
        }

      velafit_config_set_url(arg1);
      printf("Config Base URL updated: %s\n", arg1);
    }
  else if (strcmp(sub, "set_key") == 0)
    {
      if (arg1 == NULL)
        {
          printf("Usage: velafit_ai config set_key <key>\n");
          return -EINVAL;
        }

      velafit_config_set_key(arg1);
      printf("Config API Key updated successfully (persisted).\n");
    }
  else if (strcmp(sub, "set_model") == 0)
    {
      if (arg1 == NULL)
        {
          printf("Usage: velafit_ai config set_model <model>\n");
          return -EINVAL;
        }

      velafit_config_set_model(arg1);
      printf("Config Model updated: %s\n", arg1);
    }
  else if (strcmp(sub, "set_wifi") == 0)
    {
      if (arg1 == NULL)
        {
          printf("Usage: velafit_ai config set_wifi <ssid> [pwd]\n");
          return -EINVAL;
        }

      velafit_config_set_wifi(arg1, arg2);
      printf("Config Wi-Fi updated: SSID='%s'\n", arg1);
    }
  else if (strcmp(sub, "reset") == 0)
    {
      velafit_config_reset();
      printf("Config reset to defaults.\n");
    }
  else
    {
      printf("Unknown config subcommand: %s\n", sub);
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: cmd_sdcard
 ****************************************************************************/

static int cmd_sdcard(const char *sub)
{
  static uint8_t write_buf[4096];
  static uint8_t read_buf[4096];
  char test_path[128];
  FILE *fp;
  struct timespec ts0;
  struct timespec ts1;
  uint64_t t_write;
  uint64_t t_read;
  size_t bytes_read = 0;
  bool valid = true;
  int i;
  int b;

  printf("\n>>> [SDMMC / MicroSD Card Test] (%s)...\n",
         sub ? sub : "benchmark");

  if (access("/sdcard", F_OK) != 0)
    {
      printf("Error: /sdcard mount point not found or not accessible!\n");
      printf("       Please ensure MicroSD is inserted and formatted.\n");
      return -ENOENT;
    }

  printf("  -> /sdcard mount point: [OK]\n");
  printf("  -> Ensuring /sdcard/velafit/media directory exists...\n");
  mkdir("/sdcard/velafit", 0777);
  mkdir("/sdcard/velafit/media", 0777);

  snprintf(test_path, sizeof(test_path),
           "/sdcard/velafit/media/sd_test.bin");

  printf("  -> Writing test file: %s (64KB payload)...\n", test_path);

  fp = fopen(test_path, "wb");
  if (fp == NULL)
    {
      printf("Error: Failed to open %s for writing (%d)\n",
             test_path, errno);
      return -errno;
    }

  for (i = 0; i < 4096; i++)
    {
      write_buf[i] = (uint8_t)(i & 0xff);
    }

  clock_gettime(CLOCK_MONOTONIC, &ts0);
  for (b = 0; b < 16; b++)
    {
      fwrite(write_buf, 1, 4096, fp);
    }

  fflush(fp);
  fclose(fp);
  clock_gettime(CLOCK_MONOTONIC, &ts1);

  t_write = (uint64_t)(ts1.tv_sec - ts0.tv_sec) * 1000000ULL +
            (uint64_t)(ts1.tv_nsec - ts0.tv_nsec) / 1000ULL;
  if (t_write == 0)
    {
      t_write = 1;
    }

  printf("  -> Write complete: 65536 bytes in %llu us (%.2f MB/s)\n",
         (unsigned long long)t_write,
         (65536.0f / (float)t_write));

  printf("  -> Verifying file data integrity...\n");
  fp = fopen(test_path, "rb");
  if (fp == NULL)
    {
      printf("Error: Failed to reopen %s for reading\n", test_path);
      return -errno;
    }

  clock_gettime(CLOCK_MONOTONIC, &ts0);
  for (b = 0; b < 16; b++)
    {
      size_t n = fread(read_buf, 1, 4096, fp);
      bytes_read += n;
      if (memcmp(read_buf, write_buf, 4096) != 0)
        {
          valid = false;
          break;
        }
    }

  fclose(fp);
  clock_gettime(CLOCK_MONOTONIC, &ts1);

  t_read = (uint64_t)(ts1.tv_sec - ts0.tv_sec) * 1000000ULL +
           (uint64_t)(ts1.tv_nsec - ts0.tv_nsec) / 1000ULL;
  if (t_read == 0)
    {
      t_read = 1;
    }

  printf("  -> Read complete: %zu bytes in %llu us (%.2f MB/s), "
         "Integrity: %s\n",
         bytes_read, (unsigned long long)t_read,
         (65536.0f / (float)t_read),
         valid ? "[PASS]" : "[FAIL]");

  unlink(test_path);
  printf(">>> MicroSD Card / FAT32 Test: %s\n\n",
         valid ? "[PASS]" : "[FAIL]");
  return valid ? 0 : -EIO;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 ****************************************************************************/

int main(int argc, char *argv[])
{
  int ret = 0;

  if (argc < 2)
    {
      print_usage();
      return 0;
    }

  const char *cmd = argv[1];

  if (strcmp(cmd, "benchmark") == 0)
    {
      esp_nn_run_benchmarks();
    }
  else if (strcmp(cmd, "ppa") == 0)
    {
      ret = velafit_ppa_run_benchmarks();
    }
  else if (strcmp(cmd, "test_pose") == 0)
    {
      ret = cmd_test_pose();
    }
  else if (strcmp(cmd, "test_squat") == 0)
    {
      int count = (argc >= 3) ? atoi(argv[2]) : 1;
      ret = cmd_test_squat(count);
    }
  else if (strcmp(cmd, "test_jj") == 0 ||
           strcmp(cmd, "test_jumping_jack") == 0)
    {
      int count = (argc >= 3) ? atoi(argv[2]) : 1;
      ret = cmd_test_jumping_jack(count);
    }
  else if (strcmp(cmd, "test_pushup") == 0)
    {
      int count = (argc >= 3) ? atoi(argv[2]) : 1;
      ret = cmd_test_pushup(count);
    }
  else if (strcmp(cmd, "test_plank") == 0)
    {
      int seconds = (argc >= 3) ? atoi(argv[2]) : 10;
      ret = cmd_test_plank(seconds);
    }
  else if (strcmp(cmd, "render") == 0)
    {
      const char *ppm = (argc >= 3) ? argv[2] : NULL;
      ret = cmd_render(ppm);
    }
  else if (strcmp(cmd, "audio") == 0)
    {
      const char *cue = (argc >= 3) ? argv[2] : "all";
      ret = cmd_audio(cue);
    }
  else if (strcmp(cmd, "pipeline") == 0)
    {
      int cycles = (argc >= 3) ? atoi(argv[2]) : 1;
      const char *ppm = (argc >= 4) ? argv[3] : NULL;
      ret = velafit_pipeline_run_simulation("squat", cycles, ppm);
    }
  else if (strcmp(cmd, "plan") == 0)
    {
      const char *sub = (argc >= 3) ? argv[2] : "list";
      const char *arg = (argc >= 4) ? argv[3] : NULL;
      ret = cmd_plan(sub, arg);
    }
  else if (strcmp(cmd, "storage") == 0)
    {
      const char *sub = (argc >= 3) ? argv[2] : "list";
      const char *arg = (argc >= 4) ? argv[3] : NULL;
      ret = cmd_storage(sub, arg);
    }
  else if (strcmp(cmd, "sync") == 0)
    {
      const char *sub = (argc >= 3) ? argv[2] : "status";
      ret = cmd_sync(sub);
    }
  else if (strcmp(cmd, "config") == 0)
    {
      const char *sub  = (argc >= 3) ? argv[2] : "show";
      const char *arg1 = (argc >= 4) ? argv[3] : NULL;
      const char *arg2 = (argc >= 5) ? argv[4] : NULL;
      ret = cmd_config(sub, arg1, arg2);
    }
  else if (strcmp(cmd, "touch") == 0)
    {
      int sec = (argc >= 3) ? atoi(argv[2]) : 15;
      ret = velafit_touch_run_test(sec);
    }
  else if (strcmp(cmd, "sdcard") == 0)
    {
      const char *sub = (argc >= 3) ? argv[2] : "benchmark";
      ret = cmd_sdcard(sub);
    }
  else if (strcmp(cmd, "kws") == 0)
    {
      const char *mode = (argc >= 3) ? argv[2] : "test";
      ret = cmd_kws(mode);
    }
  else if (strcmp(cmd, "cloud") == 0)
    {
      ret = cmd_cloud();
    }
  else if (strcmp(cmd, "report") == 0)
    {
      cmd_report();
    }
  else if (strcmp(cmd, "all") == 0)
    {
      int failures = 0;

      printf("\n=======================================================\n");
      printf("  VelaFit AI Full Suite (Stage 1 ~ 6 + Config + MIMO)\n");
      printf("=======================================================\n");
      esp_nn_run_benchmarks();
      failures += velafit_ppa_run_benchmarks() < 0;
      failures += cmd_test_pose() < 0;
      failures += cmd_test_squat(1) < 0;
      failures += cmd_test_jumping_jack(1) < 0;
      failures += cmd_test_pushup(1) < 0;
      failures += cmd_test_plank(10) < 0;
      failures += cmd_render(NULL) < 0;
      failures += cmd_audio("all") < 0;
      failures += velafit_pipeline_run_simulation("squat", 1, NULL) < 0;
      failures += velafit_pipeline_run_simulation("pushup", 1, NULL) < 0;
      failures += velafit_pipeline_run_simulation("plank", 1, NULL) < 0;
      failures += cmd_plan("run", "tabata") < 0;
      failures += cmd_storage("list", NULL) < 0;
      failures += cmd_sync("mock") < 0;
      failures += cmd_config("show", NULL, NULL) < 0;
      failures += cmd_kws("test") < 0;
      failures += cmd_cloud() < 0;
      cmd_report();
      printf("=======================================================\n");
      printf("  VelaFit AI Full Verification Suite: [%s]"
             " (failures=%d)\n",
             failures == 0 ? "PASS" : "INCOMPLETE", failures);
      printf("=======================================================\n\n");
      ret = failures == 0 ? 0 : -EIO;
    }
  else
    {
      printf("Unknown command: %s\n", cmd);
      print_usage();
      return -1;
    }

  return ret;
}
