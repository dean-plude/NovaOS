- **2D drawing**: Direct2D (`d2d1.dll`) draws in software: geometries
  (rectangles, ellipses, paths with Béziers and arcs, groups, transforms,
  combining, widening, tessellation), strokes with caps, joins and dashes,
  solid, gradient and bitmap brushes, layers and clips, on HWND, DC and
  bitmap render targets, with text through DirectWrite (`tools/d2dtest`);
  Direct2D 1.1's devices on a Direct3D 11 device and their device
  contexts, drawing into bitmaps and DXGI surfaces (DirectComposition's
  and Windows.UI.Composition's among them) (`d2dctxtest`).
