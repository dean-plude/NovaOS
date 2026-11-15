## Windows Calculator's launch, twentieth slice: framework objects handed back under their Windows class

The twentieth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's "Specified cast is
not valid" came from its title bar's `Loaded` handler, which reads the
`ButtonBackground` brush its style sets and casts it to
`Windows.UI.Xaml.Media.SolidColorBrush`.  `GetValue` handed the app the
framework's own brush, whose class is named
`Microsoft.UI.Xaml.Media.SolidColorBrush`, and .NET Native could not cast
an object of an unknown class to the Windows one.  An object a framework
call returns as `Object` (`GetValue`, `Content`, ...) now reaches the app
as its Windows class's face, as objects handed to the app's handlers
already did; WinUI 2's objects, which carry the same names, and an app's
own objects stay as they are.  Errors originated with
`RoOriginateError` now log 24 stack frames, enough to reach a .NET Native
exception's throw site.

- **Tests:** `wuxamltest` reads a `SolidColorBrush` property's value
  back as `Windows.UI.Xaml.Media.SolidColorBrush`.
- **Not yet:** Calculator gets past its title bar and stops while its
  main page navigates: `Windows.UI.Xaml.Media.Animation.
  EntranceNavigationTransitionInfo` cannot be activated by that name
  (XAML error 0x802B000A).  The page does not draw yet.
