## Windows Calculator's launch, twenty-seventh slice: lists

The twenty-seventh part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's history and memory
lists are `ListView`s, and NovaOS's XAML framework did not build
`ListView`, so the page load stopped there.  The framework's `ListView`
is now built from Microsoft's sources (microsoft-ui-xaml, MIT), with
everything it stands on: `ListViewBase`, `Selector` and its selected
items, the list's item containers and their presenter, incremental
loading and the arguments of the list's events.  Not built yet: the list's
automation peers, dragging items out of a list, and the panels a list lays
its items out in.

- **Tests:** `wuxamltest` loads a `ListView` with two items from markup,
  reads it through Windows' `IListView`, `IListViewBase` and `ISelector`,
  and checks its two items, its `SelectionMode` (`Single`) and that
  nothing is selected yet (`SelectedIndex` -1).
- **Found:** Calculator's page gets past its lists and stops next at a
  `LinearGradientBrush`, which NovaOS does not build yet.  That is the
  next part.  The page does not draw yet.
