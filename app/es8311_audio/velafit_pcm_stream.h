/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_PCM_STREAM_H
#define VELAFIT_PCM_STREAM_H

#include <stddef.h>
#include <stdint.h>

struct velafit_pcm_stream_s;

struct velafit_pcm_stream_stats_s
{
  size_t input_bytes;
  size_t played_frames;
  unsigned int underflows;
};

/* The cloud format is signed PCM16LE mono.  The player duplicates each
 * sample into the codec's interleaved L/R frame and never stores a file. */

int velafit_pcm_stream_start(struct velafit_pcm_stream_s **stream,
                             unsigned int sample_rate);
int velafit_pcm_stream_write(struct velafit_pcm_stream_s *stream,
                             const uint8_t *pcm, size_t bytes);
int velafit_pcm_stream_finish(struct velafit_pcm_stream_s *stream,
                              struct velafit_pcm_stream_stats_s *stats);
void velafit_pcm_stream_abort(struct velafit_pcm_stream_s *stream);

#endif
