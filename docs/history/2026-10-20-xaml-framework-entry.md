## XAML's framework entry points

Eleventh part of step 32.3 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](design/windows-ui-xaml.md)): the first of the framework
itself (`dxaml/lib`) now builds from Microsoft's sources on the core.

- **Built:** `DXamlCore`, the per-thread framework core, with its
  instance storage and `DXamlServices`, `XamlReader` and
  `FrameworkApplication` (`Application`), with their generated classes.
- **Generated headers:** the Windows Runtime headers now carry what the
  framework names: disabled features' members, every unsealed class's
  factory, and the generic collections those classes require.
- **NovaOS:** MSVC's dynamic `thread_local` initialisation, which the
  framework's per-thread core uses, plus `AppPolicyGetWindowingModel`
  and `ImmDisableLegacyIME`.
- `xamlloadtest` makes `XamlReader`'s factory and calls
  `XamlReader.Load` on a thread without the framework, which fails with
  `RPC_E_WRONG_THREAD` as on Windows.
