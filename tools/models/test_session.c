/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "velafit_session.h"
int main(void)
{
  velafit_session_t s;
  velafit_session_init(&s);
  assert(velafit_session_event(&s,VELAFIT_VOICE_PAUSE,0)==-EAGAIN);
  assert(velafit_session_event(&s,VELAFIT_VOICE_WAKE,0)==0 && s.awake);
  assert(s.state==VF_BODY_CHECK);
  assert(velafit_session_event(&s,VELAFIT_VOICE_WAKE,0)==-EALREADY);
  assert(velafit_session_body_ready(&s,0,750)==0 && s.state==VF_READY);
  assert(velafit_session_tick(&s,749)==0 && s.state==VF_READY);
  assert(velafit_session_tick(&s,750)==0 && s.state==VF_COUNTDOWN);
  assert(velafit_session_tick(&s,3749)==0 && s.state==VF_COUNTDOWN);
  assert(velafit_session_tick(&s,3750)==0 && s.state==VF_RUNNING);
  assert(velafit_session_finish(&s,4000)==0 && s.state==VF_FINISHED);
  velafit_session_init(&s);
  assert(velafit_session_event(&s,VELAFIT_VOICE_START,0)==0);
  assert(s.state==VF_COUNTDOWN && s.generation==1);
  assert(velafit_session_event(&s,VELAFIT_VOICE_START,100)==-EALREADY);
  assert(velafit_session_tick(&s,2999)==0 && s.state==VF_COUNTDOWN);
  assert(velafit_session_tick(&s,3500)==0 && s.active_ms==500);
  assert(velafit_session_event(&s,VELAFIT_VOICE_PAUSE,4000)==0);
  assert(s.active_ms==1000 && s.state==VF_PAUSED);
  assert(velafit_session_event(&s,VELAFIT_VOICE_PAUSE,5000)==0);
  assert(velafit_session_tick(&s,60000)==0 && s.active_ms==1000);
  assert(velafit_session_event(&s,VELAFIT_VOICE_RESUME,61000)==0);
  assert(velafit_session_event(&s,VELAFIT_VOICE_RESUME,62000)==0);
  assert(velafit_session_event(&s,VELAFIT_VOICE_STOP,63000)==0);
  assert(s.state==VF_FINISHED && s.active_ms==3000);
  assert(velafit_session_event(&s,VELAFIT_VOICE_STOP,64000)==0);
  assert(s.active_ms==3000);
  assert(velafit_session_tick(&s,63000)==-ERANGE);
  assert(velafit_session_event(&s,99,65000)==-EINVAL);
  assert(velafit_session_event(&s,VELAFIT_VOICE_START,65000)==0);
  assert(s.active_ms==0 && s.generation==2);
  assert(velafit_session_event(&s,VELAFIT_VOICE_STOP,66000)==0);
  assert(s.active_ms==0);
  puts("session event injection: countdown/pause/time/duplicates/restart PASS");
  return 0;
}
