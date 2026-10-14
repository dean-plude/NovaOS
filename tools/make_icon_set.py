#!/usr/bin/env python3
"""Render the brand guide's app and file icons as NovaOS's "Beam" icon set.

    tools/make_icon_set.py [--chrome PATH]   (rewrites ICON_SET/*.ico)

Every SVG in docs/design/brand/icons/apps and icons/files becomes one
Windows .ico file named after it (terminal.ico, folder-documents.ico, ...)
in userland/files/Windows/Web/Icons/Beam, which the image ships beside the
kernel and installs in C:\\Windows\\Web\\Icons\\Beam.  Each icon holds PNG
images at SIZES, each size rendered by headless Chromium from the SVG
itself (not scaled down from a large one), so small sizes stay crisp.

Needs Chromium (or Chrome's headless_shell) and Pillow; the files are
checked in, so building NovaOS needs neither.  docs/design/brand/README.md
lists the names Settings > Personalization looks for.
"""
import argparse, glob, io, os, shutil, struct, subprocess, sys, tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BRAND = os.path.join(ROOT, 'docs', 'design', 'brand', 'icons')
ICON_SET = os.path.join(ROOT, 'userland', 'files', 'Windows', 'Web', 'Icons', 'Beam')
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
GAP = 8                                     # px between the sizes on the render page

def find_chrome(given):
    if given:
        return given
    for pattern in ('/opt/pw-browsers/chromium_headless_shell-*/chrome-linux/headless_shell',
                    '/opt/pw-browsers/chromium-*/chrome-linux/chrome'):
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[-1]
    for name in ('chromium', 'chromium-browser', 'google-chrome', 'headless_shell'):
        if shutil.which(name):
            return shutil.which(name)
    sys.exit('Chromium not found: pass --chrome PATH')

def render(chrome, svg, work):
    """The icon at every size: {size: RGBA image}"""
    width = sum(SIZES) + GAP * len(SIZES)
    imgs, x = [], 0
    for s in SIZES:
        imgs.append(f'<img src="file://{svg}" width="{s}" height="{s}" '
                    f'style="position:absolute;left:{x}px;top:0">')
        x += s + GAP
    page = os.path.join(work, 'page.html')
    with open(page, 'w') as f:
        f.write('<html><body style="margin:0;background:transparent">' + ''.join(imgs) + '</body></html>')
    shot = os.path.join(work, 'shot.png')
    subprocess.run([chrome, '--headless', '--no-sandbox', '--hide-scrollbars',
                    '--force-device-scale-factor=1', '--default-background-color=00000000',
                    f'--window-size={width},{SIZES[-1]}', f'--screenshot={shot}', 'file://' + page],
                   check=True, capture_output=True)
    sheet = Image.open(shot).convert('RGBA')
    out, x = {}, 0
    for s in SIZES:
        out[s] = sheet.crop((x, 0, x + s, s))
        x += s + GAP
    return out

def ico(images):
    """A .ico file with one PNG image per size"""
    pngs = []
    for s in SIZES:
        b = io.BytesIO()
        images[s].save(b, 'PNG', optimize=True)
        pngs.append((s, b.getvalue()))
    head = struct.pack('<HHH', 0, 1, len(pngs))
    off = 6 + 16 * len(pngs)
    dirs, data = b'', b''
    for s, png in pngs:
        dirs += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(png), off + len(data))
        data += png
    return head + dirs + data

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--chrome')
    a = ap.parse_args()
    chrome = find_chrome(a.chrome)
    os.makedirs(ICON_SET, exist_ok=True)
    svgs = sorted(glob.glob(os.path.join(BRAND, 'apps', '*.svg')) + glob.glob(os.path.join(BRAND, 'files', '*.svg')))
    with tempfile.TemporaryDirectory() as work:
        for svg in svgs:
            name = os.path.splitext(os.path.basename(svg))[0] + '.ico'
            path = os.path.join(ICON_SET, name)
            with open(path, 'wb') as f:
                f.write(ico(render(chrome, svg, work)))
            print(f'{os.path.relpath(path, ROOT)}: {os.path.getsize(path)} bytes')

if __name__ == '__main__':
    main()
