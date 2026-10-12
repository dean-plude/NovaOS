# VectorCraft, ArtCraft's open-source vector illustration program (Rust,
# egui on wgpu), installed from the App Store as PhotoCraft is
# (886-photocraft.py).  It draws its window with the built-in OpenGL
# (llvmpipe) and opens full screen on its welcome page (New file, Open and
# the page sizes to start from), which must match
# tests/reference/vectorcraft.png.  Its own title bar sits under the
# desktop's.  Windowed.
DOC = 'VectorCraft'
EXE = r'C:\Programs\VectorCraft\vectorcraft-0.9.0-windows-x64-portable\vectorcraft.exe'

APP = App('VectorCraft', '0.9.0',
          'https://github.com/storytold/vectorcraft/releases/download/v0.9.0/vectorcraft-0.9.0-windows-x64-portable.zip',
          'VectorCraft', [Test('install from the App Store', 'store install VectorCraft', store='VectorCraft', timeout=900),
                          Test('main window', rf'start "{EXE}"', timeout=400)],
          gui=True, store='VectorCraft')
