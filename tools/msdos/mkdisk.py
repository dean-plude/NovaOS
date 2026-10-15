#!/usr/bin/env python3
"""mkdisk.py — a bootable MS-DOS 4.0 floppy image from build_msdos.py's files

    mkdisk.py BUILT_DIR IMAGE [--files NAME...] [--add NAME=PATH ...] [--novaos]

Makes a 1.44 MB floppy the way FORMAT A: /S and SYS A: would: mtools'
mformat with the built boot sector (MSBOOT.BIN), IO.SYS and MSDOS.SYS as
the first two files of the root directory (hidden, system, read-only),
then COMMAND.COM and the rest.  Without --files the disk gets every file
the build made (all of MS-DOS 4.0 fits on one 1.44 MB disk); --add puts a
file of the caller's on it (an AUTOEXEC.BAT).  --novaos adds what NovaOS's
DOS host needs: REDIR.COM, which gives DOS the host's drive C: and runs
the program ntvdm.exe was started for, and an AUTOEXEC.BAT that runs it
(novaos_files).  boot_test.py boots such a disk in QEMU; build_userland.py
ships one as NovaOS's DOS (ntvdm.exe boots it, docs/dos.md).

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


# REDIR.COM, assembled (NASM syntax).  BOP nn (C4 C4 nn) calls the host's
# BIOS, userland/programs/ntvdm/pcbios.c: BOP 60h makes C: the host's drive
# and says whether to run a program; BOP 61h tells the host it ended.
#
#        org 100h
#        mov sp,0400h            ; keep 1 KB: code, the EXEC block, a stack
#        mov ah,4Ah
#        mov bx,0040h
#        push cs
#        pop es
#        int 21h
#        mov ah,52h              ; ES:BX = DOS's list of lists
#        int 21h
#        push es
#        push bx
#        mov ax,5D06h            ; DS:SI = DOS's swappable data area
#        int 21h
#        pop bx
#        pop es
#        db 0C4h,0C4h,60h        ; BOP 60h: AL = 1 if there is a program to run
#        push cs
#        pop ds
#        push cs
#        pop es
#        or al,al
#        jz quit
#        mov si,0211h            ; the command tail's file names into the FCBs
#        mov di,02A0h
#        mov ax,2901h
#        int 21h
#        mov di,02C0h
#        mov ax,2901h
#        int 21h
#        mov dx,02E0h            ; EXEC the program at 2E0h, parameters at 200h
#        mov bx,0200h
#        mov ax,4B00h
#        int 21h
#        mov bx,cs               ; (EXEC may leave SS:SP anywhere)
#        mov ss,bx
#        mov sp,0400h
#        jc done
#        mov ah,4Dh              ; AL = its exit code
#        int 21h
# done:  db 0C4h,0C4h,61h        ; BOP 61h: it ended (CF: EXEC failed, AX why)
# quit:  mov ax,4C00h
#        int 21h
REDIR_COM = bytes([
    0xBC, 0x00, 0x04, 0xB4, 0x4A, 0xBB, 0x40, 0x00, 0x0E, 0x07, 0xCD, 0x21, 0xB4, 0x52, 0xCD, 0x21,
    0x06, 0x53, 0xB8, 0x06, 0x5D, 0xCD, 0x21, 0x5B, 0x07, 0xC4, 0xC4, 0x60, 0x0E, 0x1F, 0x0E, 0x07,
    0x08, 0xC0, 0x74, 0x2E, 0xBE, 0x11, 0x02, 0xBF, 0xA0, 0x02, 0xB8, 0x01, 0x29, 0xCD, 0x21, 0xBF,
    0xC0, 0x02, 0xB8, 0x01, 0x29, 0xCD, 0x21, 0xBA, 0xE0, 0x02, 0xBB, 0x00, 0x02, 0xB8, 0x00, 0x4B,
    0xCD, 0x21, 0x8C, 0xCB, 0x8E, 0xD3, 0xBC, 0x00, 0x04, 0x72, 0x04, 0xB4, 0x4D, 0xCD, 0x21, 0xC4,
    0xC4, 0x61, 0xB8, 0x00, 0x4C, 0xCD, 0x21,
])

AUTOEXEC_BAT = b'@ECHO OFF\r\nPROMPT $P$G\r\nREDIR\r\n'


def novaos_files(work):
    """AUTOEXEC.BAT and REDIR.COM for NovaOS's DOS host, written into
    @work; the --add pairs for make()"""
    os.makedirs(work, exist_ok=True)
    out = []
    for name, data in (('AUTOEXEC.BAT', AUTOEXEC_BAT), ('REDIR.COM', REDIR_COM)):
        path = os.path.join(work, name)
        with open(path, 'wb') as f:
            f.write(data)
        out.append((name, path))
    return out


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
    ap.add_argument('--novaos', action='store_true', help="add NovaOS's AUTOEXEC.BAT and REDIR.COM")
    a = ap.parse_args()
    add = [tuple(x.split('=', 1)) for x in a.add]
    if a.novaos:
        add = novaos_files(os.path.splitext(os.path.abspath(a.image))[0] + '-files') + add
    make(a.built, a.image, a.files, add)


if __name__ == '__main__':
    main()
