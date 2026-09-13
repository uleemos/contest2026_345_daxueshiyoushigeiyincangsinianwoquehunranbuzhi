# VelaFit MiMo real-API compatibility audit

Date: 2026-09-13 (Asia/Shanghai)

## Acceptance boundary

The current firmware has no real MiMo network request path. Its cloud output is
simulation only. A mock sender returning success, a configured default URL, or
an HTTP 200 without validated response content must not be reported as real
cloud AI acceptance.

Four evidence levels must remain separate:

1. simulated receiver or mock callback;
2. computer calling the real MiMo API;
3. ESP32-P4/C6 calling the real MiMo API;
4. full device capture, upload, parse, display/audio playback loop.

Cloud connectivity and exercise-analysis/counting accuracy are separate test
items.

## Official contract used by this audit

- Base URL: `https://api.xiaomimimo.com/v1`
- Endpoint: `POST /chat/completions`
- Headers: `api-key: <ordinary sk- key>` and `Content-Type: application/json`
- Image understanding: `mimo-v2.5`, `image_url.url` accepts a public URL or
  `data:<mime>;base64,<data>`.
- ASR: `mimo-v2.5-asr`, `input_audio.data` accepts WAV/MP3 Base64 (10 MB encoded
  limit), with optional `asr_options.language`.
- TTS: `mimo-v2.5-tts`; target speech text must be an `assistant` message.
  Non-streaming audio is Base64 in `choices[0].message.audio.data`; streaming
  output should be PCM16 (24 kHz, mono).

Ordinary `sk-` and Token Plan `tp-` credentials are separate and must not be
mixed. This project uses only an ordinary API key read from a private runtime
configuration.

## Existing-code findings

| Area | Current behavior | Compatibility |
|---|---|---|
| URL defaults | `velafit_config.h` uses `https://api.mimo.mi.com/v1` | Incorrect host |
| Models | reasoning defaults to `mimo-v2.5-pro`; TTS defaults to voice clone | Does not match selected `mimo-v2.5` / `mimo-v2.5-tts` path |
| HTTP/TLS | No HTTP request builder, TLS client, CA store, SNI, or hostname verification | Missing |
| Authentication | No real request header generation | Missing |
| Image/audio encoding | No Base64/Data URL request construction | Missing |
| Response parsing | Only local result structures and fixed mock data | Missing |
| Session sync | Generic callback with custom topic `velafit/v1/sessions/upload` | Custom backend abstraction, not MiMo wire protocol |
| Sender registration | `velafit_ai_main.c` registers only `velafit_sync_mock_sender` | Simulation only |
| Cloud agent | ASR/reasoning/TTS outputs are hard-coded and print a simulation PASS | Simulation only |
| Runtime config | Loader reads only URL and key; saver writes more fields | Incomplete round-trip |
| Device secrets | API key is written as plaintext JSON under `/data` or `/tmp` | Not acceptable as final provisioning design |
| C6 Wi-Fi | SDIO ESP-Hosted handshake/RPC and STA association are implemented | Control plane only |
| C6 IP data plane | No NuttX netdev registration or Ethernet-frame RX/TX path | Blocking DNS/TCP/TLS |
| Defconfig | NET/DNS/TCP/UDP/ICMP enabled | HTTP client, TLS library, CA configuration absent |

The CLI form `c6_wifi connect <ssid> <password>` also exposes the password in
interactive input/history and must not be the final credential mechanism.

## Minimal implementation choices

### A. Device calls MiMo directly

Common prerequisite: first complete the C6 data plane and register a NuttX
network interface, then prove DHCP, DNS, TCP, and loss/reconnect behavior.

Additional device work:

1. Enable an HTTPS client and TLS stack with a trusted CA bundle, SNI, hostname
   verification, correct clock handling, connect/read timeouts, and bounded
   exponential-backoff retry with jitter.
2. Add typed MiMo request builders and response validators for text/image, ASR,
   and TTS. Do not pass the existing sync topic/payload directly to MiMo.
3. Add streaming or bounded Base64 encode/decode. Full-frame Base64 expands data
   by roughly one third and can collide with camera, TFLM, and display memory.
4. Add safe device credential provisioning. Never compile the key into firmware,
   pass it in a shell command, or print it.
5. Relabel mock commands/results so only real transport plus validated model
   response can produce a real-cloud PASS.

This avoids a new backend, but has the largest firmware/TLS/memory/security
surface and puts the MiMo API key on the device.

### B. Keep the device protocol and add an HTTPS adapter gateway

The gateway can retain a typed evolution of the current session-upload API,
hold the MiMo key, construct official MiMo requests, and normalize large JSON
and audio responses for the device. The device still needs the C6 IP data plane
and TLS/hostname verification to reach the gateway.

This reduces device RAM and secret exposure and is usually the safer demo
architecture, but requires a deployed, publicly reachable service. No gateway
deployment or paid public service is authorized by this audit.

## Recommended sequence

1. **P0 — computer real-API smoke: COMPLETE.** An ordinary-key request produced
   a validated non-empty `mimo-v2.5` response on 2026-09-13. See
   `artifacts/hardware/2026-09-13-headless/MIMO_PC_API_ACCEPTANCE.md`. This proves
   account/auth/API only.
2. **P0 — C6 IP data plane:** netdev RX/TX, DHCP, DNS, ping/TCP, reconnect, and
   repeated transfer stability. Association alone is not completion.
3. **P0 — device TLS:** CA trust, SNI and hostname validation, valid/invalid
   certificate cases, clock behavior, timeout and retry evidence.
4. **P1 — one real device text call:** smallest payload first, validate JSON
   structure and semantic content, and keep credentials out of logs.
5. **P1 — image request:** downscale/compress a captured frame, Base64/Data URL,
   upload, parse useful posture advice, and measure peak heap and latency.
6. **P1 — TTS decode/playback:** first save/validate decoded WAV or PCM silently;
   perform audible playback during daytime.
7. **P2 — ASR:** first use a fixed WAV fixture silently; validate live microphone
   capture and recognition during daytime.
8. **P2 — full loop and fault injection:** capture -> local inference -> cloud ->
   UI/audio, plus DNS failure, TLS failure, 401/429/5xx, timeout, reconnect, and
   offline queue behavior.

For retries, retry transient network errors, 408, 429, and selected 5xx with a
small bounded attempt count and jitter; honor `Retry-After` when present. Do not
blindly retry authentication or malformed-request 4xx responses.
