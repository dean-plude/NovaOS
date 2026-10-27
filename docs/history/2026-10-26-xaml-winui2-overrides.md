## WinUI 2's overrides

The tenth part of step 32.5 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)), in the Starlight 0.2.1 push:
WinUI 2's controls get their `OnApplyTemplate`, `MeasureOverride` and
the rest of their overrides called, so `RadioButtons` makes its
`RadioButton`s and `NavigationView` applies its template and lays out.

- **Overrides:** windows-rs's `Windows.winmd` leaves
  `OverridableAttribute` off the classes' interface rows, so C++/WinRT
  produced none of WinUI 2's overrides.  The build now takes Windows'
  `Windows.UI.Xaml` `I...Overrides[N]` interfaces as overridable, as
  Windows' own metadata marks them.
- **C++/WinRT under clang:** `get_self` and `try_as` of an implementation
  type on a null object or one that is not that type now give null (clang
  turned them into a pointer just below null, where MSVC keeps null).
- **Composition shapes:** `dcomp.dll`'s compositor makes shape visuals,
  container and sprite shapes, ellipse, rectangle, rounded-rectangle,
  line and path geometries, view boxes and animation controllers, drawn
  by a software rasteriser, and keeps implicit animation collections
  (`ProgressRing`'s and `AnimatedIcon`'s Lottie visuals, `NavigationView`'s
  pane button).
- **Resources:** `MrmCoreR.dll`, the package resource system's
  `ResourceManager`, `ResourceContext` and `ResourceLoader` over MRT
  Core, and WinUI 2's English strings in `resources.pri`.
- **What the controls ask the system:** the main view's title bar and
  `CoreApplicationView2`/`3` (twinapi.appcore), `AnalyticsInfo` (a
  desktop), the system back button's listeners (FrameworkUdk), and
  delegates crossing to WinUI 2 boxed as its own `IReference`.
- **Framework pieces vendored:** keyboard accelerators, `RoutedEvent`,
  `LayoutInformation`, `ThemeShadow`, `SymbolIcon`, `FontIcon` and their
  icon sources, `Viewbox`, `Path`, `ScrollBar` with `Thumb` and
  `RepeatButton`, the `Thickness`, `GridLength` and `CornerRadius`
  helpers and `IsEnabledChangedEventArgs`; `VisualState.Name` written
  for NovaOS.
- **Test:** `winui2test` checks the `RadioButtons`' three
  `RadioButton`s, `NavigationView`'s template, its layout, and, its pane
  opened, its two `NavigationViewItem`s.
