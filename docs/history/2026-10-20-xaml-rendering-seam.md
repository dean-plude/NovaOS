## XAML's rendering seam

Thirteenth part of step 32.3 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](design/windows-ui-xaml.md)): the render target the XAML
control makes now builds from Microsoft's sources, and
`CJupiterControl::Create` succeeds.

- **Built:** the graphics device manager (`WindowsGraphicsDeviceManager`,
  `DCompTreeHost`, the Direct3D 11 and Direct2D device wrappers), the
  compositor and UI thread schedulers, and the window render target
  (`CWindowRenderTarget`, `HWWalk`, `CompositorTreeHost`), with the error
  service, the downloader, the value store and `WUCBrushManager`.
- **Direct3D 11:** the software device answers `IDXGIDevice2`/`3`,
  `ID3D10Multithread` and `ID3D11Device4`, which XAML's graphics device
  asks for.
- **ole32:** `RoFailFastWithErrorContextInternal2`, the fail fast XAML's
  error paths call, which ends the process with the error on the
  debugger's output.
- `xamlloadtest` checks that `CJupiterControl::Create` makes the control
  and its render target.
