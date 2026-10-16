# d2dctxtest: Direct2D 1.1's devices and device contexts (d2d1.dll) on a WARP Direct3D 11 device: an
# ID2D1Factory1 with ID2D1Multithread, ID2D1Device (CreateDevice, D2D1CreateDevice), device contexts drawing
# into their own bitmaps (read through Map) and into textures (CreateBitmapFromDxgiSurface,
# D2D1CreateDeviceContext), D2D1_UNIT_MODE_PIXELS, and DirectComposition surfaces on an ID2D1Device whose
# BeginDraw hands out a device context, checked in a window; 64-bit (the software device is)
DOC = ('`d2dctxtest` (Direct2D device contexts: ID2D1Device on Direct3D 11, bitmaps on DXGI surfaces, '
       'DirectComposition surfaces drawn with Direct2D; 64-bit)')
TESTS = [
    Test('d2dctxtest', 'd2dctxtest', [r'd2dctxtest: \d+ passed, 0 failed'], timeout=180),
]
