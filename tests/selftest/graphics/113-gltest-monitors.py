# gltest's direct-presentation test on the other monitor and at scale 2:
# the four coloured quadrants shown straight from the host's GPU on the
# second monitor (the graphics boot's secondary-vga), again once it is in
# 2560x1600 (scale 2: each of the window's pixels twice as wide and high on
# that screen), and on the primary next to it (now half the desktop's
# scale); each screen must show them where the program says (in its own
# pixels).  64-bit
import os
import re
import time

COLOURS = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255))   # top left, top right, bottom left, bottom right
PLACES = (('display 2', 'head2'), ('display 2 at scale 2', 'head2'), ('display 1 at scale 2', None))


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


def look(label, device):
    """The screen @device (None: the primary) where the program says its
    quadrants are (it keeps presenting them for 6 s)"""
    def act(nova):
        nova.monitors_seen = getattr(nova, 'monitors_seen', {})
        time.sleep(2)
        name = re.sub(r'\W+', '-', label)
        try:
            log = open(nova.serial_path, 'rb').read().decode('latin-1')
            x, y, w, h = map(int, re.findall(rf'present: {label} quadrants at (-?\d+) (-?\d+) (\d+) (\d+)', log)[-1])
            ppm = os.path.abspath(f'{nova.work}/monitors-{name}.ppm')       # read without Pillow
            r = nova.qmp.cmd('screendump', filename=ppm, **({'device': device} if device else {}))
            if 'error' in r:
                raise RuntimeError(f"screendump: {r['error'].get('desc', r['error'])}")
            data = open(ppm, 'rb').read()
            got = [ppm_pixel(data, x + w // 4 + (i & 1) * w // 2, y + h // 4 + (i >> 1) * h // 2) for i in range(4)]
        except (OSError, IndexError, RuntimeError) as e:
            nova.monitors_seen[label] = f'no screenshot of the quadrants on {label}: {e}'
            return
        bad = [f'{g} instead of {c}' for g, c in zip(got, COLOURS) if max(abs(a - b) for a, b in zip(g, c)) > 24]
        nova.monitors_seen[label] = f'{label} showed ' + ', '.join(bad) if bad else None
    return act


def seen(nova):
    got = getattr(nova, 'monitors_seen', {})
    bad = [got.get(label, f'no screenshot of the quadrants on {label}') for label, _ in PLACES]
    return '; '.join(b for b in bad if b) or None


TESTS = [
    Test('gltest monitors', r'C:\Tests\gltest.exe monitors', [r'gltest monitors: \d+ passed, 0 failed'],
         timeout=600, acts=[(rf'present: {label} quadrants at', look(label, dev)) for label, dev in PLACES], check=seen),
]
