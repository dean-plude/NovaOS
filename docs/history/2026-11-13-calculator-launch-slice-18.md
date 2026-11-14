## Windows Calculator's launch, eighteenth slice: WinUI 2's classes under the framework's names

The eighteenth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's "Specified cast is
not valid" came from a name both XAML layers use: the system XAML's own
classes are `Microsoft.UI.Xaml.*` inside it, and WinUI 2 has its own
`Microsoft.UI.Xaml.Controls.FontIconSource`.  `NavigationView`'s template
made the system's `FontIconSource` for the pane button's fallback icon,
which Calculator's code could not cast to WinUI 2's `IconSource`.  Markup's
`using:Microsoft.UI.Xaml...` namespaces and dependency property
registrations now take the app's and WinUI 2's class of such a name first.

- **Tests:** `winui2test` loads an `AnimatedIcon` whose fallback icon is a
  `FontIconSource` with a binding on its `FontSize`, and reads it back
  through WinUI 2's `FontIconSource.Glyph`.
- **Not yet:** Calculator gets further and stops on an unhandled
  `E_NOTIMPL` from `NavigationView`'s layout (a windowed `Popup` asks the
  core for a localized string, which is not built).  The page does not
  draw yet.
