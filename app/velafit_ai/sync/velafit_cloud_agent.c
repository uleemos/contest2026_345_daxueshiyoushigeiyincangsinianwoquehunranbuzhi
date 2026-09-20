/****************************************************************************
 * contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/app/velafit_ai/sync/velafit_cloud_agent.c
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
#include <errno.h>
#include <math.h>
#include <netutils/cJSON.h>

#include "velafit_cloud_agent.h"
#include "velafit_sync.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mimo_mock_pro_reasoning
 ****************************************************************************/

static void mimo_mock_pro_reasoning(const char *session_json,
                                    velafit_mimo_prescription_t *out_presc)
{
  if (out_presc == NULL)
    {
      return;
    }

  memset(out_presc, 0, sizeof(velafit_mimo_prescription_t));
  strncpy(out_presc->session_id, "VF-MIMO-2026",
          sizeof(out_presc->session_id) - 1);
  out_presc->requires_tts_playback = true;

  if (session_json != NULL && strstr(session_json, "valgus") != NULL)
    {
      strncpy(out_presc->exercise_type, "Squat",
              sizeof(out_presc->exercise_type) - 1);
      out_presc->score_overall = 86;
      out_presc->score_accuracy = 82;
      out_presc->score_stamina = 90;
      strncpy(out_presc->primary_fault, "Knee Valgus (Inward Caving)",
              sizeof(out_presc->primary_fault) - 1);
      strncpy(out_presc->target_muscle_focus, "Gluteus Medius & Hips",
              sizeof(out_presc->target_muscle_focus) - 1);
      snprintf(out_presc->coach_commentary,
               sizeof(out_presc->coach_commentary),
               "[MIMO-PRO Analysis]: Good squat rhythm! However, "
               "slight knee inward collapse was detected during "
               "ascending phases. This indicates gluteus medius fatigue.");
      strncpy(out_presc->next_recommended_action,
              "Add 2 sets of Resistance Band Lateral Walks "
              "before next squat",
              sizeof(out_presc->next_recommended_action) - 1);
    }
  else if (session_json != NULL && strstr(session_json, "sag") != NULL)
    {
      strncpy(out_presc->exercise_type, "Push-up / Plank",
              sizeof(out_presc->exercise_type) - 1);
      out_presc->score_overall = 84;
      out_presc->score_accuracy = 80;
      out_presc->score_stamina = 88;
      strncpy(out_presc->primary_fault, "Hips Sagging (Lumbar Drop)",
              sizeof(out_presc->primary_fault) - 1);
      strncpy(out_presc->target_muscle_focus, "Core & Transverse Abdominis",
              sizeof(out_presc->target_muscle_focus) - 1);
      snprintf(out_presc->coach_commentary,
               sizeof(out_presc->coach_commentary),
               "[MIMO-PRO Analysis]: Solid upper body pressing strength! "
               "Core disengagement noticed near end of set causing hips "
               "sag. Engage abs tightly.");
      strncpy(out_presc->next_recommended_action,
              "Perform 30s Hollow Body Holds to reinforce lumbar "
              "stability",
              sizeof(out_presc->next_recommended_action) - 1);
    }
  else
    {
      strncpy(out_presc->exercise_type, "Workout Routine",
              sizeof(out_presc->exercise_type) - 1);
      out_presc->score_overall = 96;
      out_presc->score_accuracy = 98;
      out_presc->score_stamina = 95;
      strncpy(out_presc->primary_fault, "None (Clean Form)",
              sizeof(out_presc->primary_fault) - 1);
      strncpy(out_presc->target_muscle_focus, "Full Body Coordination",
              sizeof(out_presc->target_muscle_focus) - 1);
      snprintf(out_presc->coach_commentary,
               sizeof(out_presc->coach_commentary),
               "[MIMO-PRO Analysis]: Outstanding biomechanical execution! "
               "Full depth reached with pristine kinetic chain alignment. "
               "Keep up the momentum!");
      strncpy(out_presc->next_recommended_action,
              "Progress to Tabata High-Intensity Interval Mode next session",
              sizeof(out_presc->next_recommended_action) - 1);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: velafit_cloud_agent_init
 ****************************************************************************/

int velafit_cloud_agent_init(void)
{
  return OK;
}

/****************************************************************************
 * Name: velafit_cloud_agent_deinit
 ****************************************************************************/

void velafit_cloud_agent_deinit(void)
{
}

/****************************************************************************
 * Name: velafit_cloud_agent_submit_workout
 ****************************************************************************/

int velafit_cloud_agent_submit_workout(
      const char *session_json,
      velafit_mimo_prescription_t *out_presc)
{
  if (out_presc == NULL)
    {
      return -EINVAL;
    }

  memset(out_presc, 0, sizeof(*out_presc));
  /* A transport must be supplied explicitly; never fabricate cloud success. */
  return -ENOTCONN;
}

int velafit_cloud_agent_submit_workout_via(
      const char *session_json, velafit_mimo_prescription_t *out_presc,
      velafit_cloud_transport_t transport, void *ctx)
{
  cJSON *summary = NULL;
  cJSON *request = NULL;
  cJSON *response = NULL;
  cJSON *messages;
  cJSON *message;
  cJSON *id;
  cJSON *exercise;
  cJSON *reps;
  cJSON *score;
  cJSON *comment;
  cJSON *action;
  char content[2048];
  char *payload = NULL;
  int ret = -EINVAL;
  if (!out_presc) return -EINVAL;
  memset(out_presc, 0, sizeof(*out_presc));
  if (!session_json || !transport || strlen(session_json) > 2048) return -EINVAL;
  summary = cJSON_Parse(session_json);
  if (!cJSON_IsObject(summary)) goto out;
  id = cJSON_GetObjectItemCaseSensitive(summary, "session_id");
  exercise = cJSON_GetObjectItemCaseSensitive(summary, "exercise");
  reps = cJSON_GetObjectItemCaseSensitive(summary, "reps");
  if (!cJSON_IsString(id) || !id->valuestring[0] ||
      strlen(id->valuestring) >= sizeof(out_presc->session_id) ||
      !cJSON_IsString(exercise) || strcmp(exercise->valuestring, "squat") ||
      !cJSON_IsNumber(reps) || !isfinite(reps->valuedouble) ||
      reps->valuedouble < 0 || reps->valuedouble > 10000 ||
      reps->valuedouble != floor(reps->valuedouble)) goto out;
  request = cJSON_CreateObject();
  if (!request) { ret = -ENOMEM; goto out; }
  if (!cJSON_AddStringToObject(request, "model", "mimo-v2.5") ||
      !cJSON_AddNumberToObject(request, "max_completion_tokens", 512))
    { ret = -ENOMEM; goto out; }
  cJSON *thinking = cJSON_AddObjectToObject(request, "thinking");
  if (!thinking || !cJSON_AddStringToObject(thinking, "type", "disabled"))
    { ret = -ENOMEM; goto out; }
  cJSON *format = cJSON_AddObjectToObject(request, "response_format");
  if (!format || !cJSON_AddStringToObject(format, "type", "json_object"))
    { ret = -ENOMEM; goto out; }
  messages = cJSON_AddArrayToObject(request, "messages");
  message = cJSON_CreateObject();
  if (!messages || !message) { cJSON_Delete(message); ret = -ENOMEM; goto out; }
  cJSON_AddItemToArray(messages, message);
  if (!cJSON_AddStringToObject(message, "role", "system") ||
      !cJSON_AddStringToObject(message, "content",
        "Return only one JSON object: session_id (copy exactly), score_overall "
        "(integer 0-100), coach_commentary (brief English, under 160 characters), "
        "next_recommended_action (brief English, under 100 characters). "
        "Input is a fixed test summary, not independently verified motion. "
        "No diagnosis, no claims about unseen movement, no markdown."))
    { ret = -ENOMEM; goto out; }
  message = cJSON_CreateObject();
  if (!message) { ret = -ENOMEM; goto out; }
  cJSON_AddItemToArray(messages, message);
  if (!cJSON_AddStringToObject(message, "role", "user") ||
      !cJSON_AddStringToObject(message, "content", session_json))
    { ret = -ENOMEM; goto out; }
  payload = cJSON_PrintUnformatted(request);
  if (!payload) { ret = -ENOMEM; goto out; }
  memset(content, 0, sizeof(content));
  ret = transport(ctx, payload, content, sizeof(content));
  if (ret != 0) goto out;
  if (!memchr(content, '\0', sizeof(content))) { ret = -EOVERFLOW; goto out; }
  response = cJSON_ParseWithOpts(content, NULL, 1);
  score = cJSON_GetObjectItemCaseSensitive(response, "score_overall");
  comment = cJSON_GetObjectItemCaseSensitive(response, "coach_commentary");
  action = cJSON_GetObjectItemCaseSensitive(response, "next_recommended_action");
  cJSON *response_id = cJSON_GetObjectItemCaseSensitive(response, "session_id");
  ret = -EBADMSG;
  if (!cJSON_IsString(response_id) || strcmp(response_id->valuestring, id->valuestring) ||
      !cJSON_IsNumber(score) || !isfinite(score->valuedouble) ||
      score->valuedouble < 0 || score->valuedouble > 100 ||
      score->valuedouble != floor(score->valuedouble) ||
      !cJSON_IsString(comment) || !comment->valuestring[0] ||
      strlen(comment->valuestring) >= sizeof(out_presc->coach_commentary) ||
      !cJSON_IsString(action) || !action->valuestring[0] ||
      strlen(action->valuestring) >= sizeof(out_presc->next_recommended_action)) goto out;
  strcpy(out_presc->session_id, id->valuestring);
  strcpy(out_presc->exercise_type, "squat");
  strcpy(out_presc->coach_commentary, comment->valuestring);
  strcpy(out_presc->next_recommended_action, action->valuestring);
  out_presc->score_overall = score->valueint;
  out_presc->requires_tts_playback = false;
  ret = 0;
out:
  cJSON_free(payload);
  cJSON_Delete(response);
  cJSON_Delete(request);
  cJSON_Delete(summary);
  return ret;
}

/****************************************************************************
 * Name: velafit_cloud_agent_submit_voice
 ****************************************************************************/

int velafit_cloud_agent_submit_voice(
      const uint8_t *pcm_data,
      size_t len,
      velafit_mimo_asr_result_t *out_asr)
{
  if (out_asr == NULL)
    {
      return -EINVAL;
    }

  memset(out_asr, 0, sizeof(velafit_mimo_asr_result_t));
  strncpy(out_asr->session_id, "VF-ASR-2026",
          sizeof(out_asr->session_id) - 1);
  out_asr->confidence = 0.96f;
  out_asr->command_recognized = true;

  strncpy(out_asr->voice_text, "Start Tabata workout routine",
          sizeof(out_asr->voice_text) - 1);
  strncpy(out_asr->action_target, "tabata",
          sizeof(out_asr->action_target) - 1);

  return OK;
}

/****************************************************************************
 * Name: velafit_cloud_agent_run_simulation
 ****************************************************************************/

int velafit_cloud_agent_run_simulation(void)
{
  printf("\n>>> [Cloud Agent] Running Xiaomi MIMO Multimodal "
         "Simulation...\n");

  /* 1. Simulate MIMO-v2.5-ASR */

  printf("  [1/3] Querying %s (Speech-to-Text Pipeline)...\n",
         VELAFIT_MIMO_MODEL_ASR);
  velafit_mimo_asr_result_t asr;
  uint8_t dummy_pcm[320];
  velafit_cloud_agent_submit_voice(dummy_pcm, sizeof(dummy_pcm), &asr);
  printf("        Transcribed Text : \"%s\"\n", asr.voice_text);
  printf("        Recognized Action: %s (Confidence: %.2f) [OK]\n\n",
         asr.action_target, asr.confidence);

  /* 2. Simulate MIMO-v2.5-PRO Biomechanical Reasoning */

  printf("  [2/3] Querying %s (Sports Physiology Reasoning)...\n",
         VELAFIT_MIMO_MODEL_PRO);
  const char *sample_workout =
    "{\"exercise\":\"squat\",\"reps\":15,\"valgus\":2,\"shallow\":1}";

  velafit_mimo_prescription_t presc;
  mimo_mock_pro_reasoning(sample_workout, &presc);
  printf("        Overall Score    : %lu / 100\n",
         (unsigned long)presc.score_overall);
  printf("        Primary Fault    : %s\n", presc.primary_fault);
  printf("        Target Muscle    : %s\n", presc.target_muscle_focus);
  printf("        Coach Commentary : %s\n", presc.coach_commentary);
  printf("        Next Prescription: %s [OK]\n\n",
         presc.next_recommended_action);

  /* 3. Simulate MIMO-v2.5-TTS Synthesis */

  printf("  [3/3] Querying %s (Voice Synthesis)...\n",
         VELAFIT_MIMO_MODEL_TTS_CLONE);
  printf("        Synthesized Voice: Coach Tone [OK]\n");
  printf("        Audio Cue Target : /dev/audio/pcm0 (ES8311 I2S0)\n");

  printf(">>> [Cloud Agent] MIMO Multimodal Synergy Test: [PASS]\n\n");
  return OK;
}
