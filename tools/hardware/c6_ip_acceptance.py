#!/usr/bin/env python3
"""Exercise the C6 IP data plane without exposing Wi-Fi credentials."""

import argparse
import datetime
import pathlib
import re
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


def read_until(port: serial.Serial, token: bytes, timeout: float,
               log_file) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            data.extend(chunk)
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            log_file.write(chunk)
            log_file.flush()
            if token in data:
                return bytes(data)
        else:
            time.sleep(0.02)
    raise TimeoutError(f"timed out waiting for {token!r}")


def read_until_redacted(port: serial.Serial, token: bytes, timeout: float,
                        log_file, secret: bytes) -> bytes:
    """Buffer a response and redact a secret before emitting any output."""
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            data.extend(chunk)
            if token in data:
                clean = bytes(data).replace(secret, b"<redacted>")
                sys.stdout.buffer.write(clean)
                sys.stdout.buffer.flush()
                log_file.write(clean)
                log_file.flush()
                return clean
        else:
            time.sleep(0.02)
    raise TimeoutError(f"timed out waiting for {token!r}")


def send(port: serial.Serial, value: str) -> None:
    # NuttX treats CR as Enter.  Sending CRLF leaves the LF queued after
    # fgets() consumes the SSID, causing the following password read to see
    # an empty line.
    port.write(value.encode("utf-8") + b"\r")
    port.flush()


def command(port: serial.Serial, value: str, timeout: float, log_file) -> bytes:
    marker = f"\n===== COMMAND: {value} =====\n".encode()
    sys.stdout.buffer.write(marker)
    sys.stdout.buffer.flush()
    log_file.write(marker)
    log_file.flush()
    send(port, value)
    return read_until(port, b"nsh>", timeout, log_file)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--env", type=pathlib.Path, required=True)
    parser.add_argument("--log", type=pathlib.Path, required=True)
    parser.add_argument("--dns-host", default="api.xiaomimimo.com")
    args = parser.parse_args()

    env = load_env(args.env)
    ssid = env.get("VELAFIT_WIFI_SSID", "")
    password = env.get("VELAFIT_WIFI_PASSWORD", "")
    if not ssid or not password:
        print("missing VELAFIT_WIFI_SSID or VELAFIT_WIFI_PASSWORD",
              file=sys.stderr)
        return 2

    args.log.parent.mkdir(parents=True, exist_ok=True)
    transcript = bytearray()
    port = open_port(args.port, 20)
    port.dtr = False
    port.rts = False
    try:
        with args.log.open("wb") as log_file:
            started = datetime.datetime.now().astimezone().isoformat()
            header = (
                f"C6 IP acceptance started={started} port={args.port} "
                f"dns_host={args.dns_host}\n"
            ).encode()
            sys.stdout.buffer.write(header)
            log_file.write(header)
            send(port, "")
            transcript.extend(read_until(port, b"nsh>", 15, log_file))

            marker = b"\n===== COMMAND: c6_wifi connect (interactive) =====\n"
            sys.stdout.buffer.write(marker)
            sys.stdout.buffer.flush()
            log_file.write(marker)
            send(port, "c6_wifi connect")
            transcript.extend(read_until(port, b"Wi-Fi SSID:", 20,
                                         log_file))
            send(port, ssid)
            transcript.extend(read_until(port, b"Wi-Fi password (hidden):",
                                         10, log_file))
            send(port, password)
            connect_result = read_until_redacted(
                port, b"nsh>", 45, log_file, password.encode("utf-8")
            )
            transcript.extend(connect_result)
            if b"netdev=eth0" not in connect_result:
                print("CHECK association/netdev: FAIL", file=sys.stderr)
                return 2

            transcript.extend(command(port, "ifup eth0", 10, log_file))
            transcript.extend(command(port, "ifconfig eth0 dhcp", 30,
                                      log_file))
            transcript.extend(command(port, "ifconfig", 10, log_file))
            transcript.extend(command(port, f"nslookup {args.dns_host}", 20,
                                      log_file))
            transcript.extend(command(port, "ping -c 3 1.1.1.1", 20,
                                      log_file))
            transcript.extend(command(
                port, f"c6_wifi tcp {args.dns_host} 443", 20, log_file
            ))
    finally:
        port.close()
        password = ""

    checks = {
        "association/netdev": b"netdev=eth0" in transcript,
        "DHCP IPv4": re.search(rb"inet addr:(?!0\.0\.0\.0)", transcript)
        is not None,
        "DNS": (f"Host: {args.dns_host} Addr:").encode() in transcript,
        "external ICMP": b"3 received" in transcript,
        "MiMo TCP/443": b"TCP connect PASS:" in transcript,
    }
    for name, passed in checks.items():
        print(f"CHECK {name}: {'PASS' if passed else 'FAIL'}")
    return 0 if all(checks.values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
