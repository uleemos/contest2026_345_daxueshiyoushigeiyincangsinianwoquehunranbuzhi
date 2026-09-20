/* SPDX-License-Identifier: Apache-2.0 */
/* Real pthread mailbox; deliberately slow FAKE inference, no model/board. */
#define _POSIX_C_SOURCE 200809L
#include "velafit_pose_worker.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static void delay(unsigned ms)
{struct timespec t={ms/1000,(ms%1000)*1000000};nanosleep(&t,NULL);}
static int infer(void *ctx,const uint8_t *rgb,pose_frame_t *pose)
{
  (void)ctx;
  uint8_t first=rgb[0];
  delay(100);
  for(unsigned i=0;i<192*192*3;i++) assert(rgb[i]==first);
  pose->valid=true;pose->kpts[0].x=first;
  return first==255 ? -EIO : first==254 ? -ENODATA : 0;
}
int main(void)
{
  static uint8_t image[192*192*3];
  struct vf_pose_worker *s=vf_pose_worker_start(infer,NULL);
  assert(s);
  for(unsigned i=1;i<=30;i++)
    {
      memset(image,i,sizeof(image));
      int ret=vf_pose_worker_publish(s,image,i);
      assert(ret==0 || ret==-EBUSY);
      delay(2); /* Foreground keeps progressing while inference is blocked. */
    }
  delay(350);
  pose_frame_t pose;
  struct vf_pose_worker_stats stats={0};
  assert(vf_pose_worker_latest(s,30,1000,&pose,&stats)==0);
  assert(pose.kpts[0].x>=28 && stats.replaced>20 && stats.completed<5);
  assert(vf_pose_worker_latest(s,2000,1000,&pose,NULL)==-ESTALE);
  memset(image,255,sizeof(image));
  assert(vf_pose_worker_publish(s,image,40)==0);
  delay(150);
  assert(vf_pose_worker_latest(s,40,1000,&pose,&stats)==-EAGAIN && stats.errors==1);
  memset(image,254,sizeof(image));
  assert(vf_pose_worker_publish(s,image,41)==0);
  delay(150);
  assert(vf_pose_worker_latest(s,41,1000,&pose,&stats)==-EAGAIN);
  assert(stats.errors==1 && stats.empty==1);
  vf_pose_worker_stop(s);
  printf("HOST worker: slow FAKE inference, bounded latest mailbox, immutable buffers, stale/error/stop PASS\n");
  return 0;
}
