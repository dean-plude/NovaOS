# ntfs: a full volume, and writing cut off half-way, on drive D: (after
# 010-links.py).  `ntfstest fill` grows a file until WriteFile fails with
# ERROR_DISK_FULL, reads back what was written and finds the space free
# again once it is deleted.  `ntfstest churn` leaves kept.txt and then
# rewrites four 4 MB files without end; the machine is reset while it
# does.  NovaOS marks the volume dirty while it writes to it, so the next
# boot finds it cut off, checks and repairs it (as chkdsk would) and mounts
# D: read-write, and `ntfstest cut` finds kept.txt whole, each file one
# whole generation, old or new, and D: taking new files.  Offline: the
# disk copied once NovaOS had marked it clean after filling it, and again
# after the cut, must pass scripts/ntfs-check.py with no errors, and
# ntfs-3g must read kept.txt.
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


def _clean(nova):
    """Wait until NovaOS has marked D: clean (it does a second after the
    last change)"""
    disk = os.path.join(nova.work, 'ntfs.img')
    for _ in range(30):
        if subprocess.run([sys.executable, os.path.join(SCRIPTS, 'ntfs-dirty.py'), disk, str(1 << 20)],
                          capture_output=True).returncode == 0:
            return True
        time.sleep(0.5)
    return False


def _snapshot(name):
    """Before the next test: once NovaOS has marked D: clean, a copy of
    the disk as work/ntfs-NAME.img"""
    def take(nova):
        _clean(nova)
        shutil.copy(os.path.join(nova.work, 'ntfs.img'), os.path.join(nova.work, f'ntfs-{name}.img'))
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
    if not _clean(nova):
        return 'D: is still marked dirty after the cut, the repair and ntfstest cut'
    _snapshot('cut')(nova)
    vol = _volume(nova, 'ntfs-cut')
    why = _check(vol)
    if why:
        return why
    ntfscat = shutil.which('ntfscat') or '/usr/bin/ntfscat'
    if os.path.exists(ntfscat):
        r = subprocess.run([ntfscat, vol, 'Churn/kept.txt'], capture_output=True)
        if r.returncode or r.stdout != KEPT:
            return f'ntfscat Churn/kept.txt read {r.stdout[:80]!r} {r.stderr[:200]!r}'
    return None


TESTS = [
    Test('ntfs full', r'ntfstest fill D:\Full', [r'ntfstest fill: \d+ passed, 0 failed'], timeout=300, settle=3,
         before=_snapshot('links'), check=_full_ok),
    Test('ntfs cut', r'ntfstest churn D:\Churn', [r'\[NTFS\] \S+: "NOVATEST" was cut off while being written: checked',
                                                  r'\[DRIVES\] D: is NTFS volume "NOVATEST" on \S+ \(read-write\)'],
         cut=r'ntfstest churn: writing', before=_snapshot('full')),
    Test('ntfs after cut', r'ntfstest cut D:\Churn', [r'ntfstest cut: \d+ passed, 0 failed'], check=_cut_ok),
]
