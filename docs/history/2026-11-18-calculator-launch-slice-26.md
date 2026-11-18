## Windows Calculator's launch, twenty-sixth slice: resources the app looks up

The twenty-sixth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's page load stopped on a
`NullReferenceException` in one of its storyboard's `Completed` handlers,
which read the Calculator control's `DataContext` and found it empty.  The
control never got it because its constructor threw first: it casts
`Resources["DisplayContextMenu"]` to `MenuFlyout`, and the resources' map
(`IMap<Object, Object>`, whose interface ID is the same in the framework and
in Windows) answered with the framework's own `MenuFlyout`, which .NET
Native does not know by its lifted class name, so the cast threw
`InvalidCastException`.  A `ResourceDictionary` lookup from the app's code
now returns the value as the app's Windows object, as `GetValue` already
did; the framework's own lookups are unchanged.

- **Tests:** `xamldlltest` (a program on the system `Windows.UI.Xaml.dll`,
  as an app is) looks up a `MenuFlyout` in its page's resources through
  their `IMap` and reads its class name,
  `Windows.UI.Xaml.Controls.MenuFlyout`.
- **Found:** Calculator's control now gets its `DataContext` and the null
  reference is gone.  The page stops next at a `ListView` (its history
  list), which NovaOS does not build yet.  That is the next part.  The page
  does not draw yet.
