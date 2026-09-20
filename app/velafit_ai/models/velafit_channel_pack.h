/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_CHANNEL_PACK_H
#define VELAFIT_CHANNEL_PACK_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Caller validates offset+count<=channels and destination capacities. */
static inline void vf_pack_channels(int8_t *dst, const int8_t *src,
    size_t positions, size_t channels, size_t offset, size_t count)
{
  for (size_t i = 0; i < positions; i++)
    memcpy(dst + i * count, src + i * channels + offset, count);
}
static inline void vf_scatter_channels(int8_t *dst, const int8_t *src,
    size_t positions, size_t channels, size_t offset, size_t count)
{
  for (size_t i = 0; i < positions; i++)
    memcpy(dst + i * channels + offset, src + i * count, count);
}
#endif
