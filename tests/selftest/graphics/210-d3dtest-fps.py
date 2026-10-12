# d3dtest's frame-rate test: the same Direct3D 9 scene on Venus (the host's
# GPU through the virtio-gpu), its frames shown straight from the host and
# through the GDI, and on lavapipe (NovaOS's CPU); Venus must draw more
# frames per second than lavapipe, and direct presentation must beat the
# GDI way (move far fewer bytes through guest memory under KVM)
TESTS = [
    Test('frame rate x64', r'C:\Tests\d3dtest.exe fps 10', [r'd3dtest fps: 9 passed, 0 failed'], timeout=900,
         shot=r'fps: Venus for'),
]
