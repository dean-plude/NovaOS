## XAML's dispatcher, default styles and Window

Phase 32 push on step 32.3 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)): more of what
`DXamlCore::InitializeInstance` makes now builds from Microsoft's
sources.

- **Built:** the XAML dispatcher `DispatcherImpl`, the default control
  styles' cache `DefaultStyles`, the `Window` and the framework's peer
  base class `DirectUI::DependencyObject`.
- `xamlloadtest` makes a `DispatcherImpl` and a `DefaultStyles`; a
  `Window` follows once the thread's `DXamlCore` initializes.
