# MoveNet SinglePose Lightning INT8 v4 integration manifest

This is the selected candidate model for the first real VelaFit pose backend.
The model binary is intentionally not committed until the TFLite Micro runtime,
flash layout, and third-party notice flow are reviewed together.

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

## 2026-09-13 host probe result

The repository TFLite Micro runtime and this model were linked into its generic
Linux benchmark. `AllocateTensors()` completed with the benchmark's calculated
2,105,160-byte arena, but the first invoke reported:

```text
Input type UINT8 (3) not supported.
```

The message comes from the current TFLM `CAST` kernel: its input switch supports
INT8/INT16/INT32/UINT32/FLOAT32/BOOL but not UINT8. A temporary diagnostic
UINT8 case removed that message. The generic profiler then exceeded its fixed
64-event capacity on this graph and deliberately aborted, so a complete invoke
has **not** yet been proven. No temporary modification to third-party TFLM
sources was retained.

The repository benchmark tooling also has integration drift: it defaults to
C++11 although the benchmark uses `std::aligned_alloc`, references NuttX `FAR`
in a Linux build, uses obsolete model define names, and limits runtime-loaded
models to 512 KiB. These are tool issues, separate from the ESP32-P4 backend,
but should be fixed or bypassed with a small dedicated VelaFit model runner.

Before enabling this backend in the competition configuration, verify all of
the following on ESP32-P4:

1. FlatBuffer schema and tensor shapes are rejected if they differ from the
   manifest above.
2. Add/test UINT8 input support to the CAST kernel (preferably as a focused
   upstreamable TFLM patch), and run without the 64-event diagnostic profiler
   limit.
3. `AllocateTensors()` succeeds with the arena placed in DMA/cache-safe PSRAM;
   record the smallest passing arena and free heap before/after allocation.
4. A known test image produces finite coordinates and scores in `[0, 1]` with
   correct `y/x` conversion into `pose_frame_t`.
5. Ten warm-up invokes and at least 1,000 measured invokes complete without
   leaks, crashes, unsupported-op messages, or output drift on fixed input.
6. Record preprocess, invoke, postprocess, total latency, effective FPS, and
   peak memory. Do not claim “INT8 end-to-end”: this variation takes UINT8 input
   but exposes FLOAT32 output and contains quantize/dequantize operations.
7. Preserve the upstream Apache-2.0 license/notice when distributing the model.
