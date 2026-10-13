"""NTFS writes cut off at every point, on the host: the real kernel/fs/ntfs.c
on a RAM disk (tests/host/ntfs_cut.c).  After each cut the volume must mount
writable again (NovaOS checks and repairs what it left), keep every file
whole, and pass scripts/ntfs-check.py with no errors."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NtfsCutTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'host GCC required')
    def test_every_cut_point_checks_clean(self):
        with tempfile.TemporaryDirectory() as d:
            exe = str(Path(d) / 'ntfs_cut')
            subprocess.run(['gcc', '-O2', '-w', f'-I{ROOT / "kernel"}', f'-I{ROOT / "include"}',
                            str(ROOT / 'tests/host/ntfs_cut.c'), '-o', exe], check=True, capture_output=True)
            checker = f'{sys.executable} -I {ROOT / "scripts/ntfs-check.py"}'
            r = subprocess.run([exe, checker, str(Path(d) / 'cut.img')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-1000:])
            self.assertRegex(r.stdout, r'ntfs_cut: [1-9]\d+ cut points, 0 bad')
            # the cuts reach each kind of repair
            for what in ('lost clusters freed', 'clusters claimed', 'MFT records put right', 'directory indexes rebuilt'):
                self.assertRegex(r.stdout, r'[1-9]\d* ' + what, what)
            self.assertIn('stays read-only: marked dirty, and a record can\'t be read (torn)', r.stdout)


if __name__ == '__main__':
    unittest.main()
