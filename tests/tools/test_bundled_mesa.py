"""The built-in Mesa llvmpipe: a pinned mesa-dist-win archive, verified, all files staged."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('bundle_mesa', ROOT / 'tools/bundle_mesa.py')
mesa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mesa)


class BundleMesaTests(unittest.TestCase):
    def test_same_project_as_the_app_store(self):
        catalog = json.loads((ROOT / 'userland/store/catalog.json').read_text())
        entry = next(a for a in catalog['apps'] if a['name'] == 'Mesa 3D')
        # both are mesa-dist-win's MSVC build; the App Store's Vulkan driver
        # may be newer (lavapipe in 24.2.4 cannot compile Godot's shaders),
        # while the built-in OpenGL keeps its pinned release (later ones
        # drop libglapi.dll)
        prefix = 'https://github.com/pal1000/mesa-dist-win/releases/download/'
        self.assertTrue(entry['url'].startswith(prefix) and entry['url'].endswith('-release-msvc.7z'))
        self.assertTrue(mesa.URL.startswith(prefix))
        # the App Store installs only the Vulkan driver now: OpenGL is built in
        self.assertNotIn('opengl32', entry['system'])
        self.assertTrue(entry['exe'].endswith('vulkan_lvp.dll'))

    def test_payload_lists_both_arches_and_fails_on_a_missing_file(self):
        with tempfile.TemporaryDirectory() as d:
            for arch in mesa.SYSDIRS:
                (Path(d) / arch).mkdir()
                for name in mesa.FILES:
                    (Path(d) / arch / name).write_bytes(b'dll')
            files = dict(mesa.payload_files(d))
            self.assertIn(r'\Windows\System32\opengl32_mesa.dll', files)
            self.assertIn(r'\Windows\SysWOW64\libgallium_wgl.dll', files)
            self.assertEqual(len(files), 6)
            (Path(d) / 'x86' / 'libglapi.dll').unlink()
            with self.assertRaises(ValueError):
                mesa.payload_files(d)

    def test_bad_download_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            target = Path(d) / 'mesa.7z'
            with patch.object(mesa.urllib.request, 'urlopen', return_value=io.BytesIO(b'corrupt')):
                with self.assertRaises(ValueError):
                    mesa.download('https://example.invalid/mesa.7z', target, mesa.SHA256)
            self.assertEqual(list(Path(d).iterdir()), [])


if __name__ == '__main__':
    unittest.main()
