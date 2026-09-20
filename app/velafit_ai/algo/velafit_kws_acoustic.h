/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_KWS_ACOUSTIC_H
#define VELAFIT_KWS_ACOUSTIC_H
#include <stddef.h>
#include <stdint.h>
#define VF_KWS_DIM 13
#define VF_KWS_MAX_FRAMES 250
struct vf_kws_frontend;
struct vf_kws_matcher;
struct vf_kws_frontend *vf_kws_frontend_create(void);
void vf_kws_frontend_free(struct vf_kws_frontend *ctx);
/* One mono44100 sample; returns1 when a 10ms feature frame is ready. */
int vf_kws_frontend_sample(struct vf_kws_frontend *ctx, int16_t sample,
                           float *out, float *rms);
struct vf_kws_matcher *vf_kws_matcher_create(const float *model, size_t frames);
void vf_kws_matcher_free(struct vf_kws_matcher *ctx);
/* Experimental bounded alignment and fixed energy gate. Duration counts
 * retained feature frames, NOT wall-clock latency. Quiet 800ms / span 4s resets.
 */
struct vf_kws_matcher *vf_kws_matcher_create_bounded(const float *model, size_t frames);
float vf_kws_matcher_feed_gated(struct vf_kws_matcher *ctx, const float *features,
                               float rms, unsigned int *duration_frames);
/* Streaming subsequence DTW distance; lower is better, NOT a probability. */
float vf_kws_matcher_feed(struct vf_kws_matcher *ctx, const float *features,
                          unsigned int *duration_frames);
#endif
