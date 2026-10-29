## Windows Calculator's launch, third slice: compiled XAML from resources.pri

The third part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  The `E_FAIL` that stopped
Calculator's `OnLaunched` came from `App.xaml` never loading.  A package
keeps its compiled XAML (`app.xbf` and WinUI 2's theme resources) inside
`resources.pri`, but the system XAML's resource provider read only
files.  NovaOS's provider now also reads embedded candidates through
`MRM.dll`, choosing the package by its resource map, as Microsoft's
`ModernResourceProvider` does through MRT.

- **App model:** `CoreApplication.Properties` and the view's
  `Properties` (where WinUI 2 keeps its per-view objects).
- **Boxes:** each box names its ``Windows.Foundation.IReference`1<T>``
  class, which .NET Native unboxes by.
- **Tests:** `propvaltest` checks a box's class name, and `coreapptest`
  checks both `Properties`.
- **Not yet:** Calculator now stops on XAML classes the system DLL does
  not build yet (`SizeHelper`, then `Frame` and `Page`).
