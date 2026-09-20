#!/usr/bin/env python3
"""Build existing workspace TFLM for a host-only streaming control probe."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--model', type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    apps = here.parents[2] / 'apps'
    root = apps / 'mlearning/tflite-micro/tflite-micro'
    directories = ['c', 'core/c', 'core/api', 'kernels', 'kernels/internal',
                   'kernels/internal/optimized', 'kernels/internal/reference',
                   'micro', 'micro/arena_allocator', 'micro/kernels',
                   'micro/memory_planner', 'micro/tflite_bridge', 'schema']
    files = [p for d in directories for p in (root/'tensorflow/lite'/d).glob('*.cc')
             if not p.name.endswith('test.cc')]
    files.append(here/'mww_stream_probe.cc')
    app=here.parents[1]/'app/velafit_ai'
    frontend=root/'tensorflow/lite/experimental/microfrontend/lib'
    for name in ['frontend','frontend_util','window','window_util','filterbank',
                 'filterbank_util','noise_reduction','noise_reduction_util',
                 'pcan_gain_control','pcan_gain_control_util','log_scale',
                 'log_scale_util','log_lut']:
        files.append(frontend/(name+'.c'))
    files.extend(frontend/(name+'.cc') for name in ['fft','fft_util','kiss_fft_int16'])
    files.extend([app/'models/velafit_mww_stream.cc',app/'models/velafit_mww_chain_probe.cc',
                  app/'algo/velafit_resample.c'])
    includes = [root, apps/'math/gemmlowp/gemmlowp', apps/'math/ruy/ruy',
                apps/'system/flatbuffers/flatbuffers/include',app/'algo',app/'models',
                apps/'math/kissfft/kissfft']
    with tempfile.TemporaryDirectory(prefix='velafit-mww-stream-') as tmp:
        directory = Path(tmp)
        def compile_one(item):
            index, file = item
            obj = directory / f'{index}.o'
            compiler=['gcc','-std=c11'] if file.suffix=='.c' else ['g++','-std=c++17']
            result = subprocess.run([*compiler, '-O1', '-ffunction-sections',
                                     '-fdata-sections', '-DTF_LITE_DISABLE_X86_NEON',
                                     *['-I'+str(x) for x in includes], '-c', str(file),
                                     '-o', str(obj)], capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(f'{file.name}:\n{result.stderr[:4000]}')
            return str(obj)
        with ThreadPoolExecutor(max_workers=8) as pool:
            objects = list(pool.map(compile_one, enumerate(files)))
        binary = directory/'runner'
        subprocess.run(['g++', '-Wl,--gc-sections', *objects, '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(args.model.resolve())], check=True, timeout=60)

if __name__ == '__main__':
    main()
