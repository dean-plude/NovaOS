# gltest's frame-rate test: the same OpenGL scene on virgl (the host's GPU
# through the virtio-gpu), its frames shown straight from the host and then
# through the GDI, and on llvmpipe (NovaOS's CPU); virgl must draw more
# frames per second, and direct presentation must beat the GDI way in frame
# time and in bytes moved through guest memory
TESTS = [
    Test('OpenGL frame rate x64', r'C:\Tests\gltest.exe fps 10', [r'gltest fps: 9 passed, 0 failed'], timeout=900,
         shot=r'fps: virgl for'),
]
