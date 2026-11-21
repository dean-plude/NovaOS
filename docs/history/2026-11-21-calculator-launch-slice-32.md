## Windows Calculator's launch, thirty-second slice: the first layout pass

The thirty-second part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's first layout pass
ended on "not implemented" from WinUI 2's navigation view measuring its
`Windows.UI.Xaml` base class.  The error came from further down: a text
block whose text replaces its inlines first clears its selection, and
NovaOS's XAML core did not build the text selection manager.  It is now
built from Microsoft's sources (microsoft-ui-xaml, MIT), and its default
selection colour (the system's accent colour) replaces NovaOS's own.

- **Tests:** `wuxamltest` sets the `Text` of a `TextBlock` that holds a
  `Span` and reads it back.
- **Found:** Calculator's first layout pass now runs on until a brush
  transition (`CBrushTransition`), which NovaOS does not build yet.
  That is the next part.  The page does not draw yet.
