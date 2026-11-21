## Windows Calculator's launch, thirty-first slice: the formatted text

The thirty-first part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's page words some of
its text as a `Span` or a `Hyperlink` holding `Run`s, and NovaOS's XAML
core did not build the list those pieces live in, so the page load
stopped there.  The core's text element collection
(`CTextElementCollection`), the text element every inline is made over
(`CTextElement`) and the text schema that says which elements a text
block, a span or a link may hold (`CTextSchema`) are now built from
Microsoft's sources (microsoft-ui-xaml, MIT), with no NovaOS change.

- **Tests:** `wuxamltest` loads a `TextBlock` whose `Span` holds a `Run`
  and a `Hyperlink`.
- **Found:** Calculator's page now loads and reaches its first layout
  pass.  There a WinUI 2 control's measure calls its `Windows.UI.Xaml`
  base class's `MeasureOverride`, which answers "not implemented", and
  the app ends on the unhandled error.  That is the next part.  The page
  does not draw yet.
