## Windows Calculator's launch, fourth slice: Frame, Page and MainPage's markup

The fourth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's `OnLaunched`
navigates its `Frame` to `MainPage`.  The system XAML now builds the
framework's `Frame`, `Page`, the navigation stack, cache and event
arguments, `SizeHelper`, and the helpers `MainPage`'s markup makes
(`XamlBindingHelper`, `XamlMarkupHelper`, `Duration`, `KeyTime`,
`RepeatBehavior`, `FontFamily`, the state triggers, the data and style
selectors, `FlyoutShowOptions`, four theme transitions and
`NavigationTransitionInfo`), vendored from microsoft-ui-xaml.

- **Windows faces:** a composable `CreateInstance` passes its outer
  object through untouched, which fixes the crash in Calculator's
  `NarratorNotifier`.  A framework object handed to the app is crossed
  to its class's Windows default interface, because .NET Native casts
  to a sealed class by name.
- **App model:** `Windows.ApplicationModel.Package.Current` and its `Id`.
- **Tests:** `coreapptest` checks `Package.Current` with no package.
- **Not yet:** Calculator now stops on its first `Image`.  The core
  image element and imaging (`BitmapImage`, `BitmapSource`) are the
  next slice.
