# gltest's direct-presentation test on Mesa's virgl: four coloured quadrants
# shown straight from the host's GPU while the window is alone on screen
# (the screen itself must show them there, the right way up), through the
# GDI while another window covers part of it, and from the host again once
# it has gone; 64- and 32-bit
import os
import re
import time

COLOURS = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255))   # top left, top right, bottom left, bottom right


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


def look(bits):
    """The screen where the program says its quadrants are (it keeps presenting them for 6 s)"""
    def act(nova):
        nova.present_seen = getattr(nova, 'present_seen', {})
        time.sleep(2)
        try:
            log = open(nova.serial_path, 'rb').read().decode('latin-1')
            x, y, w, h = map(int, re.findall(r'present: quadrants at (-?\d+) (-?\d+) (\d+) (\d+)', log)[-1])
            nova.shot(f'{nova.work}/present-{bits}.png')
            ppm = os.path.abspath(f'{nova.work}/present-{bits}.ppm')    # read without Pillow
            r = nova.qmp.cmd('screendump', filename=ppm)
            if 'error' in r:
                raise RuntimeError(f"screendump: {r['error'].get('desc', r['error'])}")
            data = open(ppm, 'rb').read()
            got = [ppm_pixel(data, x + w // 4 + (i & 1) * w // 2, y + h // 4 + (i >> 1) * h // 2) for i in range(4)]
        except (OSError, IndexError, RuntimeError) as e:
            nova.present_seen[bits] = f'no screenshot of the quadrants: {e}'
            return
        bad = [f'{g} instead of {c}' for g, c in zip(got, COLOURS) if max(abs(a - b) for a, b in zip(g, c)) > 24]
        nova.present_seen[bits] = 'the screen showed ' + ', '.join(bad) if bad else None
    return act


def seen(bits):
    return lambda nova: getattr(nova, 'present_seen', {}).get(bits, 'no screenshot of the quadrants')


TESTS = [
    Test('gltest present x64', r'C:\Tests\gltest.exe present', [r'gltest present: \d+ passed, 0 failed'],
         timeout=600, acts=[(r'present: quadrants at', look('x64'))], check=seen('x64')),
    Test('gltest present x86', r'C:\Tests\gltest32.exe present', [r'gltest present: \d+ passed, 0 failed'],
         timeout=600, acts=[(r'present: quadrants at', look('x86'))], check=seen('x86')),
]
