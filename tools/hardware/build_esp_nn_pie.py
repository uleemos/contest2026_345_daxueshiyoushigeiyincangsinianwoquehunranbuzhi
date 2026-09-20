#!/usr/bin/env python3
"""Build pinned upstream kernels only; does not modify the source checkout."""
# SPDX-License-Identifier: Apache-2.0
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess

TEAM = Path(__file__).resolve().parents[2]
V132 = os.environ.get('VELAFIT_ESP_NN_V132') == '1'
SOURCE = TEAM / ('.secrets/esp-nn-speed-reference-20260915' if V132 else '.secrets/esp-nn-v1.3.0')
COMMIT = '2c222c5e02225177b44ebf21169bc66df3c8b573' if V132 else 'd8866fa3762ee9caf56712b4019d004d86e0f3f8'
BIN = TEAM / '.secrets/pie-toolchain-14.2.0/riscv32-esp-elf/bin'
OUT = TEAM / '.secrets/esp-nn-pie-build'
FILES = [
    'src/convolution/esp_nn_conv_riscv_pie.c',
    'src/convolution/esp_nn_conv_ansi.c',
    'src/convolution/esp_nn_conv_opt.c',
    'src/convolution/esp_nn_depthwise_conv_riscv_pie.c',
    'src/convolution/esp_nn_depthwise_conv_ansi.c',
    'src/convolution/esp_nn_depthwise_conv_opt.c',
    'src/common/esp_nn_multiply_by_quantized_mult_riscv_pie.S',
]
if os.environ.get('VELAFIT_QACC_CONV') == '1':
    FILES += ['app/velafit_ai/models/velafit_qacc_conv.c']


def main():
    def git(*args):
        return subprocess.check_output(['git', '-C', str(SOURCE), *args], text=True).strip()
    if git('rev-parse', 'HEAD') != COMMIT or git('status', '--porcelain', '--untracked-files=no'):
        raise SystemExit('ESP-NN source revision/cleanliness mismatch')
    version = subprocess.check_output([str(BIN / 'riscv32-esp-elf-gcc'), '--version'], text=True)
    if 'esp-14.2.0_20251107' not in version:
        raise SystemExit('Pinned PIE compiler required')
    OUT.mkdir(parents=True, exist_ok=True)
    objects = []
    hashes = {}
    qacc32 = os.environ.get('VELAFIT_ESP_NN_QACC32') == '1'
    iram = os.environ.get('VELAFIT_ESP_NN_IRAM') == '1'
    exact_requant = os.environ.get('VELAFIT_EXACT_REQUANT') == '1'
    optimization = ['-O3', '-funroll-loops'] if os.environ.get('VELAFIT_NN_O3') == '1' else ['-O2']
    patch_file = TEAM / 'tools/patches/esp-nn-qacc32.patch'
    for name in FILES:
        source = (TEAM if name.startswith('app/') else SOURCE) / name
        hashes[name] = hashlib.sha256(source.read_bytes()).hexdigest()
        if qacc32 and source.name == 'esp_nn_conv_riscv_pie.c':
            generated = OUT / source.name
            shutil.copyfile(source, generated)
            subprocess.run(['patch', '--batch', '--forward', '-p1', '-d', str(OUT),
                            '-i', str(patch_file)], check=True)
            source = generated
        if exact_requant and name.endswith('_riscv_pie.c'):
            wrapper = OUT / (source.stem + '_exact.c')
            wrapper.write_text(
                '#include <common_functions.h>\n'
                '#include "' + str(TEAM / 'app/velafit_ai/models/velafit_requant.h') + '"\n'
                '#undef esp_nn_requantize\n'
                '#define esp_nn_requantize(x,m,s) vf_requant_exact((x),(m),(s))\n'
                '#include "' + str(source) + '"\n')
            source = wrapper
        target = OUT / (source.stem + '.o')
        subprocess.run([
            str(BIN / 'riscv32-esp-elf-gcc'), '-c', str(source), '-o', str(target),
            *optimization, '-ffunction-sections', '-fdata-sections', '-fno-common',
            '-march=rv32imac_zicsr_xespv_xesploop', '-mabi=ilp32',
            '-DCONFIG_IDF_TARGET_ESP32P4=1',
            '-I' + str(SOURCE / 'include'), '-I' + str(SOURCE / 'src/common'),
            '-I' + str(SOURCE / 'src/convolution'),
        ], check=True)
        if iram:
            # Keep hot instructions off the flash/cache path. The board linker
            # already loads .iram1.* into executable internal SRAM. Rename
            # sections before linking so relocation and bounds checks remain
            # the linker's responsibility; leave constants/data unchanged.
            listing = subprocess.check_output([
                str(BIN / 'riscv32-esp-elf-objdump'), '-h', str(target)
            ], text=True)
            sections = re.findall(r'^\s*\d+\s+(\.text(?:\.\S+)?)\s+', listing, re.M)
            arguments = []
            for section in sections:
                arguments += ['--rename-section', section + '=.iram1' + section]
            if arguments:
                subprocess.run([str(BIN / 'riscv32-esp-elf-objcopy'),
                                *arguments, str(target)], check=True)
        objects.append(str(target))
    library = OUT / 'libvelafit_esp_nn.a'
    temporary = OUT / 'libvelafit_esp_nn.next.a'
    temporary.unlink(missing_ok=True)
    subprocess.run([str(BIN / 'riscv32-esp-elf-ar'), 'rcs', str(temporary), *objects], check=True)
    temporary.replace(library)
    record = {'commit': COMMIT, 'license': 'Apache-2.0', 'compiler': version.splitlines()[0],
              'sources': hashes, 'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest()}
    record['qacc32_local_patch'] = hashlib.sha256(patch_file.read_bytes()).hexdigest() if qacc32 else None
    record['instructions_in_iram'] = iram
    record['exact_requant'] = exact_requant
    record['optimization'] = optimization
    record['local_model_headers'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in sorted((TEAM / 'app/velafit_ai/models').glob('*.h'))}
    record['headers'] = {str(p.relative_to(SOURCE)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for folder in ('include', 'src/common')
                         for p in sorted((SOURCE / folder).glob('*.h'))}
    (OUT / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
