#!/usr/bin/env python3
"""Read-only ABI/dependency audit; temporary partial link is NOT a firmware test."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def run(argv):
    result = subprocess.run([str(x) for x in argv], capture_output=True, text=True,
                            check=False, timeout=120)
    return result.returncode, result.stdout + result.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--esp-sr', type=Path, required=True)
    parser.add_argument('--tool-prefix', required=True)
    parser.add_argument('--firmware-elf', type=Path, required=True)
    args = parser.parse_args()
    library = args.esp_sr / 'lib/esp32p4'
    archives = [library / name for name in (
        'libwakenet.a', 'libdl_lib.a', 'libc_speech_features.a', 'libhufzip.a')]
    for path in [*archives, args.firmware_elf]:
        if not path.is_file():
            parser.error(f'Missing input: {path}')
    report = {'scope': 'host static audit; no board execution',
              'esp_sr_commit': run(['git', '-C', args.esp_sr, 'rev-parse', 'HEAD'])[1].strip()}
    for name, path in [('wakenet', archives[0]), ('firmware', args.firmware_elf)]:
        code, output = run([args.tool_prefix + 'readelf', '-h', path])
        report[name] = {'readelf_exit': code, 'elf_flags': sorted(set(
            line.strip() for line in output.splitlines() if 'Flags:' in line))}
    with tempfile.TemporaryDirectory(prefix='velafit-wakenet-link-') as directory:
        code, output = run([args.tool_prefix + 'ld', '-r', '-u',
                            'esp_sr_wakenet9_quantized', '-o',
                            Path(directory) / 'closure.o', '--start-group',
                            *archives, '--end-group'])
        report['partial_link'] = {
            'exit_code': code,
            'unsupported_relocation_count': output.count('unsupported relocation'),
            'diagnostic_excerpt': output.splitlines()[:12],
            'note': 'Archive closure probe, not final link or ABI compatibility proof.'}
    compatible_flags = (report['wakenet']['elf_flags'] == report['firmware']['elf_flags'])
    clean = (code == 0 and 'unsupported relocation' not in output
             and compatible_flags
             and report['wakenet']['readelf_exit'] == 0
             and report['firmware']['readelf_exit'] == 0)
    report['direct_integration_gate'] = 'NOT_PROVEN' if clean else 'BLOCKED'
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if clean else 1


if __name__ == '__main__':
    raise SystemExit(main())
