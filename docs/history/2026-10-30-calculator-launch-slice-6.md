## Windows Calculator's launch, sixth slice: the framework's Popup

The sixth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  The system XAML now builds the
framework's `Popup`, vendored from microsoft-ui-xaml, which Calculator's
`NavigationView` makes for its pane.

- **Strings:** `Popup` truncates a long automation name with
  `HString::Substring`.  NovaOS's WRL wrappers gain `Substring`, and the
  string operations now make their result before letting go of the
  output's own string (the popup passes the same string as both).
  ole32 gains `WindowsSubstring` and
  `WindowsSubstringWithSpecifiedLength`.
- **Display:** kernel32's `GetIntegratedDisplaySize`, which WinUI 2 asks
  next, answers as a desktop PC with no built-in panel.
- **Tests:** `wuxamltest` loads a closed `Popup` with a child from
  markup.
- **Not yet:** Calculator now stops on
  `Windows.ApplicationModel.UserActivities`' `UserActivityRequestManager`,
  which is not registered.
