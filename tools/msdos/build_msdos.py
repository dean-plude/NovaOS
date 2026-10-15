#!/usr/bin/env python3
"""build_msdos.py — builds MS-DOS 4.0 from Microsoft's own source

    build_msdos.py OUTDIR [--src DIR] [--work DIR] [--timeout SECONDS]

Microsoft published the source of MS-DOS 4.00 under the MIT licence at
github.com/microsoft/MS-DOS (v4.0/src).  The tree carries its own build
tools as DOS programs (MASM 5, LINK, the Microsoft C 5 compiler, NMAKE,
EXE2BIN and the message tools in v4.0/src/TOOLS), so this script builds it
the way it was built in 1988: it runs the tree's own top-level NMAKE in
DOS.  The DOS is DOSBox (Ubuntu's `dosbox` package, run headless; it is a
build tool only, like the compilers, and none of it ends up in NovaOS).

The script fetches the pinned commit (MSDOS_COMMIT; a git commit id names
its content, so git itself verifies the fetch) into ~/.cache/novaos/msdos,
copies v4.0/src into a work directory and prepares it as a DOS disk would
hold it:

  * text files get DOS line ends (CR LF).  The repository stores them
    with LF only, which MASM and NMAKE read, but programs that read their
    answers from a redirected file do not: BIOS\\LOCSCR is EXE2BIN's
    answer to "Fix-ups needed - base segment (hex):" and EXE2BIN waits
    for the CR forever without one;
  * every U+FFFD in the source becomes one byte, 0xC4 (the code page 437
    line-drawing character most of them were).  The publication turned
    the code page 437 bytes of some files into the three-byte UTF-8
    replacement character; most are in comments, but in MAPPER\\GETMSG.ASM
    a comment line of them grows past MASM's 128-character line limit
    ("error A2106: Line too long") and stops the build.

Nothing else in the source changes.  OUTDIR gets the files CPY.BAT (the
tree's own "gather the binaries" script) copies: IO.SYS, MSDOS.SYS,
COMMAND.COM, the utilities and device drivers, plus the boot sector
(MSBOOT.BIN) and LICENSE.  The result is kept in
~/.cache/novaos/msdos/out-<hash of the inputs> and copied from there
while nothing it is made from changed; BUILD.LOG beside it is NMAKE's
output.
"""
import argparse, hashlib, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = 'https://github.com/microsoft/MS-DOS.git'
MSDOS_COMMIT = '2d04cacc5322951f187bb17e017c12920ac8ebe2'     # "MZ is back!", 2024-04-25
CACHE = os.path.join(os.path.expanduser('~'), '.cache', 'novaos', 'msdos')

# What v4.0/src/CPY.BAT copies, as paths under v4.0/src, plus the boot
# sector the BOOT directory builds (SYS.COM and FORMAT.COM carry their own
# copy of it).
def outputs(src):
    names = []
    with open(os.path.join(src, 'CPY.BAT'), 'rb') as f:
        for line in f.read().decode('latin-1').splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[0].lower() == 'copy' and parts[2] == '%1':
                names.append(parts[1].lstrip('.\\').replace('\\', '/'))
    names.append('boot/msboot.bin')
    return names

BUILD_BAT = r'''@echo off
set CL=
set LINK=
set MASM=
set COUNTRY=usa-ms
set BAKROOT=D:
set LIB=D:\SRC\TOOLS\BLD\LIB
set INIT=D:\SRC\TOOLS
set INCLUDE=D:\SRC\TOOLS\BLD\INC
set PATH=D:\SRC\TOOLS;Z:\
cd \SRC
nmake > D:\BUILD.LOG
'''

DOSBOX_CONF = '''[sdl]
output=surface
[dosbox]
memsize=16
[cpu]
core=dynamic
cycles=max
[render]
frameskip=10
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
mount d .
d:
call BUILD.BAT
exit
'''


def git(*args, cwd=None):
    subprocess.run(['git', *args], cwd=cwd, check=True)


def fetch(src):
    if not os.path.isdir(os.path.join(src, '.git')):
        os.makedirs(src, exist_ok=True)
        git('init', '-q', cwd=src)
        git('remote', 'add', 'origin', REPO, cwd=src)
    if subprocess.run(['git', 'cat-file', '-e', MSDOS_COMMIT + '^{commit}'], cwd=src,
                      stderr=subprocess.DEVNULL).returncode:
        git('fetch', '-q', '--depth', '1', 'origin', MSDOS_COMMIT, cwd=src)
    git('checkout', '-q', '--force', MSDOS_COMMIT, cwd=src)


