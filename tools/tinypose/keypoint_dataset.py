#!/usr/bin/env python3
"""Shared PPM and TinyPose keypoint dataset helpers."""

from __future__ import annotations

import json
import pathlib
import re

import numpy as np


KEYPOINTS = (
    "left_shoulder",
    "right_shoulder",
    "left_hip",
    "right_hip",
    "left_knee",
    "right_knee",
    "left_ankle",
    "right_ankle",
)
MOVENET_INDICES = (5, 6, 11, 12, 13, 14, 15, 16)


def read_ppm(path: pathlib.Path) -> np.ndarray:
    payload = path.read_bytes()
    match = re.fullmatch(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s(.+)", payload, re.S)
    if match is None:
        raise ValueError(f"invalid P6 PPM: {path}")
    width, height, maximum = (int(match.group(i)) for i in range(1, 4))
    pixels = match.group(4)
    if maximum != 255 or len(pixels) != width * height * 3:
        raise ValueError(f"invalid PPM payload: {path}")
    return np.frombuffer(pixels, dtype=np.uint8).reshape(height, width, 3).copy()


def load_jsonl(path: pathlib.Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def write_jsonl(path: pathlib.Path, records: list[dict]) -> None:
    path.write_text(
        "".join(json.dumps(item, sort_keys=True) + "\n" for item in records),
        encoding="utf-8",
    )
