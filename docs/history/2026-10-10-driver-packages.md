## Driver packages install from a program, a .zip or an MSI

A Windows driver package (an INF with its `.sys` and catalog) can be
installed the ways one arrives.  `setupapi.dll` reads INF files
(`SetupOpenInfFile`, `SetupGetLineText`, with `[Strings]` substitution)
and stages a package as Windows does: `SetupCopyOEMInf` copies the INF, its
`CatalogFile` and every file its `SourceDisksFiles` sections list to
`C:\Windows\System32\DriverStore\FileRepository\<inf>_<arch>_<hash>`,
publishes the INF as `C:\Windows\INF\oemN.inf`, records both under
`HKLM\SYSTEM\DriverDatabase`, refuses a package with a file missing and
finds a published one again instead of publishing it twice;
`SetupUninstallOEMInf` and `SetupGetInfDriverStoreLocation` complete it.
The new `newdev.dll` has `DiInstallDriver`, `DiUninstallDriver` and
`UpdateDriverForPlugAndPlayDevices`, which driver setup programs call, and
the new `pnputil.exe` adds (`/add-driver`, with `*.inf`, `/subdirs`,
`/install`), lists (`/enum-drivers`) and deletes (`/delete-driver`)
packages, so a driver unpacked from a .zip installs with it and an MSI's
custom action can run it.  NovaOS's own kernel drivers run its devices and
it does not load Windows kernel-mode drivers, so a staged package is not
started on a device: `UpdateDriverForPlugAndPlayDevices` answers
`ERROR_NO_SUCH_DEVINST`, as Windows does when the device is not present,
and catalogs are not checked.  The `drvtest` self-test installs
`tools/drivertest/mkdriver.py`'s package all three ways.
