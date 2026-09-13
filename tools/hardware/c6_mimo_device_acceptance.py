#!/usr/bin/env python3
"""Run credential-safe ESP32-P4/C6 TLS and real MiMo acceptance."""

import argparse
import datetime
import pathlib
import sys
import time

import serial
from serial.serialutil import SerialException


def load_env(path: pathlib.Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def open_port(name: str, timeout: float) -> serial.Serial:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            return serial.Serial(name, 115200, timeout=0.1, write_timeout=2,
                                 dsrdtr=False, rtscts=False)
        except (OSError, SerialException):
            time.sleep(0.2)
    raise SerialException(f"{name} did not become available")


def emit(data: bytes, log_file) -> None:
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()
    log_file.write(data)
    log_file.flush()


def read_until(port: serial.Serial, token: bytes, timeout: float,
               log_file, secrets: tuple[bytes, ...] = ()) -> bytes:
    """Buffer until token, then redact before any bytes leave the process."""
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            data.extend(chunk)
            if token in data:
                clean = bytes(data)
                for secret in secrets:
                    if secret:
                        clean = clean.replace(secret, b"<redacted>")
                emit(clean, log_file)
                return clean
        else:
            time.sleep(0.02)
    raise TimeoutError(f"timed out waiting for {token!r}")


def send(port: serial.Serial, value: str) -> None:
    # NuttX's canonical console consumes CR as Enter.  CRLF can leave LF for
    # the next fgets() prompt and accidentally submit an empty credential.
    port.write(value.encode("utf-8") + b"\r")
    port.flush()


def command(port: serial.Serial, value: str, timeout: float, log_file,
            secrets: tuple[bytes, ...]) -> bytes:
    marker = f"\n===== COMMAND: {value} =====\n".encode()
    emit(marker, log_file)
    send(port, value)
    return read_until(port, b"nsh>", timeout, log_file, secrets)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--env", type=pathlib.Path, required=True)
    parser.add_argument("--log", type=pathlib.Path, required=True)
    args = parser.parse_args()

    env = load_env(args.env)
    ssid = env.get("VELAFIT_WIFI_SSID", "")
    password = env.get("VELAFIT_WIFI_PASSWORD", "")
    api_key = env.get("MIMO_API_KEY", "")
    if not ssid or not password:
        print("missing VELAFIT_WIFI_SSID or VELAFIT_WIFI_PASSWORD",
              file=sys.stderr)
        return 2
    if not api_key.startswith("sk-"):
        print("missing ordinary sk- MIMO_API_KEY", file=sys.stderr)
        return 2

    secrets = (password.encode("utf-8"), api_key.encode("utf-8"))
    args.log.parent.mkdir(parents=True, exist_ok=True)
    transcript = bytearray()
    port = open_port(args.port, 20)
    port.dtr = False
    port.rts = False
    try:
        with args.log.open("wb") as log_file:
            started = datetime.datetime.now().astimezone().isoformat()
            emit((f"C6 MiMo device acceptance started={started} "
                  f"port={args.port} endpoint="
                  "https://api.xiaomimimo.com/v1/chat/completions "
                  "model=mimo-v2.5\n").encode(), log_file)
            send(port, "")
            transcript.extend(read_until(port, b"nsh>", 15, log_file,
                                         secrets))

            emit(b"\n===== COMMAND: c6_wifi connect (interactive) =====\n",
                 log_file)
            send(port, "c6_wifi connect")
            transcript.extend(read_until(port, b"Wi-Fi SSID:", 20,
                                         log_file, secrets))
            send(port, ssid)
            transcript.extend(read_until(port, b"Wi-Fi password (hidden):",
                                         10, log_file, secrets))
            send(port, password)
            connect = read_until(port, b"nsh>", 45, log_file, secrets)
            transcript.extend(connect)
            if b"netdev=eth0" not in connect:
                print("CHECK association/netdev: FAIL", file=sys.stderr)
                return 2

            transcript.extend(command(port, "ifup eth0", 10, log_file,
                                      secrets))
            transcript.extend(command(port, "ifconfig eth0 dhcp", 30,
                                      log_file, secrets))

            # First prove that VERIFY_REQUIRED rejects a deliberately wrong
            # name while the TCP connection still targets the fixed MiMo host.
            negative = command(port, "c6_wifi tls invalid.example", 60,
                               log_file, secrets)
            transcript.extend(negative)
            negative_pass = (b"TLS handshake FAIL:" in negative and
                             b"TLS verify PASS:" not in negative)

            positive = command(port, "c6_wifi tls", 60, log_file, secrets)
            transcript.extend(positive)
            positive_pass = b"TLS verify PASS: host=api.xiaomimimo.com" in positive

            emit(b"\n===== COMMAND: c6_wifi mimo (interactive key) =====\n",
                 log_file)
            send(port, "c6_wifi mimo")
            transcript.extend(read_until(port, b"MiMo API key (hidden):",
                                         30, log_file, secrets))
            send(port, api_key)
            mimo = read_until(port, b"nsh>", 180, log_file, secrets)
            transcript.extend(mimo)
            mimo_pass = (b"MiMo device PASS: status=200" in mimo and
                         b"MiMo response:" in mimo and
                         b"TLS verify PASS: host=api.xiaomimimo.com" in mimo)
    finally:
        port.close()
        password = ""
        api_key = ""

    checks = {
        "association/netdev": b"netdev=eth0" in transcript,
        "hostname mismatch rejected": negative_pass,
        "trusted TLS + hostname": positive_pass,
        "real MiMo HTTP/semantic response": mimo_pass,
    }
    for name, passed in checks.items():
        print(f"CHECK {name}: {'PASS' if passed else 'FAIL'}")
    return 0 if all(checks.values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
