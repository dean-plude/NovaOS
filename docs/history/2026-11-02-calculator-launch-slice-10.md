## Windows Calculator's launch, tenth slice: the CoreWindow's site bridge

The tenth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  NovaOS's `Microsoft.UI.Input.dll`
now serves `Microsoft.UI.Content.CoreWindowSiteBridge`, through which a
UWP app's XAML shows its root content island in the app's CoreWindow: a
bridge window fills the CoreWindow, follows its size and takes its
keyboard focus, and the island connected to it draws there and gets the
window's input.

- **Tests:** `contentislandtest` makes the bridge on a stand-in CoreWindow,
  connects an island at the window's size and resizes it.
- **Not yet:** Calculator now stops on the framework's `InputSiteAdapter`,
  which connects the island's input to XAML.
