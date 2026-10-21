# wuxamltest: the XAML framework under its Windows.UI.Xaml names (roadmap
# step 32.5, WinUI 2 on the system XAML, first part): a Button activated by
# its Windows name and driven through Windows' interfaces, a Windows-boxed
# enum, DependencyProperty.Register with Windows type names, XamlReader.Load
# and the page's children through Windows' interfaces, a base class's
# statics, a Windows event handler, and an app's control composed over
# Windows.UI.Xaml.Controls.Control whose Windows MeasureOverride the layout calls
DOC = ('`wuxamltest` (the XAML framework under its `Windows.UI.Xaml` names, as WinUI 2 and UWP apps see it: '
       '`Windows.UI.Xaml.Controls.Button` activated by its Windows name and driven through Windows\' interfaces, a '
       'Windows-boxed `Visibility`, `DependencyProperty.Register` with Windows type names, `XamlReader.Load` and the page\'s '
       'children through Windows\' `IVector<UIElement>`, `Control.IsTabStopProperty`, `Click` with a Windows handler, and an '
       'app\'s control composed over `Windows.UI.Xaml.Controls.Control` whose `MeasureOverride` the framework\'s layout calls)')
TESTS = [
    Test('wuxamltest', 'wuxamltest', [r'wuxamltest: \d+ passed, 0 failed \(64-bit\)'], timeout=150),
]
