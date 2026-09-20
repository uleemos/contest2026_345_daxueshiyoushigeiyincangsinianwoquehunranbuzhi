---
name: velafit-demo-acceptance
description: Validate, record, and freeze the end-to-end VelaFit workout demo on the ESP32-P4 contest board. Use for pre-recording regression, human squat acceptance, serial evidence collection, firmware hashing, release commits, or contest PR preparation. Do not use for model training, architecture expansion, or unrelated board bring-up.
---

# Validate the VelaFit demo

Protect the last hardware-proven path. Treat wake word, BODY_CHECK, countdown,
TinyPose, squat FSM, MiMo advice, LCD, TTS, I2S, and ES8311 as frozen unless a
reproducible failure blocks the recorded demo.

## Establish the candidate

1. Inspect both the team repository and `nuttx/` status and current branches.
2. Exclude `.secrets`, credentials, build outputs, and bulk diagnostic logs from
   commits. Do not print secret values while checking configuration.
3. Run `python3 tools/hardware/delivery_regression.py` before flashing.
4. Build with `tools/build_tinypose_diagnostic.sh` and preserve the build log.
5. Record SHA-256 values for the ELF, flash BIN, and deployed TinyPose model.

Do not report PC tests as proof of hardware latency or end-to-end behavior.

## Run staged hardware acceptance

Use the real SC2336 camera, ESP32-P4, LCD, ESP32-C6 network path, ES8311, and
speaker. Keep one serial log per attempt.

1. Run two or three complete sessions with `velafit_demo 3`.
2. Verify wake → BODY_CHECK → countdown → three detected squats → FINISHED →
   MiMo advice → LCD result → streamed TTS → PLAYBACK_DONE → IDLE.
3. After short sessions are stable, run one `velafit_demo 20` session while a
   person performs the repetitions and records the demo video.
4. From wake-word detection onward, do not inject NSH commands.

Tell the operator explicitly when wake listening begins. Explain positioning
prompts in physical terms: `MOVE BACK` means step farther from the camera;
`STEP INTO FRAME` means bring the whole body, including ankles, into view;
`STAND READY` means hold the pose until countdown starts.

## Judge results from evidence

Extract these values from the device log rather than estimating them:

- actual, detected, missed, and duplicate repetitions
- form warning repetitions and body-lost count
- advice/TTS HTTP status and latency
- TTS first-PCM and total time
- PCM underflows
- heap before/after and delta
- C6 probe attempts and recovery result

`form_warning_reps` are completed repetitions with quality warnings. Do not
describe them as additional failed repetitions. A first C6 probe failure is
acceptable only when bounded retry recovers automatically during the same
session.

If TTS fails, preserve the LCD advice and require a bounded return to IDLE. If
a failure is intermittent, reproduce it before changing code and keep the fix
limited to the blocking path.

## Freeze a successful recording

After the first successful 20-repetition recording:

1. Save the video and complete serial log.
2. Run `git diff --check`, review staged files, and scan for credentials.
3. Commit team application changes and NuttX changes in their owning
   repositories with `Signed-off-by` trailers.
4. Push the team branch and the NuttX PR head branch.
5. Record both commit IDs and the final artifact hashes.
6. Check PR CI and fix deterministic style/build failures before handoff.

Keep the successful commit available if later experiments continue.
