"""The Windows.UI.Xaml classes the kernel's registry names Windows.UI.Xaml.dll
for (kernel/um/um_wuxaml_classes.h) are what tools/winrt/wuxclasses.py
writes from Windows' metadata and the framework's activation table."""
from pathlib import Path
import subprocess
import sys
import unittest

TOP = Path(__file__).resolve().parents[2]


class WuxClassesTests(unittest.TestCase):
    def test_list_is_current(self):
        r = subprocess.run([sys.executable, str(TOP / 'tools' / 'winrt' / 'wuxclasses.py'), '--check'],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_application_and_window_listed(self):
        text = (TOP / 'kernel' / 'um' / 'um_wuxaml_classes.h').read_text()
        for cls in ('Windows.UI.Xaml.Application', 'Windows.UI.Xaml.Window', 'Windows.UI.Xaml.Markup.XamlReader',
                    'Windows.UI.Xaml.Controls.Button'):
            self.assertIn('"%s",' % cls, text)


if __name__ == '__main__':
    unittest.main()
