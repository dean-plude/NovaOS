# Krita 5.3 (Qt 5, built with LLVM's MinGW toolchain: libc++ and libunwind)
# installed the way a user gets it: the App Store's download (the portable
# zip from download.kde.org, put in C:\Downloads) unpacked by the Store
# into C:\Programs\Krita.  Ctrl+N, typed as soon as its window is up with
# no click first (the keyboard must already be Krita's), opens the
# new-document dialog, whose Create gives an empty A4 image on the OpenGL
# canvas.  A stroke dragged across the canvas with the mouse (the default
# freehand brush) must change its pixels; Ctrl+S then saves the drawing
# through the Save dialog as C:\Apps\data\drawing.kra, Krita's own format
# (a zip), so the image closes with no "Save?".  7-Zip then reads the
# saved file back: it must hold Krita's document, its layer and the merged
# image.  Krita needs OpenGL 3 for its canvas: Mesa's llvmpipe is built
# in, so nothing else is installed first.  Windowed; it loads about 90
# DLLs, its resources and Python plugins before the window.  The screen
# shows the drawing, so only the stroke and the saved file decide (the
# screenshot is kept in the output).
import os, tempfile, time

DOC = 'Krita (draws a stroke and saves the image)'
KRITA = r'C:\Programs\Krita\krita-x64-5.3.4\bin\krita.exe'
SEVENZIP = r'C:\Programs\7-Zip\7z.exe'
CANVAS = (400, 380, 680, 600)       # inside the empty image (logical screen points)


def dark_pixels(nova, path):
    """How many pixels of CANVAS are not near-white"""
    from PIL import Image
    nova.shot(path)
    time.sleep(1)
    im = Image.open(path).convert('L')
    k = im.width // 1280                # screenshots are at the display's scale
    box = im.crop(tuple(v * k for v in CANVAS))
    return sum(1 for v in box.getdata() if v < 200)


def draw_and_save(nova, echo):
    """Ctrl+N with no click first, Create in the dialog, a stroke, Ctrl+S"""
    nova.keys('ctrl-n')
    time.sleep(60)
    nova.keys('ret')
    time.sleep(90)
    work = tempfile.mkdtemp(prefix='krita')
    before = dark_pixels(nova, os.path.join(work, 'before.png'))
    nova.drag(430, 420, 650, 560)
    time.sleep(30)
    after = dark_pixels(nova, os.path.join(work, 'after.png'))
    print(f'  canvas: {before} dark pixels before the stroke, {after} after', flush=True)
    if after < before + 200:
        return f'the stroke did not draw on the canvas ({before} dark pixels before, {after} after)'
    nova.keys('ctrl-s')                 # a new image: the Save dialog
    time.sleep(60)
    nova.keys('ctrl-a')                 # the file name box has the focus
    time.sleep(2)
    nova.qmp.type(A + r'\data\drawing.kra')
    time.sleep(3)
    nova.keys('ret')
    time.sleep(60)                      # the file is written
    return None


APP = App('Krita', '5.3.4', 'https://download.kde.org/stable/krita/5.3.4/krita-x64-5.3.4.zip',
          'Krita', [Test('install from the App Store', 'store install Krita', store='Krita', timeout=900),
                    Test('draw and save', rf'start {KRITA} --nosplash', timeout=300),
                    Test('saved .kra', rf'{SEVENZIP} l {A}\data\drawing.kra',
                         [r'maindoc\.xml', r'mergedimage\.png', r'layers'], timeout=120)],
          store='Krita', gui=True, interact=draw_and_save, reference=False)
