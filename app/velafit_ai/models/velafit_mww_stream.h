/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_MWW_STREAM_H
#define VELAFIT_MWW_STREAM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Single-owner continuous 44.1kHz mono PCM16 stream. Model storage must outlive
 * the stream. No microphone access, playback, or wake event dispatch here.
 * Reset on capture discontinuity; never carry feature/model state across gaps.
 */
struct vf_mww_stream;
typedef void (*vf_mww_score)(void *ctx, uint8_t raw_score, uint32_t feature_frame);
struct vf_mww_stream *vf_mww_stream_create(const uint8_t *model, size_t bytes);
int vf_mww_stream_reset(struct vf_mww_stream *stream);
int vf_mww_stream_process(struct vf_mww_stream *stream, const int16_t *pcm,
                          size_t samples, vf_mww_score score, void *ctx);
size_t vf_mww_stream_arena(const struct vf_mww_stream *stream);
void vf_mww_stream_destroy(struct vf_mww_stream *stream);
#ifdef __cplusplus
}
#endif
#endif
