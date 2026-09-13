# VelaFit P0-1b pose performance investigation (2026-09-13)

## Acceptance state

| Gate | Current evidence | State |
| --- | --- | --- |
| Real INT8 inference and 17 keypoints | MoveNet Lightning INT8 v4, 17/17 keypoints | PASS |
| First-stage latency <= 2 s | 50.220 s forward inference | FAIL |
| Demo latency <= 200 ms (>= 5 FPS) | about 0.02 FPS before camera/display cost | FAIL |
| 1,000-invoke stability | 30/1000 completed before the user stopped it | DEFERRED |

The current README claim of sub-50-ms edge inference is a design target, not an
accepted result.

## Operator-level bottleneck

The bounded TFLM operator profiler was run on the ESP32-P4 Rev 3.2 at 400 MHz.
The complete raw transcript is `hardware-logs/velafit-pose-op-profile-20260913.log`.
After removing the experimental ESP-NN wiring, a clean rebuild, flash, and
second COM3 run reproduced 17/17 valid keypoints at 50.220 s. Its transcript is
`hardware-logs/velafit-pose-op-profile-postrestore-20260913.log`; this also
confirms that the board was returned to the reference-kernel baseline.

| Operator | Calls | Total | Share of Invoke |
| --- | ---: | ---: | ---: |
| `CONV_2D` | 50 | 45.420 s | 90.44% |
| `DEPTHWISE_CONV_2D` | 24 | 3.940 s | 7.85% |
| All other operators | 109 | about 0.86 s | about 1.71% |

`CONV_2D + DEPTHWISE_CONV_2D` account for 98.29% of forward time. Optimizing
pre/post-processing, geometry, JSON, display, or networking cannot close the
latency gate before these two kernels are replaced.

Evidence SHA-256:

- first profile: `933163694402fec1da1462164dfee86bbba5fbe5859874db76af4cd4ec556c28`
- clean-restore profile: `0921d13eaa28bc33f828d2aee78d41fc49c4989ff57ae23bc408992460fdf56a`

## ESP-NN integration experiment

The official ESP-NN P4 PIE/QACC sources were compiled from commit
`2c222c5e02225177b44ebf21169bc66df3c8b573` and wired experimentally into the
existing INT8 Conv2D and DepthwiseConv2D TFLM paths.

The experiment established these concrete platform gaps:

1. openvela's bundled upstream `riscv-none-elf` assembler does not recognize
   `esp.*` PIE instructions. Espressif `riscv32-esp-elf` GCC 14.2 compiles them.
2. Objects containing ESP hardware-loop relocations require Espressif's patched
   linker or removal of `esp.lp.setup`; the upstream linker misidentifies the
   relocation and reports `R_RISCV_RVC_LUI` truncation.
3. The resulting image builds, flashes, and verifies, but the first optimized
   convolution does not return. ESP-IDF has explicit HWLP/PIE lazy exception
   handling plus task context save/restore. The current NuttX ESP32-P4 port has
   neither equivalent path.
4. Replacing hardware loops with ordinary branches and forcing the PIE CSR to
   DIRTY did not make the full TFLM invocation safe. The experiment was therefore
   removed from the default configuration. No accelerated latency is claimed.

The production fix is an architecture/BSP task: port and test PIE/HWLP state
handling in the NuttX interrupt and scheduler context paths, then re-enable the
ESP-NN kernel adapter. Disabling interrupts for an entire inference is not an
acceptable substitute because it would break timer, camera, networking, and
display responsiveness.

## INT8 pose-model comparison

| Candidate | Keypoints/input | Device evidence | Decision |
| --- | --- | --- | --- |
| MoveNet SinglePose Lightning INT8 v4 | 17, 192x192 | 2.894 MB; works in TFLM; 50.220 s with reference kernels | Keep as correctness baseline |
| ESP-DL YOLO11n-Pose INT8 | 17, 640x640 | Official P4 result requires 16 MB flash and 32 MB PSRAM; model latency is about 2.72 s before the full pipeline | Reject for the 2 s and 200 ms gates |
| MediaPipe BlazePose/Pose Landmarker Lite | 33, two-stage detector + landmark | Official downloadable Lite task is float16, not a drop-in INT8 TFLM model | Reject for this INT8 milestone |

No smaller, licence-clear, drop-in INT8 model with the same 17-keypoint contract
was found that is more credible than MoveNet Lightning. Merely changing its input
tensor to 96x96 or 128x128 is invalid; a separately trained and quantized model
is required.

## Ordered next work

1. **P0:** add a tiny PIE/HWLP smoke test and port the ESP-IDF-equivalent
   exception/context handling to NuttX. Acceptance requires concurrent timer
   interrupts and two tasks without register corruption.
2. **P0:** integrate the released ESP-NN P4 kernels using the Espressif compiler
   and linker, then check output tensors against the reference backend before
   measuring latency.
3. **P0:** rerun the fixed fixture once and ten times. The first target is <=2 s
   with identical valid 17-keypoint output.
4. **P0 fallback:** if optimized MoveNet remains above 200 ms, train/quantize a
   purpose-built 96/128-pixel single-person 17-keypoint network for the fixed
   exercise camera geometry. Validate accuracy on standing, frontal squat, and
   45-degree squat images before selecting it.
5. **P1:** only after one-frame latency passes, connect the camera and measure the
   complete capture -> preprocess -> inference -> posture FSM path at >=5 FPS.
6. **Deferred overnight:** finish the remaining 970/1000 stability invocations
   using the finally selected production kernel/model, not the 50-second
   reference build.

## User-photo fixtures

The three submitted compositions are suitable for acceptance: frontal neutral
standing, frontal squat bottom, and 45-degree squat bottom all show the full body
and the major joints. The chat-rendered images were visually reviewed, but their
original JPEG bytes were not mounted into the workspace. Save the originals as
files before deterministic preprocessing and hash-based regression evidence is
created.

## Primary references

- ESP-NN source, licence, P4 kernel and model benchmarks:
  <https://github.com/espressif/esp-nn>
- Espressif TFLM integration:
  <https://github.com/espressif/esp-tflite-micro>
- ESP-DL COCO pose P4 resource and latency table:
  <https://github.com/espressif/esp-dl/blob/master/models/coco_pose/README.md>
- MediaPipe pose model architecture and 33-landmark contract:
  <https://github.com/google-ai-edge/mediapipe/blob/master/docs/solutions/pose.md>
