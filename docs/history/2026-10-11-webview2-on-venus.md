## WebView2 on Venus

With the App Store's DXVK installed (Direct3D on Venus, the 3D virtio-gpu,
or on Mesa's lavapipe), WebView2 loaded its page and ran its script, but
its GPU process exited six times in a row and the browser process then
stopped at a breakpoint.  Each GPU process created its Direct3D 11 device
on Venus for ANGLE (feature level 11.1) and then, for the frames it
presents, a WARP device and a swap chain for DirectComposition from
`CreateDXGIFactory2`'s factory.  With DXVK installed, WARP went to DXVK and
so did the factory, and DXVK does not implement
`CreateSwapChainForComposition` (`E_NOTIMPL`), so Chromium restarted the
GPU process until it gave up.  Now the GPU process starts once, keeps its
device on Venus, and the page is drawn.  Nothing in the runtime or in DXVK
is changed.

- **d3d11.dll**: a WARP device that accepts feature level 9_1 is NovaOS's
  software device on the Basic Render Driver whether DXVK is installed or
  not, as WARP on Windows is the CPU's rasterizer whatever the GPU.  WARP
  at 9_3 and up (ANGLE's D3D11-WARP renderer) and every WARP request in a
  32-bit process still go to DXVK.
- **dxgi.dll**: the factory programs get from DXVK is wrapped: a swap chain
  for NovaOS's software device (for a window, or for DirectComposition)
  comes from NovaOS's own factory, everything else from DXVK's.  The
  software device's adapter stays the Basic Render Driver
  (`NovaCreateBasicRenderFactory`, private to `d3d11.dll`).

The self-test `dcomptest dxvk` (graphics suite) runs `dcomptest`'s checks
with DXVK on Venus: DXVK's factory and devices for the GPU, the software
device for WARP, and its composition and window swap chains made through
DXVK's factory.  The GPU-composited path, DirectComposition on the GPU
(`DCompositionCreateDevice3`, which NovaOS's `dcomp.dll` does not export),
is the next step.
