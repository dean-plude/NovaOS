## Windows Calculator's launch, nineteenth slice: the framework's localized strings

The nineteenth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's "not implemented"
during `NavigationView`'s layout came from the browser host's
`GetLocalizedResourceString`, which was never built: a windowed `Popup`
asks it for the title of its host window, and automation peers for their
control type names.  Microsoft's reads these strings from a localized
resource DLL whose table is not in the public repository, so NovaOS now
keeps their English text itself; an id without text reads as the empty
string, as Microsoft's does when its table has no row.

- **Tests:** `wuxamltest` reads the button's automation peer's localized
  control type (`button`).
- **Not yet:** Calculator gets through layout to its first frame's
  `Loaded` events and stops there: one of its `Loaded` handlers throws
  "Specified cast is not valid" (E_NOINTERFACE), which the framework
  reports as unhandled.  The page does not draw yet.
