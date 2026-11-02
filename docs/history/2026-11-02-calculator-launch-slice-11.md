## Windows Calculator's launch, eleventh slice: the input site adapter

The eleventh part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  The XAML framework's
`InputSiteAdapter` is now built: it hands the keyboard, pointer, focus and
activation input of a UWP app's CoreWindow island to XAML.  NovaOS's
content islands answer the input site it asks for, the window their
input arrives at.

- **Tests:** `contentislandtest` checks the CoreWindow island's input site
  and each input source the adapter asks for.
- **Not yet:** Calculator now stops on `NavigationThemeTransition`, the
  page transition its frame plays.
