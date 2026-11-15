## Windows Calculator's launch, twenty-first slice: navigation transitions by their Windows names

The twenty-first part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's main page navigates
with a new `EntranceNavigationTransitionInfo`, which it could not create:
the registry named `Windows.UI.Xaml.dll` only for the framework's own
classes, not for the phone library's navigation transitions NovaOS
builds into it.  The seven of them (`NavigationThemeTransition` and the
six transition infos) are now registered under their
`Windows.UI.Xaml.Media.Animation` names, and their objects answer
Windows' interfaces through the framework's faces.

- **Tests:** `wuxamltest` activates
  `Windows.UI.Xaml.Media.Animation.EntranceNavigationTransitionInfo` by
  its Windows name and asks it for Windows' interfaces.
- **Not yet:** Calculator creates the transition info and still stops in
  the same main page `Loaded` handler: a framework call returns XAML parse
  error 0x802B000A with no error logged beneath it.  The page does not
  draw yet.
