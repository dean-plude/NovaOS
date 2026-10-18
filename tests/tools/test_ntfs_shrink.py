"""Shrinking an NTFS volume, on the host: the real kernel/fs/ntfs.c on a RAM
disk (tests/host/ntfs_shrink.c), as Setup shrinks Windows' volume to make
room for NovaOS.  The shrunk volume must mount from a disk of its new size
with every file byte for byte and nothing for the check to put right, and
pass scripts/ntfs-check.py; a shrink cut off part way must leave a volume
(old size or new) that does the same."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NtfsShrinkTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'host GCC required')
    def test_shrink_moves_everything_and_survives_cuts(self):
        with tempfile.TemporaryDirectory() as d:
            exe = str(Path(d) / 'ntfs_shrink')
            subprocess.run(['gcc', '-O2', '-w', f'-I{ROOT / "kernel"}', f'-I{ROOT / "include"}',
                            str(ROOT / 'tests/host/ntfs_shrink.c'), '-o', exe], check=True, capture_output=True)
            checker = f'{sys.executable} -I {ROOT / "scripts/ntfs-check.py"}'
            r = subprocess.run([exe, checker, str(Path(d) / 'shrink.img'), '3'], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-1000:])
            # $MFTMirr, $LogFile and files past the new end all moved
            self.assertRegex(r.stdout, r'shrinking "Windows" from 95 to 43 MiB: \d+ MiB of 1\d attributes to move')
            self.assertIn('checked, 0 lost clusters freed, 0 clusters claimed, 0 MFT records put right, '
                          '0 directory indexes rebuilt', r.stdout)
            self.assertRegex(r.stdout, r'ntfs_shrink: [1-9]\d+ cut points, 0 bad')


if __name__ == '__main__':
    unittest.main()
