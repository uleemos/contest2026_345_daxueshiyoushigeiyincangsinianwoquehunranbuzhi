/* SPDX-License-Identifier: Apache-2.0 */
#include "velafit_pose_worker.h"
#include <pthread.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define RGB_BYTES (192*192*3)
struct vf_pose_worker
{
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t ready;
  bool stop,pending,valid;
  uint64_t pending_ms,result_ms;
  uint8_t next[RGB_BYTES],work[RGB_BYTES];
  pose_frame_t result;
  struct vf_pose_worker_stats stats;
  vf_pose_inference infer;
  void *ctx;
};
static uint64_t clock_ms(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC,&t);
  return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void *run(void *arg)
{
  struct vf_pose_worker *s=arg;
  pthread_mutex_lock(&s->lock);
  for(;;)
    {
      while(!s->stop && !s->pending) pthread_cond_wait(&s->ready,&s->lock);
      if(s->stop) break;
      memcpy(s->work,s->next,RGB_BYTES);
      uint64_t captured=s->pending_ms;
      s->pending=false;
      pthread_mutex_unlock(&s->lock);
      pose_frame_t pose={0};
      uint64_t start=clock_ms();
      int ret=s->infer(s->ctx,s->work,&pose);
      uint64_t elapsed=clock_ms()-start;
      pthread_mutex_lock(&s->lock);
      ++s->stats.completed;
      if(elapsed>s->stats.max_inference_ms) s->stats.max_inference_ms=elapsed;
      if(ret==-ENODATA) ++s->stats.empty;
      else if(ret) ++s->stats.errors;
      s->valid=ret==0;
      if(!ret)
        {
          pose.timestamp_ms=(uint32_t)captured;
          s->result=pose;s->result_ms=captured;
        }
    }
  pthread_mutex_unlock(&s->lock);
  return NULL;
}
struct vf_pose_worker *vf_pose_worker_start(vf_pose_inference infer,void *ctx)
{
  if(!infer) return NULL;
  struct vf_pose_worker *s=calloc(1,sizeof(*s));
  if(!s) return NULL;
  s->infer=infer;s->ctx=ctx;
  if(pthread_mutex_init(&s->lock,NULL)) {free(s);return NULL;}
  if(pthread_cond_init(&s->ready,NULL))
    {pthread_mutex_destroy(&s->lock);free(s);return NULL;}
  pthread_attr_t attr;
  int ret=pthread_attr_init(&attr);
  if(!ret)
    {
      ret=pthread_attr_setstacksize(&attr,32768);
#ifdef __NuttX__
      /* Camera/UI remains the foreground task. Equal-priority compute was
       * measured to halve preview throughput; run inference below its caller.
       * It still runs whenever capture blocks, without blocking camera work. */
      struct sched_param priority;
      int policy;
      if(!ret) ret=pthread_getschedparam(pthread_self(),&policy,&priority);
      if(!ret)
        {
          int minimum=sched_get_priority_min(policy);
          if(priority.sched_priority>minimum)
            {
              priority.sched_priority-=10;
              if(priority.sched_priority<minimum) priority.sched_priority=minimum;
              ret=pthread_attr_setinheritsched(&attr,PTHREAD_EXPLICIT_SCHED);
              if(!ret) ret=pthread_attr_setschedpolicy(&attr,policy);
              if(!ret) ret=pthread_attr_setschedparam(&attr,&priority);
            }
        }
#endif
      if(!ret) ret=pthread_create(&s->thread,&attr,run,s);
      pthread_attr_destroy(&attr);
    }
  if(ret)
    {pthread_cond_destroy(&s->ready);pthread_mutex_destroy(&s->lock);free(s);return NULL;}
  return s;
}
int vf_pose_worker_publish(struct vf_pose_worker *s,const uint8_t *rgb,uint64_t ms)
{
  if(!s || !rgb) return -EINVAL;
  /* No wait for inference: lock covers only mailbox copies and metadata. */
  int ret=pthread_mutex_trylock(&s->lock);
  if(ret) return -ret;
  if(s->stop) {pthread_mutex_unlock(&s->lock);return -ESHUTDOWN;}
  if(s->pending && ms<s->pending_ms)
    {pthread_mutex_unlock(&s->lock);return -ESTALE;}
  ++s->stats.submitted;
  if(s->pending) ++s->stats.replaced;
  memcpy(s->next,rgb,RGB_BYTES);s->pending_ms=ms;s->pending=true;
  pthread_cond_signal(&s->ready);
  pthread_mutex_unlock(&s->lock);
  return 0;
}
int vf_pose_worker_latest(struct vf_pose_worker *s,uint64_t now,uint32_t age,
                          pose_frame_t *out,struct vf_pose_worker_stats *stats)
{
  if(!s || !out) return -EINVAL;
  int ret=pthread_mutex_trylock(&s->lock);
  if(ret) return -ret;
  if(stats) *stats=s->stats;
  ret=0;
  if(!s->valid) ret=-EAGAIN;
  else if(now<s->result_ms || now-s->result_ms>age) ret=-ESTALE;
  else *out=s->result;
  pthread_mutex_unlock(&s->lock);
  return ret;
}
void vf_pose_worker_stop_with_stats(struct vf_pose_worker *s,
                                    struct vf_pose_worker_stats *stats)
{
  if(!s) return;
  pthread_mutex_lock(&s->lock);s->stop=true;s->pending=false;
  pthread_cond_signal(&s->ready);pthread_mutex_unlock(&s->lock);
  pthread_join(s->thread,NULL);
  if(stats) *stats=s->stats;
  pthread_cond_destroy(&s->ready);pthread_mutex_destroy(&s->lock);free(s);
}
void vf_pose_worker_stop(struct vf_pose_worker *s)
{
  vf_pose_worker_stop_with_stats(s,NULL);
}
