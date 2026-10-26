## XAML's control layer and the DispatcherQueue

Twelfth part of step 32.3 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](design/windows-ui-xaml.md)): the control layer the
framework starts up on now builds from Microsoft's sources.

- **New DLL:** `CoreMessaging.dll`, the `DispatcherQueue` every XAML
  thread runs on, for both `Microsoft.UI.Dispatching` and
  `Windows.System`: priorities, timers, shutdown events with deferrals,
  `RunEventLoop`, dedicated threads and
  `CreateDispatcherQueueController` (test `dispatchqtest`).
- **ole32:** the restricted error info `RoOriginateError` makes and
  `GetRestrictedErrorInfo` hands back, as the framework's error helper
  reads it.
- **Built:** the error helper, `CJupiterControl`, `CControlBase`, the
  XAML window's dispatcher `CXcpDispatcher` and the browser host.
- `xamlloadtest` runs a `CXcpDispatcher` on a `DispatcherQueue` and
  calls `CJupiterControl::Create`, which now stops only at the
  rendering seam (the window render target), not yet built.
