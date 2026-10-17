## The XAML core's toolchain, and its lowest layer built

Third part of step 32.3 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](../design/windows-ui-xaml.md)): Microsoft's XAML sources
(`dxaml`, microsoft-ui-xaml, MIT) now compile for NovaOS as Microsoft
compiles them, and the first of them are built and tested.

- **The build** (`userland/windows_ui_xaml/build.py`): clang in MSVC mode,
  C++17 without C++ exceptions, over Microsoft's STL and MinGW-w64's
  Windows headers, each component's sources, include directories and
  definitions read from its MSBuild project (`vcxproj.py`), its forced
  precompiled header included.  A program whose manifest says
  `"xaml": true` is built the same way and linked with the result.
- **What the sources need beyond those headers:** WIL (MIT, vendored in
  `third_party/wil`); NovaOS's own WRL (the Windows SDK's Windows Runtime
  C++ template library: `ComPtr`, `WeakRef`, `RuntimeClass`, `Make`,
  `Callback`, `EventSource`, `HString` and the rest); `weakreference.h`,
  XAML diagnostics' `xamlom.h`, SAL annotations and Windows Runtime
  `HRESULT`s MinGW-w64 lacks; TraceLogging and ETW compiled out (the events
  header `mc.exe` would make, written by `etwgen.py`); MIDL's per-IDL-file
  headers from `abigen.py`; and one-line headers mapping each include
  spelling to the file a case-insensitive disk would find.
- **Built:** the components below `CDependencyObject` (`base`, `xstring`,
  `criticalsection`, `dependencyLocator`, `runtimeEnabledFeatures`, `com`,
  `threading`, `staticpal`, `qualifiers`) as `xaml_components.lib`.
- **`ole32.dll`** gained the string buffers XAML's string builder uses
  (`WindowsPreallocateStringBuffer`, `WindowsPromoteStringBuffer`,
  `WindowsDeleteStringBuffer`).
- **Found:** 75 headers the open sources include come from closed Windows
  App SDK pieces (FrameworkUdk's `InputSiteHelpers.h`, which
  `CDependencyObject.h` includes, the lifted composition interop headers,
  the PTLS text engine); NovaOS writes those next.

New core self-test `xamlbasetest` (64-bit, 46 checks) checks NovaOS's WRL (QueryInterface,
weak references, delegates, events, strings) and the built components.
