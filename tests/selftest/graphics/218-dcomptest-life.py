# dcomptest life with DXVK installed and Venus the GPU, 64- and 32-bit: a
# composition swap chain on the GPU device and a DirectComposition virtual
# surface through window resizes; a visual, its target, the
# DirectComposition and Direct3D devices and the window torn down while
# another thread presents; and the GPU process's window composited on the
# GPU inside another process's window, resized, and its process killed
# while presenting (what WebView2's and Steam's browsers do)
TESTS = [
    Test('dcomptest life x64', 'dcomptest life', [r'dcomptest: \d+ passed, 0 failed \(64-bit\)'], timeout=400),
    Test('dcomptest life x86', r'C:\Programs\x86\dcomptest.exe life', [r'dcomptest: \d+ passed, 0 failed \(32-bit\)'], timeout=400),
]
