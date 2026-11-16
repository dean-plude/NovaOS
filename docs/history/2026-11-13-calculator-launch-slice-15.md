## Windows Calculator's launch, fifteenth slice: layout bounds and text trimming

The fifteenth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's page now measures
itself: the framework's `LayoutBoundsChangedHelper` (Microsoft's source,
vendored), which a page asks its XAML root for, is built, and NovaOS's
DirectWrite makes the ellipsis trimming sign a trimmed `TextBlock` asks
for (`CreateEllipsisTrimmingSign`, which had answered "not implemented").

- **Tests:** `wuxamltest` checks the helper on the desktop path; `dwtest`
  measures and draws the ellipsis sign.
- **Not yet:** Calculator gets past both, builds its navigation view's
  template and stops on `E_NOTIMPL` from `Window.Activated`, which has no
  counterpart in the framework's `Window` yet.  The page does not draw
  yet.
