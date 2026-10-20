## App packages: .msix and .appx

Step 32.6 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](../design/windows-ui-xaml.md)), first part: Windows app packages
install, start and read their `resources.pri`.

- **`appx`** (`C:\Windows\System32\appx.exe`) installs a package
  (`appx install app.msix`), lists, removes and starts them (`appx start
  <family>!<app>`); opening an `.msix` or `.appx` file runs it.  It
  checks every 64 KiB of every file against the package's block map
  (SHA-256, -384 or -512), unpacks into `C:\Programs\WindowsApps\<full
  name>`, records the package in the user's package repository, registers
  its in-process Windows Runtime classes and adds its applications to the
  Start menu.  Full and family names are made as Windows makes them.
  Installing needs an administrator (UAC asks): Windows' deployment
  service writes `WindowsApps` as the system, and NovaOS has none yet.
- **Signatures:** NovaOS does not verify package signatures yet, so a
  signed package is refused, never installed unchecked; an unsigned one
  installs as on Windows 11, with `-AllowUnsigned` and a publisher
  carrying Microsoft's "unsigned" OID.
- **Package identity:** a process whose program is in an installed
  package belongs to it (`GetCurrentPackageFullName`, family, path,
  `GetCurrentApplicationUserModelId`, the app policies).  ole32 finds the
  package's own Windows Runtime classes for it.
- **Starting:** twinapi.appcore's `ApplicationActivationManager` starts an
  application by its user model id, in the package's folder.
- **`resources.pri`:** `MRM.dll` is Microsoft's MRT Core (MIT), built
  unchanged; `SHLoadIndirectString` resolves `@{<package>?ms-resource:...}`
  with it, which names packages and their Start menu entries.
  `tools/appx/makepri.py` and `makeappx.py` write `.pri` files and
  packages for tests.
- `appxtest` installs the test packages (`tools/appx/mktest.py`), refuses
  the damaged, the signed and the unsigned without the OID, starts the
  installed one by its user model id (which checks its identity, its
  strings and its Windows Runtime class) and removes it.

Not yet: bundles (`.msixbundle`), framework dependencies (VCLibs,
`Microsoft.UI.Xaml`), signature verification, file type and protocol
activation, the app container.  The Calculator package needs those and
steps 32.3 to 32.5.
