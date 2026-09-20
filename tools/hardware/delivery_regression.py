#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile/run real portable C units. HOST evidence, no board/audio/network.

Temporary output files are disposable, NOT required build inputs.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

TEAM = Path(__file__).resolve().parents[2]
APP = TEAM / 'app/velafit_ai'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repeat', type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.repeat <= 1000:
        parser.error('--repeat must be 1..1000')
    cases = {
        'pose_worker': [TEAM/'tools/models/test_pose_worker.c',APP/'pipeline/velafit_pose_worker.c'],
        'coordinates': [TEAM/'tools/models/test_sc2336_raw10.c',
                        TEAM/'app/sc2336_probe/sc2336_raw10.c'],
        'model_preview': [TEAM/'tools/models/test_model_preview.c',
                          TEAM/'app/sc2336_probe/sc2336_model_preview.c'],
        'squat': [TEAM/'tools/models/test_squat_replay.c',
                  APP/'algo/squat_fsm.c', APP/'algo/geometry.c'],
        'body_check': [TEAM/'tools/models/test_body_check.c',
                       APP/'algo/velafit_body_check.c'],
        'session': [TEAM/'tools/models/test_session.c',
                    APP/'pipeline/velafit_session.c'],
        'renderer': [TEAM/'tools/models/test_render_session.c',
                     APP/'render/velafit_render.c'],
        'pipeline': [TEAM/'tools/models/test_pipeline_events.c',
                     APP/'pipeline/velafit_pipeline.c',APP/'pipeline/velafit_session.c',
                     APP/'render/velafit_render.c',APP/'models/sample_pose_frames.c',
                     *[APP/'algo'/name for name in ('one_euro_filter.c','geometry.c',
                       'calorie_calc.c','squat_fsm.c','jumping_jack_fsm.c',
                       'pushup_fsm.c','plank_fsm.c')]],
    }
    includes = [APP/'include', APP/'algo', APP/'pipeline',
                TEAM/'app/sc2336_probe',APP/'render',
                TEAM.parent/'nuttx/arch/risc-v/src/esp32p4']
    includes += [APP/p for p in ('models','audio','storage','sync','engine','plan')]
    with tempfile.TemporaryDirectory(prefix='velafit-delivery-test-') as tmp:
        # Host-only config: hardware PPA disabled, real software renderer used.
        (Path(tmp)/'nuttx').mkdir()
        (Path(tmp)/'nuttx/config.h').write_text('#define OK 0\n#define FAR\n')
        includes.append(Path(tmp))
        for name, sources in cases.items():
            binary = str(Path(tmp)/name)
            subprocess.run(['cc', '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                            '-Wl,--wrap=velafit_render_to_fb0',
                            '-Wno-implicit-fallthrough', '-fsanitize=undefined',
                            '-Wno-unused-parameter', '-ffunction-sections',
                            '-fdata-sections','-Wl,--gc-sections',
                            '-fno-sanitize-recover=all',
                            *['-I'+str(p) for p in includes],
                            *map(str,sources), '-lm', '-o',binary], check=True)
            for i in range(args.repeat):
                subprocess.run([binary], check=True, timeout=10,
                               stdout=None if i==0 else subprocess.DEVNULL)
            print(f'HOST {name}: PASS repetitions={args.repeat}', flush=True)

if __name__ == '__main__':
    main()
