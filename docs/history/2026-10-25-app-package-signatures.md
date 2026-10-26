## App packages: signatures, framework packages and bundles

Step 32.6 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](design/windows-ui-xaml.md)), second part: signed packages,
the framework packages apps depend on, and bundles.

- **Signatures:** wintrust's `WinVerifyTrust` verifies an app package
  (`.msix`, `.appx` and their bundles) as Windows' APPX SIP does: the
  hashes in `AppxSignature.p7x` of the package's zip records, central
  directory, content types, block map and code integrity catalog, then
  the signer's signature, timestamp and chain to a trusted root.  `appx`
  installs a signed package only when it verifies and its publisher is
  the signer; a package whose signature fails is never installed.
  Microsoft's own signed `Microsoft.VCLibs.140.00.UWPDesktop`,
  `Microsoft.UI.Xaml.2.8` and .NET Native framework packages install
  unchanged in a local boot.
- **Certificates:** crypt32 now reads certificates that mark Microsoft's
  application policies extension, or certificate policies other than
  anyPolicy, critical, as CryptoAPI does: the Store's signing
  certificates and the older Microsoft Time-Stamp PCA 2010 do.  Before,
  their signatures could not be checked at all.
- **Framework packages:** a framework (`Microsoft.VCLibs`,
  `Microsoft.UI.Xaml`, the .NET Native framework and runtime) installs
  like any package.  A package that depends on one installs once the
  framework is in, at its minimum version, and a framework is not
  removed while a package uses it.  The kernel's loader searches a
  packaged program's frameworks for its DLLs (its package graph, as
  Windows' loader does), kernel32's `GetCurrentPackageInfo` lists the
  graph, and ole32 finds the frameworks' Windows Runtime classes.
- **Bundles:** a `.msixbundle` or `.appxbundle` is checked as a package
  is, then its application package for this computer installs from
  inside it.
- `appxtest` now also trusts a test root and installs a signed framework
  package and a signed bundle whose program finds the framework's DLL
  and class; it refuses the framework before its root is trusted, with
  its records changed after signing, or with another publisher, the
  bundle before its framework, and removing the framework while the app
  uses it.  `tools/appx/signappx.py` signs the test packages.

Not yet: a bundle's language and scale resource packages, and a
download route for the Microsoft Store's Calculator bundle, whose
frameworks these packages are.
