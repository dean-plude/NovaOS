## Windows Calculator's launch, fourteenth slice: x:Bind's connector

The fourteenth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  A page's compiled bindings
(x:Bind) now get their targets: the XAML parser keeps the app's binding
connector by a weak reference, which had resolved to an object that did
not answer the framework's `IComponentConnector`, so the parser never
connected the targets and Calculator's bindings threw a .NET
`NullReferenceException` on their first update.

- **Tests:** `wuxamltest` wraps an app's connector, keeps it weakly,
  resolves it and connects through it.
- **Not yet:** Calculator gets past it and stops on `E_NOTIMPL` in its
  page's layout (`LayoutBoundsChangedHelper`, not built yet).  The page
  does not draw yet.
