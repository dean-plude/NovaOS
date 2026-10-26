## App packages: the Store's bundles

Step 32.6 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](design/windows-ui-xaml.md)), fourth part: Calculator's own
bundle from the Microsoft Store installs unchanged, which completes the
step.

- **Store-layout bundles.** The Store's bundles name only
  `AppxMetadata\AppxBundleManifest.xml` in their block map; each package
  inside carries its own `AppxSignature.p7x` and code integrity catalog.
  `appx` now takes a file outside a bundle's block map only when the
  bundle's manifest names it as one of its packages, and installs such a
  package only by its own signature, verified as any package's (an
  unsigned one, or one signed by someone else, is refused).  A bundle's
  package must also have the bundle's publisher, as Windows requires,
  whether or not the bundle's block map covers it.
- In a local boot, Calculator 11.2607's bundle (downloaded with
  `tools/appx/storefetch.py`) installs its x64 package after its five
  frameworks, all with Microsoft's signatures verified, and
  `appx list` shows Windows Calculator.
- `appxtest` installs a test bundle laid out the Store's way
  (`tools/appx/makeappx.py`'s `store_layout`) and refuses two copies:
  one whose package is unsigned, one whose package is signed by another
  publisher.

Not yet: starting Calculator (its remaining WinUI controls, step 32.5)
and a bundle's language and scale resource packages.
