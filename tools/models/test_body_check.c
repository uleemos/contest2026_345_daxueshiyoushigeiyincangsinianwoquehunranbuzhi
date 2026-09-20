/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "velafit_body_check.h"

static pose_frame_t standing(float top, float bottom, float width)
{
  pose_frame_t pose;
  memset(&pose, 0, sizeof(pose));
  pose.valid = true;
  const int ids[8] = {KPT_LEFT_SHOULDER,KPT_RIGHT_SHOULDER,
    KPT_LEFT_HIP,KPT_RIGHT_HIP,KPT_LEFT_KNEE,KPT_RIGHT_KNEE,
    KPT_LEFT_ANKLE,KPT_RIGHT_ANKLE};
  const float levels[4] = {0.0f,.36f,.70f,1.0f};
  for (int level=0;level<4;level++)
    for (int side=0;side<2;side++)
      {
        kpt_2d_t *p=&pose.kpts[ids[level*2+side]];
        p->x=.5f+(side ? width : -width);
        p->y=top+(bottom-top)*levels[level];
        p->score=.9f;
      }
  return pose;
}

int main(void)
{
  velafit_body_check_config_t config;
  velafit_body_check_t check;
  velafit_body_check_result_t result;
  velafit_body_check_default_config(&config);
  assert(config.minimum_body_height==.375f);
  assert(config.maximum_body_height==.54f);
  config.frame_margin=.05f;
  config.minimum_body_height=.35f;
  config.maximum_body_height=.80f;
  config.stable_frames_required=5;
  velafit_body_check_init(&check,&config);

  pose_frame_t good=standing(.18f,.78f,.10f);
  for(int i=0;i<4;i++)
    assert(velafit_body_check_update(&check,&good,&result)==
           BODY_CHECK_STABILIZING);
  assert(velafit_body_check_update(&check,&good,&result)==BODY_CHECK_READY);
  assert(result.orientation==BODY_ORIENTATION_UPRIGHT);
  assert(result.confident_points==8 && result.strong_points==8 &&
         result.stable_frames==5);

  pose_frame_t low=good;
  low.kpts[KPT_RIGHT_SHOULDER].score=.19f;
  assert(velafit_body_check_update(&check,&low,&result)==
         BODY_CHECK_LOW_CONFIDENCE);
  assert(result.stable_frames==0);

  pose_frame_t quantized_jitter=good;
  quantized_jitter.kpts[KPT_RIGHT_SHOULDER].score=.29f;
  quantized_jitter.kpts[KPT_RIGHT_ANKLE].score=.21f;
  for(int i=0;i<4;i++)
    assert(velafit_body_check_update(&check,&quantized_jitter,&result)==
           BODY_CHECK_STABILIZING);
  assert(velafit_body_check_update(&check,&quantized_jitter,&result)==
         BODY_CHECK_READY);
  assert(result.confident_points==8 && result.strong_points==6);

  pose_frame_t too_many_weak=quantized_jitter;
  too_many_weak.kpts[KPT_LEFT_ANKLE].score=.29f;
  assert(velafit_body_check_update(&check,&too_many_weak,&result)==
         BODY_CHECK_LOW_CONFIDENCE);

  pose_frame_t cropped=good;
  cropped.kpts[KPT_LEFT_KNEE].score=.19f;
  cropped.kpts[KPT_RIGHT_KNEE].score=.18f;
  cropped.kpts[KPT_LEFT_ANKLE].score=.12f;
  cropped.kpts[KPT_RIGHT_ANKLE].score=.10f;
  cropped.valid=false; /* TinyPose requires six confident points globally. */
  assert(velafit_body_check_update(&check,&cropped,&result)==
         BODY_CHECK_NOT_FULLY_VISIBLE);

  pose_frame_t edge=good;
  edge.kpts[KPT_LEFT_ANKLE].y=.98f;
  assert(velafit_body_check_update(&check,&edge,&result)==
         BODY_CHECK_NOT_FULLY_VISIBLE);
  pose_frame_t asymmetric=good;
  asymmetric.kpts[KPT_RIGHT_KNEE].y+=.12f;
  assert(velafit_body_check_update(&check,&asymmetric,&result)==
         BODY_CHECK_NOT_FULLY_VISIBLE);
  pose_frame_t hip_bias=good;
  hip_bias.kpts[KPT_RIGHT_HIP].y+=.12f;
  assert(velafit_body_check_update(&check,&hip_bias,&result)==
         BODY_CHECK_STABILIZING);

  pose_frame_t close=standing(.08f,.90f,.10f);
  assert(velafit_body_check_update(&check,&close,&result)==
         BODY_CHECK_TOO_CLOSE);
  pose_frame_t far=standing(.35f,.62f,.05f);
  assert(velafit_body_check_update(&check,&far,&result)==BODY_CHECK_TOO_FAR);

  pose_frame_t rotated=good;
  const int pairs[4][2]={{KPT_LEFT_SHOULDER,KPT_RIGHT_SHOULDER},
    {KPT_LEFT_HIP,KPT_RIGHT_HIP},{KPT_LEFT_KNEE,KPT_RIGHT_KNEE},
    {KPT_LEFT_ANKLE,KPT_RIGHT_ANKLE}};
  for(int level=0;level<4;level++)
    for(int side=0;side<2;side++)
      {
        rotated.kpts[pairs[level][side]].x=.2f+level*.2f;
        rotated.kpts[pairs[level][side]].y=.5f+(side ? .08f : -.08f);
      }
  assert(velafit_body_check_update(&check,&rotated,&result)==
         BODY_CHECK_ORIENTATION_INVALID);
  assert(result.orientation==BODY_ORIENTATION_ROTATED_OR_INVALID);

  puts("BODY_CHECK confidence/orientation/boundary/distance/stability PASS");
  return 0;
}
