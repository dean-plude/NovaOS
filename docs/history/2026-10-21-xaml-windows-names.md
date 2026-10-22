## XAML under its Windows.UI.Xaml names

The first part of step 32.5 of `Windows.UI.Xaml` on NovaOS
([design](../design/windows-ui-xaml.md)), in the Phase 32 push: the XAML
framework, Microsoft's WinUI 3 source with its lifted `Microsoft.UI.Xaml`
interfaces, now also answers as `Windows.UI.Xaml`, the system XAML that
WinUI 2 and UWP apps are compiled against.

- **Faces:** `tools/winrt/winfaces.py` generates, from the Windows SDK's
  and the framework's metadata, a face per `Windows.UI.Xaml` interface
  over the framework's (2106 faces, 7550 methods matched by name), the
  reverse faces the framework calls on an app's objects (overrides,
  event handlers), boxes of Windows enums and structs, and the type-name
  table. A framework object asked for a Windows interface hands out the
  face; a method with no counterpart says so once and returns
  `E_NOTIMPL`.
- **Activation:** `Windows.UI.Xaml.*` class names activate the
  framework's classes through its own module and activation table,
  vendored from Microsoft's sources (`comModule.cpp`, `Factories.g.cpp`).
- **Built from their sources:** the dependency properties' objects
  (`DependencyProperty.Register`, `MetadataAPI`, `PropertyMetadata`) and
  the enums' WinRT boxes.
- **ole32:** `RoParseTypeName`, with which the framework reads a box's
  element type.
- **Test:** `wuxamltest` creates and uses `Windows.UI.Xaml` controls,
  dependency properties, `XamlReader.Load`, events and a control composed
  over `Control` with its own `MeasureOverride` (64-bit).
- **Next:** WinUI 2's own controls built from Microsoft's sources on these
  names, then Calculator.
