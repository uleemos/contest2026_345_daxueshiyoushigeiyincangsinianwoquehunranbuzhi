# VelaFit SC2336 camera-to-MoveNet acceptance record

Date: 2026-09-13 (Asia/Shanghai)

## Implemented path

```text
SC2336 1280x720 packed RAW10 (BGGR)
  -> guarded ESP32-P4 CSI DMA capture in PSRAM
  -> fused RAW10 unpack + bilinear BGGR demosaic
  -> aspect-preserving nearest-neighbour resize/letterbox
  -> black-level removal + bounded gray-world AWB + gamma approximation
  -> 192x192 RGB888 UINT8
  -> TFLite Micro MoveNet SinglePose Lightning INT8 v4
  -> 17 COCO keypoints
```

The fused conversion samples only source pixels needed by the 192x192 model
input, avoiding a 1280x720 RGB framebuffer.  A 16:9 camera frame occupies
`192x108` at `(0,42)` and the remaining rows are black letterbox bars.

## Automated evidence

- Host synthetic packed-RAW10 test: PASS.  It checks MIPI four-pixel/five-byte
  unpacking, BGGR channel reconstruction, letterbox geometry and truncated
  input rejection.
- Firmware build and link: PASS.
- Flash write and hash verification on ESP32-P4 revision 3.2: PASS.
- Real CSI capture: PASS, `capture_us=100000`, DMA guard PASS.
- Real conversion: PASS, `convert_us=40000`, content `192x108+0+42`.
- Real frame linear mean RGB `55,82,51`; corrected model-input mean RGB
  `187,188,189` with Q8 gains `918,543,1024`.
- Empty-scene MoveNet invocation: PASS, all 17 finite/in-range keypoints were
  returned and correctly classified `valid=no` (`0/17` score >= 0.2).
- Profiled inference latency: `50,320,000 us`; `CONV_2D` consumed 90.20% and
  `DEPTHWISE_CONV_2D` 7.83%.  This is a model-kernel performance failure
  relative to the separate 2 s / 200 ms targets, not a camera data-path
  failure.

Evidence logs:

- `hardware-logs/velafit-camera-input-awb-20260913.log`
- `hardware-logs/velafit-camera-pose-empty-kpts-20260913.log`
- `hardware-logs/velafit-camera-input-awb-20260913.png`

## Manual acceptance still required

1. Confirm that the preview is upright and that its left-side door/hanging
   clothes match the physical scene.  This determines whether rotation is
   required.
2. Put a full standing person in frame with head and both feet visible, then
   rerun `velafit_ai camera_pose` and require `valid=yes` with plausible 17
   points.
3. Visually confirm skin/clothes colors under the intended demonstration
   lighting.  The current lightweight AWB/gamma stage is suitable for model
   bring-up, but it is not a calibrated ESP32-P4 ISP tuning profile.

## Reproduction

```sh
cc -std=c11 -Wall -Wextra -Werror -Iapp/sc2336_probe \
  tools/models/test_sc2336_raw10.c app/sc2336_probe/sc2336_raw10.c \
  -o /tmp/test_sc2336_raw10
/tmp/test_sc2336_raw10
```

On the board:

```text
velafit_ai camera_input dump
velafit_ai camera_pose
```
