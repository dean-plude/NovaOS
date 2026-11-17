## Windows Calculator's launch, twenty-fourth slice: the ItemsControl

The twenty-fourth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Making Calculator's `Pivot` (its
history and memory panel) stopped at the class it is composed over, the
framework's `ItemsControl`, which NovaOS did not build.  NovaOS now
vendors and builds it from Microsoft's XAML sources (MIT): `ItemsControl`,
its `ItemCollection` and collection view manager, its
`ItemContainerGenerator` and the `ItemsPresenter`, with the other
framework classes the `Pivot` asks for by name as it is made (the `Rect`
and `Point` helpers, the layout transition and secondary content helpers,
`FocusManager`, the element sound player, the automation identifiers and
the item and items control automation peers).

- **Tests:** `wuxamltest` loads a `Pivot` with two `PivotItem`s from
  markup and reads its two items through Windows' `IItemsControl`.
- **Found:** Calculator's main page now makes its `Pivot` and stops next
  at its context menu, a `MenuFlyout`, which NovaOS does not build yet.
  That is the next part.  The page does not draw yet.
