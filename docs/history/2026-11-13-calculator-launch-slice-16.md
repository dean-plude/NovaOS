## Windows Calculator's launch, sixteenth slice: the window's events

The sixteenth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  A UWP app's `Window.Activated`,
`Closed`, `SizeChanged` and `VisibilityChanged` now work: their Windows
handlers take `Windows.UI.Core`'s event arguments, which the framework's
`Window` (WinUI 3, with its own argument types) has no counterpart of, so
NovaOS's `Windows.UI.Xaml` face subscribes them on the thread's
`CoreWindow` itself, as Windows does, with the window as sender.

- **Tests:** `wuxamltest` adds `Window.Activated`'s handler, activates the
  thread's `CoreWindow` and checks the call, its sender and its arguments,
  then removes it.
- **Not yet:** Calculator gets past its window's events and stops on an
  unhandled `E_BOUNDS` (after an `ERROR_NOT_FOUND`) reported from the
  framework's dispatcher tick.  The page does not draw yet.
