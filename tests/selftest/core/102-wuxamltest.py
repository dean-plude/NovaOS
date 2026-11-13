# wuxamltest: the XAML framework under its Windows.UI.Xaml names (roadmap
# step 32.5, WinUI 2 on the system XAML, first part): a Button activated by
# its Windows name and driven through Windows' interfaces, a Windows-boxed
# enum, DependencyProperty.Register with Windows type names, XamlReader.Load
# and the page's children through Windows' interfaces, an Image whose
# markup URI source becomes a BitmapImage, measured (its image brush loads
# the source), and a Popup holding its child
# (step 32.6), a base class's
# statics, Windows event handlers (one a reference tracker target, kept while
# subscribed), an app's x:Bind connector kept weakly (resolving to the
# framework's IComponentConnector), and an app's control composed over
# Windows.UI.Xaml.Controls.Control whose Windows MeasureOverride the layout calls,
# and the phone library's navigation transitions (a Frame's
# NavigationThemeTransition and the transition infos) activated by name, and
# the helper a XAML root's layout bounds come through (a page's measure)
DOC = ('`wuxamltest` (the XAML framework under its `Windows.UI.Xaml` names, as WinUI 2 and UWP apps see it: '
       '`Windows.UI.Xaml.Controls.Button` activated by its Windows name and driven through Windows\' interfaces, a '
       'Windows-boxed `Visibility`, `DependencyProperty.Register` with Windows type names, `XamlReader.Load` and the page\'s '
       'children through Windows\' `IVector<UIElement>`, an `Image` whose markup URI source becomes a `BitmapImage`, measured (its image brush loads the source), a `Popup` from markup holding its child, the text block\'s and button\'s automation peers, `Control.IsTabStopProperty`, `Click` with a Windows handler, `Loading` with a Windows handler that is a reference tracker target (kept while subscribed), an app\'s x:Bind connector kept by a weak reference that resolves to the framework\'s `IComponentConnector`, the helper a XAML root\'s layout bounds come through (no `ApplicationView` on the desktop path, callbacks added and removed), and an '
       'app\'s control composed over `Windows.UI.Xaml.Controls.Control` whose `MeasureOverride` the framework\'s layout calls; '
       'and the phone library\'s navigation transitions, a Frame\'s `NavigationThemeTransition` and the six transition infos, '
       'activated by name, composed over the framework\'s `Transition` and `NavigationTransitionInfo`, their dependency '
       'properties set and read back)')
TESTS = [
    Test('wuxamltest', 'wuxamltest', [r'wuxamltest: \d+ passed, 0 failed \(64-bit\)'], timeout=150),
]
