#!/usr/bin/env python3
"""vdm_test.py — boots the built MS-DOS 4.0 on NovaOS's own real-mode CPU and BIOS

    vdm_test.py BUILT_DIR [--work DIR] [--timeout SECONDS]

ntvdm.exe (userland/programs/ntvdm.c) runs DOS on an x86 interpreter and a
PC BIOS of NovaOS's own (userland/programs/ntvdm/cpu86.c, pcbios.c).  They
are plain C, so this script also builds them for the build machine with
tools/msdos/vdm_test.c as the host, makes the boot disk NovaOS ships
(mkdisk.py), boots it and types at COMMAND.COM's prompt as dostest does in
NovaOS: VER, DIR, MEM, CHKDSK, a COPY and TYPE through the disk, and a few
instructions assembled and run in DEBUG (MUL's 32-bit product and flags).
CI's MS-DOS job runs it beside boot_test.py (the same disk in QEMU's PC),
so a change to the CPU or BIOS is tested in seconds without booting
NovaOS.  Needs a C compiler (cc) and mtools.
"""
import argparse, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
VDM = os.path.join(ROOT, 'userland', 'programs', 'ntvdm')
sys.path.insert(0, HERE)
import mkdisk

STEPS = [
    'wait=A:\\>',
    'type=VER\\r', 'wait=MS-DOS Version 4.00', 'wait=A:\\>',
    'type=DIR\\r', 'wait=COMMAND  COM', 'wait=bytes free', 'wait=A:\\>',
    'type=CLS\\r', 'type=MEM\\r', 'wait=bytes total memory', 'wait=largest executable program size',
    'type=CHKDSK\\r', 'wait=bytes available on disk', 'wait=bytes free',
    'type=CLS\\r', 'type=COPY AUTOEXEC.BAT X.BAT\\r', 'wait=1 File(s) copied', 'wait=A:\\>',
    'type=TYPE X.BAT\\r', 'wait=PROMPT $P$G', 'wait=A:\\>',
    'type=CLS\\r', 'type=DEBUG\\r', 'wait=-',
    'type=A 100\\r', 'type=MOV AX,1234\\r', 'type=MOV BX,5678\\r', 'type=MUL BX\\r', 'type=INT 3\\r',
    'type=\\r', 'type=G=100\\r', 'wait=AX=0060  BX=5678  CX=0000  DX=0626', 'wait=OV UP EI PL NZ NA PO CY',
    'type=Q\\r', 'wait=A:\\>',
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('built')
    ap.add_argument('--work')
    ap.add_argument('--timeout', type=int, default=120)
    a = ap.parse_args()
    built = os.path.abspath(a.built)
    work = os.path.abspath(a.work or os.path.join(built, 'vdm-test'))
    os.makedirs(work, exist_ok=True)
    exe = os.path.join(work, 'vdm_test')
    cc = os.environ.get('CC') or shutil.which('cc') or 'gcc'
    subprocess.run([cc, '-O2', '-Wall', '-Wextra', '-Werror', '-I', VDM, '-o', exe,
                    os.path.join(HERE, 'vdm_test.c'), os.path.join(VDM, 'cpu86.c'),
                    os.path.join(VDM, 'pcbios.c')], check=True)
    autoexec = os.path.join(work, 'AUTOEXEC.BAT')
    with open(autoexec, 'wb') as f:
        f.write(b'@ECHO OFF\r\nPROMPT $P$G\r\n')       # (what build_userland.py puts on NovaOS's disk)
    img = mkdisk.make(built, os.path.join(work, 'msdos.img'), add=[('AUTOEXEC.BAT', autoexec)])
    r = subprocess.run([exe, img] + STEPS + ['--timeout', str(a.timeout)])
    if r.returncode:
        sys.exit('vdm_test: MS-DOS on the real-mode CPU and BIOS did not do what was typed')
    print('vdm_test: MS-DOS 4.0 boots on ntvdm\'s CPU and BIOS and runs VER, DIR, MEM, CHKDSK, COPY, TYPE and DEBUG')


if __name__ == '__main__':
    main()
