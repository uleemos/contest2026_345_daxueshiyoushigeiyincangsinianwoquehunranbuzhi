/* SPDX-License-Identifier: Apache-2.0 */
#include "velafit_voice_command.h"
#include <stddef.h>
#include <string.h>

enum velafit_voice_command velafit_voice_parse(const char *text)
{
  static const struct
  {
    const char *text;
    enum velafit_voice_command command;
  } allowed[] =
  {
    {"你好openvela", VELAFIT_VOICE_WAKE},
    {"你好欧鹏薇拉", VELAFIT_VOICE_WAKE},
    {"开始训练", VELAFIT_VOICE_START},
    {"暂停训练", VELAFIT_VOICE_PAUSE},
    {"继续训练", VELAFIT_VOICE_RESUME},
    {"结束训练", VELAFIT_VOICE_STOP}
  };
  char normalized[128];
  size_t used = 0;
  if (!text || strlen(text) >= sizeof(normalized)) return VELAFIT_VOICE_UNKNOWN;
  while (*text)
    {
      unsigned char c = (unsigned char)*text;
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
          c == ',' || c == '.' || c == '!')
        {
          text++;
          continue;
        }
      if (!strncmp(text, "，", 3) || !strncmp(text, "。", 3) ||
          !strncmp(text, "！", 3))
        {
          text += 3;
          continue;
        }
      normalized[used++] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
      text++;
    }
  normalized[used] = 0;
  for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
    if (!strcmp(normalized, allowed[i].text)) return allowed[i].command;
  return VELAFIT_VOICE_UNKNOWN;
}
