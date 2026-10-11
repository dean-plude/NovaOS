# tools/d3d12test: Direct3D 12 through NovaOS's d3d12.dll and VKD3D-Proton
# on Mesa's lavapipe: a triangle drawn and read back, then presented in a
# window through a flip-model swap chain on the command queue (DXVK's
# factory), 64- and 32-bit (C:\Tests)
TESTS = [
    Test('d3d12test x64', r'C:\Tests\d3d12test.exe 3', [r'd3d12test: \d+ passed, 0 failed'], timeout=900,
         shot=r'D3D12 window centre'),
    Test('d3d12test x86', r'C:\Tests\d3d12test32.exe 3', [r'd3d12test: \d+ passed, 0 failed'], timeout=900,
         shot=r'D3D12 window centre'),
]
