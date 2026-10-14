"""A program the Terminal cannot stop must cost one test, not the job's hour; no VM required."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import selftest


class FakeNova:
    """Stands in for novarun.Nova: the first program hangs for good (Ctrl+C does nothing)"""
    instances = []

    def __init__(self, img, work, serial_path=None, **kw):
        self.serial_path = serial_path
        open(serial_path, 'w').close()
        self.boot_log = 'boot\n'
        self.sr = SimpleNamespace(read_new=lambda: '')
        self.q = SimpleNamespace(poll=lambda: None)
        self.wedged = self.stuck = False
        self.resets = 0
        self.starts = 0
        self.ran = []
        self.qmp = SimpleNamespace(cmd=self.reset)
        FakeNova.instances.append(self)

    def reset(self, name, **kw):
        assert name == 'system_reset'
        self.resets += 1

    def start(self, boot_timeout=300):
        self.starts += 1
        self.boot_log = 'boot again\n'

    def shot(self, path):
        pass

    def close(self):
        pass

    def run(self, cmd, timeout=120, shot=None, acts=()):
        self.ran.append(cmd)
        if len(self.ran) == 1:
            self.stuck = True                   # the Terminal never comes back
            return '', False
        self.stuck = False
        return '[UM] Started ok.exe (PID 7)\n[UM] ok.exe (PID 7) exited with code 0 (0x0)\nok: 1 passed, 0 failed', True


class HangTests(unittest.TestCase):
    def test_a_hung_program_resets_the_machine_and_later_tests_still_run(self):
        tests = [selftest.Test('hang', r'C:\hang.exe', timeout=1), selftest.Test('after', r'C:\ok.exe', [r'ok: 1 passed'])]
        with tempfile.TemporaryDirectory() as d, mock.patch.object(selftest, 'Nova', FakeNova), \
                contextlib.redirect_stdout(io.StringIO()):
            a = SimpleNamespace(img=None, out=d)
            results = selftest.run_boot(a, tests, d, None)
        nova = FakeNova.instances[-1]
        self.assertEqual(nova.resets, 1)
        self.assertEqual(nova.ran, [r'C:\hang.exe', r'C:\ok.exe'])
        self.assertEqual([(r[0], r[1]) for r in results],
                         [('hang', 'did not finish in 1 s'), ('after', None)])


if __name__ == '__main__':
    unittest.main()
