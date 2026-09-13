# MoveNet SinglePose Lightning INT8 v4 integration manifest

This is the selected candidate model for the first real VelaFit pose backend.
The model binary is embedded in the dedicated ESP32-P4 acceptance firmware.
Distribution still requires preserving the upstream Apache-2.0 notice.

- Upstream: Google MoveNet SinglePose Lightning, TFLite INT8 variation, v4
- Model card: <https://www.kaggle.com/models/google/movenet/tensorFlow2/singlepose-lightning/4>
- Download endpoint used for local inspection:
  <https://tfhub.dev/google/lite-model/movenet/singlepose/lightning/tflite/int8/4?lite-format=tflite>
- License stated by the upstream model card: Apache-2.0
- Local inspection SHA-256:
  `cd7cc22fa946e5d146a7b98d496853e1923e22828d3972d579973f27f91bb105`
- Size: 2,894,840 bytes
- FlatBuffer schema version: 3
- Input: UINT8 `[1, 192, 192, 3]`
- Output: FLOAT32 `[1, 1, 17, 3]`; each keypoint is `[y, x, score]`

The inspected graph uses these 19 builtin operator types:

```text
ADD
ARG_MAX
CAST
CONCATENATION
CONV_2D
DEPTHWISE_CONV_2D
DEQUANTIZE
DIV
FLOOR_DIV
GATHER_ND
LOGISTIC
MUL
PACK
QUANTIZE
RESHAPE
RESIZE_BILINEAR
SQRT
SUB
UNPACK
```

Matching registrations exist in the repository's current TFLite Micro source,
but symbol availability is not runtime acceptance.

## 2026-09-13 acceptance status

The generic benchmark was replaced for acceptance purposes by a dedicated
VelaFit runner that constructs `MicroInterpreter` without a profiler. Focused
compatibility changes add UINT8 `CAST`, INT32 `FLOOR_DIV`, and INT32 `SUB`.
The `FLOOR_DIV` tests pass 5/5 and the `SUB` tests pass 16/16.

On the ESP32-P4, one invoke and a two-invoke repeatability run pass with output
CRC32 `c5e345da`. The model is embedded in flash/DROM and uses a 3,145,728-byte
tensor arena. Inference takes approximately 50.3 seconds per fixed input.

The requested 1,000-invoke soak was manually stopped at 30/1000 with no failure
or CRC drift reported. It remains an open overnight acceptance item; at the
measured rate, the remaining 970 invokes require approximately 13 hours 33
minutes. See `artifacts/hardware/2026-09-13-headless/TFLM_RUNNER_ACCEPTANCE.md`
for firmware hashes and raw-log hashes.

The repository benchmark tooling also has integration drift: it defaults to
C++11 although the benchmark uses `std::aligned_alloc`, references NuttX `FAR`
in a Linux build, uses obsolete model define names, and limits runtime-loaded
models to 512 KiB. These are tool issues, separate from the ESP32-P4 backend,
but should be fixed or bypassed with a small dedicated VelaFit model runner.

Before enabling this backend in the competition configuration, complete or
retain evidence for all of the following on ESP32-P4:

1. FlatBuffer schema and tensor shapes are rejected if they differ from the
   manifest above.
2. Keep the tested compatibility patches and the no-profiler runner in the
   final integration.
3. Revisit arena placement and minimum size during camera/pipeline integration;
   the standalone runner's 3,145,728-byte arena is proven, but concurrent
   camera/display memory pressure is not.
4. A known test image produces finite coordinates and scores in `[0, 1]` with
   correct `y/x` conversion into `pose_frame_t`.
5. Resume and finish the interrupted 1,000-invoke overnight soak without leaks,
   crashes, unsupported-op messages, or output drift on fixed input.
6. Record preprocess, invoke, postprocess, total latency, effective FPS, and
   peak memory. Do not claim “INT8 end-to-end”: this variation takes UINT8 input
   but exposes FLOAT32 output and contains quantize/dequantize operations.
7. Preserve the upstream Apache-2.0 license/notice when distributing the model.
