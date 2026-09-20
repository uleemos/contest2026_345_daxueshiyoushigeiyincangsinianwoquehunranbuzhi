/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "squat_fsm.h"

static pose_frame_t pose(float angle)
{
  pose_frame_t p;
  memset(&p, 0, sizeof(p));
  p.valid = true;
  for (int i = 0; i < 17; i++) p.kpts[i].score = 1;
  for (int i = 0; i < 2; i++)
    {
      float x = .3f + i * .4f;
      float a = angle * 3.14159265359f / 180;
      p.kpts[KPT_LEFT_KNEE+i].x = x;
      p.kpts[KPT_LEFT_KNEE+i].y = .5f;
      p.kpts[KPT_LEFT_HIP+i].x = x;
      p.kpts[KPT_LEFT_HIP+i].y = .3f;
      p.kpts[KPT_LEFT_SHOULDER+i].x = x;
      p.kpts[KPT_LEFT_SHOULDER+i].y = .1f;
      p.kpts[KPT_LEFT_ANKLE+i].x = x + .2f * sinf(a);
      p.kpts[KPT_LEFT_ANKLE+i].y = .5f - .2f * cosf(a);
    }
  return p;
}

static void step(squat_fsm_t *s, float angle, uint32_t t)
{
  pose_frame_t p = pose(angle);
  squat_fsm_update(s, &p, t);
}

