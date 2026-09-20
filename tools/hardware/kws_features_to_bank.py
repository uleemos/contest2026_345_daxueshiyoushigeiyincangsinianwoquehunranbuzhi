#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Convert a private board feature transcript into a private template bank."""

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ROW = re.compile(r"^\[KWS-FEATURE\] (\d+) ([0-9a-f]{8})((?: [0-9a-f]{8}){13})\r?$")


def private_new(path: Path) -> None:
    if not path.resolve().is_relative_to((ROOT / ".secrets").resolve()):
        raise ValueError("output must stay under .secrets")
    if path.exists():
        raise FileExistsError(f"refusing to overwrite {path}")


def decode(word: str) -> float:
    return struct.unpack("<f", struct.pack("<I", int(word, 16)))[0]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--phrases", type=int, default=5)
    parser.add_argument("--join-gap", type=int, default=50)
    args = parser.parse_args()
    private_new(args.header)
    private_new(args.report)
    raw = args.log.read_bytes()
    if b"[PCM]" in raw or b"[PCM-BEGIN]" in raw:
        raise ValueError("raw PCM is not accepted by this feature-only converter")
    rows = []
    for line in raw.decode("ascii", errors="ignore").splitlines():
        match = ROW.match(line)
        if match:
            words = match.group(3).split()
            rows.append((int(match.group(1)), decode(match.group(2)),
                         [decode(word) for word in words]))
    if not rows:
        raise ValueError("no feature rows")
    segments = []
    current = []
    previous = None
    for row in rows:
        if previous is not None and row[0] - previous > args.join_gap:
            if len(current) >= 20:
                segments.append(current)
            current = []
        current.append(row)
        previous = row[0]
    if len(current) >= 20:
        segments.append(current)
    if len(segments) != args.phrases:
        raise ValueError(f"expected {args.phrases} phrases, found {len(segments)}")
    lines = ["/* PRIVATE derived voice features. DO NOT PUBLISH. */"]
    for index, segment in enumerate(segments):
        lines.append(f"static const float g_kws_live_bank_{index}[] = {{")
        for _, _, values in segment:
            lines.append(",".join(f"{value:.9e}f" for value in values) + ",")
        lines.append("};")
    lines.append("static const float * const g_kws_live_bank[] = {" +
                 ",".join(f"g_kws_live_bank_{i}" for i in range(len(segments))) +
                 "};")
    lines.append("static const unsigned int g_kws_live_bank_frames[] = {" +
                 ",".join(str(len(segment)) for segment in segments) + "};")
    lines.append(f"#define KWS_LIVE_BANK_COUNT {len(segments)}u")
    args.header.write_text("\n".join(lines) + "\n")
    args.header.chmod(0o600)
    report = {
        "source_sha256": hashlib.sha256(raw).hexdigest(),
        "contains_raw_pcm": False,
        "feature_dimension": 13,
        "rms_gate": 80,
        "join_gap_frames": args.join_gap,
        "segments": [
            {"first_frame": segment[0][0], "last_frame": segment[-1][0],
             "active_frames": len(segment),
             "peak_rms": round(max(row[1] for row in segment), 3)}
            for segment in segments
        ],
        "status": "TRAINING_ONLY_NOT_ACCEPTANCE",
    }
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    args.report.chmod(0o600)
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
