#!/usr/bin/env python3
"""Run a credential-safe Xiaomi MiMo text API smoke test."""

from __future__ import annotations

import argparse
import base64
import json
import os
import ssl
import sys
import time
import wave
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path
from typing import Any


DEFAULT_ENV = ".secrets/velafit.env"
ALLOWED_HOST = "api.xiaomimimo.com"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise urllib.error.HTTPError(
            req.full_url, code, "redirect refused", headers, fp
        )


def load_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[name.strip()] = value
    return values


def redact(value: str, secret: str) -> str:
    return value.replace(secret, "[REDACTED]") if secret else value


def get_message_content(response: dict[str, Any]) -> str:
    choices = response.get("choices")
    if not isinstance(choices, list) or not choices:
        raise ValueError("response has no choices")
    message = choices[0].get("message")
    if not isinstance(message, dict):
        raise ValueError("response has no choices[0].message")
    content = message.get("content")
    if not isinstance(content, str) or not content.strip():
        raise ValueError("response message content is empty")
    return content.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env-file", default=DEFAULT_ENV)
    parser.add_argument("--output")
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--audio-wav", type=Path,
                        help="Transcribe a real WAV recording using MiMo ASR")
    args = parser.parse_args()

    env_path = Path(args.env_file)
    values = load_env(env_path)
    api_key = os.environ.get("MIMO_API_KEY") or values.get("MIMO_API_KEY", "")
    base_url = (
        os.environ.get("MIMO_BASE_URL")
        or values.get("MIMO_BASE_URL")
        or "https://api.xiaomimimo.com/v1"
    ).rstrip("/")

    if not api_key.startswith("sk-"):
        print("ERROR: ordinary MIMO_API_KEY (sk-) is not configured", file=sys.stderr)
        return 2

    endpoint = base_url + "/chat/completions"
    parsed = urllib.parse.urlparse(endpoint)
    if parsed.scheme != "https" or parsed.hostname != ALLOWED_HOST:
        print("ERROR: refusing to send the key to a non-official host", file=sys.stderr)
        return 2

    request_body = {
        "model": "mimo-v2.5",
        "messages": [
            {
                "role": "user",
                "content": "只回复：VelaFit MiMo 云端连接正常",
            }
        ],
        # MiMo may emit reasoning tokens before visible content. Keep enough
        # headroom that a successful HTTP response also has an answer to
        # validate; the official examples use 1024.
        "max_completion_tokens": 512,
    }
    encoded = json.dumps(request_body, ensure_ascii=False).encode("utf-8")
    model = "mimo-v2.5"
    if args.audio_wav:
        model = "mimo-v2.5-asr"
        if args.audio_wav.stat().st_size > 7_000_000:
            parser.error("WAV too large for this smoke test")
        with wave.open(str(args.audio_wav), "rb") as wav:
            if wav.getnframes() == 0 or wav.getsampwidth() != 2:
                parser.error("expected nonempty PCM16 WAV")
        request_body = {
            "model": model,
            "messages": [{"role": "user", "content": [{
                "type": "input_audio",
                "input_audio": {"data": "data:audio/wav;base64," +
                    base64.b64encode(args.audio_wav.read_bytes()).decode("ascii")},
            }]}],
            "asr_options": {"language": "auto"},
        }
        encoded = json.dumps(request_body).encode("utf-8")
    request = urllib.request.Request(
        endpoint,
        data=encoded,
        method="POST",
        headers={
            "api-key": api_key,
            "Content-Type": "application/json",
            "Accept": "application/json",
            "User-Agent": "VelaFit-MiMo-Smoke/1.0",
        },
    )

    context = ssl.create_default_context()
    opener = urllib.request.build_opener(
        urllib.request.HTTPSHandler(context=context), NoRedirect()
    )
    started_at = datetime.now().astimezone().isoformat(timespec="seconds")
    start = time.monotonic()
    status = 0
    response_id = None

    try:
        with opener.open(request, timeout=args.timeout) as response:
            status = response.status
            raw = response.read()
            header_request_id = response.headers.get("x-request-id")
    except urllib.error.HTTPError as exc:
        latency_ms = round((time.monotonic() - start) * 1000)
        raw = exc.read(4096)
        safe_error = redact(raw.decode("utf-8", errors="replace"), api_key)
        result = {
            "test_time": started_at,
            "execution_location": "computer",
            "model": model,
            "endpoint": endpoint,
            "auth": "api-key (value redacted)",
            "http_status": exc.code,
            "latency_ms": latency_ms,
            "result": "FAIL",
            "error_preview": safe_error[:300],
        }
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 1
    except Exception as exc:
        latency_ms = round((time.monotonic() - start) * 1000)
        safe_error = redact(f"{type(exc).__name__}: {exc}", api_key)
        result = {
            "test_time": started_at,
            "execution_location": "computer",
            "model": model,
            "endpoint": endpoint,
            "auth": "api-key (value redacted)",
            "http_status": None,
            "latency_ms": latency_ms,
            "result": "FAIL",
            "error_preview": safe_error[:300],
        }
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 1

    latency_ms = round((time.monotonic() - start) * 1000)
    finish_reason = None
    reasoning_chars = 0
    try:
        parsed_response = json.loads(raw)
        choices = parsed_response.get("choices", [])
        if choices and isinstance(choices[0], dict):
            finish_reason = choices[0].get("finish_reason")
            message = choices[0].get("message", {})
            if isinstance(message, dict):
                reasoning = message.get("reasoning_content")
                if isinstance(reasoning, str):
                    reasoning_chars = len(reasoning)
        content = get_message_content(parsed_response)
        response_id = parsed_response.get("id") or header_request_id
        result_name = "PASS" if 200 <= status < 300 else "FAIL"
    except (json.JSONDecodeError, ValueError) as exc:
        content = f"invalid response: {exc}"
        result_name = "FAIL"

    result = {
        "test_time": started_at,
        "execution_location": "computer",
        "model": model,
        "endpoint": endpoint,
        "auth": "api-key (value redacted)",
        "tls_verification": "system CA and hostname verification enabled",
        "http_status": status,
        "latency_ms": latency_ms,
        "response_id": response_id,
        "finish_reason": finish_reason,
        "reasoning_chars": reasoning_chars,
        "response_preview": redact(content, api_key)[:300],
        "result": result_name,
        "acceptance_scope": "nonempty transcription; phrase accuracy requires review"
                            if args.audio_wav else "text API smoke",
    }
    rendered = json.dumps(result, ensure_ascii=False, indent=2)
    print(rendered)
    if args.output:
        Path(args.output).write_text(rendered + "\n", encoding="utf-8")
    return 0 if result_name == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
