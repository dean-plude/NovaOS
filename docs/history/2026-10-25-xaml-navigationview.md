## WinUI 2's NavigationView and RadioButtons

The ninth part of step 32.5 of `Windows.UI.Xaml` on NovaOS
([design](../design/windows-ui-xaml.md)), in the Phase 32 push: the rest
of Calculator's WinUI 2 controls, `NavigationView` and `RadioButtons`,
apply their templates and lay out.

- **Controls:** WinUI 2 now builds `Repeater` (`ItemsRepeater` and its
  layouts), `NavigationView` and `RadioButtons`.  Building them took
  C++/WinRT projecting a class's overridable and protected interfaces,
  as the version WinUI 2 pins does, and MIDL 3's `method_name` naming
  an overload's ABI method (about 25 more Windows faces connect with it,
  `LoadComponent` and `Frame.Navigate` among them).
- **Framework pieces they need:** `AutomationProperties`,
  `PropertyChangedEventArgs`, `NotifyCollectionChangedEventArgs`,
  `ToggleButton`, `RadioButton`, `PathIcon`, `TransformGroup`, `ToolTip` and `ToolTipService`, `Flyout`
  and `FlyoutBase`, `SplitView` and `XamlRoot` (vendored from
  Microsoft's WinUI 3 source); `ElementSoundPlayer` plays nothing, as on
  a PC.
- **What they ask the system:** the elements' `Dispatcher` on a desktop
  XAML thread (the hidden `CoreWindow`'s, as on Windows) and
  `CoreDispatcher.RunIdleAsync` (twinapi.appcore), where `ItemsRepeater`
  runs its cache work.
- **DirectWrite:** the framework's calls through `IDWriteFactory6` reach
  the methods they name (the build's DirectWrite headers had reordered
  that interface's vtable in the MSVC ABI).
- **Boxes:** an enum or struct value the framework boxes reaches WinUI 2
  boxed under Windows' `IReference` ID (`StackLayout.Orientation`).
- **Test:** `winui2test` makes each control from markup and checks its
  template, then loads a page with a `NavigationView` (its items in
  `MenuItems`) and `RadioButtons`, checks the menu items, the
  `RadioButtons`' items and selected index, and lays out the
  `RadioButtons`; its `Application` answers for `IVector<Object>`
  as a collection, as the app's generated `XamlTypeInfo` does on Windows.
- **Next (32.5):** WinUI 2's overrides (`OnApplyTemplate` and the rest:
  windows-rs's `Windows.winmd` leaves `OverridableAttribute` off, so
  C++/WinRT does not produce them, and `RadioButtons`' `ItemsRepeater`
  gets no items yet), then `NavigationView`'s layout, which waits on
  composition shapes (its pane button's `AnimatedIcon` draws with them).  Calculator
  itself is a .NET Native app: its start-up needs the .NET Native
  framework packages (32.6).
