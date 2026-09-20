"""Pure synthetic checks: no private recordings or generated model required."""
import ctypes as C
import math
import tempfile
import unittest
from kws_acoustic_probe import library, extract, scores


class AcousticTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="velafit-kws-tests-")
        cls.lib = library(cls.tmp.name)

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def test_frontend_silence_finite_and_rate(self):
        frames = extract(self.lib, [0] * 44100)
        self.assertEqual(len(frames), 98)
        for _, row, rms in frames:
            self.assertEqual(rms, 0)
            self.assertTrue(all(math.isfinite(x) for x in row))

    def test_frontend_deterministic(self):
        pcm = [int(1000 * math.sin(i * 0.1)) for i in range(44100)]
        self.assertEqual(extract(self.lib, pcm), extract(self.lib, pcm))

    def test_fullscale_input_finite(self):
        for pcm in ([32767] * 2205, [-32768, 32767] * 1103):
            for _, row, rms in extract(self.lib, pcm):
                self.assertTrue(math.isfinite(rms))
                self.assertTrue(all(math.isfinite(x) and abs(x) <= 1.001 for x in row))

    def test_exact_sequence_and_mismatch(self):
        rows = [[float(i % 13 == k) for k in range(13)] for i in range(60)]
        frames = [(i / 100, row, 100) for i, row in enumerate(rows)]
        result = scores(self.lib, rows, frames)
        self.assertAlmostEqual(result[-1]["distance"], 0)
        self.assertEqual(result[-1]["duration_ms"], 600)
        negative = scores(self.lib, rows, [(i / 100, [-1.0] * 13, 100) for i in range(100)])
        self.assertTrue(all(r["distance"] > 0.12 for r in negative))

    def test_invalid_models(self):
        self.assertFalse(self.lib.vf_kws_matcher_create(None, 50))
        model = (C.c_float * (251 * 13))()
        for frames in (0, 19, 251):
            self.assertFalse(self.lib.vf_kws_matcher_create(model, frames))
        model[0] = float("nan")
        self.assertFalse(self.lib.vf_kws_matcher_create(model, 50))


class BoundedAcousticTest(AcousticTest):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="velafit-kws-bounded-tests-")
        cls.lib = library(cls.tmp.name, bounded=True)

    def test_half_and_double_duration(self):
        base = [[float(i % 13 == k) for k in range(13)] for i in range(30)]
        doubled = [row for row in base for _ in range(2)]
        for model, stream in ((doubled, base), (base, doubled)):
            result = scores(self.lib, model,
                            [(i / 100, row, 100) for i, row in enumerate(stream)])
            self.assertAlmostEqual(result[-1]['distance'], 0)
            # Subsequence alignment can skip the first of two identical
            # leading frames at equal zero cost in the doubled stream.
            self.assertIn(result[-1]['duration_ms'],
                          [len(stream) * 10, (len(stream) - 1) * 10])

    def test_short_fragment_cannot_complete_model(self):
        model = [[float(i % 13 == k) for k in range(13)] for i in range(60)]
        result = scores(self.lib, model,
                        [(i / 100, row, 100) for i, row in enumerate(model[:20])])
        self.assertEqual(result, [])

    def test_gated_quiet_reset(self):
        first = [1.0] + [0.0] * 12
        second = [0.0, 1.0] + [0.0] * 11
        model = [first] * 30 + [second] * 30
        def stream(gap):
            rows = [(row, 100) for row in model[:30]]
            rows += [([0.0] * 13, 0)] * gap
            rows += [(row, 100) for row in model[30:]]
            return [(i / 100, row, rms) for i, (row, rms) in enumerate(rows)]
        short = scores(self.lib, model, stream(20), gated=True)
        self.assertAlmostEqual(short[-1]['distance'], 0)
        long = scores(self.lib, model, stream(80), gated=True)
        self.assertTrue(all(r['distance'] >= .08 for r in long if r['time'] >= 1.1))
        self.assertEqual(scores(self.lib, model,
                         [(i / 100, first, 0) for i in range(1000)], gated=True), [])


if __name__ == "__main__": unittest.main()
