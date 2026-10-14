# diskmgr: Disk Management (diskmgmt.exe) on a blank second disk.  QEMU
# attaches a 256 MB disk with nothing on it (diskmgr_boot).  Disk
# Management is opened from the Start menu first (Win+S, "Disk
# Management", Enter) and closed again; then "diskmgmt --test" finds the
# disk not initialized, initializes it as GPT, makes a 128 MB NTFS volume
# and a FAT32 volume from the rest (New Simple Volume: a partition, a
# format with fmifs.dll's FormatEx, a letter), writes files on both, moves
# the FAT32 volume to drive T: and checks each step through
# GetVolumeInformation, GetDiskFreeSpaceEx, the volume and mount-point
# APIs and the disk IOCTLs; it shows the disks with the new volume
# selected: the screenshot is the reference picture
# (docs/screenshots/disk-management.png).  NovaOS restarts and T: is still
# T: (HKLM\SYSTEM\MountedDevices).  Once QEMU has quit, the host reads the
# disk's GPT, scripts/ntfs-check.py looks the NTFS volume over as chkdsk
# would, fsck.fat checks the FAT32 one, and ntfs-3g and mtools read the
# files Disk Management wrote.
import os
import re
import shutil
import struct
import subprocess
import sys
import time

SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'scripts')
BASIC = bytes.fromhex('a2a0d0ebe5b9334487c068b6b72699c7')     # (the basic data partition GUID, as stored)


def _wait(nova, pattern, timeout=60):
    for _ in range(int(timeout / 0.25)):
        if re.search(pattern, open(nova.serial_path, 'rb').read().decode('latin-1')):
            return True
        time.sleep(0.25)
    return False


def _from_start_menu(nova):
    nova.qmp.key('meta_l', 's')
    time.sleep(2)
    nova.qmp.type('Disk Management')
    time.sleep(2)
    nova.qmp.key('ret')
    if _wait(nova, r'diskmgmt: showing \d+ volumes on \d+ disks'):
        time.sleep(3)
        nova.qmp.key('alt', 'f4')
        _wait(nova, r'\[UM\] diskmgmt\.exe \(PID \d+\) exited', 30)
    else:
        nova.qmp.key('esc')
        time.sleep(0.5)
        nova.qmp.key('esc')
    time.sleep(2)


def _partitions(disk):
    """The GPT partitions of @disk: [(first byte, length)]"""
    with open(disk, 'rb') as f:
        f.seek(512)
        h = f.read(92)
        if h[:8] != b'EFI PART':
            return None
        entries, count, size = struct.unpack_from('<QII', h, 72)
        f.seek(entries * 512)
        parts = []
        for _ in range(count):
            e = f.read(size)
            first, last = struct.unpack_from('<QQ', e, 32)
            if e[:16] == BASIC:
                parts.append((first * 512, (last - first + 1) * 512))
        return parts


def _volume(disk, at, n, name):
    vol = disk + name
    with open(disk, 'rb') as src, open(vol, 'wb') as out:
        src.seek(at)
        out.write(src.read(n))
    return vol


def _disk_ok(nova):
    disk = os.path.join(nova.work, 'blank.img')
    parts = _partitions(disk)
    if not parts or len(parts) != 2:
        return f'the disk\'s GPT has {parts and len(parts)} basic data partitions, not 2'
    ntfs = _volume(disk, *parts[0], '.ntfs')
    r = subprocess.run([sys.executable, os.path.join(SCRIPTS, 'ntfs-check.py'), ntfs], capture_output=True, text=True)
    if r.returncode:
        return 'ntfs-check.py: ' + ' / '.join(l for l in r.stdout.splitlines() if l.startswith('ERROR'))[:400]
    ntfscat = shutil.which('ntfscat') or '/usr/bin/ntfscat'
    if os.path.exists(ntfscat):
        r = subprocess.run([ntfscat, ntfs, 'Disk Management.txt'], capture_output=True)
        if r.returncode or r.stdout != b'Written on NTFS by Disk Management\r\n':
            return f'ntfscat "Disk Management.txt" read {r.stdout[:80]!r}'
    fat = _volume(disk, *parts[1], '.fat')
    fsck = shutil.which('fsck.fat') or '/usr/sbin/fsck.fat'
    if os.path.exists(fsck):
        r = subprocess.run([fsck, '-n', fat], capture_output=True, text=True)
        if r.returncode:
            return 'fsck.fat: ' + ' / '.join(r.stdout.splitlines()[1:4])[:400]
    mtype = shutil.which('mtype')
    if mtype:
        r = subprocess.run([mtype, '-i', fat, '::/Disk Management.txt'], capture_output=True,
                           env=dict(os.environ, MTOOLS_SKIP_CHECK='1'))
        if r.returncode or r.stdout != b'Written on FAT32 by Disk Management\r\n':
            return f'mtype "Disk Management.txt" read {r.stdout[:80]!r}'
        r = subprocess.run([mtype, '-i', fat, '::/New Folder/inside.txt'], capture_output=True,
                           env=dict(os.environ, MTOOLS_SKIP_CHECK='1'))
        if r.returncode or r.stdout != b'inside\r\n':
            return f'mtype "New Folder/inside.txt" read {r.stdout[:80]!r}'
    return None


TESTS = [
    Test('disk management', 'diskmgmt --test',
         [r'diskmgmt: \d+ passed, 0 failed',
          r'diskmgmt: Disk \d+: [^\n]*, 256 MB, Not Initialized',
          r'diskmgmt: Disk \d+: [^\n]*, 256 MB, GPT, 2 partition\(s\), 0 unallocated',
          r'diskmgmt: volume NOVANTFS \([D-Z]:\): NTFS, 1\d\d MB, Healthy \(Basic Data Partition\)',
          r'diskmgmt: volume NOVAFAT \(T:\): FAT32, 1\d\d MB, Healthy \(Basic Data Partition\)',
          r'\[DRIVES\] Formatting volume \d+ on \S+ as NTFS "NOVANTFS" \(128 MiB\)',
          r'\[DRIVES\] Formatting volume \d+ on \S+ as FAT32 "NOVAFAT"'],
         timeout=180, before=_from_start_menu, shot=r'diskmgmt: showing the disks', settle=3,
         boot_expect=[r'command line: "C:\\Windows\\System32\\diskmgmt\.exe"']),     # (the Start menu's launch)
    Test('disk management restart', 'shutdown /r',
         [r'\[DRIVES\] Volume \d+: drive letter [D-Z]: -> T: \(as chosen before\)'], reboot=True),
    Test('disk management list', 'diskmgmt --list',
         [r'diskmgmt: \d+ passed, 0 failed',
          r'diskmgmt: volume NOVAFAT \(T:\): FAT32',
          r'diskmgmt: volume NOVANTFS \([D-Z]:\): NTFS'],
         timeout=90, settle=3, check=_disk_ok),
]
