## A drawn triangle in the ANGLE-style Direct3D 11 test

`d3dtest angle`, which sets up Direct3D 11 the way ANGLE does for
Chromium's GPU process, now also draws on its swap chain: two HLSL shaders
compiled with NovaOS's `d3dcompiler_47.dll` draw a red triangle, and the
centre pixel is read back through a staging texture.  On DXVK over Venus,
64- and 32-bit, the graphics suite expects the Venus adapter and a red
pixel, so a Direct3D 11 draw that reaches the GPU is checked, not only the
device set-up.  ANGLE's own libraries are not loaded by the test; drawing
WebView2 pages with ANGLE on DXVK stays on the roadmap.
