#!/usr/bin/env python3
"""Run NSH commands over the ESP32-P4 USB Serial/JTAG console."""

import argparse
import pathlib
import sys
import time
from typing import BinaryIO

import serial
from serial.serialutil import SerialException


def open_port(port_name: str, deadline: float) -> serial.Serial:
    while time.monotonic() < deadline:
        try:
            port = serial.Serial()
            port.port = port_name
            port.baudrate = 115200
            port.timeout = 0.1
            port.write_timeout = 2
            port.dtr = False
            port.rts = False
            port.open()
            return port
        except (OSError, SerialException):
            time.sleep(0.2)

    raise SerialException(f"{port_name} did not become available")


def reopen_port(port: serial.Serial, deadline: float) -> None:
    """Reopen the same Serial object after USB Serial/JTAG re-enumeration."""
    port.close()
    while time.monotonic() < deadline:
        try:
            port.open()
            return
        except (OSError, SerialException):
            time.sleep(0.2)

    raise SerialException(f"{port.port} did not become available")


def write_command(port: serial.Serial, payload: bytes) -> None:
    """Reset input and write, retrying across a transient COM re-enumeration."""
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        try:
            port.reset_input_buffer()
            port.write(payload)
            port.flush()
            return
        except (OSError, SerialException):
            reopen_port(port, deadline)

    raise SerialException(f"write to {port.port} timed out")


def read_until_prompt(
    port: serial.Serial,
    timeout: float,
    command: bytes | None = None,
    log_file: BinaryIO | None = None,
    echo_output: bool = True,
) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        try:
            chunk = port.read(port.in_waiting or 1)
        except (OSError, SerialException):
            reopen_port(port, time.monotonic() + 10)
            continue

        if chunk:
            data.extend(chunk)
            if echo_output:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
            if log_file is not None:
                log_file.write(chunk)
                log_file.flush()
            if command is None:
                if b"nsh>" in data:
                    break
            else:
                echo = data.find(command)
                if echo >= 0 and data.find(b"nsh>", echo + len(command)) >= 0:
                    break
        else:
            time.sleep(0.02)

    return bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("commands", nargs="+")
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--expect", action="append", default=[])
    parser.add_argument("--log", type=pathlib.Path, required=True)
    args = parser.parse_args()

    args.log.parent.mkdir(parents=True, exist_ok=True)
    port = open_port(args.port, time.monotonic() + 20)
    transcript = bytearray()
    with args.log.open("wb") as log_file:
        try:
            write_command(port, b"\r\n")
            transcript.extend(read_until_prompt(port, 15, log_file=log_file))
            time.sleep(0.25)
            port.read(port.in_waiting or 1)
            for command in args.commands:
                marker = f"\n===== COMMAND: {command} =====\n".encode()
                sys.stdout.buffer.write(marker)
                sys.stdout.buffer.flush()
                log_file.write(marker)
                log_file.flush()
                transcript.extend(marker)
                encoded = command.encode("ascii")
                write_command(port, encoded + b"\r\n")
                response = read_until_prompt(port, args.timeout, encoded, log_file)
                transcript.extend(response)
                echo = response.find(encoded)
                if echo < 0 or response.find(b"nsh>", echo + len(encoded)) < 0:
                    message = b"\nFAIL: command did not return to NSH before timeout\n"
                    log_file.write(message)
                    log_file.flush()
                    sys.stderr.buffer.write(message)
                    return 2
        finally:
            port.close()

    missing = [item for item in args.expect if item.encode() not in transcript]
    for item in missing:
        print(f"MISSING expected text: {item}", file=sys.stderr)

    return 2 if missing or b"nsh>" not in transcript else 0


if __name__ == "__main__":
    raise SystemExit(main())
