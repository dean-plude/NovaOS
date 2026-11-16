## Windows Calculator's launch, twelfth slice: the navigation transitions

The twelfth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  A frame's page transition,
`NavigationThemeTransition`, and the six navigation transition infos
(Common, Slide, Continuum, DrillIn, Suppress, Entrance) now activate.
They come from WinUI's phone library, which NovaOS now vendors and builds
into the XAML framework, with the pieces of WRL it needs.

- **Tests:** `wuxamltest` activates the transition and the six infos by
  name and checks they compose over the framework's bases.
- **Not yet:** Calculator now gets past the transition and stops later
  in the dispatcher's tick with `E_NOTIMPL` (0x80004001), most likely a
  core function not built yet such as `CImageBrush::ReloadImage`.
  The page does not draw yet.
