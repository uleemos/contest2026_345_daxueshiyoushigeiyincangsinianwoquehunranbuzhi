/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_POSE_WORKER_H
#define VELAFIT_POSE_WORKER_H
#include <stdint.h>
#include "velafit_types.h"
struct vf_pose_worker;
typedef int (*vf_pose_inference)(void *,const uint8_t *,pose_frame_t *);
struct vf_pose_worker_stats
{
  uint32_t submitted, replaced, completed, errors, empty;
  uint64_t max_inference_ms;
};
/* At most one pending RGB192 frame and one running frame; publication copies
 * data before returning. Inference callback must return (no forced cancellation).
 * Only this worker may own the model. stop joins before freeing any buffers.
 */
struct vf_pose_worker *vf_pose_worker_start(vf_pose_inference infer,void *ctx);
int vf_pose_worker_publish(struct vf_pose_worker *,const uint8_t *,uint64_t captured_ms);
int vf_pose_worker_latest(struct vf_pose_worker *,uint64_t now_ms,uint32_t max_age_ms,
                          pose_frame_t *,struct vf_pose_worker_stats *);
void vf_pose_worker_stop(struct vf_pose_worker *);
void vf_pose_worker_stop_with_stats(struct vf_pose_worker *,
                                    struct vf_pose_worker_stats *);
#endif
