## Windows.Foundation.Metadata.ApiInformation

The nightly app corpus caught Godot 4.7 ("Squash the Creeps") ending at
start-up: once CoreMessaging gave it a DispatcherQueue (step 32.3, part
12), Godot asks `ApiInformation.IsApiContractPresent` before looking for
the window's `DisplayInformation`, outside any `try`, and a class that
would not activate ended the program (`winrt::hresult_class_not_registered`).

- **New class:** `ole32` answers `Windows.Foundation.Metadata.ApiInformation`
  (`IApiInformationStatics`, static and agile).  API contracts are those
  of the Windows 10 22H2 NovaOS reports (SDK 10.0.19041):
  `Windows.Foundation.UniversalApiContract` up to 10.0 and
  `FoundationContract` up to 4.0.  A type is present when NovaOS
  activates it (ole32's own classes, or one the registry's
  `ActivatableClassId` key names); members are reported absent, since
  NovaOS ships no `System32\WinMetadata` to read them from (test
  `apiinfotest`, 64- and 32-bit).
