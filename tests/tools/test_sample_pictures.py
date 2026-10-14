"""The pictures installed in C:\\Pictures decode with the kernel's PNG decoder,
and the brand guide's SVG files are well-formed."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / 'userland/samples'
BRAND = ROOT / 'docs/design/brand'
WALLPAPERS = ['Aurora-Lights', 'Beam', 'Glass-Dawn', 'Horizon', 'Nova-Night',
              'Ocean-Waves', 'Sunset-Waves', 'Twilight-Waves']
# The desktop decodes a wallpaper only up to this size (load_aurora_wallpaper).
MAX_BYTES = 8 * 1024 * 1024

class SamplePictureTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'host GCC required')
    def test_kernel_decodes_every_sample_png(self):
        pngs = sorted(SAMPLES.glob('*.png'))
        self.assertTrue(pngs)
        with tempfile.TemporaryDirectory() as d:
            exe = str(Path(d) / 'png_decode')
            subprocess.run(['gcc', '-O1', f'-I{ROOT / "kernel"}', f'-I{ROOT / "include"}',
                            str(ROOT / 'tests/host/png_decode.c'), '-o', exe],
                           check=True, capture_output=True)
            out = subprocess.run([exe, *map(str, pngs)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout)
            sizes = {Path(line.split(' ', 1)[1]).stem: line.split(' ', 1)[0]
                     for line in out.stdout.splitlines()}
        for name in WALLPAPERS:
            self.assertEqual(sizes.get(name), '2560x1440', name)

    def test_wallpapers_fit_the_desktop_loader(self):
        for name in WALLPAPERS:
            path = SAMPLES / f'{name}.png'
            self.assertTrue(path.is_file(), path)
            self.assertLessEqual(path.stat().st_size, MAX_BYTES, path)

    def test_brand_svgs_are_well_formed(self):
        svgs = sorted(BRAND.rglob('*.svg'))
        self.assertGreaterEqual(len(svgs), 40)
        for path in svgs:
            root = ET.parse(path).getroot()
            self.assertEqual(root.tag, '{http://www.w3.org/2000/svg}svg', path)
            self.assertIn('viewBox', root.attrib, path)

if __name__ == '__main__':
    unittest.main()
