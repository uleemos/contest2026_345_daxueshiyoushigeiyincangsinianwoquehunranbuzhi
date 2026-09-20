#!/usr/bin/env python3
"""Snapshot the verified clean P4 HAL dependency; never overwrite a cache.

Run after a completed build, not during clone/submodule initialization.
Avoids repeatedly downloading identical HAL history after configuration changes.
"""
from pathlib import Path
import argparse
import shutil
import subprocess

TEAM = Path(__file__).resolve().parents[2]
SOURCE = TEAM.parent / 'nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty'
CACHE = TEAM / '.secrets/dependency-cache/esp-hal-3rdparty'
COMMIT = '8d0a898910084206721a0892ab093021bca1496a'


def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()


def verify(path):
    if git(path, 'rev-parse', 'HEAD') != COMMIT:
        raise RuntimeError('wrong HAL revision')
    if git(path, 'status', '--porcelain', '--untracked-files=no'):
        raise RuntimeError('tracked HAL changes; refusing to snapshot/adopt')
    crypto = path / 'components/mbedtls/mbedtls'
    expected = git(path, 'ls-tree', 'HEAD', 'components/mbedtls/mbedtls').split()[2]
    if git(crypto, 'rev-parse', 'HEAD') != expected:
        raise RuntimeError('missing/wrong crypto submodule')
    if git(crypto, 'status', '--porcelain', '--untracked-files=no'):
        raise RuntimeError('crypto has tracked changes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-cache', action='store_true')
    args = parser.parse_args()
    if args.verify_cache:
        verify(CACHE)
        print('Pinned HAL cache verified:', COMMIT)
        return
    verify(SOURCE)
    if CACHE.exists():
        verify(CACHE)
        print('Existing pinned HAL cache verified; unchanged')
        return
    CACHE.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    shutil.copytree(SOURCE, CACHE, symlinks=True)
    verify(CACHE)
    print('Pinned HAL and crypto cache verified:', COMMIT)
    print('No user source changes discarded, no commit or checkout performed')


if __name__ == '__main__':
    main()
