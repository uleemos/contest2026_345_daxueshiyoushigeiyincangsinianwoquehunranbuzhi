#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Development-only leave-one-utterance and known-negative bank check."""

import argparse
import json
import tempfile
from pathlib import Path

from kws_acoustic_probe import extract, library, read_pcm, scores
from kws_features_to_bank import ROW, decode


def load_segments(path: Path, join_gap: int):
    rows = []
    for line in path.read_text(errors="ignore").splitlines():
        match = ROW.match(line)
        if match:
            rows.append((int(match.group(1)), decode(match.group(2)),
                         [decode(word) for word in match.group(3).split()]))
    segments, current, previous = [], [], None
    for row in rows:
        if previous is not None and row[0] - previous > join_gap:
            if len(current) >= 20:
                segments.append(current)
            current = []
        current.append(row)
        previous = row[0]
    if len(current) >= 20:
        segments.append(current)
    return segments


def best(lib, templates, frames):
    values = []
    for template in templates:
        result = scores(lib, [row[2] for row in template], frames, gated=True)
        eligible = [row for row in result if row["rms"] >= 80]
        if eligible:
            values.append(min(row["distance"] for row in eligible))
    return min(values) if values else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--negative", required=True, type=Path)
    parser.add_argument("--join-gap", type=int, default=50)
    args = parser.parse_args()
    segments = load_segments(args.log, args.join_gap)
    with tempfile.TemporaryDirectory(prefix="kws-bank-check-") as directory:
        lib = library(directory, bounded=True)
        positives = []
        for index, segment in enumerate(segments):
            frames = [(row[0] / 100.0, row[2], row[1]) for row in segment]
            positives.append(best(lib, segments[:index] + segments[index + 1:],
                                  frames))
        negative_frames = extract(lib, read_pcm(args.negative))
        negative = best(lib, segments, negative_frames)
    print(json.dumps({
        "status": "DEVELOPMENT_ONLY_NOT_ACCEPTANCE",
        "same_session_leave_one_out_best": positives,
        "known_negative_best": negative,
        "threshold": 0.17,
        "positive_all_pass": all(value is not None and value < 0.17
                                 for value in positives),
        "known_negative_pass": negative is None or negative >= 0.17,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
