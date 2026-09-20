/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_SESSION_H
#define VELAFIT_SESSION_H
#include <stdbool.h>
#include <stdint.h>
#include "velafit_voice_command.h"

enum velafit_session_state { VF_IDLE, VF_WAKEUP, VF_BODY_CHECK, VF_READY,
                             VF_COUNTDOWN, VF_RUNNING, VF_PAUSED,
                             VF_FINISHED };
typedef struct
{
  enum velafit_session_state state;
  uint64_t last_ms;
  uint64_t active_ms;
  uint64_t countdown_end_ms;
  uint64_t ready_end_ms;
  uint32_t generation;
  bool clock_valid;
  bool awake;
} velafit_session_t;

void velafit_session_init(velafit_session_t *s);
/* One entry for UI, parsed ASR and test injection. Origin must be logged by
 * caller; this API itself does not recognize speech or implement KWS. */
int velafit_session_event(velafit_session_t *s,
                          enum velafit_voice_command event, uint64_t now_ms);
int velafit_session_tick(velafit_session_t *s, uint64_t now_ms);
int velafit_session_body_ready(velafit_session_t *s, uint64_t now_ms,
                               uint32_t ready_hold_ms);
int velafit_session_finish(velafit_session_t *s, uint64_t now_ms);
const char *velafit_session_state_name(enum velafit_session_state state);
#endif
