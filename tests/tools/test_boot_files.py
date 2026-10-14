"""The files beside the kernel (userland/files -> \\EFI\\NOVA\\Files ->
drive C:, kernel/fs/bootfiles.c): the wallpapers and pictures decode with the
kernel's PNG decoder, the Beam icon set's .ico files hold PNG images the
kernel decodes and are named as kernel/apps/iconset.c looks them up, the
paths fit the boot protocol, and the brand guide's SVG files are
well-formed."""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
FILES = ROOT / 'userland/files'
WALLPAPER = FILES / 'Windows/Web/Wallpaper'
BEAM = FILES / 'Windows/Web/Icons/Beam'
BRAND = ROOT / 'docs/design/brand'
WALLPAPERS = ['Aurora-Lights', 'Beam', 'Glass-Dawn', 'Horizon', 'Nova-Night',
              'Ocean-Waves', 'Sunset-Waves', 'Twilight-Waves']
ICON_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def define(header, name):
    return int(re.search(rf'#define {name}\s+\(?(\d+)', (ROOT / header).read_text()).group(1))


def ico_images(path):
    """[(size, PNG bytes)] of an .ico file"""
    data = path.read_bytes()
    _, kind, count = struct.unpack_from('<HHH', data)
    assert kind == 1, path
    out = []
    for i in range(count):
        w, h, _, _, _, _, size, off = struct.unpack_from('<BBBBHHII', data, 6 + 16 * i)
        out.append((w or 256, data[off:off + size]))
    return out


class BootFilesTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'host GCC required')
    def test_kernel_decodes_every_picture_and_icon_image(self):
        with tempfile.TemporaryDirectory() as d:
            pngs = sorted(FILES.rglob('*.png'))
            self.assertTrue(pngs)
            for ico in sorted(BEAM.glob('*.ico')):          # each icon's images, as files
                for size, png in ico_images(ico):
                    p = Path(d) / f'{ico.stem}-{size}.png'
                    p.write_bytes(png)
                    pngs.append(p)
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
        for ico in BEAM.glob('*.ico'):
            for s in ICON_SIZES:
                self.assertEqual(sizes.get(f'{ico.stem}-{s}'), f'{s}x{s}', ico.name)

    def test_wallpapers_fit_the_decoder_limit(self):
        limit = int(re.search(r'#define FILE_MAX\s+\((\d+)u << 20\)',
                              (ROOT / 'kernel/wm/wallpaper.c').read_text()).group(1)) << 20
        for name in WALLPAPERS + ['Aurora-Sunrise']:
            path = WALLPAPER / f'{name}.png'
            self.assertTrue(path.is_file(), path)
            self.assertLessEqual(path.stat().st_size, limit, path)

    def test_icon_set_names_match_the_kernel_and_the_brand_icons(self):
        source = (ROOT / 'kernel/apps/iconset.c').read_text()
        table = source[source.index('g_names[K_COUNT] = {'):source.index('};', source.index('g_names[K_COUNT] = {'))]
        names = set(re.findall(r'"([a-z-]+)"', table))
        svgs = {p.stem for p in (BRAND / 'icons/apps').glob('*.svg')} | \
               {p.stem for p in (BRAND / 'icons/files').glob('*.svg')}
        icons = {p.stem for p in BEAM.glob('*.ico')}
        self.assertEqual(icons, svgs)
        self.assertEqual(icons, names)

    def test_paths_fit_the_boot_protocol(self):
        path_max = define('include/boot_protocol.h', 'BOOT_FILE_PATH_MAX')
        files_max = define('include/boot_protocol.h', 'BOOT_FILES_MAX')
        files = [p for p in FILES.rglob('*') if p.is_file()]
        self.assertLess(len(files), files_max)
        for p in files:
            c_path = '\\' + '\\'.join(p.relative_to(FILES).parts)
            self.assertTrue(c_path.isascii(), c_path)
            self.assertLess(len(c_path) + 1, path_max, c_path)

    def test_brand_svgs_are_well_formed(self):
        svgs = sorted(BRAND.rglob('*.svg'))
        self.assertGreaterEqual(len(svgs), 40)
        for path in svgs:
            root = ET.parse(path).getroot()
            self.assertEqual(root.tag, '{http://www.w3.org/2000/svg}svg', path)
            self.assertIn('viewBox', root.attrib, path)


if __name__ == '__main__':
    unittest.main()
