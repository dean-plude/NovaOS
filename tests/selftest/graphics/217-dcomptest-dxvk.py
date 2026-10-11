# dcomptest with DXVK installed and Venus the GPU: programs' DXGI factory and
# hardware devices are DXVK's, ANGLE's WARP renderer (feature level 9_3 and
# up) is DXVK's too, while a WARP device at feature level 9_1 stays NovaOS's
# software device on the Basic Render Driver, whose factory makes the swap
# chains for DirectComposition that DXVK's does not (Chromium's software
# output in WebView2's GPU process); and a composition swap chain on the GPU
# device itself, rendered on the GPU and shown by a DirectComposition
# visual (dxgi.dll's, read back at each present).  The software device is
# 64-bit; the 32-bit run checks the factory, the GPU swap chain and the GPU
# process's window parented by another process.
TESTS = [
    Test('dcomptest dxvk x64', 'dcomptest dxvk', [r'dcomptest: \d+ passed, 0 failed \(64-bit\)'], timeout=300),
    Test('dcomptest dxvk x86', r'C:\Programs\x86\dcomptest.exe dxvk', [r'dcomptest: \d+ passed, 0 failed \(32-bit\)'], timeout=300),
]
