## App packages: a download route for Microsoft Store apps

Step 32.6 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](../design/windows-ui-xaml.md)), third part: getting a Store
app's packages the way the Store app gets them.

- **`tools/appx/storefetch.py`** downloads a free Store product's bundle
  and the framework packages its manifest names, through the Store's
  public delivery service: the product's catalog entry, then the update
  service's `GetCookie`, `SyncUpdates` and `GetExtendedUpdateInfo2`
  (the steps StoreDev's StoreLib, MIT, takes).  The update service's
  certificate chains to Microsoft Root Certificate Authority 2011, which
  Windows trusts but the usual Linux bundle does not, so the tool pins
  that root by its thumbprint.  Only free apps download this way, and
  the packages stay exactly as Microsoft signed them.
- In a local boot, Calculator 11.2607's five frameworks from the Store
  (`Microsoft.UI.Xaml.2.8`, the .NET Native framework and runtime 2.2,
  `Microsoft.VCLibs.140.00` and its desktop variant) install unchanged
  with their signatures verified.
- `tests/tools/test_storefetch.py` checks reading the update service's
  answer and choosing the packages, without the network.

Not yet: Calculator's own bundle.  The Store's bundles name their
packages in the bundle manifest but leave them out of the bundle's
block map, each package carrying its own signature; `appx` refuses such
a bundle until it verifies each such package by its own signature, with
the bundle's publisher, as Windows does.
