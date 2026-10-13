"""Exercise the actual kernel console queue on the host, without a VM."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class ConsoleCursorTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'host GCC required')
    def test_queue_controls_bounds_and_cancellation(self):
        with tempfile.TemporaryDirectory() as d:
            executable = str(Path(d) / 'console_cursor')
            includes = ['kernel', 'include', 'third_party/lwip/src/include', 'kernel/net/port',
                        'third_party/mbedtls/include', 'third_party/uacpi/include',
                        'third_party/libvterm/include']
            vterm = []                                   # (libvterm on the host's C library)
            for src in sorted((ROOT / 'third_party/libvterm/src').glob('*.c')):
                vterm.append(str(Path(d) / (src.stem + '.o')))
                subprocess.run(['gcc', '-c', '-w', '-ffunction-sections', '-fdata-sections',
                                '-include', 'stdio.h', f'-I{ROOT / "third_party/libvterm/include"}',
                                str(src), '-o', vterm[-1]], check=True, capture_output=True)
            subprocess.run(['gcc', '-ffunction-sections', '-fdata-sections',
                            *[f'-I{ROOT / x}' for x in includes],
                            str(ROOT / 'tests/host/console_cursor.c'), *vterm, '-Wl,--gc-sections',
                            '-o', executable], check=True, capture_output=True)
            result = subprocess.run([executable], check=True, capture_output=True, text=True)
            self.assertIn('screen-buffer APIs', result.stdout)

if __name__ == '__main__':
    unittest.main()