int main(void)
{
  squat_fsm_t s;
  const unsigned rates[] = {5, 10, 15, 30};
  for (unsigned r = 0; r < 4; r++)
    {
      squat_fsm_init(&s);
      unsigned dt = 1000 / rates[r];
      for (unsigned t = 0; t <= 3000; t += dt)
        {
          float a = t < 1000 ? 180 - .09f*t :
                    t < 2000 ? 90 : fminf(180, 90 + .09f*(t-2000));
          step(&s, a, t);
        }
      assert(s.total_reps == 1 && s.valid_reps == 1);
      assert(s.last_quality_flags == SQUAT_QUALITY_OK);
      assert(s.state == SQUAT_STATE_STAND);
      printf("full rep %u FPS: count=1 valid=1 PASS\n", rates[r]);
    }
  squat_fsm_init(&s);
  step(&s,180,0); step(&s,135,100); step(&s,125,300);
  assert(s.state == SQUAT_STATE_DESCENDING);
  step(&s,135,500); step(&s,180,800);
  assert(s.total_reps == 1 && s.shallow_count == 1);
  assert(s.last_quality_flags == SQUAT_QUALITY_SHALLOW);
  puts("shallow rep: count=1 valid=0 hint=SHALLOW PASS");
  squat_fsm_init(&s);
  step(&s,180,0);
  for (unsigned t=100;t<3000;t+=100) step(&s,t%200?139:141,t);
  assert(s.total_reps == 0);
  squat_fsm_interrupt(&s);
  step(&s,180,3100);
  assert(s.total_reps == 0);
  puts("threshold jitter + interrupted partial rep: count=0 PASS");
  for (int fault=0;fault<5;fault++)
    {
      squat_fsm_init(&s);
      step(&s,180,0); step(&s,130,200); step(&s,90,400);
      pose_frame_t p=pose(90);
      if (fault==0) p.valid=false;
      if (fault==1) p.kpts[KPT_LEFT_KNEE].score=.1f;
      if (fault==2) p.kpts[KPT_LEFT_KNEE].x=NAN;
      if (fault==3) squat_fsm_interrupt(&s); /* pause */
      squat_fsm_update(&s,&p,fault==4?2000:600);
      step(&s,180,fault==4?2200:800);
      assert(s.total_reps==0);
    }
  puts("loss/confidence/NaN/pause/gap: no phantom rep PASS");
  pose_frame_t low_score=pose(180);
  for(int i=0;i<17;i++) low_score.kpts[i].score=.23f;
  squat_fsm_init(&s);
  squat_fsm_update(&s,&low_score,0);
  assert(!s.armed);
  squat_fsm_update_confidence(&s,&low_score,100,.20f);
  assert(s.armed);
  puts("backend-specific confidence threshold: default reject, 0.20 accept PASS");
  const squat_fsm_config_t tiny={155,150,143,146,148,.72f,45,500};
  const float tiny_angles[]={159,149,141,149,157};
  squat_fsm_init(&s);
  for(unsigned i=0;i<5;i++)
    {
      pose_frame_t p=pose(tiny_angles[i]);
      for(int k=0;k<17;k++) p.kpts[k].score=.23f;
      squat_fsm_update_config(&s,&p,i*200,.20f,&tiny);
    }
  assert(s.total_reps==1);
  puts("TinyPose calibrated angle range: one complete rep PASS");
  squat_fsm_init(&s);
  const float tiny_jitter[]={159,153,151,154,158};
  for(unsigned i=0;i<5;i++)
    {
      pose_frame_t p=pose(tiny_jitter[i]);
      squat_fsm_update_config(&s,&p,i*200,.20f,&tiny);
    }
  assert(s.total_reps==0);
  puts("TinyPose calibrated standing jitter: no phantom rep PASS");
  const float adaptive_deep_a[]={159,159,150,140,150,159};
  const float adaptive_deep_b[]={166,166,157,146,157,166};
  const float adaptive_shallow[]={166,166,158,158,158,166};
  const float adaptive_jitter[]={166,163,161,164,166};
  const float *adaptive_sets[]={adaptive_deep_a,adaptive_deep_b,
                                adaptive_shallow,adaptive_jitter};
  const unsigned adaptive_lengths[]={6,6,6,5};
  const unsigned adaptive_counts[]={1,1,1,0};
  for(unsigned set=0;set<4;set++)
    {
      squat_fsm_adaptive_t adaptive;
      squat_fsm_adaptive_init(&adaptive);
      for(unsigned i=0;i<adaptive_lengths[set];i++)
        {
          pose_frame_t p=pose(adaptive_sets[set][i]);
          for(int k=0;k<17;k++) p.kpts[k].score=.23f;
          squat_fsm_adaptive_update(&adaptive,&p,i*200,.20f);
        }
      if(adaptive.fsm.total_reps!=adaptive_counts[set])
        fprintf(stderr,"adaptive set %u got %u expected %u baseline %.1f state %d\n",
          set,adaptive.fsm.total_reps,adaptive_counts[set],
          adaptive.standing_angle,adaptive.fsm.state);
      assert(adaptive.fsm.total_reps==adaptive_counts[set]);
    }
  puts("TinyPose adaptive baseline: two offsets/deep/shallow/jitter PASS");
  const float adaptive_shifted_multi[]={166,166,150,50,100,140,160,
                                        150,140,150,160,
                                        151,138,150,160};
  squat_fsm_adaptive_t shifted;
  squat_fsm_adaptive_init(&shifted);
  for(unsigned i=0;i<sizeof(adaptive_shifted_multi)/sizeof(adaptive_shifted_multi[0]);i++)
    {
      pose_frame_t p=pose(adaptive_shifted_multi[i]);
      for(int k=0;k<17;k++) p.kpts[k].score=.23f;
      squat_fsm_adaptive_update(&shifted,&p,i*200,.20f);
    }
  assert(shifted.fsm.total_reps==3);
  puts("TinyPose adaptive baseline: severe shift plus consecutive reps PASS");
  squat_fsm_adaptive_t countdown;
  squat_fsm_adaptive_init(&countdown);
  for(unsigned i=0;i<2;i++)
    {
      pose_frame_t p=pose(166);
      squat_fsm_adaptive_update(&countdown,&p,i*200,.20f);
    }
  squat_fsm_adaptive_begin_capture(&countdown);
  const float immediate_descent[]={150,140,150,166};
  for(unsigned i=0;i<4;i++)
    {
      pose_frame_t p=pose(immediate_descent[i]);
      squat_fsm_adaptive_update(&countdown,&p,400+i*200,.20f);
    }
  assert(countdown.fsm.total_reps==1);
  puts("TinyPose countdown handoff: immediate first-frame descent PASS");
  squat_fsm_init(&s);
  step(&s,180,0); step(&s,130,200); step(&s,90,400);
  step(&s,180,400); step(&s,180,300);
  assert(s.total_reps==0);
  step(&s,130,620); step(&s,180,950); step(&s,180,1100);
  assert(s.total_reps==1);
  puts("duplicate/out-of-order/irregular timestamps: count=1 PASS");
  squat_fsm_init(&s);
  step(&s,180,0xffffff00u); step(&s,130,0xffffffc8u);
  step(&s,90,144); step(&s,130,344); step(&s,180,544);
  assert(s.total_reps==1);
  puts("timestamp wrap: count=1 PASS (synthetic poses, not human accuracy)");
  return 0;
}
