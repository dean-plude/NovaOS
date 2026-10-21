## XAML's Application.Start on the desktop

The last part of step 32.3 of `Windows.UI.Xaml` on NovaOS
([design](../design/windows-ui-xaml.md)), in the Phase 32 push:
`Application.Start` runs, built from Microsoft's sources, which with
`XamlReader.Load` (the part before) completes the step.

- **Desktop start-up:** `Application.Start` takes the desktop path as on
  Windows: a `DispatcherQueue` on the thread, the app's initialization
  callback (the app composes its `Application` over the framework's),
  `WindowsXamlManager.InitializeForCurrentThread` starting the
  application on the thread and raising `OnLaunched`, the desktop message
  loop, and `Application.Exit` ending it so `Application.Start` returns.
- **Built from their sources:** `WindowsXamlManager`, the launch and
  initialization arguments, the debug settings, the core's application,
  visual tree, content root, focus manager and focus components, layout
  manager, time manager, input services, root scale, access keys and
  popups, the framework's desktop window object (`CJupiterWindow`) and the
  control layer's resource manager.
- **ole32:** `RoRegisterActivationFactories` and
  `RoRevokeActivationFactories` serve the classes a program registers
  itself, ahead of the registry; `RoOriginateLanguageException` now
  originates its error.
- **Test:** `xamlapptest` starts an app, loads a `Grid` holding a
  `TextBlock` in `OnLaunched` and exits from work queued on the
  `DispatcherQueue` (64-bit).
- **Next (32.4):** a `Window` on the Windows App SDK's content islands,
  and a page drawn.
