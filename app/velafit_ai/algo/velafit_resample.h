/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_RESAMPLE_H
#define VELAFIT_RESAMPLE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Causal 44.1 -> 16 kHz FIR. Reset at stream discontinuity.
 * Output starts at input index zero with 47.5 input-sample group delay.
 * No artificial end padding; caller supplies at least count output slots.
 */
struct vf_resample;
struct vf_resample *vf_resample_create(void);
void vf_resample_destroy(struct vf_resample *s);
int vf_resample_process(struct vf_resample *s, const int16_t *input,
                        size_t count, int16_t *output, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
