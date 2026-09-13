# Xiaomi MiMo computer API acceptance

Date: 2026-09-13 (Asia/Shanghai)

## Scope

This test proves only that the ordinary API account/key, official endpoint,
TLS connection, authentication header, request schema, and minimal text-model
response work from the development computer. It does not prove the ESP32-P4/C6
network path, image/ASR/TTS requests, or the complete VelaFit device loop.

Credentials were read from the Git-ignored `.secrets/velafit.env`. The key was
not printed, copied into this report, or stored in the test result.

## Contract

- Execution location: development computer
- Endpoint: `POST https://api.xiaomimimo.com/v1/chat/completions`
- Authentication: `api-key` header, value redacted
- Content type: `application/json`
- Model: `mimo-v2.5`
- TLS: system CA trust and hostname verification enabled

## Results

| Time | Limit | HTTP | Latency | Semantic validation | Result |
|---|---:|---:|---:|---|---|
| 2026-09-13 11:16:37 +08:00 | 64 completion tokens | 200 | 1,653 ms | `message.content` empty | FAIL |
| 2026-09-13 11:17:05 +08:00 | 512 completion tokens | 200 | 1,872 ms | `finish_reason=stop`; non-empty expected reply | PASS |

The passing request returned response ID
`d0c730c7-3f1b-4015-bb07-b4b48aedb523_7dbcb48947d5405f8e6c960931ee4785`,
160 reasoning characters, and the following short response:

```text
VelaFit MiMo 云端连接正常
```

The first HTTP 200 is deliberately retained as a failed attempt: transport
success alone is insufficient when the expected model content is absent. The
second request is the accepted computer-level real MiMo API smoke result.

## Remaining acceptance layers

1. C6 IP data plane plus DHCP/DNS/TCP stability.
2. ESP32-P4 TLS CA/SNI/hostname verification and failure cases.
3. Real device text request and validated response.
4. Image, ASR, and TTS request/response paths.
5. Full capture, inference, cloud, UI/audio loop.
