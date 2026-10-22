# winui2test: WinUI 2's own controls (Microsoft.UI.Xaml.dll 2.x, built from
# Microsoft's winui2/main sources) on the system XAML (roadmap step 32.5,
# second part): an InfoBadge and a DropDownButton activated from the DLL,
# composed over the framework's Windows.UI.Xaml controls, and WinUI 2's XAML
# type table reading and setting InfoBadge.Value
DOC = ('`winui2test` (WinUI 2\'s own controls, `Microsoft.UI.Xaml.dll` 2.x built from Microsoft\'s `winui2/main` '
       'sources, on the system XAML: an `InfoBadge` activated from the DLL and composed over the framework\'s '
       '`Windows.UI.Xaml.Controls.Control`, WinUI 2\'s XAML type table (`XamlControlsXamlMetaDataProvider`) reading, '
       'setting and reading back `InfoBadge.Value`, and a `DropDownButton` composed over `Button`)')
TESTS = [
    Test('winui2test', 'winui2test', [r'winui2test: \d+ passed, 0 failed \(64-bit\)'], timeout=150),
]
