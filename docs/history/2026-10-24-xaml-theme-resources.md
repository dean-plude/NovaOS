## WinUI 2's theme resources load whole

The seventh part of step 32.5 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)), in the Phase 32 push:
`XamlControlsResources`, the dictionary of WinUI 2's theme resources an
app's `App.xaml` merges, now loads from its `Source` and answers lookups.

- **Composition types across the faces:** the framework's interfaces take
  `Microsoft.UI.Composition` objects where Windows' take
  `Windows.UI.Composition` ones of the same name; the compositor's objects
  answer both, so the Windows faces now pass them across (WinUI 2's
  `RevealBrush` setting its `CompositionBrush`, `UIElement.StartAnimation`).
- **Resources in lookup order:** Microsoft compiles WinUI 2's resources to
  XBF, which creates each resource when it is first looked up, so one may
  name a resource later in its dictionary; XAML text lets a resource see
  only the ones before it.  The merged files now list each resource after
  the ones it names.
- **Acrylic brushes:** the theme resources' `AcrylicBrush` is WinUI 2's
  on NovaOS (the framework has none), and it answers Windows'
  `IAcrylicBrush` and `IAcrylicBrush2` (the same methods), which
  `XamlControlsResources` uses to tint the dark theme's brushes.
- **Test:** `winui2test` checks the dictionary, a theme resource and a
  tinted brush (65 checks).
- **Next (32.5):** the controls' default styles applied (`InfoBadge`'s
  template), then the controls Calculator uses.  Calculator itself is a
  .NET Native app: its start-up needs the .NET Native framework packages
  (32.6) before NovaOS can run it.
