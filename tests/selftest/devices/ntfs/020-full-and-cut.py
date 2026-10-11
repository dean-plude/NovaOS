# ntfs: a full volume, and writing cut off half-way, on drive D: (after
# 010-links.py).  `ntfstest fill` grows a file until WriteFile fails with
# ERROR_DISK_FULL, reads back what was written and finds the space free
# again once it is deleted.  `ntfstest churn` leaves kept.txt and then
# rewrites four 4 MB files without end; the machine is reset while it
# does.  NovaOS marks the volume dirty while it writes to it, so the next
# boot mounts D: read-only, and `ntfstest cut` finds kept.txt whole and
# each file one whole generation, old or new.  Offline: the disk copied
# once NovaOS had marked it clean after filling it must pass
# scripts/ntfs-check.py as it is; the disk after the cut must be marked
# dirty and pass it with --after-cut (nothing lost; only what chkdsk
# mends without loss), and ntfs-3g must read kept.txt.
import os
import shutil
import subprocess
import sys
import time

SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'scripts')
KEPT = b'Kept by ntfstest before the cut\r\n'


def _volume(nova, name):
    """The NTFS volume of disk copy @name (the partition starts at 1 MiB)"""
    vol = os.path.join(nova.work, name + '-vol.img')
    with open(os.path.join(nova.work, name + '.img'), 'rb') as src, open(vol, 'wb') as out:
        src.seek(1 << 20)
        shutil.copyfileobj(src, out)
    return vol


def _snapshot(name):
    """Before the next test: once NovaOS has marked D: clean (it does a
    second after the last change), a copy of the disk as work/ntfs-NAME.img"""
    def take(nova):
        disk = os.path.join(nova.work, 'ntfs.img')
        for _ in range(30):
            if subprocess.run([sys.executable, os.path.join(SCRIPTS, 'ntfs-dirty.py'), disk, str(1 << 20)],
                              capture_output=True).returncode == 0:
                break
            time.sleep(0.5)
        shutil.copy(disk, os.path.join(nova.work, f'ntfs-{name}.img'))
    return take


def _check(vol, *flags):
    r = subprocess.run([sys.executable, os.path.join(SCRIPTS, 'ntfs-check.py'), *flags, vol],
                       capture_output=True, text=True)
    if r.returncode:
        return 'ntfs-check.py: ' + ' / '.join(l for l in r.stdout.splitlines() if l.startswith('ERROR'))[:400]
    return None


def _full_ok(nova):
    if not os.path.exists(os.path.join(nova.work, 'ntfs-full.img')):
        return 'no copy of the disk after it was filled'
    return _check(_volume(nova, 'ntfs-full'))


def _cut_ok(nova):
    vol = _volume(nova, 'ntfs')
    r = subprocess.run([sys.executable, os.path.join(SCRIPTS, 'ntfs-dirty.py'), vol], capture_output=True, text=True)
    if r.returncode != 1:
        return f'the volume is not marked dirty after the cut ({r.stdout.strip() or r.stderr.strip()})'
    why = _check(vol, '--after-cut')
    if why:
        return why
    ntfscat = shutil.which('ntfscat') or '/usr/bin/ntfscat'
    if os.path.exists(ntfscat):
        r = subprocess.run([ntfscat, '-f', vol, 'Churn/kept.txt'], capture_output=True)    # (-f: dirty as it is)
        if r.returncode or r.stdout != KEPT:
            return f'ntfscat Churn/kept.txt read {r.stdout[:80]!r} {r.stderr[:200]!r}'
    return None


TESTS = [
    Test('ntfs full', r'ntfstest fill D:\Full', [r'ntfstest fill: \d+ passed, 0 failed'], timeout=300, settle=3,
         before=_snapshot('links'), check=_full_ok),
    Test('ntfs cut', r'ntfstest churn D:\Churn', [r'\[NTFS\] \S+: "NOVATEST" stays read-only: marked dirty',
                                                  r'\[DRIVES\] D: is NTFS volume "NOVATEST" on \S+ \(read-only\)'],
         cut=r'ntfstest churn: writing', before=_snapshot('full')),
    Test('ntfs after cut', r'ntfstest cut D:\Churn', [r'ntfstest cut: \d+ passed, 0 failed'], check=_cut_ok),
]
