# dcomptest's GPU composition on Venus (DXVK's Direct3D 11 through Mesa's
# Win32 swapchain): a DirectComposition swap chain on the GPU device,
# composited on the GPU into the window's own swap chain, shown straight
# from the host while the window is alone (the screen itself must show the
# four coloured quadrants there, the right way up), through the GDI while
# another window covers it, the GDI way again once a swap chain of another
# device joins the tree (the screen must then show the yellow frame drawn
# that way, not the host's last one), and the GPU process's window,
# embedded in the browser's, shown straight from the host; 64- and 32-bit
import os
import re
import time

QUADRANTS = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255))   # top left, top right, bottom left, bottom right
YELLOW = ((255, 255, 0),) * 4


def ppm_pixel(data, x, y):
    """Pixel (x, y) of a binary PPM (P6, 8-bit) as an (r, g, b) tuple"""
    m = re.match(rb'P6\s+(?:#[^\n]*\s+)*(\d+)\s+(\d+)\s+255\s', data)
    if not m:
        raise OSError('not an 8-bit P6 picture')
    width, height = int(m[1]), int(m[2])
    if not (0 <= x < width and 0 <= y < height):
        raise IndexError(f'({x}, {y}) is off the {width}x{height} screen')
    o = m.end() + (y * width + x) * 3
    return tuple(data[o:o + 3])


def look(key, what, colours):
    """The screen where the program says its frame is (it keeps presenting it for 6 s)"""
    def act(nova):
        nova.dcomp_seen = getattr(nova, 'dcomp_seen', {})
        time.sleep(2)
        try:
            log = open(nova.serial_path, 'rb').read().decode('latin-1')
            x, y, w, h = map(int, re.findall(rf'dcomp: {what} at (-?\d+) (-?\d+) (\d+) (\d+)', log)[-1])
            nova.shot(f'{nova.work}/{key}.png')
            ppm = os.path.abspath(f'{nova.work}/{key}.ppm')    # read without Pillow
            r = nova.qmp.cmd('screendump', filename=ppm)
            if 'error' in r:
                raise RuntimeError(f"screendump: {r['error'].get('desc', r['error'])}")
            data = open(ppm, 'rb').read()
            got = [ppm_pixel(data, x + w // 4 + (i & 1) * w // 2, y + h // 4 + (i >> 1) * h // 2) for i in range(4)]
        except (OSError, IndexError, RuntimeError) as e:
            nova.dcomp_seen[key] = f'no screenshot of the {what}: {e}'
            return
        bad = [f'{g} instead of {c}' for g, c in zip(got, colours) if max(abs(a - b) for a, b in zip(g, c)) > 24]
        nova.dcomp_seen[key] = 'the screen showed ' + ', '.join(bad) + f' for the {what}' if bad else None
    return act


def seen(bits):
    def check(nova):
        s = getattr(nova, 'dcomp_seen', {})
        for key, what in ((f'dcomp-{bits}-quadrants', 'quadrants'), (f'dcomp-{bits}-yellow', 'yellow frame')):
            if s.get(key, f'no screenshot of the {what}'):
                return s.get(key, f'no screenshot of the {what}')
        return None
    return check


def test(bits, cmd):
    return Test(f'dcomptest direct {bits}', cmd, [r'dcomptest: \d+ passed, 0 failed'], timeout=600,
                acts=[(r'dcomp: quadrants at', look(f'dcomp-{bits}-quadrants', 'quadrants', QUADRANTS)),
                      (r'dcomp: yellow at', look(f'dcomp-{bits}-yellow', 'yellow', YELLOW))],
                check=seen(bits))


TESTS = [
    test('x64', 'dcomptest direct'),
    test('x86', r'C:\Programs\x86\dcomptest.exe direct'),
]
