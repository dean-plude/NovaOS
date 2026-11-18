## Windows Calculator's launch, twenty-fifth slice: the context menus

The twenty-fifth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's history and memory
lists hold a context menu, a `MenuFlyout`, which NovaOS did not build, so
the main page stopped there.  NovaOS now vendors and builds it from
Microsoft's XAML sources (MIT): `MenuFlyout`, its items (`MenuFlyoutItem`,
`ToggleMenuFlyoutItem`, `MenuFlyoutSeparator`, `MenuFlyoutSubItem`), the
`MenuFlyoutPresenter` that shows them, their automation peers and the
arguments of a context menu request.

- **Tests:** `wuxamltest` loads a `MenuFlyout` with an item, a separator
  and a toggle item from markup, reads its three items through Windows'
  `IMenuFlyout` and the first item's `Text`.
- **Found:** Calculator's main page now gets past its context menus and
  stops next when one of the app's own event handlers, called by the
  framework as the page loads, throws a `NullReferenceException`.  That is
  the next part.  The page does not draw yet.
