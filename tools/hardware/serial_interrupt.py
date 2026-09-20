#!/usr/bin/env python3
"""Send Ctrl-C to a NuttX NSH console and wait for its prompt."""

import argparse
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()
    deadline = time.monotonic() + args.timeout
    with serial.Serial(args.port, 115200, timeout=0.1, write_timeout=2) as port:
        port.dtr = False
        port.rts = False
        port.reset_input_buffer()
        port.write(b"\x03\r\n")
        port.flush()
        data = bytearray()
        while time.monotonic() < deadline:
            chunk = port.read(port.in_waiting or 1)
            if chunk:
                data.extend(chunk)
                print(chunk.decode("utf-8", "replace"), end="", flush=True)
                if b"nsh>" in data:
                    return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
