# comptest: Windows.UI.Composition (dcomp.dll): the Compositor from RoActivateInstance, a desktop window
# target, container and sprite visuals with colour brushes (offsets, opacity, anchor point, rotation, inset
# clip, scale, visibility, InsertBelow, Remove, RemoveAll) checked in the window's pixels, and in 64 bits a
# drawing surface on a WARP Direct3D 11 device shown through a surface brush, and one on an ID2D1Device
# drawn through BeginDraw's ID2D1DeviceContext; then the later interface versions in both namespaces
# (Windows' and Microsoft.UI.Composition's IIDs), property sets, a rectangle clip, key frame animations in a
# scoped batch, an animation group and easing functions
DOC = ('`comptest` (Windows.UI.Composition: visuals, colour and surface brushes, a desktop window target, '
       'drawing surfaces on Direct3D 11 and Direct2D, later interface versions, property sets, animations; '
       '64- and 32-bit)')
TESTS = [
    Test('comptest', 'comptest', [r'comptest: \d+ passed, 0 failed \(64-bit\)'], timeout=180),
    Test('comptest x86', r'C:\Programs\x86\comptest.exe', [r'comptest: \d+ passed, 0 failed \(32-bit\)'], timeout=180),
]
