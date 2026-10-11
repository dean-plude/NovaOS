# ntfs: hard links on an NTFS volume.  A second disk holds a fresh, empty
# NTFS volume (mkntfs) that NovaOS mounts read-write as drive D:.  linktest
# runs its checks there and leaves one file under three names, two in
# D:\LinkTest and one in D:\LinkTest\kept-sub; NovaOS restarts, and
# `linktest restarted` finds them still one file, its link count and its
# names (FindFirstFileNameW) whole before kept-sub has been read.  Once
# QEMU has quit, scripts/ntfs-check.py looks the volume over as chkdsk
# would (records, bitmaps, $I30 indexes, link counts against names) and
# ntfs-3g reads the file by its third name (on the copy of the disk
# 020-full-and-cut.py takes before it fills the volume, when there is one).
import os
import shutil
import subprocess
import sys


def _volume_ok(nova):
    disk = os.path.join(nova.work, 'ntfs-links.img')
    if not os.path.exists(disk):
        disk = os.path.join(nova.work, 'ntfs.img')
    vol = os.path.join(nova.work, 'ntfs-vol.img')
    with open(disk, 'rb') as src, open(vol, 'wb') as out:      # (the partition starts at 1 MiB)
        src.seek(1 << 20)
        shutil.copyfileobj(src, out)
    check = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'scripts', 'ntfs-check.py')
    r = subprocess.run([sys.executable, check, vol], capture_output=True, text=True)
    if r.returncode:
        return 'ntfs-check.py: ' + ' / '.join(l for l in r.stdout.splitlines() if l.startswith('ERROR'))[:400]
    ntfscat = shutil.which('ntfscat') or '/usr/bin/ntfscat'
    if os.path.exists(ntfscat):
        r = subprocess.run([ntfscat, vol, 'LinkTest/kept-sub/kept-c.txt'], capture_output=True)
        if r.returncode or r.stdout != b'Kept by linktest\r\nand again\r\n':
            return f'ntfscat LinkTest/kept-sub/kept-c.txt read {r.stdout[:80]!r}'
    return None


TESTS = [
    Test('ntfs links', r'linktest D:\LinkTest', [r'linktest: \d+ passed, 0 failed'], settle=2,
         boot_expect=[r'\[DRIVES\] D: is NTFS volume "NOVATEST" on \S+ \(read-write\)']),
    Test('ntfs restart', 'shutdown /r', [r'\[DRIVES\] D: is NTFS volume "NOVATEST" on \S+ \(read-write\)'], reboot=True),
    Test('ntfs links restarted', r'linktest restarted D:\LinkTest', [r'linktest restarted: \d+ passed, 0 failed'],
         settle=3, check=_volume_ok),
]
