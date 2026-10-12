# WordCraft, ArtCraft's open-source word processor (Rust, egui on wgpu),
# installed from the App Store as PhotoCraft is (886-photocraft.py).  It
# draws its window with the built-in OpenGL (llvmpipe) and opens a blank
# Document1 under its ribbon.  Its window is wider than the 1280x800 screen
# and starts beside the Terminal, whose text shows through differently from
# run to run, so only its running decides (the screenshot is kept in the
# output).  Windowed.
DOC = 'WordCraft'
EXE = r'C:\Programs\WordCraft\wordcraft-0.5.0-windows-x64-portable\wordcraft.exe'

APP = App('WordCraft', '0.5.0',
          'https://github.com/storytold/wordcraft/releases/download/v0.5.0/wordcraft-0.5.0-windows-x64-portable.zip',
          'WordCraft', [Test('install from the App Store', 'store install WordCraft', store='WordCraft', timeout=900),
                        Test('main window', rf'start "{EXE}"', timeout=400)],
          gui=True, store='WordCraft', reference=False)
