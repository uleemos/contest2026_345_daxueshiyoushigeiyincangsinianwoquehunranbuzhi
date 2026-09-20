#!/usr/bin/env python3
"""Build local TFLM frontend; synthetic-only streaming regression, no hardware."""
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
WORKSPACE = HERE.parents[2]


class FrontendTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='velafit-mww-frontend-')
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        root = WORKSPACE / 'apps/mlearning/tflite-micro/tflite-micro'
        source = root / 'tensorflow/lite/experimental/microfrontend/lib'
        kiss = WORKSPACE / 'apps/math/kissfft/kissfft'
        objects = []
        names = ['frontend', 'frontend_util', 'window', 'window_util',
                 'filterbank', 'filterbank_util', 'noise_reduction',
                 'noise_reduction_util', 'pcan_gain_control',
                 'pcan_gain_control_util', 'log_scale', 'log_scale_util', 'log_lut']
        files = [source / (name+'.c') for name in names]
        files += [source / (name+'.cc') for name in ['fft', 'fft_util', 'kiss_fft_int16']]
        files.append(HERE / 'mww_frontend_probe.cc')
        for file in files:
            obj = directory / (file.name+'.o')
            compiler = 'gcc' if file.suffix == '.c' else 'g++'
            subprocess.run([compiler, '-O2', '-g', '-I'+str(root), '-I'+str(kiss),
                            '-c', str(file), '-o', str(obj)], check=True)
            objects.append(str(obj))
        cls.binary = directory / 'probe'
        subprocess.run(['g++', *objects, '-lm', '-o', str(cls.binary)], check=True)
        board_source = HERE.parents[1]/'app/velafit_ai/models/velafit_mww_frontend.cc'
        board_object = directory/'board_probe.o'
        subprocess.run(['g++','-O2','-DVELAFIT_FRONTEND_HOST_MAIN',
                        '-I'+str(root),'-c',str(board_source),'-o',str(board_object)],
                       check=True)
        cls.board_binary = directory/'board_probe'
        subprocess.run(['g++',*objects[:-1],str(board_object),'-lm',
                        '-o',str(cls.board_binary)],check=True)

    def features(self, samples, chunk):
        pcm = struct.pack('<'+'h'*len(samples), *samples)
        result = subprocess.run([str(self.binary), str(chunk)], input=pcm,
                                capture_output=True, check=True, timeout=15)
        return result.stdout

    def test_chunk_invariance(self):
        samples = [int(5000*math.sin(i*2*math.pi*440/16000)) for i in range(16000)]
        baseline = self.features(samples, 16000)
        self.assertEqual(len(baseline.splitlines()), 98)
        self.assertTrue(all(len(line.split(b',')) == 40 for line in baseline.splitlines()))
        for chunk in [1, 127, 160, 441, 512, 1024]:
            with self.subTest(chunk=chunk):
                self.assertEqual(baseline, self.features(samples, chunk))
        self.assertNotEqual(baseline, self.features([0]*16000, 160))

    def test_window_boundaries(self):
        for count, expected in [(0, 0), (479, 0), (480, 1), (639, 1), (640, 2)]:
            with self.subTest(samples=count):
                self.assertEqual(len(self.features([0]*count, 127).splitlines()), expected)

    def test_training_frontend_parity(self):
        try:
            from pymicro_features import MicroFrontend
        except ImportError:
            self.skipTest('Install pymicro-features==2.0.2 for training parity')
        samples = [int(5000*math.sin(i*2*math.pi*440/16000)) for i in range(16000)]
        samples += [0]*16000
        frontend = MicroFrontend()
        pcm = struct.pack('<'+'h'*len(samples), *samples)
        expected = []
        pos = 0
        while pos < len(pcm):
            result = frontend.process_samples(pcm[pos:pos+320])
            self.assertGreater(result.samples_read, 0)
            pos += result.samples_read * 2
            if result.features:
                expected.append(list(result.features))
        # pymicro-features 2.0.2 src/micro_features.cpp FLOAT32_SCALE.
        actual = [[int(x) * 0.0390625 for x in line.split(b',')]
                  for line in self.features(samples, 441).splitlines()]
        self.assertEqual(len(actual), len(expected))
        self.assertEqual(actual, expected)

    def test_truncated_pcm_rejected(self):
        result = subprocess.run([str(self.binary), '160'], input=b'\x00', capture_output=True)
        self.assertEqual(result.returncode, 4)

    def test_board_frontend_contract(self):
        samples = [(i%1024)*16-8000 for i in range(16000)]
        value = 2166136261
        for line in self.features(samples,160).splitlines():
            for field in line.split(b','):
                feature = int(field)
                for byte in (feature & 255, feature >> 8):
                    value = ((value ^ byte)*16777619) & 0xffffffff
        result = subprocess.run([str(self.board_binary)],check=True,
                                capture_output=True,text=True,timeout=10)
        self.assertEqual(result.stdout.count(f'hash={value:08x}'),2)
        print(result.stdout,end='')

    def test_invalid_chunk_rejected(self):
        for chunk in ['0', '-1', 'garbage', '65537']:
            result = subprocess.run([str(self.binary), chunk], input=b'', capture_output=True)
            self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
