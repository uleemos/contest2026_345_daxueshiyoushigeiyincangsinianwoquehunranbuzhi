# VelaFit fixed-image MoveNet acceptance (2026-09-13)

## Verdict

The dedicated no-profiler MoveNet runner is integrated into
`velafit_pose_model` and produced all 17 COCO keypoints on the ESP32-P4 from a
fixed real-person image.  Functional inference passes.  Performance does not:
the measured forward pass was 50,210 ms against the provisional 2,000 ms hard
gate.  This result must not be described as real-time pose inference.

This test exercises P4-local inference only.  It does not use the ESP32-C6,
Wi-Fi, cloud service, camera, display, touch, microphone, or speaker.

## Reproducible inputs

- Model: MoveNet SinglePose Lightning INT8 v4
- Model file size: 2,894,840 bytes
- Model SHA-256:
  `cd7cc22fa946e5d146a7b98d496853e1923e22828d3972d579973f27f91bb105`
- Tensor contract: UINT8 `[1,192,192,3]` to FLOAT32 `[1,1,17,3]`
- Image source: TensorFlow MoveNet tutorial sample, whose page links to
  `https://images.pexels.com/photos/4384679/pexels-photo-4384679.jpeg`
- Downloaded image SHA-256:
  `13f8c94b720c35274841a70d5ba51618178a416c0d560c73f2ee318783097216`
- Prepared RGB fixture: aspect-preserving bilinear resize to 157x192, centered
  on a black 192x192 canvas at offset 17,0
- RGB fixture size: 110,592 bytes
- RGB fixture SHA-256:
  `65769e2eba019f3ddcb5e0b26e512d95561d0369b84816b4f25ba93d62bd0fed`

The source photo and generated binary are deliberately not checked into Git.
Regenerate the build input with:

```sh
python3 tools/models/prepare_pose_fixture.py \
  https://images.pexels.com/photos/4384679/pexels-photo-4384679.jpeg \
  /tmp/velafit_pose_fixture_rgb192.bin \
  --preview /tmp/velafit_pose_fixture_rgb192.png
```

Pillow is required by the preparation tool.  The image remains subject to its
source terms; the URL and hashes above preserve provenance without
redistributing it in this repository.

## Device and build evidence

- Board: ESP32-P4, revision v3.2, dual core, 400 MHz
- Console/flash port: COM3 through USB Serial/JTAG
- Team source before this change: `3c7bdaa80255e896988db44406802b0a951e7ed5`
- Apps source: `1be91957428649e2bc4875a211b1433e8d61e755`
- NuttX source: `f620a900906210e74cdb2947753c0031f5d6149b`
- Firmware image SHA-256:
  `15541afb08fbac3f30b2d61708edee9c1e40d97e7da954b8764072575bdd9b48`
- Flash write and verification: passed
- Command: `velafit_ai test_pose`
- Raw transcript:
  `hardware-logs/velafit-pose-real-image-20260913.log`
- Normalized transcript SHA-256:
  `fde1d3646cd5e18215c9956f41081bfb8693d1d4abc8bb3f6dcadeab7e7dd238`

## Acceptance results

| Check | Requirement | Measured | Result |
|---|---:|---:|---|
| Backend identity | real TFLM backend | `tflm-movenet-lightning-int8-v4` | PASS |
| Output shape | 17 keypoints | 17 | PASS |
| Finite normalized values | x, y, score each in `[0,1]` | all 17 valid | PASS |
| Confident points | at least 5 with score >= 0.20 | 17/17 | PASS |
| Forward latency hard gate | <= 2,000 ms | 50,210 ms | **FAIL** |
| Total latency | recorded | 50,220 ms | informational |

Observed keypoint confidence ranged from 0.43 to 0.93.  Derived values included
left/right knee angles of 79.8/102.5 degrees and trunk lean of 38.1 degrees.
These values prove that model output reaches the existing geometry code; this
single fixture does not establish pose-estimation or exercise-count accuracy.

## Performance policy and next gate

The current 2,000 ms value is a provisional P0 hard ceiling that prevents a
50-second reference implementation from being presented as usable.  A contest
demo needs at least 5 pose updates per second, so the eventual demonstration
target is <= 200 ms per frame, including preprocessing and postprocessing.
That target cannot be enforced until an accelerated/smaller inference path is
available.

The next engineering task is therefore inference optimization or model
replacement, followed by repeat tests on this identical fixture.  Only after
the static path meets an interactive budget should camera-frame conversion and
live-motion accuracy be treated as a reliable end-to-end acceptance test.

## Later user-photo fixture

A user-owned JPG or PNG can be attached in chat or placed anywhere under the
workspace with its absolute path supplied.  For a neutral camera fixture, face
the camera, keep the entire body and both feet visible, stand with feet about
shoulder-width apart, hold the arms 20--30 degrees away from the torso, use
even lighting, and remain still for two seconds.  A second squat fixture is
useful later, but no user photo was required for this P4 integration proof.
