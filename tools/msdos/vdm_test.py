#!/usr/bin/env python3
"""vdm_test.py — boots the built MS-DOS 4.0 on NovaOS's own real-mode CPU and BIOS

    vdm_test.py BUILT_DIR [--work DIR] [--timeout SECONDS]

ntvdm.exe (userland/programs/ntvdm.c) runs DOS on an x86 interpreter and a
PC BIOS of NovaOS's own (userland/programs/ntvdm/cpu86.c, pcbios.c).  They
are plain C, so this script also builds them for the build machine with
tools/msdos/vdm_test.c as the host, makes the boot disk NovaOS ships
(mkdisk.py), boots it and types at COMMAND.COM's prompt as dostest does in
NovaOS: VER, DIR, MEM, CHKDSK, a COPY and TYPE through the disk, a few
instructions assembled and run in DEBUG (MUL's 32-bit product and flags)
and a file written in EDLIN.  Then DOS programs run on a directory as
drive C: as CreateProcess starts them, and bootRogue (third_party/
bootrogue, assembled with NASM), a game that draws straight into video
memory, plays a few moves and ends on Esc.
CI's MS-DOS job runs it beside boot_test.py (the same disk in QEMU's PC),
so a change to the CPU or BIOS is tested in seconds without booting
NovaOS.  Needs a C compiler (cc), NASM and mtools.
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
    'type=CLS\\r', 'type=EDLIN NOTE.TXT\\r', 'wait=New file', 'wait=*',
    'type=I\\r', 'wait=1:*', 'type=written in EDLIN\\r', 'wait=2:*', 'type=\x03', 'wait=*',
    'type=E\\r', 'wait=A:\\>', 'type=TYPE NOTE.TXT\\r', 'wait=written in EDLIN', 'wait=A:\\>',
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
                    os.path.join(VDM, 'pcbios.c'), os.path.join(VDM, 'redir.c')], check=True)
    img = mkdisk.make(built, os.path.join(work, 'msdos.img'),          # (the disk build_userland.py ships)
                      add=mkdisk.novaos_files(os.path.join(work, 'files')))
    timeout = ['--timeout', str(a.timeout)]
    r = subprocess.run([exe, img] + STEPS + timeout)
    if r.returncode:
        sys.exit('vdm_test: MS-DOS on the real-mode CPU and BIOS did not do what was typed')
    print('vdm_test: MS-DOS 4.0 boots on ntvdm\'s CPU and BIOS and runs VER, DIR, MEM, CHKDSK, COPY, TYPE, DEBUG '
          'and EDLIN')

    drive = os.path.join(work, 'c')
    shutil.rmtree(drive, ignore_errors=True)
    folder = os.path.join(drive, 'Users', 'Dean', 'My DOS programs')    # (DOS: C:\USERS\DEAN\MYDOSP~1)
    os.makedirs(folder)
    subprocess.run([exe, '--put-dosrw=' + folder], check=True)
    os.rename(os.path.join(folder, 'DOSRW.COM'), os.path.join(folder, 'dosrw.com'))
    shutil.copy(os.path.join(built, 'MEM.EXE'), os.path.join(drive, 'mem.exe'))
    with open(os.path.join(folder, 'in.txt'), 'wb') as f:
        f.write(b'hello from the host\r\n')
    rel = os.path.join('Users', 'Dean', 'My DOS programs')
    out = os.path.join(folder, 'OUT.TXT')
    for prog in ('dosrw.com', 'DOSRW.EXE'):
        if os.path.exists(out):
            os.unlink(out)
        r = subprocess.run([exe, img, '--drive-c=' + drive, '--cwd=' + rel, '--run=' + os.path.join(rel, prog),
                            '--args=IN.TXT', '--exit=42', 'wait=DOSRW: wrote OUT.TXT'] + timeout)
        if r.returncode or not os.path.exists(out):
            sys.exit(f'vdm_test: {prog} did not run on drive C: as CreateProcess starts it')
        with open(out, 'rb') as f:
            if f.read() != b'DOS read: hello from the host\r\n':
                sys.exit(f'vdm_test: {prog} wrote the wrong OUT.TXT')
    steps = [
        'wait=A:\\>', 'type=C:\\r', 'wait=C:\\>', 'type=DIR\\r', 'wait=MEM      EXE', 'wait=bytes free',
        'type=CD USERS\\DEAN\\MYDOSP~1\\r', 'wait=C:\\USERS\\DEAN\\MYDOSP~1>',
        'type=CLS\\r', 'type=DIR\\r', 'wait=DOSRW    COM       141', 'wait=bytes free',
        'type=DOSRW\\r', 'wait=DOSRW: wrote OUT.TXT', 'type=TYPE OUT.TXT\\r', 'wait=DOS read: hello from the host',
        'type=COPY IN.TXT COPY.TXT\\r', 'wait=1 File(s) copied',
        'type=MD SUB\\r', 'type=CD SUB\\r', 'wait=MYDOSP~1\\SUB>', 'type=CD ..\\r', 'type=RD SUB\\r',
        'type=REN COPY.TXT RENAMED.TXT\\r', 'type=DEL OUT.TXT\\r',
        'type=CLS\\r', 'type=C:\\MEM\\r', 'wait=largest executable program size', 'wait=>',
    ]
    os.unlink(out)
    r = subprocess.run([exe, img, '--drive-c=' + drive] + steps + timeout)
    names = sorted(os.listdir(folder))
    if r.returncode or names != ['DOSRW.EXE', 'RENAMED.TXT', 'dosrw.com', 'in.txt']:
        sys.exit(f'vdm_test: drive C: at the prompt did not do what was typed (the folder has {names})')
    print('vdm_test: drive C: works: DOSRW.COM and DOSRW.EXE run as CreateProcess starts them, and DIR, CD, '
          'TYPE, COPY, MD, RD, REN, DEL and MEM at the prompt')

    # bootRogue: the 80 x 25 screen written directly (the status line is
    # level, weapon, armor and HP at the bottom right), arrow keys from the
    # BIOS, the PIT's counter as its random seed; Esc ends it (INT 20h)
    # (in a folder laid out as NovaOS ships it: C:\Tests\Dos, the licence's
    # long name beside it needs an alias)
    games = os.path.join(drive, 'Tests', 'Dos')
    os.makedirs(games)
    subprocess.run(['nasm', '-f', 'bin', '-Dcom_file=1', '-o', os.path.join(games, 'ROGUE.COM'),
                    os.path.join(ROOT, 'third_party', 'bootrogue', 'rogue.asm')], check=True)
    shutil.copy(os.path.join(ROOT, 'third_party', 'bootrogue', 'LICENSE'), os.path.join(games, 'ROGUE-LICENSE.TXT'))
    moves = ['key=right', 'key=down', 'key=left', 'key=up'] * 4
    r = subprocess.run([exe, img, '--drive-c=' + drive, '--cwd=Tests\\Dos', '--run=Tests\\Dos\\ROGUE.COM', '--exit=0', 'screen= 16  ', 'screen=@'] +
                       moves + ['key=esc'] + timeout)
    if r.returncode:
        sys.exit('vdm_test: bootRogue did not draw its dungeon, take the arrow keys and end on Esc')
    print('vdm_test: bootRogue draws its dungeon, takes 16 moves and ends on Esc')


if __name__ == '__main__':
    main()
