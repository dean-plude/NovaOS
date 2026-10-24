"""WinUI 2's XAML merge (userland/microsoft_ui_xaml/mergexaml.py), the port
of Microsoft's BatchMergeXaml, MergedDictionary and StripNamespaces."""
from pathlib import Path
import sys
import tempfile
import unittest
from xml.dom import minidom

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'userland' / 'microsoft_ui_xaml'))
import mergexaml

HEAD = ('<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" '
        'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" ')
C7 = 'http://schemas.microsoft.com/winfx/2006/xaml/presentation?IsApiContractPresent(Windows.Foundation.UniversalApiContract,7)'
C14 = 'http://schemas.microsoft.com/winfx/2006/xaml/presentation?IsApiContractPresent(Windows.Foundation.UniversalApiContract,14)'


def keys(el):
    return [c.getAttribute('x:Key') or c.getAttribute('TargetType') for c in el.childNodes if c.nodeType == c.ELEMENT_NODE]


class MergeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def page(self, name, text):
        p = Path(self.tmp.name) / name
        p.write_text(text, encoding='utf-8')
        return str(p)

    def test_later_page_wins_and_prefixes_are_standardized(self):
        a = self.page('a.xaml', HEAD + 'xmlns:muxc="using:Microsoft.UI.Xaml.Controls"><x:Double x:Key="Size">1</x:Double>'
                      '<Style TargetType="muxc:InfoBadge"/></ResourceDictionary>')
        b = self.page('b.xaml', HEAD + '><x:Double x:Key="Size">2</x:Double></ResourceDictionary>')
        text = mergexaml.merge({'RS2': [a, b]})['21H1'][1]
        root = minidom.parseString(text).documentElement
        self.assertEqual(keys(root), ['Size', 'controls:InfoBadge'])
        self.assertEqual(root.getElementsByTagName('x:Double')[0].firstChild.data, '2')
        self.assertEqual(root.getAttribute('xmlns:controls'), 'using:Microsoft.UI.Xaml.Controls')

    def test_theme_dictionaries_merge_by_theme_with_dark_as_default(self):
        a = self.page('a.xaml', HEAD + '><ResourceDictionary.ThemeDictionaries><ResourceDictionary x:Key="Dark">'
                      '<Color x:Key="One">#000000</Color></ResourceDictionary></ResourceDictionary.ThemeDictionaries>'
                      '<Color x:Key="Two">#111111</Color></ResourceDictionary>')
        b = self.page('b.xaml', HEAD + '><ResourceDictionary.ThemeDictionaries><ResourceDictionary x:Key="Default">'
                      '<Color x:Key="Two">#222222</Color></ResourceDictionary></ResourceDictionary.ThemeDictionaries>'
                      '</ResourceDictionary>')
        text = mergexaml.merge({'RS1': [a, b]})['RS1'][1]
        root = minidom.parseString(text).documentElement
        themes = root.getElementsByTagName('ResourceDictionary.ThemeDictionaries')[0]
        default, = [d for d in themes.childNodes if d.nodeType == d.ELEMENT_NODE]
        self.assertEqual(default.getAttribute('x:Key'), 'Default')
        self.assertEqual(keys(default), ['One', 'Two'])
        # a key a theme dictionary has is taken out of the dictionary above it
        self.assertEqual(keys(root), [''])

    def test_conditional_xaml_resolved_for_each_release(self):
        a = self.page('a.xaml', HEAD + f'xmlns:contract7Present="{C7}" xmlns:contract14Present="{C14}">'
                      '<Style x:Key="S"><Setter Property="A" Value="1"/>'
                      '<contract7Present:Setter Property="B" Value="2"/>'
                      '<contract14Present:Setter Property="C" Value="3"/></Style></ResourceDictionary>')
        out = mergexaml.merge({'RS1': [a]})
        for rel, want in (('RS1', ['A']), ('19H1', ['A', 'B']), ('21H1', ['A', 'B', 'C'])):
            root = minidom.parseString(out[rel][1]).documentElement
            self.assertEqual([s.getAttribute('Property') for s in root.getElementsByTagName('Setter')], want, rel)
            self.assertNotIn('contract', out[rel][1])


class LeaveOutTests(unittest.TestCase):
    def test_types_neither_winui2_nor_the_framework_has_are_left_out(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('winui2_build', str(Path(mergexaml.__file__).with_name('build.py')))
        build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build)
        text = (HEAD + 'xmlns:controls="using:Microsoft.UI.Xaml.Controls">'
                '<Style x:Key="Ink" TargetType="InkToolbar"/>'
                '<Style x:Key="Item"><Setter Property="Template"><Setter.Value><ControlTemplate>'
                '<InkToolbarFlyoutItem/></ControlTemplate></Setter.Value></Setter></Style>'
                '<Style x:Key="UsesItem" BasedOn="{StaticResource Item}"/>'
                '<AcrylicBrush x:Key="Acrylic"/>'
                '<Style x:Key="Badge" TargetType="controls:InfoBadge"/>'
                '<Style x:Key="Grid" TargetType="Grid"><Setter Property="Grid.Row" Value="1"/></Style>'
                '</ResourceDictionary>')
        out, left = build.leave_out_unbuilt(text, {'Microsoft.UI.Xaml.Controls.InfoBadge',
                                                   'Microsoft.UI.Xaml.Media.AcrylicBrush'}, {'Style', 'Setter', 'Grid'})
        self.assertEqual(left, ['Ink', 'Item', 'UsesItem'])
        self.assertEqual(keys(minidom.parseString(out).documentElement), ['Acrylic', 'Badge', 'Grid'])
        # the framework's own type table, as its parser has it
        self.assertIn('ItemsWrapGrid', build.framework_types())
        self.assertNotIn('InkToolbarFlyoutItem', build.framework_types())

    def test_resources_follow_what_they_name(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('winui2_build', str(Path(mergexaml.__file__).with_name('build.py')))
        build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build)
        text = (HEAD + '>'
                '<ResourceDictionary.ThemeDictionaries><ResourceDictionary x:Key="Default">'
                '<Style TargetType="Button"/><SolidColorBrush x:Key="Brush"/></ResourceDictionary>'
                '</ResourceDictionary.ThemeDictionaries>'
                '<Style TargetType="MenuFlyoutItem" BasedOn="{StaticResource Item}"/>'
                '<Style x:Key="Item" TargetType="MenuFlyoutItem"/>'
                '<Style TargetType="Button" BasedOn="{StaticResource Plain}"/>'
                '<Style x:Key="Plain" TargetType="Button"/>'
                '<Style x:Key="Bar"><Setter Property="Style" Value="{ThemeResource Presenter}"/></Style>'
                '<Style x:Key="Presenter" BasedOn="{StaticResource Plain}"/>'
                '</ResourceDictionary>')
        root = minidom.parseString(build.lookup_order(text)).documentElement
        items = [e for e in root.childNodes if e.nodeType == e.ELEMENT_NODE]
        self.assertEqual(items[0].tagName, 'ResourceDictionary.ThemeDictionaries')
        self.assertEqual([e.getAttribute('x:Key') or e.getAttribute('TargetType') for e in items[1:]],
                         ['Item', 'MenuFlyoutItem', 'Plain', 'Button', 'Presenter', 'Bar'])
        theme = root.getElementsByTagName('ResourceDictionary')[0]
        self.assertEqual([e.getAttribute('x:Key') or e.getAttribute('TargetType') for e in theme.childNodes],
                         ['Button', 'Brush'])


if __name__ == '__main__':
    unittest.main()