def inputs_hash():
    h = hashlib.sha256(MSDOS_COMMIT.encode())
    with open(os.path.abspath(__file__), 'rb') as f:
        h.update(f.read())
    return h.hexdigest()[:16]


def prepare(tree, work):
    """Copy v4.0/src to WORK/SRC as a DOS disk holds it (see the docstring)."""
    dst = os.path.join(work, 'SRC')
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(tree, dst)
    for root, _, files in os.walk(dst):
        for name in files:
            path = os.path.join(root, name)
            with open(path, 'rb') as f:
                data = f.read()
            if b'\0' in data:
                continue                                # EXE, OBJ, LIB, CPI: binaries
            new = data.replace(b'\xef\xbf\xbd', b'\xc4')
            new = new.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
            if new != data:
                with open(path, 'wb') as f:
                    f.write(new)
    for name, text in (('BUILD.BAT', BUILD_BAT), ('dosbox.conf', DOSBOX_CONF)):
        with open(os.path.join(work, name), 'w', newline='\r\n' if name.endswith('.BAT') else '\n') as f:
            f.write(text)
    return dst


def find_ci(base, rel):
    """The path of REL under BASE, matching each part without regard to case."""
    path = base
    for part in rel.split('/'):
        entries = {e.lower(): e for e in os.listdir(path)} if os.path.isdir(path) else {}
        if part.lower() not in entries:
            return None
        path = os.path.join(path, entries[part.lower()])
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('out')
    ap.add_argument('--src', help='a checkout of github.com/microsoft/MS-DOS (default: fetched into the cache)')
    ap.add_argument('--work', help='the build directory (default: under the cache)')
    ap.add_argument('--timeout', type=int, default=3600)
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    kept = os.path.join(CACHE, 'out-' + inputs_hash())
    tree_root = os.path.abspath(a.src) if a.src else os.path.join(CACHE, 'src')
    if not os.path.isfile(os.path.join(kept, 'COMMAND.COM')):
        if not a.src:
            fetch(tree_root)
        if not shutil.which('dosbox'):
            sys.exit('build_msdos: needs DOSBox (apt install dosbox)')
        work = os.path.abspath(a.work or os.path.join(CACHE, 'work'))
        os.makedirs(work, exist_ok=True)
        tree = prepare(os.path.join(tree_root, 'v4.0', 'src'), work)
        log = os.path.join(work, 'BUILD.LOG')
        if os.path.exists(log):
            os.unlink(log)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        print('build_msdos: running NMAKE in DOSBox (MS-DOS 4.0, commit %s)' % MSDOS_COMMIT[:7], flush=True)
        try:
            subprocess.run(['dosbox', '-conf', 'dosbox.conf', '-noconsole'], cwd=work, env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=a.timeout)
        except subprocess.TimeoutExpired:
            print(f'build_msdos: DOSBox did not finish in {a.timeout} s', file=sys.stderr)
        text = open(log, 'rb').read().decode('latin-1') if os.path.exists(log) else ''
        missing = [n for n in outputs(tree) if not find_ci(tree, n)]
        if missing:
            sys.stdout.write(text[-6000:])
            sys.exit(f'build_msdos: NMAKE did not build {", ".join(missing)} (log above: {log})')
        tmp = kept + '.tmp'
        shutil.rmtree(tmp, ignore_errors=True)
        os.makedirs(tmp)
        for n in outputs(tree):
            shutil.copy(find_ci(tree, n), os.path.join(tmp, os.path.basename(n).upper()))
        shutil.copy(log, os.path.join(tmp, 'BUILD.LOG'))
        shutil.copy(os.path.join(tree_root, 'LICENSE'), os.path.join(tmp, 'LICENSE'))
        shutil.rmtree(kept, ignore_errors=True)
        os.rename(tmp, kept)
        print(f'build_msdos: built {len(outputs(tree))} files')
    else:
        print(f'build_msdos: unchanged (from {kept})')
    os.makedirs(out, exist_ok=True)
    for n in os.listdir(kept):
        shutil.copy(os.path.join(kept, n), os.path.join(out, n))


if __name__ == '__main__':
    main()
