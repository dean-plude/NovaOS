## Windows Runtime headers without MIDL

Second part of step 32.3 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](../design/windows-ui-xaml.md)), and the first question the
`dxaml` build has to answer: Microsoft's XAML sources are written against
the C++ "ABI" headers MIDL makes from their IDL, and against the Windows
SDK's, and MIDL is a closed tool.

- **`tools/winrt/abigen.py`** writes those headers from Windows Runtime
  metadata (`.winmd`, read by `tools/winrt/winmd.py`) and MIDL 3 IDL
  (`tools/winrt/idl.py`, which synthesizes a runtime class's `IFoo`,
  `IFooStatics`, `IFooFactory`, `IFooOverrides`, `IFooProtected` and its
  versioned member blocks' interfaces as MIDL does): one header per
  namespace with each interface in vtable order and its IID, delegates,
  enums (`Visibility_Collapsed`), structs, generic interface templates and
  the specializations of the instances each namespace uses, with the IIDs
  Windows computes for them (`__FIVector_1_HSTRING` and the rest), and
  `RuntimeClass_`/`InterfaceName_` strings.  For the 380 namespaces of
  Windows' metadata, the Windows App SDK's lifted `Microsoft.UI.*` types and
  the XAML framework's IDL, in about three seconds; every header compiles
  in one translation unit, 64- and 32-bit.
- **Inputs, vendored:** `Windows.winmd` and the `Microsoft.UI*` metadata from
  windows-rs (MIT) in `third_party/winmd`, and the XAML framework's IDL from
  microsoft-ui-xaml (MIT) in `third_party/microsoft-ui-xaml`, pinned.
  The IIDs MIDL 3 would invent for synthesized interfaces come from the
  published `Microsoft.UI.Xaml.winmd`; `abigen.py --check` (part of CI's
  checks) confirms all 2136 XAML types the IDL gives match that metadata in
  names, member order and signatures.
- **The build** generates them for programs and DLLs whose manifest says
  `"winrt_abi": true`, on the new `winrt_abi.h` (`IInspectable`,
  `IActivationFactory`, `AggregateType`).

New core self-test `winrtabitest` (64- and 32-bit) checks published IIDs
and drives the Compositor through the generated interfaces.  Next in
32.3: the Windows SDK, WRL and WIL pieces the `dxaml` sources include, then
their build as `Windows.UI.Xaml.dll`.
