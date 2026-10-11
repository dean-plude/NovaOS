## Composition swap chains on the GPU

The first step of WebView2's GPU-composited output on Venus.  A program
that renders with a GPU device (DXVK's, on Venus or lavapipe) and shows
its frames through DirectComposition asks the factory for a swap chain
made for composition on that device, and DXVK does not make those
(`CreateSwapChainForComposition` returns `E_NOTIMPL`).  Now `dxgi.dll`
makes them when DXVK refuses: the swap chain's buffer is a texture of the
GPU device, which the program renders into on the GPU; each present copies
the part that changed into a staging texture on the GPU and reads it back,
and `dcomp.dll`'s visual draws that frame into its window as it does the
software device's.  Nothing in DXVK is changed.

- **dxgi.dll**: swap chains for composition on any Direct3D 11 device,
  from DXVK's factory (wrapped) and from NovaOS's own; dirty rectangles
  read back only the part that changed.
- **dcomptest dxvk** (graphics suite, 64- and 32-bit): a hardware device,
  its factory's composition swap chain, whose buffer is a texture of that
  device; frames cleared and copied on the GPU, presented whole and with a
  dirty rectangle, are in the window.
- Chromium still uses its software output: its DirectComposition path
  needs `DCompositionCreateDevice3` and surfaces, and the swap chains from
  the factory of its device's adapter; those are the next step.
