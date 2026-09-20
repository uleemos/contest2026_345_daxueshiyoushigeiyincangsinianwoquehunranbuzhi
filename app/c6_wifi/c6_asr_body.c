/* SPDX-License-Identifier: Apache-2.0 */
#include "c6_asr_body.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void put32(unsigned char *p, uint32_t n)
{
  for (unsigned int i = 0; i < 4; i++) p[i] = n >> (8 * i);
}

/* Encode a virtual WAV without allocating a second full PCM copy. */
int c6_asr_body(const int16_t *pcm, size_t frames, unsigned int rate,
                char **body, size_t *length)
{
  static const char prefix[] =
    "{\"model\":\"mimo-v2.5-asr\",\"messages\":[{\"role\":\"user\","
    "\"content\":[{\"type\":\"input_audio\",\"input_audio\":{"
    "\"data\":\"data:audio/wav;base64,";
  static const char suffix[] = "\"}}]}],\"asr_options\":{\"language\":\"auto\"}}";
  static const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  unsigned char header[44] = {0};
  if (!body || !length) return -EINVAL;
  *body = NULL;
  *length = 0;
  if (!pcm || !frames || (rate != 16000 && rate != 44100) ||
      frames > (size_t)rate * 10) return -EINVAL;
  size_t bytes = 44 + frames * 2;
  size_t encoded = ((bytes + 2) / 3) * 4;
  size_t total = sizeof(prefix) - 1 + encoded + sizeof(suffix) - 1;
  char *out = malloc(total + 1);
  if (!out) return -ENOMEM;
  memcpy(header, "RIFF", 4);
  put32(header + 4, bytes - 8);
  memcpy(header + 8, "WAVEfmt ", 8);
  put32(header + 16, 16);
  header[20] = 1;
  header[22] = 1;
  put32(header + 24, rate);
  put32(header + 28, rate * 2);
  header[32] = 2;
  header[34] = 16;
  memcpy(header + 36, "data", 4);
  put32(header + 40, frames * 2);
  memcpy(out, prefix, sizeof(prefix) - 1);
  char *p = out + sizeof(prefix) - 1;
  for (size_t offset = 0; offset < bytes; offset += 3)
    {
      uint32_t triple = 0;
      for (size_t j = 0; j < 3; j++)
        {
          size_t index = offset + j;
          unsigned int byte = 0;
          if (index < 44) byte = header[index];
          else if (index < bytes)
            byte = ((uint16_t)pcm[(index - 44) / 2] >>
                    (((index - 44) % 2) * 8)) & 255;
          triple = (triple << 8) | byte;
        }
      *p++ = alphabet[(triple >> 18) & 63];
      *p++ = alphabet[(triple >> 12) & 63];
      *p++ = offset + 1 < bytes ? alphabet[(triple >> 6) & 63] : '=';
      *p++ = offset + 2 < bytes ? alphabet[triple & 63] : '=';
    }
  memcpy(p, suffix, sizeof(suffix));
  *body = out;
  *length = total;
  return 0;
}
