#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check persistent build assets. Never reads API credentials or downloads."""
import hashlib
from pathlib import Path
import sys

TEAM = Path(__file__).resolve().parents[2]
EXPECTED = {
 'movenet_singlepose_lightning_int8_v4.tflite':
 'cd7cc22fa946e5d146a7b98d496853e1923e22828d3972d579973f27f91bb105',
 'velafit_pose_fixture_rgb192.bin':
 '65769e2eba019f3ddcb5e0b26e512d95561d0369b84816b4f25ba93d62bd0fed',
 'okay_nabu.tflite':
 '0689abe1912a95a3318a0d8cb2e67bad0cbcfe3e24dd6e050c75debddfb6f891',
}

def main():
    failed = False
    for name, digest in EXPECTED.items():
        path = TEAM/'.secrets/build-inputs'/name
        good = path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == digest
        print(f'{name}: {"PASS" if good else "MISSING OR HASH MISMATCH"}')
        failed |= not good
    return int(failed)

if __name__ == '__main__':
    sys.exit(main())
