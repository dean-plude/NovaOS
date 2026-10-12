# PhotoCraft, ArtCraft's open-source image editor (Rust, egui on wgpu),
# installed the way a user gets it: the App Store's "Install" button unpacks
# the portable zip its catalog lists (GitHub release asset, put in
# C:\Downloads) with 7-Zip into C:\Programs\PhotoCraft, and the editor then
# starts to its main window, which must match tests/reference/photocraft.png.
# PhotoCraft is the first of the twelve ArtCraft apps in the Store; the
# others share its toolkit and graphics stack.  Its portable.txt keeps its
# settings in PhotoCraftData beside the executable.  Windowed.
DOC = 'PhotoCraft'
EXE = r'C:\Programs\PhotoCraft\photocraft-0.7.0-windows-x64-portable\photocraft.exe'

APP = App('PhotoCraft', '0.7.0',
          'https://github.com/storytold/photocraft/releases/download/v0.7.0/photocraft-0.7.0-windows-x64-portable.zip',
          'PhotoCraft', [Test('install from the App Store', 'store install PhotoCraft', store='PhotoCraft', timeout=900),
                         Test('main window', rf'start "{EXE}"', timeout=360)],
          gui=True, store='PhotoCraft')
