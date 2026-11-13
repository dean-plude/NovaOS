## Windows Calculator's launch, seventeenth slice: views and framework strings

The seventeenth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's unhandled `E_BOUNDS`
had two causes: `UIViewSettings.GetForCurrentView` refused the app's view
thread (`ERROR_NOT_FOUND`), and WinUI 2 could not find its framework
package's strings in `ResourceManager.AllResourceMaps`.  The first now
answers on a thread with a `CoreWindow`; the second map now holds each
framework package's `resources.pri` by its package's name.  Every
originated Windows Runtime error also logs the stack it was raised from.

- **Tests:** `wuxamltest` calls `GetForCurrentView` on the `CoreWindow`'s
  thread (`Mouse`); `qtthemetest` checks it is `ERROR_NOT_FOUND` on a
  Win32 thread.
- **Not yet:** Calculator gets further and stops when `NavigationView`'s
  template sets `AnimatedIcon.FallbackIconSource` ("Specified cast is not
  valid").  The page does not draw yet.
