## WebView2 composited on the GPU

The second step of WebView2's GPU-composited output on Venus, and the one
that turns it on: with DXVK installed, WebView2's GPU process now takes
Chromium's hardware DirectComposition path, rendering the page with ANGLE
on the GPU into a composition swap chain, instead of its software output
on the WARP device.

- **dcomp.dll**: `DCompositionCreateDevice2` and `DCompositionCreateDevice3`,
  with `IDCompositionDesktopDevice` and `IDCompositionDevice3`;
  `IDCompositionVisual2` and `3` (offsets, a transform's translation,
  rectangle clips as a rectangle or an `IDCompositionRectangleClip`,
  visibility, opacity from the visual or an effect group, children in
  order); surfaces (`IDCompositionSurface`, also virtual): `BeginDraw`
  hands the program a texture of the rendering device at the update
  rectangle's offset and `EndDraw` reads that rectangle back; `Commit`
  draws what changed into the window.  Transform objects, other effects
  and animations are not made.
- **dxgi.dll**: DXVK's adapters are wrapped like its factory, so a device's
  adapter's `GetParent` is the factory that makes composition swap chains
  on the GPU (Chromium takes its swap chains from there); swap chains
  answer `IDXGISwapChain2`, `3` and `4` (Chromium sets their color space
  and stopped its GPU process when they did not).
- **dcomptest** (core and `dcomptest dxvk`): surfaces drawn whole and in
  part, a clip, and the adapter's factory.
- WebView2 on Venus (local run): one GPU process, `DCompositionCreateDevice3`,
  a composition swap chain on ANGLE's device, the page drawn in the host's
  window.  Frames still go back through the CPU and GDI; presenting
  directly on the virtio GPU is the next step.
