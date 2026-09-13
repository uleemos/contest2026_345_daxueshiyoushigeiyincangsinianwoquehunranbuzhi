#!/usr/bin/env python3
"""Create a deterministic 192x192 RGB888 MoveNet fixture."""

import argparse
import hashlib
import io
import pathlib
import urllib.request

from PIL import Image, ImageOps


def read_source(source: str) -> bytes:
    if source.startswith(("https://", "http://")):
        request = urllib.request.Request(
            source, headers={"User-Agent": "VelaFit-fixture-preparer/1.0"})
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.read()
    return pathlib.Path(source).read_bytes()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", help="JPG/PNG path or HTTPS URL")
    parser.add_argument("output", type=pathlib.Path,
                        help="output RGB888 binary")
    parser.add_argument("--preview", type=pathlib.Path)
    args = parser.parse_args()

    source_data = read_source(args.source)
    with Image.open(io.BytesIO(source_data)) as opened:
        image = ImageOps.exif_transpose(opened).convert("RGB")
        resized = ImageOps.contain(image, (192, 192), Image.Resampling.BILINEAR)

    canvas = Image.new("RGB", (192, 192), (0, 0, 0))
    offset = ((192 - resized.width) // 2, (192 - resized.height) // 2)
    canvas.paste(resized, offset)
    raw = canvas.tobytes()
    if len(raw) != 192 * 192 * 3:
        raise RuntimeError(f"unexpected RGB size: {len(raw)}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(raw)
    if args.preview:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        canvas.save(args.preview)

    print(f"source_sha256={hashlib.sha256(source_data).hexdigest()}")
    print(f"rgb192_sha256={hashlib.sha256(raw).hexdigest()}")
    print(f"rgb192_bytes={len(raw)} resized={resized.width}x{resized.height} "
          f"offset={offset[0]},{offset[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
