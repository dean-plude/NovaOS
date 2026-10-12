# EffectCraft, ArtCraft's open-source motion graphics program (Rust, egui on
# wgpu), installed from the App Store as PhotoCraft is (886-photocraft.py).
# It draws its window with the built-in OpenGL (llvmpipe) and opens full
# screen on its welcome page with an untitled project, which must match
# tests/reference/effectcraft.png.  Windowed.
DOC = 'EffectCraft'
EXE = r'C:\Programs\EffectCraft\effectcraft-0.8.0-windows-x64-portable\effectcraft.exe'

APP = App('EffectCraft', '0.8.0',
          'https://github.com/storytold/effectcraft/releases/download/v0.8.0/effectcraft-0.8.0-windows-x64-portable.zip',
          'EffectCraft', [Test('install from the App Store', 'store install EffectCraft', store='EffectCraft', timeout=900),
                          Test('main window', rf'start "{EXE}"', timeout=400)],
          gui=True, store='EffectCraft')
