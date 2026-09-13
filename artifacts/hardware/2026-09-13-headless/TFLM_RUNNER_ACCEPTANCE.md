# ESP32-P4 no-profiler MoveNet runner acceptance

Date: 2026-09-13 (Asia/Shanghai)

## Test target

- Board: ESP32-P4X Function EV Board V1.8
- SoC reported by esptool: ESP32-P4 revision v3.2, dual HP cores at 400 MHz
- Console/download port: COM3 (USB Serial/JTAG)
- Runtime: openvela/NuttX 13.0.0
- Model: MoveNet SinglePose Lightning INT8 v4
- Model SHA-256: `cd7cc22fa946e5d146a7b98d496853e1923e22828d3972d579973f27f91bb105`
- Model contract: UINT8 `[1,192,192,3]` to FLOAT32 `[1,1,17,3]`
- Final firmware SHA-256: `58eca21cf4ca0eb73e64d9bb581c73fce6c4b09eb4e4cfd1260a768956b721d2`
- Firmware size: 3,494,612 bytes

The runner constructs `MicroInterpreter` without providing a profiler and
prints `backend=real profiler=disabled`. It embeds the model in flash/DROM,
allocates a 3,145,728-byte aligned tensor arena, validates tensor contracts,
reloads deterministic input before every invocation, rejects non-finite or
out-of-range output, and fails immediately on output CRC drift.

## Build and compatibility results

- ESP32-P4 clean rebuild: PASS
- Flash write and esptool hash verification: PASS
- TFLM `FLOOR_DIV` unit tests: 5/5 PASS
- TFLM `SUB` unit tests: 16/16 PASS
- Required compatibility additions: UINT8 `CAST`, INT32 `FLOOR_DIV`, and
  INT32 `SUB` (including broadcast paths)

## P4 hardware results

| Run | Result | Output CRC32 | Timing |
|---|---|---|---|
| 1 invoke | PASS, failures=0 | `c5e345da` | 50,300,000 us |
| 2 invokes | PASS, failures=0, no drift | `c5e345da` | min 50,270,000 us; mean 50,285,000 us; max 50,300,000 us |
| 1,000-invoke soak (partial) | 30/1000 PASS; user stopped to defer overnight | no drift reported | 970 invokes remain |

Single-run allocation took 70,000 us. The runner reported heap use increasing
from 40,456 bytes before arena allocation to 3,186,192 bytes while the arena
was live; the arena is freed when the runner exits.

## Evidence files

Raw serial logs are kept locally beside this document and excluded from Git.

| Transcript | SHA-256 | Meaning |
|---|---|---|
| `tflm-runner-single-final.log` | `38e6f5ec125e27c4ec34536e0549edd7c08a882930f865152a32a86290cec750` | Final firmware, one invoke PASS |
| `tflm-runner-2-final.log` | `5831e033d937c82f45907cb27e2bb711f14c978684872c59f07278eb664c52b1` | Two invokes PASS with identical output CRC |
| `tflm-runner-1000-partial-30.log` | `9e08a763b95baabe75af4719435ce5858c3dcf14e633f0c9ec6b32f23a4f0afb` | Progress 10, 20, and 30 recorded; no FAIL line before manual stop |

The 1,000-invoke acceptance item remains **open**. At approximately 50.3
seconds per invocation, the remaining run requires about 13 hours 33 minutes.
Schedule it as an uninterrupted overnight test and replace the partial log
entry with the completed `progress=1000/1000` and `PASS runs=1000 failures=0`
evidence. A partial run must not be reported as full soak acceptance.
