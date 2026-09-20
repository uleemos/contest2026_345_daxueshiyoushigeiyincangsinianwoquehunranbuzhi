/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_COACH_SERVICE_H
#define VELAFIT_COACH_SERVICE_H

#include <stddef.h>
#include <stdbool.h>

struct velafit_workout_summary_s
{
  unsigned int target_reps;
  unsigned int completed_reps;
  unsigned int form_warning_reps;
  unsigned int shallow_warning_reps;
  unsigned int knee_caving_warning_reps;
  unsigned int trunk_lean_warning_reps;
  unsigned int duration_sec;
  unsigned int body_lost_count;
};

struct velafit_coach_result_s
{
  int http_status;
  unsigned long advice_latency_ms;
  unsigned long tts_first_pcm_ms;
  unsigned long tts_elapsed_ms;
  size_t advice_bytes;
  size_t pcm_bytes;
  char advice[512];
};

int velafit_coach_run(const char *api_key,
                      const struct velafit_workout_summary_s *summary,
                      struct velafit_coach_result_s *result);
int velafit_coach_runtime_set_key(const char *api_key);
int velafit_coach_runtime_configure(const char *ssid, const char *password,
                                    const char *api_key);
bool velafit_coach_runtime_ready(void);
int velafit_coach_run_runtime(
                      const struct velafit_workout_summary_s *summary,
                      struct velafit_coach_result_s *result);

#endif
