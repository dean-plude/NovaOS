## XAML's UWP start-up runs through

The sixth part of step 32.5 of `Windows.UI.Xaml` on NovaOS
([design](../design/windows-ui-xaml.md)), in the Phase 32 push: a UWP
app's `Application.Start` now runs to the end, and WinUI 2's theme
resources load much further.

- **UWP start-up:** inside its package, `winui2test` gets
  `Window.Current` (the view's window) and its `CoreWindow`, sets the
  window's content, activates it and exits (15 checks).  On the way:
  the framework's debug tool lookup, `CoreWindow`'s `ICoreWindow5`, and
  the `WindowCreatedEventArgs` class.
- **Windows-only types:** WinUI 2's merged theme resources leave out the
  controls neither WinUI 2 nor the framework has (`InkToolbar`, `Pivot`,
  `LoopingSelector`, `MediaTransportControls`).
- **Attached and content properties:** WinUI 2's type table marks them
  as XAML text needs them (`AnimatedIcon.State`, an `AnimatedIcon`'s
  source).
- **What WinUI 2's brushes ask for:** `DesignMode` and `PowerManager`
  (twinapi.appcore), `Window.Current` on a desktop thread, the
  framework's `CompositionTarget` and `XamlLight`.
- **Next (32.5):** the theme resources stop at `RevealBrush.TargetTheme`;
  then the controls' default styles and Calculator's controls.
