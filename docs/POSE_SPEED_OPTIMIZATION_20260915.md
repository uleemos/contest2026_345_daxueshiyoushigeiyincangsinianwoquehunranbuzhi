# MoveNet diagnostic latency optimization — 2026-09-15

Target: <200ms on the real P4, retaining numerical verification. This is not
camera-to-display latency acceptance. No commits/pushes, microphone recording,
audio playback or SD data removal are part of this task.

## Baselines and experiments

- Same MoveNet Lightning INT8 v4 and RGB192 fixture as the existing acceptance.
- No-profiler five-round baseline: 2.38–2.39s, all51 components identical.
  Evidence: `hardware-logs/pose-speed-baseline-20260915.log`.
- ESP-NN instructions in IRAM: mean2.386s, no material benefit. Costs18,816
  bytes internal SRAM. Remains opt-in `VELAFIT_ESP_NN_IRAM=1`, default off.
- Official newer ESP-NN commit `2c222c5e02225177b44ebf21169bc66df3c8b573`
  (component1.3.2) is separately pinned and selected by `VELAFIT_ESP_NN_V132=1`.
  Large-channel filter-major traversal and depthwise caller-owned scratch
  replace the old packed-channel workaround in this variant. Five-round
  evidence: `hardware-logs/pose-speed-v132-20260915.log`.
- Existing1.3.0 source is retained unchanged. QACC32 patch remains explicitly
  enabled for the small-channel path. Build manifest now hashes header inputs.

## Official open-source references

- https://github.com/espressif/esp-nn — Apache-2.0, versions pinned above.
- https://github.com/espressif/esp-dl — master ref observed
  `b7e9d88a570948d1d6cf9ef064c8cb8e04bda99e`, root MIT license. The QACC inner loop in
  `app/velafit_ai/models/velafit_qacc_conv.c` is adapted from its P4 s8 Conv2D
  assembly. Full upstream MIT notice is retained in `models/LICENSE.esp-dl`.
  This does not claim acceptance of the ESP-DL runtime or its models.
- Other-model published timings are not MoveNet latency measurements.

## Hardware evidence for FPU/cycle experiment

Exact board: ESP32-P4X Function EV Board V1.8, fitted C6-MINI-1;
previous board reports siliconv3.2. Hardware document preflight passed,
all four local hashes match the skill ledger. No pin/connector/rail changes.
Local TRM section2.6.3, printed110: RV32F independent32-register file,
mstatus.FS enable prerequisite. Section2 CSR register2.37, printed93: mcycle
0xb00; mcycleh0xb80 provides high32 bits. No relevant FPU entry found in
local errata; that does not replace testing.

Experimental `CONFIG_ESP32P4_FPU_DIAGNOSTIC` enables hardware RV32F but keeps
ilp32 soft calling convention for compatibility with the existing HAL and
isolated ESP-NN archives. FP contraction is off. Standard eager NuttX FPU
context is used; integer frame has12 padding bytes after the existing PIE
payload: 412 integer +132 FPU =544 bytes, preserving16-byte alignment.
Startup sets FS before C code can execute FP instructions. A two-thread
20-round probe checks a live FPR and distinct fcsr rounding modes across
multiple timer ticks without caller-clobbering function calls.

FPU build and board context probe passed (20 rounds per thread, no errors);
see `hardware-logs/pose-speed-fpu-smoke-20260915.log`.
Private pre-FPU source snapshots and per-experiment firmware bundles retained.


## Latest measured candidate

**The <200ms target is NOT met.** Fixed-fixture full-model Invoke is
1.44–1.45s over five rounds, mean1.448s. Approximately39% less latency than
2.39s, still7.25 times the200ms ceiling at the measured maximum.

| Experiment | Five-round inference time | Evidence in hardware-logs |
| --- | --- | --- |
| Original PIE diagnostic | 2.38–2.39s | pose-speed-baseline-20260915.log |
| FPU, soft ABI | mean2.282s | pose-speed-fpu-bench-20260915.log |
| QACC packed convolution | mean2.238s | pose-speed-gemm-bench-20260915.log |
| Filter reuse + unary LUT | 1.98s | pose-speed-lut-tiled-bench-20260915.log |
| Exact integer requantization | 1.86s | pose-speed-requant-bench-20260915.log |
| Binary LUT | 1.46s | pose-speed-binary-lut-bench-20260915.log |
| O3 + loop unrolling | 1.46–1.47s | pose-speed-o3-bench-20260915.log |
| 32-pixel convolution tiles | 1.44–1.45s | pose-speed-tile32-bench-20260915.log |

O3 provides no demonstrated independent improvement. IRAM relocation similarly
provided no benefit and remains disabled. Latest tile experiment retains O3
so its exact tested configuration is reproducible.

### What changed

- ESP-NN1.3.2 fixes the caller scratch contract for large depthwise channels;
  the isolated vendor compiler builds only the kernels. General openvela
  compilation still uses the project GCC13.4.0.
- A local ESP-DL-derived convolution packs16 output channels, precomputes bias
  plus asymmetric input offset, uses32-bit QACC extraction, and visits32-pixel
  tiles to improve filter/output locality. Padding and channel tails are kept.
- Exact requantization removes expensive generic64-bit rounding steps without
  changing TFLM two-stage rounding. Independent reference, edge cases and random
  tests:1,010,478 comparisons pass with UBSan, evidence
  `hardware-logs/pose-speed-requant-host-20260915.log`.
