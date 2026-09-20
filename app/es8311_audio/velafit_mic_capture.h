/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_MIC_CAPTURE_H
#define VELAFIT_MIC_CAPTURE_H
#include <stdint.h>
#include <stddef.h>
/* Diagnostic bridge: exclusive audio ownership required; no file/network IO. */
int velafit_mic_capture_pcm(int16_t *samples, unsigned int seconds);
const int16_t *velafit_audio_fixture(size_t *frames);
#endif
