#!/usr/bin/env python3
"""Render PPM keypoint records as a dependency-light PNG review sheet."""

from __future__ import annotations

import argparse
import binascii
import json
import pathlib
import struct
import zlib

import numpy as np

from keypoint_dataset import load_jsonl, read_ppm


COLORS = (
    (255, 64, 64), (255, 64, 64),
    (64, 255, 64), (64, 255, 64),
    (64, 160, 255), (64, 160, 255),
    (255, 224, 64), (255, 224, 64),
)
SEGMENTS = ((0, 1), (0, 2), (1, 3), (2, 3),
            (2, 4), (3, 5), (4, 6), (5, 7))


def line(image: np.ndarray, a: tuple[int, int], b: tuple[int, int]) -> None:
    count = max(abs(b[0] - a[0]), abs(b[1] - a[1]), 1) + 1
    xs = np.rint(np.linspace(a[0], b[0], count)).astype(int)
    ys = np.rint(np.linspace(a[1], b[1], count)).astype(int)
    good = (xs >= 0) & (xs < image.shape[1]) & (ys >= 0) & (ys < image.shape[0])
    image[ys[good], xs[good]] = (255, 255, 255)


def circle(image: np.ndarray, point: tuple[int, int], color: tuple[int, int, int],
           radius: int = 4) -> None:
    x, y = point
    yy, xx = np.ogrid[:image.shape[0], :image.shape[1]]
    mask = (xx - x) ** 2 + (yy - y) ** 2 <= radius ** 2
    image[mask] = color


def png_bytes(image: np.ndarray) -> bytes:
    height, width, _ = image.shape
    raw = b"".join(b"\0" + image[y].tobytes() for y in range(height))

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", binascii.crc32(kind + payload) & 0xffffffff))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                        8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--labels", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--scale", type=int, default=2)
    args = parser.parse_args()
    records = load_jsonl(args.labels)
    if not records or args.columns < 1 or args.scale < 1:
        parser.error("records, columns and scale must be positive")
    side = 192 * args.scale
    rows = (len(records) + args.columns - 1) // args.columns
    sheet = np.zeros((rows * side, args.columns * side, 3), dtype=np.uint8)
    for index, record in enumerate(records):
        image = read_ppm(args.labels.parent / record["image"])
        image = np.repeat(np.repeat(image, args.scale, axis=0), args.scale, axis=1)
        points = []
        for point in record["keypoints"]:
            points.append((round(point["x"] * (side - 1)),
                           round(point["y"] * (side - 1))))
        for first, second in SEGMENTS:
            line(image, points[first], points[second])
        for point_index, (point, keypoint) in enumerate(zip(points, record["keypoints"])):
            color = COLORS[point_index] if keypoint["confidence"] >= 0.20 else (255, 0, 255)
            circle(image, point, color, 5)
        y = (index // args.columns) * side
        x = (index % args.columns) * side
        sheet[y:y + side, x:x + side] = image
    args.output.write_bytes(png_bytes(sheet))
    print(f"PASS records={len(records)} output={args.output}")


if __name__ == "__main__":
    main()