- Unary INT8/UINT8 operators enumerate all256 possible input bytes using the
  original TFLM implementation. Eligible ADD/SUB/MUL operators similarly
  enumerate all65,536 byte pairs and preserve4D broadcasting. Tables cache
  operator mappings, not model outputs; every measured round runs full Invoke.
- Diagnostic arena grows from3MiB to12MiB. The latest five-round run peaks at
  16,879,432 bytes of user heap and returns to1,236,640 bytes after deinit.
  Cold initialization takes1.19s separately. Unary table initialization is
  included in first Invoke; binary tables are prepared during initialization.

### Measurement and numerical boundaries

The original reference is about50s. `pose_bench` executes that reference anew;
`pose_fastbench` compares full inference against the archived51-value reference
in `models/velafit_pose_oracle.h` to shorten iterative testing. The fast command
is valid only for the pinned model and fixture below. It is not a substitute
for a new reference comparison after changing the model or fixture.

- MoveNet Lightning INT8 v4, UINT8[1,192,192,3] to FLOAT[1,1,17,3].
- Model SHA256: `cd7cc22fa946e5d146a7b98d496853e1923e22828d3972d579973f27f91bb105`.
- RGB fixture SHA256: `65769e2eba019f3ddcb5e0b26e512d95561d0369b84816b4f25ba93d62bd0fed`.
- Kernel probe:40 convolution +40 depthwise shapes and3 large-depthwise cases
  (256/320/960 channels), numerical comparison and scratch guards pass.
- Each timed full-model round matches51/51 output float bit patterns.
- Wall clock granularity is10ms. Cycle counts confirm approximately400MHz.
  Invoke timing excludes camera capture, input copy, rendering and cold init.
- This is a diagnostic-only path. Production/live camera inference has not
  been switched to this backend; live accuracy and end-to-end latency are not
  accepted by these tests.

### Reproduction and recovery

From the workspace root, using the existing pinned private toolchain/source
and hash-checked build inputs:

```sh
bash contest2026_345_daxueshiyoushigeiyincangsinianwoquehunranbuzhi/tools/build_pose_diagnostic.sh 16
```

At NSH, after flashing the verified bundle:

```text
velafit_ai esp_nn_probe
velafit_ai pose_bench 5
velafit_ai pose_fastbench 5
free
```

The benchmark reports separate numerical and latency gates. `result=0` alone
DOES NOT mean200ms passed: require `gate_200ms=PASS` and inspect completed rounds.

Latest snapshot: `.secrets/pose-speed-tile32-20260915`, retained alongside all
previous rollback bundles. BIN SHA256:
`a7fe058d1bcc2f7b983b34fcc35edc36e943bca56e720160877a7208bb7f6756`.
ELF SHA256: `f2f6795c1f88e3b6fc631d140a04b6b6460e5003eab1d5f3f91faa5fb623eb0f`.
Config SHA256: `9f376dafa50c84a02bf4e8b7902b5444e7ceecd9a5980552dbc2550b5598ada3`.
Build evidence: `hardware-logs/pose-speed-tile32-build-20260915.log`.
CMake diagnostic flag integration/clean build has not been verified.

### Remaining to reach the target

1. Further optimize convolution and depthwise output quantization/vector paths;
   the final profile measured850ms Conv2D and340ms Depthwise alone (82% of
   total Invoke time).
   This model has about270.55 million convolution/depthwise MACs per inference.
2. Evaluate a more efficient pose model/runtime if exact-kernel optimization
   remains insufficient. A replacement needs separate keypoint quality and
   squat-counting validation; no replacement has been selected or accepted.
3. Validate varied images and concurrent workloads, then integrate the accepted
   backend into live camera processing and measure capture-to-result latency.
4. Treat200ms as an unmet requirement until hardware logs show it passes.


## Final regression on the tile32 bundle

`hardware-logs/pose-speed-final-regression-20260915.log`:

- New original-reference execution50.13s versus candidate1.45s, differences0/51,
  max absolute difference0. FPU two-thread20-round probe errors0/0; HWLP and
  PIE voluntary context switches pass.
- Original fixed microWakeWord control model:1000 invocations,4.52s total;
  reset/replay100 frames twice passes. This is not wakeword quality acceptance.
- `uname -a`, `free`, `ps`, `ls`, `help` return successfully; final heaps recover.

`hardware-logs/pose-speed-final-cloud-regression-20260915.log`:

- Wi-Fi association, wrong-hostname rejection, trusted TLS and real fixed
  synthetic MiMo workout response pass.
- Existing16KiB SD file readback before/after the request passes; storage
  is read-only in this regression. No microphone recording or audio playback.

The final board image remains the tile32 bundle identified above. No commits
or pushes were made. Latest build helper adds no automatic flashing action.


Build-helper smoke test also passed, evidence
`hardware-logs/pose-speed-reproduce-build-20260915.log`. It rebuilt the same
configuration; generated images differ from the archived tested bundle.
Current workspace BIN SHA256 is
`01d65b00e01f93ebea5bd5f6568c66ff5842fd58dcabb75d23db00ae1e518027`
and has not been flashed. Use the archived tile32 bundle for the exact
hardware-tested binary, not an assumption of bit-reproducible build output.
