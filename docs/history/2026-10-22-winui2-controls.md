## WinUI 2's controls built

The second part of step 32.5 of `Windows.UI.Xaml` on NovaOS
([design](../design/windows-ui-xaml.md)), in the Phase 32 push: WinUI 2,
the controls library UWP apps ship, is built from Microsoft's MIT
`winui2/main` sources as `Microsoft.UI.Xaml.dll` 2.x and runs on the
system XAML under its `Windows.UI.Xaml` names.

- **No SDK tools:** `tools/winrt/idl.py` reads WinUI 2's IDL (MIDL 3 and
  the effects' MIDL 2) and `tools/winrt/winmdwrite.py` writes it as a
  `.winmd`; C++/WinRT's code generator, vendored and built for the build
  machine, writes the projection and the component's files; the XAML
  type table is written as Microsoft's T4 template writes it.
- **Features:** Microsoft's inner-loop set (shared code, collections,
  composition effects, materials, lights, icons, `AnimatedVisualPlayer`,
  `AnimatedIcon`, `SplitButton`, `DropDownButton`) and `InfoBadge`.
- **The framework:** its metadata provider (`XamlLibMetadataProvider`) is
  built and asks the app's `Application` for the types it does not know;
  a composed framework object hands out the app's Windows interface as
  its own; a method Windows split into a later interface version
  (`IXamlType2.BoxedType`) is reached there; a dependency property's
  changed callback gets its event arguments, and `VisualStateManager`'s
  statics run on the visual state machine (components/vsm), now built.
- **urlmon:** `Windows.Foundation.Uri`, which C++/WinRT code makes for
  every URI it passes, as Windows' `urlmon.dll` serves it.
- **ole32 and the C runtime:** `RoTransformError`; clang's guarded
  `thread_local` start-up (`__tls_guard`, `__dyn_tls_on_demand_init`).
- **Test:** `winui2test` makes an `InfoBadge` and a `DropDownButton`
  through WinUI 2's type table and reads, sets and reads back
  `InfoBadge.Value` (64-bit).
- **Next:** WinUI 2's XAML resources, the interface IDs apps were
  compiled with, then Calculator.
