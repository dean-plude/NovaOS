# winui2test: WinUI 2's own controls (Microsoft.UI.Xaml.dll 2.x, built from
# Microsoft's winui2/main sources) on the system XAML (roadmap step 32.5,
# second part): an InfoBadge and a DropDownButton activated from the DLL,
# composed over the framework's Windows.UI.Xaml controls, and WinUI 2's XAML
# type table reading and setting InfoBadge.Value; the framework's data
# binding and control templates (fourth part), an AcrylicBrush's inherited
# FallbackColor (fifth part); and the UWP start-up through Window.Current
# (sixth part), the
# program installed as a UWP app and started by its AUMID (elevated: appx
# install); WinUI 2's theme resources, XamlControlsResources, loaded
# whole (seventh part); and the controls' default styles with their
# templates applied: InfoBadge and the controls Calculator uses (eighth part);
# NavigationView and RadioButtons with their items from markup (ninth part);
# WinUI 2's overrides called: the RadioButtons' and NavigationView's
# ItemsRepeaters make their items, NavigationView lays out (tenth part)
DOC = ('`winui2test` (WinUI 2\'s own controls, `Microsoft.UI.Xaml.dll` 2.x built from Microsoft\'s `winui2/main` '
       'sources, on the system XAML: an `InfoBadge` activated from the DLL and composed over the framework\'s '
       '`Windows.UI.Xaml.Controls.Control`, WinUI 2\'s XAML type table (`XamlControlsXamlMetaDataProvider`) reading, '
       'setting and reading back `InfoBadge.Value`, and a `DropDownButton` composed over `Button`; data binding: '
       '`{Binding}` to the element, its `DataContext` and a control template\'s templated parent, a `Binding` made in '
       'code following its source, `{TemplateBinding}`, `Control.ApplyTemplate`; and the UWP start-up: the program '
       'installed as a UWP app, started by its AUMID, with its package identity, the Universal windowing model and '
       'the app\'s `Application` made on `Application.Start`\'s UWP route, `Window.Current` with its `CoreWindow`, its '
       'content set, `Activate` and `Application.Exit`; and an `AcrylicBrush` loaded from markup '
       'with `FallbackColor`, a property of its framework base; and WinUI 2\'s theme resources: '
       '`XamlControlsResources` loaded from its `Source`, a theme resource looked up and its dark theme\'s '
       'acrylic brushes tinted through Windows\' `IAcrylicBrush2`; and the controls\' default styles: '
       '`XamlControlsResources` merged into the app\'s resources, `InfoBadge`, `ProgressBar`, `ProgressRing` and '
       '`SwipeControl` made from markup with their templates applied, and a page with a `ProgressRing`, a '
       '`SwipeControl` with `SwipeItems`, an `InfoBadge` and `BackdropMaterial.ApplyToRootOrPageBackground`; and '
       '`NavigationView` and `RadioButtons` with their templates, and a page with a `NavigationView` holding '
       '`NavigationViewItem`s in `MenuItems` and `RadioButtons` with three `RadioButton`s: the menu items and the '
       '`RadioButtons`\' items and `SelectedIndex` added from markup, and the `RadioButtons` measured and arranged '
       'with the `ItemsRepeater` in its template; and WinUI 2\'s overrides called: the `RadioButtons`\' '
       '`ItemsRepeater` making its three `RadioButton`s, the `NavigationView` applying its template, laid out, and '
       'with its pane opened making its two `NavigationViewItem`s)')
TESTS = [
    Test('winui2test', 'winui2test', [r'winui2test: \d+ passed, 0 failed \(64-bit\)'], timeout=300, elevated=True),
]
