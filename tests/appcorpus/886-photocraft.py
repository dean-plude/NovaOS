# PhotoCraft, ArtCraft's open-source image editor (Rust, egui on wgpu),
# installed the way a user gets it: the App Store's "Install" button unpacks
# the portable zip its catalog lists (GitHub release asset, put in
# C:\Downloads) with 7-Zip into C:\Programs\PhotoCraft, and the editor then
# starts to its main window.  Without a hardware graphics adapter (QEMU's
# VGA: the only one is llvmpipe, which wgpu reports as a CPU) PhotoCraft
# composites images on the CPU by design and says so in a "Switched to CPU
# rendering" box, as on Windows with only the Basic Render Driver; the test
# keeps the CPU, makes a 1920x1080 document (New document..., Create) and
# the window with the white canvas must match tests/reference/photocraft.png.
# Its brush cursor over the canvas (a custom cursor) needs SetBitmapBits.
# PhotoCraft is the first of the twelve ArtCraft apps in the Store; the
# others share its toolkit and graphics stack.  Its portable.txt keeps its
# settings in PhotoCraftData beside the executable.  Windowed.
DOC = 'PhotoCraft'
EXE = r'C:\Programs\PhotoCraft\photocraft-0.7.0-windows-x64-portable\photocraft.exe'

import time


def new_document(nova, echo):
    nova.click(511, 461)              # the CPU box's "Keep Using CPU"
    time.sleep(20)
    nova.click(357, 351)              # "New document..." on the start page
    time.sleep(60)
    nova.keys('ret')                  # the dialog's Create (default size)
    time.sleep(120)
    return None


APP = App('PhotoCraft', '0.7.0',
          'https://github.com/storytold/photocraft/releases/download/v0.7.0/photocraft-0.7.0-windows-x64-portable.zip',
          'PhotoCraft', [Test('install from the App Store', 'store install PhotoCraft', store='PhotoCraft', timeout=900),
                         Test('main window', rf'start "{EXE}"', timeout=360)],
          gui=True, interact=new_document, store='PhotoCraft')
