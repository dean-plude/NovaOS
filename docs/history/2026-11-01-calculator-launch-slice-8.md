## Windows Calculator's launch, eighth slice: Windows.Foundation.Diagnostics logging

The eighth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  `twinapi.appcore.dll` now serves
`Windows.Foundation.Diagnostics`' `LoggingChannel`,
`LoggingChannelOptions`, `LoggingOptions`, `LoggingFields` and
`LoggingActivity`, which Calculator's trace logger creates at start-up.
NovaOS has no ETW sessions, so a channel is never enabled.  `Enabled`
and the `IsEnabled` methods answer false, and logged events succeed but
go nowhere.

- **Tests:** `wrtpooltest` makes a channel, checks it is disabled, logs
  an event with fields and options, calls each `LoggingFields` `Add`
  method and starts an activity, 64- and 32-bit.
- **Not yet:** Calculator now stops on
  `FrameworkElementAutomationPeer`, whose factory the XAML core does not
  build yet.
