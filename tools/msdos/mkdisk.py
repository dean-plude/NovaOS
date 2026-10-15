#!/usr/bin/env python3
"""mkdisk.py — a bootable MS-DOS 4.0 floppy image from build_msdos.py's files

    mkdisk.py BUILT_DIR IMAGE [--files NAME...] [--add NAME=PATH ...]

Makes a 1.44 MB floppy the way FORMAT A: /S and SYS A: would: mtools'
mformat with the built boot sector (MSBOOT.BIN), IO.SYS and MSDOS.SYS as
the first two files of the root directory (hidden, system, read-only),
then COMMAND.COM and the rest.  Without --files the disk gets every file
the build made (all of MS-DOS 4.0 fits on one 1.44 MB disk); --add puts a
file of the caller's on it (an AUTOEXEC.BAT).  boot_test.py boots such a
disk in QEMU; build_userland.py ships one as NovaOS's DOS (ntvdm.exe boots
it, docs/dos.md).

Needs mtools.
"""
import argparse, os, subprocess, sys

SKIP = {'BUILD.LOG', 'MSBOOT.BIN'}
FIRST = ['IO.SYS', 'MSDOS.SYS', 'COMMAND.COM']


def boot_sector(built):
    """the boot sector: EXE2BIN writes it (ORG 7C00h) at offset 7C00h of
    MSBOOT.BIN, after zeros, so it is the last 512 bytes"""
    with open(os.path.join(built, 'MSBOOT.BIN'), 'rb') as f:
        sector = f.read()[-512:]
    if sector[-2:] != b'\x55\xaa':
        sys.exit('mkdisk: MSBOOT.BIN does not end in a boot sector')
    return sector


def make(built, img, names=None, add=()):
    """@img: a bootable floppy of @names (default: all the built files)
    and @add, (name on the disk, local path) pairs"""
    if names is None:
        names = sorted(n for n in os.listdir(built) if n.upper() not in SKIP and n.upper() == n)
    names = FIRST + [n for n in names if n not in FIRST]
    if os.path.exists(img):
        os.unlink(img)
    env = dict(os.environ, MTOOLS_NO_VFAT='1', MTOOLS_SKIP_CHECK='1')
    def mtool(*args):
        subprocess.run(list(args), check=True, env=env)
    boot = img + '.bootsect'
    with open(boot, 'wb') as f:
        f.write(boot_sector(built))
    mtool('mformat', '-C', '-f', '1440', '-N', '4E4F5641', '-B', boot, '-i', img, '::')   # (a fixed serial number, "NOVA": the same disk every build)
    os.unlink(boot)
    # mformat takes the geometry from the disk; the BIOS drive number the
    # boot sector reads from (offset 24h, 80h in the source) is set the way
    # FORMAT sets it for a floppy: drive 0
    with open(img, 'r+b') as f:
        f.seek(0x24)
        f.write(b'\x00')
    for name in names:
        mtool('mcopy', '-m', '-i', img, os.path.join(built, name), '::' + name)
    for name, path in add:
        mtool('mcopy', '-m', '-i', img, path, '::' + name)
    mtool('mattrib', '-i', img, '+s', '+h', '+r', '::IO.SYS', '::MSDOS.SYS')
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('built')
    ap.add_argument('image')
    ap.add_argument('--files', nargs='*')
    ap.add_argument('--add', action='append', default=[], metavar='NAME=PATH')
    a = ap.parse_args()
    make(a.built, a.image, a.files, [tuple(x.split('=', 1)) for x in a.add])


if __name__ == '__main__':
    main()
