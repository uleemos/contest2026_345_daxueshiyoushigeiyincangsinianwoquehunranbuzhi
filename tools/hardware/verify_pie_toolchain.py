#!/usr/bin/env python3
"""Verify and safely unpack the pinned official PIE toolchain privately.

Download is deliberately separate. No global PATH or active compiler changes.
Only the official SHA256 artifact below is accepted. Existing directories are
never overwritten. Python >=3.12 is required for tarfile's data extraction.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tarfile

TEAM = Path(__file__).resolve().parents[2]
SHA256 = '1d3a1b6a064686d9b77c4db7731f82e26c072e312e27969c45fe96410ecb2671'
URL = ('https://github.com/espressif/crosstool-NG/releases/download/'
       'esp-14.2.0_20251107/'
       'riscv32-esp-elf-14.2.0_20251107-x86_64-linux-gnu.tar.xz')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('archive', type=Path)
    p.add_argument('destination', type=Path)
    a = p.parse_args()
    destination = a.destination.resolve()
    if not destination.is_relative_to((TEAM / '.secrets').resolve()):
        p.error('destination must be private under .secrets')
    if destination.exists():
        p.error('destination already exists; refusing to overwrite')
    with a.archive.open('rb') as f:
        actual = hashlib.file_digest(f, 'sha256').hexdigest()
    if actual != SHA256:
        p.error('official archive SHA256 mismatch; not extracting')
    with tarfile.open(a.archive, 'r:xz') as archive:
        members = archive.getmembers()
        for m in members:
            if m.name != 'riscv32-esp-elf' and not m.name.startswith('riscv32-esp-elf/'):
                p.error('unexpected archive root')
        destination.mkdir(mode=0o700)
        archive.extractall(destination, filter='data')
    compiler = destination / 'riscv32-esp-elf/bin/riscv32-esp-elf-gcc'
    subprocess.run([str(compiler), '--version'], check=True, timeout=10)
    print('Official SHA256 PASS:', actual)
    print('Source:', URL)
    print('Isolated toolchain ready; active project compiler unchanged')


if __name__ == '__main__':
    main()
