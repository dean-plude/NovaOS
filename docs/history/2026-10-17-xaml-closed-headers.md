## The closed Windows App SDK headers, and XAML's type metadata

Fourth part of step 32.3 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](../design/windows-ui-xaml.md)): the headers the XAML core's lower
layers include from closed Windows App SDK pieces now have NovaOS
stand-ins, and the next layers of Microsoft's sources build.

- **FrameworkUdk's helpers** (`userland/windows_ui_xaml/inc`):
  `InputSiteHelpers.h` (an island's input site and its window, which
  `CDependencyObject.h` includes), `InputHelpers.h`,
  `ContentExternalLinkHelper.h`, `TextInputProducerHelper.h` and
  `FrameworkUdk/FrameworkInputView.h`, written from how the open sources
  call them.
- **The lifted IXP's interop headers:** `microsoft.ui.composition.interop.h`,
  `microsoft.ui.composition.experimental.h`,
  `microsoft.ui.input.inputpretranslatesource.interop.h` and the Windows
  App SDK's `Microsoft.DirectManipulation.h`; lifted types newer than
  windows-rs's `Microsoft.UI.winmd` come from NovaOS's own IDL.
- **Windows SDK pieces MinGW-w64 lacks:** WRL's `async.h` (`AsyncBase`),
  `rometadataresolution.h`, `d2d1_3.h` (Direct2D 1.1 so far), the UI
  Automation interfaces with their IIDs from Windows' Win32 metadata,
  `GUID_NULL`, constexpr enum-flag operators; `AsyncStatus` became a
  scoped enum, as in the SDK.
- **Built:** the type metadata (`metadata`, `DynamicMetadataStorage`),
  linked, and `CDependencyObject`'s property system (`DependencyObject`),
  compiled, in `xaml_components.lib`.

`xamlbasetest` now also looks up XAML's built-in types and dependency
properties by index and by name.
