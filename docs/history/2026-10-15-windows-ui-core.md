## The UWP app model's core: CoreApplication and CoreWindow

First step of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](../design/windows-ui-xaml.md)).  XAML's `Application.Start` and
every DirectX UWP app stand on the Windows Runtime's app model, so that
comes first:

- **Activation from the registry.**  `RoGetActivationFactory` used to know
  only the classes built into `ole32`.  It now also loads the in-process
  server the registry names for a class, as combase does:
  `HKLM\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\<class>` with
  `DllPath` (and `ActivationType` 0), whose `DllGetActivationFactory`
  makes the factory.
- **`twinapi.appcore.dll`** (new) serves
  `Windows.ApplicationModel.Core.CoreApplication`: `Run` takes the app's
  `IFrameworkViewSource`, makes its view and calls `Initialize`,
  `SetWindow`, `Load`, the launch's `Activated` event
  (`LaunchActivatedEventArgs` with the command line as `Arguments`), `Run`
  and `Uninitialize`; `Exit`, `GetCurrentView` and `MainView`.  The view's
  `Windows.UI.Core.CoreWindow` is a top-level window (its HWND through
  `ICoreWindowInterop`) with `Activate`, `Close`, `Bounds` and key states
  in DIPs, pointer capture and a hidden cursor, and the `Activated`,
  `CharacterReceived`, `Closed`, `KeyDown`, `KeyUp`, `SizeChanged` and
  `VisibilityChanged` events from its messages; its `CoreDispatcher`
  pumps them (`ProcessEvents` with every option) and runs `RunAsync` work
  posted from other threads, completing its `IAsyncAction`.
  `CoreWindow.GetForCurrentThread` answers on the window's thread.
- **DXGI's `CreateSwapChainForCoreWindow`** makes a flip-model swap chain
  on the CoreWindow's HWND, and `GetCoreWindow` gives the window back, so a
  DirectX UWP app's frame loop presents.

New core self-test `coreapptest` (64- and 32-bit).  Not yet: the pointer
events (they take handlers and are not raised; `PointerPoint` comes with
composition in step 32.2), `RunIdleAsync`, `CoreCursor` objects, secondary
views, `Properties`, suspend and resume, and app packages.
