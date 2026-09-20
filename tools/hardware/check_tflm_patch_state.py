#!/usr/bin/env python3
"""Read-only verification of essential nested TFLM changes before building."""
import hashlib
from pathlib import Path
import subprocess
import sys

TEAM = Path(__file__).resolve().parents[2]
SOURCE = TEAM.parent / 'apps/mlearning/tflite-micro/tflite-micro'
PATCHES = {
    'tflm-movenet-integer-postprocess.patch':
        '942556b4bbb17d737bfc3d12f96fabde9ca181a7ae3b6e10f60fd67e2b881b7e',
    'tflm-esp-nn-existing.patch':
        '282508c461d390b9acc1eee445566af7d8400f8b29ad3deaecd3813555ebd584',
}


def main():
    failed = False
    for name, expected in PATCHES.items():
        patch = TEAM / 'tools/patches' / name
        if hashlib.sha256(patch.read_bytes()).hexdigest() != expected:
            print('FAIL: saved patch hash changed:', name)
            failed = True
            continue
        result = subprocess.run(
            ['git', '-C', str(SOURCE), 'apply', '--reverse', '--check', str(patch)],
            capture_output=True, text=True, timeout=15)
        if result.returncode:
            print('FAIL: nested dependency patch absent or changed:', name)
            print('Inspect source and saved patch; no automatic overwrite performed.')
            failed = True
        else:
            print('PASS: nested TFLM source matches saved patch:', name)
    print('Source checks only; no inference or ESP-NN activation claimed.')
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
