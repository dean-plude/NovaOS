## Windows.System's MemoryManager and thread pool classes

The second item of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)), part of
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)): the Windows Runtime classes
Windows Calculator asks `RoGetActivationFactory` for after
`Windows.UI.Xaml.Application`.

- **`Windows.System.MemoryManager`:** the app's memory use (the
  process's private commit) against its limit (the system's commit
  limit), its usage level, the app and process memory reports; its
  events take handlers; `TrySetAppMemoryUsageLimit` refuses, as for a
  desktop app.
- **`Windows.System.Threading.ThreadPool`:** `RunAsync` runs a work item
  on the process's thread pool behind an `IAsyncAction` it is given:
  completion, failure with its `HRESULT`, cancellation, priority.
- **`Windows.System.Threading.ThreadPoolTimer`:** one-shot and periodic
  timers on the thread pool's timers, alive until fired or cancelled,
  with their destroyed handlers.
- **Where:** `twinapi.appcore.dll`, with `ActivatableClassId` keys in
  the registry, 64- and 32-bit.
- **Fixed on the way:** kernel32's `GetProcessMemoryInfo` refused a
  32-bit program's `PROCESS_MEMORY_COUNTERS` (it wanted the 64-bit
  size).
- **Test:** `wrtpooltest` (64- and 32-bit).
- **Not yet:** the memory events are never raised (NovaOS sets apps no
  memory limit); `WorkItemOptions.TimeSliced` is the default's
  behaviour.
