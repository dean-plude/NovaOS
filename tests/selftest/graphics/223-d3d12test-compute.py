# tools/d3d12test compute: a Direct3D 12 compute pipeline from a real
# game's shader (Godot 4.7's SDFGI direct-light shader, whose branches
# VKD3D-Proton 3.0.1's own translator turned into SPIR-V that lavapipe
# refused with VK_ERROR_UNKNOWN) builds through the App Store's
# VKD3D-Proton (tools/build_vkd3d_proton.py) on lavapipe, 64- and 32-bit
# (C:\Tests)
TESTS = [
    Test('d3d12test compute x64', r'C:\Tests\d3d12test.exe compute', [r'd3d12test: \d+ passed, 0 failed'], timeout=900),
    Test('d3d12test compute x86', r'C:\Tests\d3d12test32.exe compute', [r'd3d12test: \d+ passed, 0 failed'], timeout=900),
]
