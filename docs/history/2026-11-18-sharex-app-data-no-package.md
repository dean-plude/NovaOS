## ShareX starts again: app data only for packaged apps

ShareX crashed at start-up (the app corpus's "capture the screen" check):
its history opens through Microsoft.Data.Sqlite, whose `SqliteConnection`
looks for a UWP app's data folder first.  NovaOS handed every process
`Windows.Storage.ApplicationData.Current`, so Sqlite went on to ask for
`LocalFolder`, which NovaOS does not have yet, and the exception ended the
program.  On Windows a process with no package identity is refused
`Current` with `APPMODEL_ERROR_NO_PACKAGE` (0x80073D54, "The process has no
package identity"), which Sqlite takes to mean it is not a UWP app.
`Windows.Storage.ApplicationData.dll` now does the same, asking kernel32's
`GetCurrentPackageFullName`; a packaged app such as Calculator still gets
its `LocalSettings`.

- **Tests:** `wrtapptest` checks that `Current` is refused to a program
  with no package; its `LocalSettings` checks moved to `appxtest`'s child,
  which runs inside a package.  The app corpus's ShareX check covers the
  app.
- **Not yet:** `LocalFolder`, `LocalCacheFolder`, `RoamingFolder` and
  `TemporaryFolder` (StorageFolders) for packaged apps.
