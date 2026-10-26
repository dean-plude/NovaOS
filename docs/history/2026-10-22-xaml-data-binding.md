## XAML data binding, control templates and the UWP start-up

The fourth part of step 32.5 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)), in the Phase 32 push: the
framework's data binding and control templates, which WinUI 2's styles
are written with, and the start of a UWP app.

- **Data binding, built from Microsoft's sources:** `Binding`,
  `BindingExpression`, `RelativeSource`, `PropertyPath` with its parser,
  steps and property accessors, the value converters, the markup
  extensions' service provider, `EffectiveValueEntry`,
  `VisualTreeHelper`, and the framework's `EventArgs`.
- **Control templates:** the style component (`CStyle` and the optimized
  style), `{TemplateBinding}` to a property the core does not bind
  itself, the core's create functions of the framework-only types (the
  default styles' theme transitions, with their states and stagger
  functions), and `XamlCompositionBrushBase` with its core half.
  uxtheme answers the framework's animation queries with no theme open.
- **Parser errors with their text:** `CommonErrors.rc`'s strings as a
  table written at build time, so a failed load says what failed.
- **UWP start-up:** the framework's view source, view and `CoreWindow`
  wrapper; the core's `EnableUWPWindow` feature on; twinapi.appcore's
  `CoreApplication` answers `ICoreApplication2` and serves `CoreCursor`.
- **Test:** `winui2test` loads `{Binding}`s to the element, its
  `DataContext` and a template's templated parent, makes a `Binding` in
  code and sees it follow its source, applies four kinds of template;
  then packs itself as a UWP app (`XamlUwpTest.msix`), installs it and
  starts it by its AUMID, which reaches `Application.Start`'s UWP route
  with the app's `Application` made (64-bit).
- **Next (32.5):** the animation component (the core's `DispatcherTimer`
  stops the UWP route), then `Window.Current`; WinUI 2's theme resources
  (`AcrylicBrush`'s inherited `FallbackColor`) and its controls' default
  styles.
