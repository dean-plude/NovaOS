"""tools/ci/perf.py's judgements (the performance budget, roadmap 26.4) without a VM."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('perf', ROOT / 'tools/ci/perf.py')
perf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(perf)


class PerfBudgetTests(unittest.TestCase):
    def ppm(self, d, share):
        """A 64x40 screenshot whose left @share is the page's colour (off by
        the tolerance) and the rest a desktop grey"""
        w, h = 64, 40
        near = bytes(min(255, c + perf.TOLERANCE) for c in perf.PAGE_RGB)
        row = near * int(w * share) + b'\x40\x40\x40' * (w - int(w * share))
        path = os.path.join(d, 's.ppm')
        Path(path).write_bytes(b'P6\n%d %d\n255\n' % (w, h) + row * h)
        return path

    def test_page_cover(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(perf.ppm_cover(self.ppm(d, 0)), 0)
            self.assertAlmostEqual(perf.ppm_cover(self.ppm(d, 0.5)), 0.5)
            self.assertEqual(perf.ppm_cover(self.ppm(d, 1)), 1)
            Path(d, 'bad.ppm').write_bytes(b'not a picture')
            self.assertEqual(perf.ppm_cover(os.path.join(d, 'bad.ppm')), 0)

    def test_times_against_the_budget(self):
        self.assertEqual(perf.verdict('boot', 10, 60, True), ('within budget', False))
        self.assertTrue(perf.verdict('boot', 61, 60, True)[1])
        self.assertEqual(perf.verdict('boot', 500, 60, False), ('recorded (TCG: not judged)', False))
        self.assertTrue(perf.verdict('boot', None, 60, False)[1])     # never measured fails, TCG or not

    def test_budget_file_names_every_time(self):
        budget = json.loads((ROOT / 'tools/ci/perf-budget.json').read_text())
        self.assertEqual(set(budget), {'boot_seconds', 'firefox_seconds'})
        self.assertTrue(all(isinstance(v, (int, float)) and v > 0 for v in budget.values()))

    def test_page_and_profile(self):
        self.assertIn('background:#3cb371', perf.PAGE)
        self.assertIn('user_pref("browser.aboutwelcome.enabled", false);', perf.USER_JS)


if __name__ == '__main__':
    unittest.main()
