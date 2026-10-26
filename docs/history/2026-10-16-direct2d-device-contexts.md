## Direct2D device contexts

First part of step 32.3 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](design/windows-ui-xaml.md)): the XAML framework draws its
text and shapes with Direct2D 1.1's device contexts, into
DirectComposition surfaces, so those come before its build.

- **`ID2D1Factory1`** (with `ID2D1Multithread`), **`ID2D1Device`** on a
  Direct3D 11 device (`ID2D1Factory1::CreateDevice`, `D2D1CreateDevice`)
  and **`ID2D1DeviceContext`**: every render-target call, plus
  `CreateBitmap` with `D2D1_BITMAP_PROPERTIES1`,
  `CreateBitmapFromDxgiSurface`, `SetTarget`/`GetTarget`, unit modes,
  `DrawImage` and `DrawBitmap` of bitmaps, 1.1's gradient stop
  collections, bitmap brushes, stroke styles, path geometries and drawing
  state blocks; `ID2D1Bitmap1` (`GetOptions`, `GetSurface`, `Map` for a
  `CPU_READ` bitmap); `D2D1CreateDeviceContext`.  Drawing stays in
  software: a bitmap on a DXGI surface keeps its pixels in memory, read
  from the texture when a context starts drawing into it and written back
  at `Flush` and `EndDraw`, so it works on the software Direct3D device
  and on DXVK's.  Not yet: effects, command lists, image brushes, colour
  contexts, `ID2D1DeviceContext1` and later, perspective in `DrawBitmap`,
  primitive blends other than source-over.
- **DirectComposition and Windows.UI.Composition on an `ID2D1Device`.**
  Given one as the rendering device (`DCompositionCreateDevice2`, the new
  `IDCompositionSurfaceFactory`, `ICompositorInterop::CreateGraphicsDevice`),
  surfaces are made on its Direct3D device and `BeginDraw` asked for an
  `ID2D1DeviceContext` hands out one drawing into the surface, as Windows
  does.  The software Direct3D device's textures now answer
  `IDXGISurface`.

New core self-test `d2dctxtest` (64-bit); `comptest` draws a drawing
surface of a graphics device made on an `ID2D1Device`.  Next in 32.3:
building the `dxaml` core as `Windows.UI.Xaml.dll`.
