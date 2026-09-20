import unittest
from kws_template_compare import summarize


class SummaryTests(unittest.TestCase):
    def test_empty(self):
        self.assertEqual(summarize([]), {'best': None, 'candidates': []})

    def test_gates_and_cooldown(self):
        rows = [dict(time=t, rms=rms, distance=distance) for t, rms, distance in
                [(0, 79, .01), (1, 80, .08), (2, 80, .07),
                 (3, 90, .06), (4, 90, .07)]]
        result = summarize(rows)
        self.assertEqual([r['time'] for r in result['candidates']], [2, 4])
        self.assertEqual(result['best']['time'], 3)


if __name__ == '__main__':
    unittest.main()
