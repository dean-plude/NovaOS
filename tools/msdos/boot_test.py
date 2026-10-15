#!/usr/bin/env python3
"""boot_test.py — boots the MS-DOS 4.0 that build_msdos.py built

    boot_test.py BUILT_DIR [--work DIR] [--timeout SECONDS]

Checks that the build is a working DOS, not just a set of files: it makes a
1.44 MB floppy image the way SYS A: would (mkdisk.py: IO.SYS and MSDOS.SYS
as the first two files of the root directory, then COMMAND.COM and
MEM.EXE), with an AUTOEXEC.BAT that sends the
output of COMMAND.COM's VER and DIR and of MEM.EXE to AUX, the first serial
port, and boots it in QEMU's PC (SeaBIOS).  It passes when the serial port
shows "MS-DOS Version 4.00", the directory listing and MEM's report, and
the marker the batch file echoes last.

Needs qemu-system-i386 (or qemu-system-x86_64) and mtools.
"""
import argparse, os, shutil, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkdisk

MARK = 'NOVAOS-MSDOS-BOOT-OK'
AUTOEXEC = f'''@ECHO OFF
VER > AUX
DIR > AUX
MEM > AUX
ECHO {MARK} > AUX
'''
EXPECT = ['MS-DOS Version 4.00', 'COMMAND  COM', 'bytes total memory', MARK]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('built')
    ap.add_argument('--work')
    ap.add_argument('--timeout', type=int, default=300)
    a = ap.parse_args()
    built = os.path.abspath(a.built)
    work = os.path.abspath(a.work or os.path.join(built, 'boot-test'))
    os.makedirs(work, exist_ok=True)
    img = os.path.join(work, 'msdos.img')
    log = os.path.join(work, 'com1.log')
    with open(os.path.join(work, 'AUTOEXEC.BAT'), 'w', newline='\r\n') as f:
        f.write(AUTOEXEC)
    mkdisk.make(built, img, ['IO.SYS', 'MSDOS.SYS', 'COMMAND.COM', 'MEM.EXE'],
                [('AUTOEXEC.BAT', os.path.join(work, 'AUTOEXEC.BAT'))])

    qemu = shutil.which('qemu-system-i386') or shutil.which('qemu-system-x86_64')
    if not qemu:
        sys.exit('boot_test: needs qemu-system-i386 or qemu-system-x86_64')
    if os.path.exists(log):
        os.unlink(log)
    vm = subprocess.Popen([qemu, '-M', 'pc', '-m', '4', '-display', 'none', '-monitor', 'none',
                           '-drive', f'if=floppy,format=raw,file={img}', '-boot', 'a',
                           '-serial', f'file:{log}', '-no-reboot'])
    text = ''
    deadline = time.time() + a.timeout
    try:
        while time.time() < deadline and vm.poll() is None:
            time.sleep(1)
            if os.path.exists(log):
                with open(log, 'rb') as f:
                    text = f.read().decode('latin-1')
                if MARK in text:
                    break
    finally:
        vm.kill()
        vm.wait()
    sys.stdout.write(text.replace('\r', ''))
    missing = [e for e in EXPECT if e not in text]
    if missing:
        sys.exit(f'boot_test: the serial port never showed {missing} (log: {log})')
    print('boot_test: MS-DOS 4.0 built from source boots and runs COMMAND.COM and MEM.EXE')


if __name__ == '__main__':
    main()
