## Windows Calculator's launch, seventh slice: UserActivityRequestManager

The seventh part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  `twinapi.appcore.dll` now serves
`Windows.ApplicationModel.UserActivities`' `UserActivityRequestManager`,
which Calculator's `MainPage` asks for the view's manager.
`GetForCurrentView` returns the one view's manager.  Its
`UserActivityRequested` event takes and removes handlers but is never
raised, because NovaOS has no Timeline.

- **Tests:** `wrtpooltest` gets the manager from its statics (the same
  one twice) and adds and removes a handler, 64- and 32-bit.
- **Not yet:** Calculator now stops on
  `Windows.Foundation.Diagnostics.LoggingFields`, which is not registered.
