## The content islands under a XAML Window

The first part of step 32.4 of `Windows.UI.Xaml` on NovaOS
([design](../design/windows-ui-xaml.md)), in the Phase 32 push: the
Windows App SDK's lifted content, input and windowing classes, which a
XAML `Window` sits on, and the framework's window classes built.

- **Microsoft.UI.Input.dll** (new, `userland/microsoft_ui_input`):
  `ContentIsland` and its environment, `DesktopChildSiteBridge` (a child
  window of the app's window, its composition target from the
  compositor's desktop interop), `ContentCoordinateConverter`, the
  island's input sources (`InputPointerSource`, `InputKeyboardSource`,
  `InputPreTranslateKeyboardSource`, `InputFocusController`,
  `InputActivationListener`, raising their events from the bridge
  window's messages), `InputNonClientPointerSource`, `InputSystemCursor`,
  `FocusNavigationRequest` and `AppWindow` with its overlapped presenter
  and title bar; registered as Windows Runtime classes.
- **Built from Microsoft's sources:** the framework's desktop window
  (`DesktopWindowImpl`), `DesktopWindowXamlSource`, the XAML island root,
  the window chrome and the window's event arguments; the platform
  layer's DirectWrite font and script services and the text core's font
  families and typefaces.
- **Headers:** MinGW-w64's DirectWrite headers are made to declare their
  interfaces as the Windows SDK does (a base interface's overloads stay
  in scope), generated at build time.
- **Test:** `contentislandtest` sizes a site bridge and an island
  together and checks the island's pointer, key and focus events and
  `AppWindow`'s `Changed` (64-bit).
- **Next (32.4):** the framework's theme resources (`generic.xaml`), which
  a `Window` loads as it is created, and DirectWrite's Windows 10 font
  model, which XAML's text is measured with; then a page drawn, typed
  into and clicked.
