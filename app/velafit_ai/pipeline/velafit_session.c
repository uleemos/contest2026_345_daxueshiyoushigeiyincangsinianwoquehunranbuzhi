/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include "velafit_session.h"

const char *velafit_session_state_name(enum velafit_session_state state)
{
  static const char *names[] = {"IDLE", "WAKEUP", "BODY_CHECK", "READY",
                                "COUNTDOWN", "RUNNING", "PAUSED", "FINISHED"};
  return (unsigned)state < sizeof(names) / sizeof(names[0]) ?
         names[state] : "INVALID";
}

void velafit_session_init(velafit_session_t *s)
{
  if (s) memset(s, 0, sizeof(*s));
}

int velafit_session_tick(velafit_session_t *s, uint64_t now)
{
  if (!s) return -EINVAL;
  if (s->clock_valid && now < s->last_ms) return -ERANGE;
  if (s->state == VF_RUNNING)
    s->active_ms += now - s->last_ms;
  if (s->state == VF_READY && now >= s->ready_end_ms)
    {
      s->state = VF_COUNTDOWN;
      s->countdown_end_ms = s->ready_end_ms + 3000;
    }
  if (s->state == VF_COUNTDOWN && now >= s->countdown_end_ms)
    {
      s->state = VF_RUNNING;
      s->active_ms += now - s->countdown_end_ms;
    }
  s->clock_valid = true;
  s->last_ms = now;
  return 0;
}

int velafit_session_event(velafit_session_t *s,
                          enum velafit_voice_command event, uint64_t now)
{
  if (!s || event <= VELAFIT_VOICE_UNKNOWN || event > VELAFIT_VOICE_STOP)
    return -EINVAL;
  int ret = velafit_session_tick(s, now);
  if (ret) return ret;
  switch (event)
    {
      case VELAFIT_VOICE_WAKE:
        if (s->state != VF_IDLE && s->state != VF_FINISHED) return -EALREADY;
        s->awake = true;
        s->state = VF_WAKEUP;
        s->generation++;
        /* WAKEUP is an observable event boundary. Camera positioning owns
         * the next state, so repeated wake events cannot restart it. */
        s->state = VF_BODY_CHECK;
        return 0;
      case VELAFIT_VOICE_START:
        if (s->state != VF_IDLE && s->state != VF_FINISHED) return -EALREADY;
        if (now > UINT64_MAX - 3000) return -ERANGE;
        s->state = VF_COUNTDOWN;
        s->active_ms = 0;
        s->countdown_end_ms = now + 3000;
        s->generation++;
        return 0;
      case VELAFIT_VOICE_PAUSE:
        if (s->state == VF_PAUSED) return 0;
        if (s->state != VF_RUNNING) return -EAGAIN;
        s->state = VF_PAUSED;
        return 0;
      case VELAFIT_VOICE_RESUME:
        if (s->state == VF_RUNNING) return 0;
        if (s->state != VF_PAUSED) return -EAGAIN;
        s->state = VF_RUNNING;
        return 0;
      case VELAFIT_VOICE_STOP:
        if (s->state == VF_IDLE) return -EAGAIN;
        s->state = VF_FINISHED;
        return 0;
      default: return -EINVAL;
    }
}

int velafit_session_body_ready(velafit_session_t *s, uint64_t now,
                               uint32_t ready_hold_ms)
{
  if (!s || ready_hold_ms > 5000 || now > UINT64_MAX - ready_hold_ms)
    return -EINVAL;
  int ret = velafit_session_tick(s, now);
  if (ret) return ret;
  if (s->state != VF_BODY_CHECK) return -EALREADY;
  s->state = VF_READY;
  s->ready_end_ms = now + ready_hold_ms;
  s->active_ms = 0;
  return 0;
}

int velafit_session_finish(velafit_session_t *s, uint64_t now)
{
  if (!s) return -EINVAL;
  int ret = velafit_session_tick(s, now);
  if (ret) return ret;
  if (s->state == VF_FINISHED) return 0;
  if (s->state != VF_RUNNING) return -EAGAIN;
  s->state = VF_FINISHED;
  return 0;
}
