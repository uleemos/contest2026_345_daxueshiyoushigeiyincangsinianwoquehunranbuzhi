#!/usr/bin/env python3
import ctypes as c
import math
import random
from pathlib import Path
import subprocess
import tempfile
import unittest

class ResampleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='velafit-resample-')
        cls.addClassCleanup(cls.temp.cleanup)
        source = Path(__file__).resolve().parents[2] / 'app/velafit_ai/algo/velafit_resample.c'
        lib = Path(cls.temp.name) / 'resample.so'
        subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                        '-shared', '-fPIC', str(source), '-lm', '-o', str(lib)], check=True)
        cls.lib = c.CDLL(str(lib))
        cls.lib.vf_resample_create.restype = c.c_void_p
        cls.lib.vf_resample_destroy.argtypes = [c.c_void_p]
        cls.lib.vf_resample_process.argtypes = [c.c_void_p, c.POINTER(c.c_int16),
                                               c.c_size_t, c.POINTER(c.c_int16), c.c_size_t]
        reference=Path(cls.temp.name)/'reference.so'
        subprocess.run(['gcc','-std=c11','-O2','-shared','-fPIC','-I'+str(source.parent),
            str(Path(__file__).with_name('resample_float_reference.c')),'-lm',
            '-o',str(reference)],check=True)
        cls.reference=c.CDLL(str(reference))
        cls.reference.vf_resample_create.restype=c.c_void_p
        cls.reference.vf_resample_destroy.argtypes=[c.c_void_p]
        cls.reference.vf_resample_process.argtypes=cls.lib.vf_resample_process.argtypes

    def convert(self, samples, chunk, library=None):
        library=library or self.lib
        s = library.vf_resample_create()
        self.assertTrue(s)
        result = []
        try:
            for pos in range(0, len(samples), chunk):
                block = samples[pos:pos+chunk]
                data = (c.c_int16*len(block))(*block)
                output = (c.c_int16*len(block))()
                count = library.vf_resample_process(s, data, len(block), output, len(block))
                self.assertGreaterEqual(count, 0)
                result.extend(output[:count])
        finally:
            library.vf_resample_destroy(s)
        return result

    @staticmethod
    def tone(hz):
        return [round(12000*math.sin(2*math.pi*hz*i/44100)) for i in range(44100)]

    def test_rate_and_chunking(self):
        samples = self.tone(1000)
        expected = self.convert(samples, 44100)
        self.assertEqual(len(expected), 16000)
        for chunk in [1, 127, 441, 512, 1024]:
            self.assertEqual(self.convert(samples, chunk), expected)

    def test_passband_and_alias_rejection(self):
        def rms(values):
            return math.sqrt(sum(v*v for v in values[200:])/len(values[200:]))
        passed = rms(self.convert(self.tone(1000), 441))
        rejected = rms(self.convert(self.tone(12000), 441))
        self.assertLess(abs(passed/(12000/math.sqrt(2))-1), 0.02)
        self.assertLess(rejected/passed, 0.01)  # >40 dB at this test frequency

    def test_silence(self):
        self.assertEqual(self.convert([0]*44100, 512), [0]*16000)

    def test_q20_against_float_reference(self):
        rng=random.Random(345)
        cases=[self.tone(1000),self.tone(7000),
               [rng.randrange(-32768,32768) for _ in range(44100)],
               [32767]*44100,[-32768]*44100,[32767]+[0]*44099]
        for samples in cases:
            actual=self.convert(samples,441)
            reference=self.convert(samples,441,self.reference)
            self.assertEqual(len(actual),len(reference))
            error=max(abs(a-b) for a,b in zip(actual,reference))
            self.assertLessEqual(error,1)
            print(f'Q20 vs frozen float: max_pcm_lsb={error}')

if __name__ == '__main__':
    unittest.main(verbosity=2)
