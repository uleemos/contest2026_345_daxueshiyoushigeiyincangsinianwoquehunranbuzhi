/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_ASR_BODY_H
#define C6_ASR_BODY_H
#include <stddef.h>
#include <stdint.h>
/* Caller owns returned JSON; contains private audio and must erase on release. */
int c6_asr_body(const int16_t *pcm, size_t frames, unsigned int rate,
                char **body, size_t *length);
#endif
