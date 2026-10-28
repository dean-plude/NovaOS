## Windows Calculator's launch, first slice

The first part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)): Windows Calculator's Store package
(11.2607, .NET Native over WinUI 2.8) now gets its `App` made, its window
opened and its `OnLaunched` run, up to its first page's globalization
classes.

- **The `App` object:** a composable class's `CreateInstance` face now
  crosses the new instance through the inner object it was handed, as
  .NET Native's outer object cannot answer for the class's interfaces
  until `CreateInstance` returns (it failed `0x80004001` before).
- **Faces that are part of their objects:** the `Windows.UI.Xaml` face
  a framework object gives (`nova_xaml_face_query`) counts the object's
  references and lives as long as it, one per object and interface, as
  an object's own interfaces do: C++/CX keeps a factory's statics and
  .NET Native the composed framework object's interfaces after releasing
  them (both crashed `0x1007`).
- **`OnLaunched`:** `Application`'s Windows override interfaces
  (`IApplicationOverrides`, `IApplicationOverrides2`) are faces of their
  own, so the framework's launch reaches the app's `OnLaunched` with
  Windows' `LaunchActivatedEventArgs`, which now carry the `User`,
  `PrelaunchActivated` and a `SplashScreen`.
- **New Windows Runtime classes:** `ResourceContext`'s
  `QualifierValues` and `Languages` (a new shared string map and string
  list, `userland/winrtmap`); `Windows.System.User`'s `GetFromId`,
  `Windows.Management.Policies.NamedPolicy` (nothing is managed) and
  `Windows.UI.ViewManagement.ApplicationView` (`TryResizeView`,
  `SetPreferredMinSize`, title, view id) in `twinapi.appcore.dll`; and a
  new `Windows.Storage.ApplicationData.dll` with
  `ApplicationData.Current`'s `LocalSettings`.
- **Fixed on the way:** ntdll ran a fiber-local storage callback after
  clearing the slot, so .NET Native's thread detach found its thread
  gone and failed fast (`0xc0000602`) whenever a thread ended; `appx`
  registered at most 64 classes of a package's in-process server
  (WinUI 2.8 has 136, so `XamlControlsXamlMetaDataProvider` was
  missing); clang 18 could hang building `dlltest.c`'s exception filter.
- **Diagnostics:** a fail fast (`RoFailFastWithErrorContext`,
  `RaiseFailFastException`) prints its stack as module+offset.
- **Tests:** `wrtapptest` (new), `coreapptest` (the launch's User,
  prelaunch and splash screen; `ApplicationView`), `tlsslots` (a thread's
  FLS callback still reads its value).
- **Not yet:** Calculator next asks for
  `Windows.Globalization.ApplicationLanguages`, the first of the
  `Windows.Globalization` classes its pages use (number and currency
  formatters, regions, calendars), which NovaOS does not have yet;
  `LocalSettings` live for the process only (no `settings.dat`) and the
  data folders (`LocalFolder`, `LocalCacheFolder`) are not there; part
  faces are never freed.
