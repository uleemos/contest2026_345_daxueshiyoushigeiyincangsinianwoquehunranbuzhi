#!/usr/bin/env python3
"""Capture private SC2336 PPM frames from the ESP32-P4 NSH console."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import pathlib
import re
import sys
import time

import serial

from run_nsh import open_port, read_until_prompt, write_command


BEGIN = b"VELAFIT_CAMERA_PPM_BASE64_BEGIN"
END = b"VELAFIT_CAMERA_PPM_BASE64_END"
EXPECTED_WIDTH = 192
EXPECTED_HEIGHT = 192


def parse_ppm(payload: bytes) -> tuple[int, int, bytes]:
    match = re.fullmatch(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s(.+)", payload, re.S)
    if match is None:
        raise ValueError("invalid binary PPM")
    width, height, maximum = (int(match.group(i)) for i in range(1, 4))
    pixels = match.group(4)
    if maximum != 255 or len(pixels) != width * height * 3:
        raise ValueError("unexpected PPM dimensions, maximum, or payload length")
    return width, height, pixels


def extract_ppm(transcript: bytes) -> bytes:
    begin = transcript.find(BEGIN)
    end = transcript.find(END, begin + len(BEGIN))
    if begin < 0 or end < 0:
        raise ValueError("camera base64 markers missing")
    encoded = re.sub(rb"\s+", b"", transcript[begin + len(BEGIN) : end])
    return base64.b64decode(encoded, validate=True)


def rotate_cw90_ppm(payload: bytes) -> bytes:
    width, height, pixels = parse_ppm(payload)
    rotated = bytearray(len(pixels))
    output_width = height
    for source_y in range(height):
        for source_x in range(width):
            destination_x = height - 1 - source_y
            destination_y = source_x
            source = (source_y * width + source_x) * 3
            destination = (destination_y * output_width + destination_x) * 3
            rotated[destination : destination + 3] = pixels[source : source + 3]
    return f"P6\n{height} {width}\n255\n".encode("ascii") + rotated


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--session", required=True)
    parser.add_argument("--count", type=int, default=1)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--start-delay", type=float, default=0.0)
    parser.add_argument("--preview-seconds", type=int, default=0,
                        help="run LCD preview, then capture immediately in the same session")
    parser.add_argument("--preview-mode", choices=("camera", "model"),
                        default="camera",
                        help="camera preview or final 96x96 TinyPose input")
    parser.add_argument("--lcd-prompts", action="store_true",
                        help="show CAPTURING/DONE on LCD around frame export")
    parser.add_argument("--capture-start-delay", type=float, default=3.0,
                        help="settling time after the LCD active prompt")
    parser.add_argument("--rotate", choices=("none", "cw90"), default="none")
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()
    if (args.count < 1 or args.interval < 0 or args.start_delay < 0 or
            args.preview_seconds < 0 or args.capture_start_delay < 0):
        parser.error("count must be positive and delays must be non-negative")
    output = args.output.resolve()
    if ".secrets" not in output.parts:
        parser.error("private camera output must be under a .secrets directory")
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / "manifest.jsonl"
    if manifest_path.exists():
        parser.error(f"refusing to append to existing session: {output}")

    port = open_port(args.port, time.monotonic() + 20)
    records: list[dict[str, object]] = []
    try:
        write_command(port, b"\r\n")
        read_until_prompt(port, 15, echo_output=False)
        if args.start_delay:
            print(f"PREPARE seconds={args.start_delay:g}", flush=True)
            time.sleep(args.start_delay)
        if args.preview_seconds:
            preview_command = "preview_model" if args.preview_mode == "model" else "preview"
            preview_suffix = " 4" if args.preview_mode == "model" else ""
            preview = (f"sc2336_probe {preview_command} {args.preview_seconds}"
                       f"{preview_suffix}").encode("ascii")
            print(f"LCD_PREVIEW mode={args.preview_mode} "
                  f"seconds={args.preview_seconds}", flush=True)
            write_command(port, preview + b"\r\n")
            response = read_until_prompt(
                port, args.preview_seconds + 30, preview, echo_output=False
            )
            if b"LCD PREVIEW PASS" not in response:
                raise RuntimeError("LCD preview did not pass")
            print("LCD_PREVIEW PASS; capturing immediately", flush=True)
        if args.lcd_prompts:
            prompt = b"sc2336_probe capture_prompt active"
            write_command(port, prompt + b"\r\n")
            response = read_until_prompt(port, args.timeout, prompt,
                                         echo_output=False)
            if b"DATASET CAPTURE PROMPT ACTIVE" not in response:
                raise RuntimeError("LCD active capture prompt failed")
            print("LCD_PROMPT ACTIVE", flush=True)
            if args.capture_start_delay:
                print(f"CAPTURE_SETTLE seconds={args.capture_start_delay:g}",
                      flush=True)
                time.sleep(args.capture_start_delay)
        for index in range(args.count):
            if index:
                time.sleep(args.interval)
            command = b"velafit_ai camera_input dump"
            print(f"CAPTURE {index + 1}/{args.count}", flush=True)
            write_command(port, command + b"\r\n")
            transcript = read_until_prompt(port, args.timeout, command, echo_output=False)
            ppm = extract_ppm(transcript)
            if args.rotate == "cw90":
                ppm = rotate_cw90_ppm(ppm)
            width, height, _ = parse_ppm(ppm)
            if (width, height) != (EXPECTED_WIDTH, EXPECTED_HEIGHT):
                raise ValueError(f"unexpected image size {width}x{height}")
            name = f"frame-{index:04d}.ppm"
            (output / name).write_bytes(ppm)
            records.append(
                {
                    "image": name,
                    "session": args.session,
                    "capture_index": index,
                    "width": width,
                    "height": height,
                    "preview_seconds": args.preview_seconds,
                    "transform": args.rotate,
                    "sha256": hashlib.sha256(ppm).hexdigest(),
                }
            )
        if args.lcd_prompts:
            prompt = b"sc2336_probe capture_prompt done"
            write_command(port, prompt + b"\r\n")
            response = read_until_prompt(port, args.timeout, prompt,
                                         echo_output=False)
            if b"DATASET CAPTURE PROMPT DONE" not in response:
                raise RuntimeError("LCD done capture prompt failed")
            print("LCD_PROMPT DONE", flush=True)
    finally:
        port.close()

    manifest_path.write_text(
        "".join(json.dumps(record, sort_keys=True) + "\n" for record in records),
        encoding="utf-8",
    )
    print(f"PASS frames={len(records)} output={output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise
