## Windows Calculator's launch, twenty-ninth slice: the list items panel

The twenty-ninth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's history and memory
lists are `ListView`s, whose template lays their items out in an
`ItemsStackPanel`, and NovaOS's XAML framework did not build it, so the
page load stopped there.  The framework's `ItemsStackPanel` is now built
from Microsoft's sources (microsoft-ui-xaml, MIT), with the panel every
virtualizing list panel shares under it (`ModernCollectionBasePanel`: its
container and header mapping, cache and window management, viewport,
sticky headers, snap points and interaction) and the stacking layout
strategy it lays items out with (`StackingLayoutStrategy` and the
layout strategy component, `components/moco`).  No NovaOS change was
needed for them to compile.

- **Tests:** `wuxamltest` loads an `ItemsStackPanel` from markup and reads
  its `Orientation` and `CacheLength` back through Windows' interfaces.
- **Found:** Calculator's page gets past its lists' panel and stops next
  at a `HyperlinkButton`, which NovaOS does not build yet.  That is the
  next part.  The page does not draw yet.
