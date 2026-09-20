#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Private immutable P4 firmware snapshot, verification and explicit restore.

No credentials are read. Firmware/config may contain private material, so
snapshots must stay under .secrets. Flash defaults to dry-run; --execute uses
the already-authorized esptool workflow. No chip erase or filesystem formatting.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

TEAM = Path(__file__).resolve().parents[2]
FILES = {'nuttx.bin': 'nuttx.bin', 'nuttx.elf': 'nuttx', 'config': '.config'}


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def verify(bundle):
    manifest = json.loads((bundle/'manifest.json').read_text())
    if not isinstance(manifest, dict):
        raise ValueError('invalid manifest')
    if manifest.get('chip') != 'esp32p4' or manifest.get('offset') != '0x2000':
        raise ValueError('wrong platform/layout')
    if not isinstance(manifest.get('files'), dict) or set(manifest['files']) != set(FILES):
        raise ValueError('unexpected bundle contents')
    for name, expected in manifest['files'].items():
        path = bundle/name
        if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
            raise ValueError('missing/linked/empty artifact: '+name)
        if digest(path) != expected:
            raise ValueError('hash mismatch: '+name)
    if (bundle/'nuttx.bin').read_bytes()[:1] != b'\xe9':
        raise ValueError('not an Espressif application image')
    if (bundle/'nuttx.elf').read_bytes()[:4] != b'\x7fELF':
        raise ValueError('not ELF')
    return manifest


def snapshot(bundle, evidence):
    if not bundle.is_relative_to((TEAM/'.secrets').resolve()):
        raise ValueError('snapshots must remain below .secrets')
    if not evidence or any(not p.is_file() for p in evidence):
        raise ValueError('existing evidence file(s) required; acceptance not inferred')
    build = TEAM.parent/'nuttx'
    hashes = {dst: digest(build/src) for dst, src in FILES.items()}
    bundle.mkdir(parents=True, exist_ok=False)
    bundle.chmod(0o700)
    for dst, src in FILES.items():
        shutil.copyfile(build/src, bundle/dst)
        (bundle/dst).chmod(0o600)
        if digest(bundle/dst) != hashes[dst] or digest(build/src) != hashes[dst]:
            raise RuntimeError('build changed during snapshot; partial bundle not usable')
    manifest = dict(chip='esp32p4', offset='0x2000', files=hashes,
        created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        evidence=[dict(path=str(p.resolve().relative_to(TEAM)), sha256=digest(p)) for p in evidence],
        scope='See evidence; this tool does NOT certify complete system acceptance')
    (bundle/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    verify(bundle)


def flash_command(bundle, python, port):
    if not re.fullmatch(r'COM[1-9][0-9]*|/dev/tty[A-Za-z0-9_-]+', port):
        raise ValueError('invalid serial port')
    binary = str((bundle/'nuttx.bin').resolve())
    if python.lower().endswith('.exe') and sys.platform != 'win32':
        binary = subprocess.check_output(['wslpath', '-w', binary], text=True).strip()
    return [python, '-m', 'esptool', '--chip', 'esp32p4', '--port', port,
            '--baud', '460800', '--before', 'default-reset', '--after', 'hard-reset',
            'write-flash', '0x2000', binary]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['snapshot', 'verify', 'flash'])
    p.add_argument('bundle', type=Path)
    p.add_argument('--evidence', type=Path, action='append', default=[])
    p.add_argument('--port', default='COM3')
    p.add_argument('--python', default=sys.executable)
    p.add_argument('--execute', action='store_true')
    a = p.parse_args()
    bundle = a.bundle.resolve()
    try:
        if a.action == 'snapshot':
            snapshot(bundle, a.evidence)
        manifest = verify(bundle)
        print('Bundle verified: '+manifest['files']['nuttx.bin'])
        if a.action == 'flash':
            cmd = flash_command(bundle, a.python, a.port)
            if a.execute:
                # Reverify immediately before launch; never fall back on another image.
                verify(bundle)
                subprocess.run(cmd, check=True, timeout=240)
            else:
                print('DRY RUN (no serial access): '+json.dumps(cmd))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        p.exit(1, str(exc)+'\n')


if __name__ == '__main__':
    main()
