## Windows Calculator's launch, ninth slice: automation peers

The ninth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  The XAML core now builds the
automation peers Calculator asks for: `FrameworkElementAutomationPeer`,
`TextBlockAutomationPeer`, `ButtonAutomationPeer` and its base
`ButtonBaseAutomationPeer`, from Microsoft's sources.  An app's settings
container now answers a setting never written with null, as Windows does,
instead of an error that .NET turned into `KeyNotFoundException`.
`RoOriginateError` names each error it originates on the debugger's output.

- **Tests:** `wuxamltest` makes the peers of a text block and a button and
  finds the button's again; `wrtapptest` looks up a setting not set.
- **Not yet:** Calculator now stops on
  `Microsoft.UI.Content.CoreWindowSiteBridge`, which the framework needs to
  show a CoreWindow's content.
