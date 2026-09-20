/* SPDX-License-Identifier: Apache-2.0 */
/* Real pipeline/FSM/filter/renderer. Audio, framebuffer and persistence are
 * explicit HOST test boundaries, never presented as device validation. */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "velafit_pipeline.h"
#include "sample_pose_frames.h"

static int saves;
static int storage_result;
int velafit_audio_cue_play(velafit_audio_cue_type_t cue) { (void)cue; return 0; }
int velafit_storage_save_session(const velafit_session_record_t *rec,const char *json)
{
  assert(rec && json && strstr(json,"COMPLETED"));
  saves++;
  return storage_result;
}
int __wrap_velafit_render_to_fb0(const velafit_canvas_t *c) { (void)c; return 0; }

static void frame(velafit_pipeline_t *p,const pose_frame_t *sample,uint32_t t)
{
  pose_frame_t pose=*sample;
  pose.timestamp_ms=t;
  assert(velafit_pipeline_tick(p,t)==0);
  assert(velafit_pipeline_step_pose(p,&pose)==0);
}

int main(void)
{
  velafit_pipeline_t p;
  assert(velafit_pipeline_init(&p,"squat",320,240)==0);
  assert(velafit_pipeline_event(&p,VELAFIT_VOICE_START,0)==0);
  pose_frame_t pose=g_sample_pose_standing;
  pose.timestamp_ms=1000;
  assert(velafit_pipeline_step_pose(&p,&pose)==-EAGAIN);
  for (unsigned t=3000;t<=3400;t+=100) frame(&p,&g_sample_pose_standing,t);
  for (unsigned t=3500;t<=4400;t+=100) frame(&p,&g_sample_pose_squat_deep,t);
  for (unsigned t=4500;t<=5500;t+=100) frame(&p,&g_sample_pose_standing,t);
  assert(p.stats.total_reps==1);
  char premature[2048];
  assert(velafit_pipeline_finish(&p,premature,sizeof(premature))==-EAGAIN);
  assert(velafit_pipeline_event(&p,VELAFIT_VOICE_START,5500)==-EALREADY);
  assert(p.stats.total_reps==1 && saves==0);
  assert(velafit_pipeline_event(&p,VELAFIT_VOICE_PAUSE,5600)==0);
  assert(velafit_pipeline_tick(&p,16000)==0 && p.stats.duration_sec==2);
  assert(velafit_pipeline_event(&p,VELAFIT_VOICE_RESUME,16000)==0);
  pose.timestamp_ms=5500;
  assert(velafit_pipeline_step_pose(&p,&pose)==-ESTALE);
  for (unsigned t=16100;t<=16500;t+=100) frame(&p,&g_sample_pose_standing,t);
  pose=g_sample_pose_squat_deep;pose.timestamp_ms=16600;
  pose.kpts[KPT_LEFT_KNEE].x=NAN;
  assert(velafit_pipeline_tick(&p,16600)==0);
  assert(velafit_pipeline_step_pose(&p,&pose)==0);
  for (unsigned t=16700;t<=17000;t+=100) frame(&p,&g_sample_pose_standing,t);
  for (unsigned t=17100;t<=18000;t+=100) frame(&p,&g_sample_pose_squat_deep,t);
  for (unsigned t=18100;t<=19000;t+=100) frame(&p,&g_sample_pose_standing,t);
  assert(p.stats.total_reps==2); /* NaN must not poison later observations. */
  assert(velafit_pipeline_event(&p,VELAFIT_VOICE_STOP,19100)==0);
  assert(p.stats.duration_sec==5);
  char json[2048];
  assert(velafit_pipeline_finish(&p,json,8)==-ENOSPC && saves==0);
  storage_result=-EIO;
  assert(velafit_pipeline_finish(&p,json,sizeof(json))==-EIO);
  storage_result=0;
  assert(velafit_pipeline_finish(&p,json,sizeof(json))==0);
  assert(strstr(json,"\"duration_sec\": 5") && strstr(json,"\"total_reps\": 2"));
  assert(saves==2); /* Failed attempt plus successful retry. */
  assert(velafit_pipeline_finish(&p,json,sizeof(json))==0 && saves==2);
  assert(velafit_pipeline_event(&p,VELAFIT_VOICE_START,20000)==0);
  assert(p.stats.total_reps==0 && p.stats.duration_sec==0);
  velafit_pipeline_deinit(&p);
  puts("HOST pipeline: count/pause/stale/NaN recovery/summary/errors/restart PASS");
  return 0;
}
