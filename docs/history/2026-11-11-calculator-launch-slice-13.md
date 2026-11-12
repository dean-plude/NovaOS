## Windows Calculator's launch, thirteenth slice: an Image's brush and kept event handlers

The thirteenth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  An `Image` now paints through the
framework's image brush (`CImageBrush`, vendored), whose missing
`ReloadImage` stopped Calculator with `E_NOTIMPL`.  And an app's event
handler that the framework would hold by reference tracking, as .NET
Native's delegates are, now stays alive while it is subscribed: NovaOS's
wrapper of it had been freed and its memory reused.

- **Tests:** `wuxamltest` measures an `Image` with a source, and adds and
  removes a `Loading` handler that is a reference tracker target.
- **Not yet:** Calculator now gets past both and stops in its page's
  `Loading` handler on a .NET `NullReferenceException`.  The page does
  not draw yet.
