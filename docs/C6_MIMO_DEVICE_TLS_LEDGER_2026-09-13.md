# ESP32-P4 device TLS and real MiMo API evidence ledger

Date: 2026-09-13 (Asia/Shanghai)

## Acceptance scope

This milestone proves that the ESP32-P4 reaches the official MiMo service over
the fitted ESP32-C6 data plane.  The request is created, authenticated, sent,
received and parsed on the P4; the PC only drives the NSH commands and stores a
redacted serial transcript.

- Endpoint: `POST https://api.xiaomimimo.com/v1/chat/completions`
- Authentication header: `api-key`, populated from hidden interactive input
- Model: `mimo-v2.5`
- Request and response media type: `application/json`
- Trust anchor: DigiCert Global Root G2; the server supplies its leaf and
  intermediate certificates
- TLS policy: peer verification required, SNI enabled, hostname checked
- Clock bootstrap: NTP before certificate validity checking
- Retry policy: up to three attempts with one- and two-second backoff;
  permanent client errors are not retried, except HTTP 408 and 429

The ordinary `sk-` API key and hotspot password are read by the host runner
from the Git-ignored `.secrets/velafit.env`.  They are entered through a
non-echoing console prompt, never compiled into the image, redacted before
host output is emitted, and erased from the application's RAM buffer after
use.

## Build and flash identity

- Target: ESP32-P4X Function EV Board V1.8, physical P4 revision v3.2
- Configuration: `contest_board:velafit_headless`
- Compiler: `riscv-none-elf-gcc 13.4.0`
- Firmware: `nuttx.bin`, 3,742,804 bytes
- Firmware SHA-256:
  `f1b4ee414752e155d8afabcd0469571075d8d40754f98ba559a0d7c6c2a418f0`
- Link audit: `mbedtls_hardware_poll` is present; P4 `/dev/random` supplies
  Mbed TLS entropy and platform fallback entropy is disabled
- Flash: esptool reported `Hash of data verified` and reset the board

The ROM's later `SHA-256 comparison failed` message is a known image-header
warning for this RAM-only image format: the image-generation command states
that the digest is not appended.  It is distinct from esptool's successful
write verification and did not prevent boot or any test below.  Adding an
image digest or secure-boot policy remains a separate release-hardening task.

## Physical-device result

Acceptance started at `2026-09-13T14:04:22.104735+08:00` through COM3.  The
device obtained network connectivity through the fitted C6 and the real 2.4
GHz hotspot.

| Check | Result | Device evidence |
| --- | --- | --- |
| C6 association and NuttX interface | PASS | `eth0`, C6 MAC reported, carrier connected |
| Network time before TLS | PASS | epoch `1789279469`, two-second initial wait |
| Wrong hostname rejection | PASS | `invalid.example` handshake rejected with `-0x7780` while TCP still targeted MiMo |
| Certificate chain and hostname | PASS | `api.xiaomimimo.com`, verification flags zero |
| Negotiated transport | PASS | TLS 1.2, `TLS-ECDHE-RSA-WITH-CHACHA20-POLY1305-SHA256` |
| Official API authentication/request | PASS | HTTP 200 on attempt 1 of 3 |
| Response framing and JSON parse | PASS | 602-byte body, non-empty `choices[0].message.content` |
| Real semantic response | PASS | `VelaFit MiMo device cloud OK` |
| Device request latency | PASS | 2,130 ms, including TLS and API processing |

Credential-safe raw evidence is in
`hardware-logs/c6-mimo-device-20260913-final.log`.

## Security and operational notes

- Certificate verification is mandatory; the negative hostname test prevents
  a TCP-only success from being reported as TLS acceptance.
- A single embedded public root minimizes the current image, but a production
  certificate bundle and root-rotation procedure are still required.
- NTP is an unauthenticated clock bootstrap.  A persisted trusted time floor or
  authenticated time strategy should be added for production hardening.
- The command accepts only an ordinary `sk-` key.  It deliberately rejects a
  Token Plan `tp-` key for the device-runtime path.
- Success requires HTTP 2xx plus a non-empty parsed semantic content field;
  HTTP 200 alone is not sufficient.

## Boundary of this milestone

This closes the **device-side TLS plus real MiMo text API** milestone and is
level 3 evidence: the physical P4 performs the cloud request through C6.  It
does not claim image upload, camera-to-cloud action analysis, ASR, TTS audio
decoding/playback, display output, or the full capture/inference/prompt loop.
Those remain separate functional and end-to-end acceptance items.

