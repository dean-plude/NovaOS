"""Hyper-V's integration services (shutdown, time sync, heartbeat), on the host."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class HvUtilTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'host GCC required')
    def test_shutdown_timesync_heartbeat(self):
        with tempfile.TemporaryDirectory() as d:
            executable = str(Path(d) / 'hv_util')
            includes = ['kernel', 'include']     # (not kernel/net/port: its assert.h checks nothing)
            subprocess.run(['gcc', '-ffunction-sections', '-fdata-sections',
                            *[f'-I{ROOT / x}' for x in includes],
                            str(ROOT / 'tests/host/hv_util.c'), '-Wl,--gc-sections',
                            '-o', executable], check=True, capture_output=True)
            result = subprocess.run([executable], check=True, capture_output=True, text=True)
            self.assertIn('hv_util: all passed', result.stdout)

if __name__ == '__main__':
    unittest.main()
