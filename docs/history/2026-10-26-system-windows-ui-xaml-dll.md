## The system Windows.UI.Xaml.dll

The first item of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)), part of
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)): the XAML framework is a Windows
component apps activate, as on Windows, instead of a library each test
links.

- **The DLL:** `Windows.UI.Xaml.dll` in System32 is NovaOS's XAML
  framework (Microsoft's WinUI 3 source). Its `DllGetActivationFactory`
  serves the framework's classes by their `Windows.UI.Xaml` names and
  starts the framework on the first call.
- **The registry:** every `Windows.UI.Xaml` class the framework serves
  (501) has its `ActivatableClassId` key naming the DLL, so
  `RoGetActivationFactory(Windows.UI.Xaml.Application)` loads it.
- **Test:** `xamldlltest` links nothing of the framework and registers
  no classes: it starts `Windows.UI.Xaml.Application`, makes its
  `Application`, shows a page from `XamlReader` in a `Window`, checks
  the layout and exits (64-bit).
- **Not yet:** a UWP app's own `OnLaunched`, Windows' arguments rather
  than the framework's; the Windows Runtime classes Calculator asks for
  next (`MemoryManager`, `ThreadPool`); a 32-bit DLL.
