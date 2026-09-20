# TinyPose INT8 latency prototype — 2026-09-19

## Scope and current state

This is an untrained latency prototype independent from the existing MoveNet
acceptance and benchmark path. It does not use the camera and does not claim
keypoint or action-recognition accuracy.

Host generation, asset validation, openvela build and two real ESP32-P4 runs
pass their stated checks. The default TFLM Conv2D baseline misses both latency
gates. Reusing the existing ESP-NN/PIE Conv2D registration passes both gates
with exact reference output. Further kernel optimization stops at this point.

## Network and model metadata

- Input: INT8 `[1,96,96,3]`, scale `0.007843134924769402`, zero point `0`.
- Five SAME 3x3 stride-2 Conv2D layers with fused ReLU and channels
  `8,16,24,32,48`.
- Flatten, then FullyConnected 24.
- Output: INT8 `[1,24]`, scale `0.0009573690476827323`, zero point `0`.
- Output semantic: 8 keypoints x `{x,y,confidence}`; weights are random.
- Operators: five `CONV_2D`, one `RESHAPE`, one `FULLY_CONNECTED`.
- Parameters: 36,080.
- TFLite size: 43,672 bytes.
- Model SHA256:
  `115808b060483613cc5ecbc258b04b96a118d9cf981f82d28589d711b71be48d`.
- Fixture SHA256:
  `1b8fd4cfa4e9de3ba249f9718e489e60a61ee8f8e096b2903a7fe1373a5f375b`.
- Oracle SHA256:
  `c9f751564074086c13ee3ac41300ddbc9efc8ec6953b4197201ee7f4cfd3c3ae`.

The source of truth for all tensor metadata is the private generated file
`.secrets/tinypose-latency-prototype-20260919-v3/metadata.json`. The fixed
oracle uses the TFLite `BUILTIN_REF` resolver. The earlier XNNPACK oracle had
nine one-LSB differences from both TFLM reference implementations and is not
used for acceptance.

## Board benchmark contract

The command is:

```text
velafit_ai pose_tiny_bench 100
```

It allocates an independent 512 KiB arena, reports cold initialization
separately, performs five warmup Invokes, then measures 100 complete TFLM
Invokes. Every warmup and measured output is compared byte-for-byte with the
fixed 24-byte host oracle. The final line reports MIN, AVG, P50, P95, MAX,
`gate_150ms` and `gate_200ms`.

Recommended pass conditions:

- `P95 < 150 ms`
- `MAX < 200 ms`
- 100/100 measured Invokes completed
- zero output mismatches
- heap reported before init, after init, after Invoke and after deinit

The measured Invoke scope excludes camera capture, resize, RGB conversion,
rendering and cold model initialization. The device output prints these
exclusions explicitly.

## Real ESP32-P4 results

| Conv2D path | MIN | AVG | P50 | P95 | MAX | Numerical check | Gates |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| TFLM default | 350 ms | 353.3 ms | 350 ms | 360 ms | 360 ms | 0 mismatches | FAIL / FAIL |
| Existing ESP-NN/PIE | 10 ms | 11.5 ms | 10 ms | 20 ms | 20 ms | 0 mismatches | PASS / PASS |

Both measurements used five warmup rounds followed by 100 complete Invokes on
the ESP32-P4. The board timer has 10 ms granularity, so the result is a bounded
latency measurement rather than microsecond-resolution profiling. The ESP-NN
run used 77,284 bytes of its 512 KiB arena. The outer `free` readings before and
after the command were identical: Umem used 1,236,640 bytes and Kmem used 4,704
bytes. Cold model initialization took 1,818,994 cycles, or about 4,547 us at
400 MHz; the 10 ms wall clock reported zero for this sub-tick interval.

The first diagnostic copied the fixture only once. TFLM legally reused the
input arena after the first Invoke, so subsequent calls did not receive the
same input. The accepted implementation recopies the fixed fixture before each
Invoke, outside the timed interval. Host reference, first device output and all
105 device outputs then match byte-for-byte.

## Build evidence and immutable firmware

- Final build log: `hardware-logs/tinypose-espnn-final-build-20260919.log`.
- Model generation: `hardware-logs/tinypose-model-generation-v3-20260919.log`.
- Default TFLM hardware log:
  `hardware-logs/tinypose-final-p4-100round-20260919.log`.
- Accepted ESP-NN hardware log:
  `hardware-logs/tinypose-espnn-final-p4-100round-20260919.log`.
- Structured result:
  `hardware-logs/tinypose-espnn-acceptance-20260919.json`.
- Private verified firmware bundle:
  `.secrets/tinypose-espnn-final-bundle-20260919`.
- BIN SHA256:
  `e438d9ce4b3c09be91789524225275f1bcc8d93f8f7ed7690a50d2bc98b4ab94`.
- ELF SHA256:
  `7590dc7601f833559d577109c813197e98f78d0f4efe5599e889e4c65f36c23c`.
- Config SHA256:
  `9f376dafa50c84a02bf4e8b7902b5444e7ceecd9a5980552dbc2550b5598ada3`.

The build completed successfully. Its only source warning is the pre-existing
unused `machine_cycles()` function in the MoveNet implementation. No TinyPose
warning was emitted. The firmware ELF contains the expected model, fixture,
oracle and benchmark symbols.

## Reproduction

Generate a fresh private asset directory with the pinned TensorFlow 2.18.1
environment, then build without flashing:

```sh
.secrets/mww-train-venv/bin/python \
  tools/tinypose/generate_tinypose.py \
  --output .secrets/<fresh-tinypose-directory>

VELAFIT_TINY_ASSETS=.secrets/<fresh-tinypose-directory> \
  bash tools/build_tinypose_diagnostic.sh 16

VELAFIT_TINY_ASSETS=.secrets/<fresh-tinypose-directory> \
VELAFIT_TINY_CONV=espnn \
  bash tools/build_tinypose_diagnostic.sh 16
```

PC TFLite execution and a successful firmware build are supporting evidence;
the latency result above comes from the exact verified bundle on the real
ESP32-P4 revision v3.2. Phase 2 model training and keypoint-quality validation
have not started and are outside this untrained latency prototype.
