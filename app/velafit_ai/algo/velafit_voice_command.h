/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VELAFIT_VOICE_COMMAND_H
#define VELAFIT_VOICE_COMMAND_H
enum velafit_voice_command
{
  VELAFIT_VOICE_UNKNOWN = 0,
  VELAFIT_VOICE_WAKE,
  VELAFIT_VOICE_START,
  VELAFIT_VOICE_PAUSE,
  VELAFIT_VOICE_RESUME,
  VELAFIT_VOICE_STOP
};
/* Exact whole utterance matching, not acoustic KWS; has no side effects. */
enum velafit_voice_command velafit_voice_parse(const char *text);
#endif
