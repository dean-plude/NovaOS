# PdfCraft, ArtCraft's open-source PDF workbench (Rust, egui on wgpu),
# installed from the App Store as PhotoCraft is (886-photocraft.py).  It
# draws its window with the built-in OpenGL (llvmpipe) and opens on its
# welcome page and tool list.  Its window is wider than the 1280x800 screen
# and starts beside the Terminal, whose text shows through differently from
# run to run, so only its running decides (the screenshot is kept in the
# output).  Windowed.
DOC = 'PdfCraft'
EXE = r'C:\Programs\PdfCraft\pdfcraft-0.6.0-windows-x64-portable\pdfcraft.exe'

APP = App('PdfCraft', '0.6.0',
          'https://github.com/storytold/pdfcraft/releases/download/v0.6.0/pdfcraft-0.6.0-windows-x64-portable.zip',
          'PdfCraft', [Test('install from the App Store', 'store install PdfCraft', store='PdfCraft', timeout=900),
                       Test('main window', rf'start "{EXE}"', timeout=400)],
          gui=True, store='PdfCraft', reference=False)
