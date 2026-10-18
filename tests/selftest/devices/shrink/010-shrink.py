# shrink: a PC whose Windows volume takes all the room on its NVMe disk, up
# to the recovery partition at the end, as a PC sold with Windows has it
# (tools/selftest.py shrink_disk: ESP "SYSTEM" with bootmgfw.efi, Microsoft
# reserved, NTFS "Windows" of 2400 MiB with its files past the middle,
# Windows recovery), and nova.iso on a USB stick that starts first.
# `install` offers to make room by shrinking Windows' volume; Setup's window,
# driven from the keyboard, shrinks it (its files past the new end moved) and
# installs NovaOS in the room (a screenshot of each page in --out).  Windows'
# volume, marked for checking, is checked as it mounts with nothing to put
# right, and its file is read.  A file is kept in \Users on NovaOS's drive C:
# partition (NOVADATA, F: here: the test's own drive C: is on its data disk);
# started from the stick again, Setup reinstalls NovaOS keeping it.  Once QEMU has quit the host checks the
# GPT (Windows' volume smaller, nothing else of Windows moved, NovaOS's ESP
# and data partition in the room), Windows' reserved and recovery partitions
# and ESP files unchanged, its NTFS volume with scripts/ntfs-check.py and
# ntfsresize's check, every file on it byte for byte, and the kept file on
# NOVADATA.
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time

DOC = ('a PC whose Windows volume fills its NVMe disk, and nova.iso on a USB stick: Setup shrinks Windows\' '
       'volume to make room and installs NovaOS there, then reinstalls NovaOS keeping drive C:, and Windows\' '
       'partitions and every file on its volume are checked from the host')

SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'scripts')
ESP = bytes.fromhex('28732ac11ff8d211ba4b00a0c93ec93b')       # (the GUIDs as stored)
MSR = bytes.fromhex('16e3c9e35c0bb84d817df92df00215ae')
BASIC = bytes.fromhex('a2a0d0ebe5b9334487c068b6b72699c7')
RECOVERY = bytes.fromhex('a4bb94ded106404da16abfd50179d6ac')
KEEP = 'Kept across a reinstall'
CLEAN = (r'"Windows" was cut off while being written: checked, 0 lost clusters freed, 0 clusters claimed, '
         r'0 MFT records put right, 0 directory indexes rebuilt')


def _serial(nova):
    return open(nova.serial_path, 'rb').read().decode('latin-1')


def _page(nova, name, keys=('ret',)):
    """Screenshot Setup's page as setup-@name.png in --out, then press @keys"""
    time.sleep(2)
    nova.shot(os.path.join(getattr(sys.modules['__main__'], 'OUT', nova.work), f'setup-{name}.png'))
    for k in keys:
        nova.qmp.key(k)
        time.sleep(1)


def _setup(nova, kind, done):
    """Setup's window from the keyboard: welcome, the disk (the NVMe disk is
    chosen), where (keeping what is there: shrinking Windows, or reinstalling
    over NovaOS, chosen as it can), confirm, then the done page once the
    log shows @done"""
    nova.sr.read_new()
    nova.run('start setup', 30)
    _page(nova, f'{kind}-1-welcome')
    _page(nova, f'{kind}-2-disk')
    _page(nova, f'{kind}-3-where')
    _page(nova, f'{kind}-4-confirm')
    _page(nova, f'{kind}-5-progress', keys=())     # (while it works)
    got, end = '', time.time() + 900
    while time.time() < end and not re.search(done + r'|\[SETUP\] Failed', got):
        time.sleep(1)
        got += nova.sr.read_new()
    _page(nova, f'{kind}-6-done', keys=('esc',))
    time.sleep(1)


def shrink_window(nova):
    _setup(nova, 'shrink', r'\[SETUP\] 100% NovaOS is installed')


def reinstall_window(nova):
    _setup(nova, 'reinstall', r'\[SETUP\] 100% NovaOS is installed')


def _gpt(disk):
    """The disk's GPT entries in table order: [(type, first sector, sectors)]"""
    import struct
    with open(disk, 'rb') as f:
        f.seek(512)
        h = f.read(92)
        if h[:8] != b'EFI PART':
            return None
        table, count, size = struct.unpack_from('<QII', h, 72)
        f.seek(table * 512)
        ents = f.read(count * size)
    out = []
    for i in range(count):
        e = ents[i * size:(i + 1) * size]
        if e[:16] != bytes(16):
            first, last = struct.unpack_from('<QQ', e, 32)
            out.append((e[:16], first, last - first + 1))
    return out


def _extract(disk, lba, sectors, out):
    with open(disk, 'rb') as src, open(out, 'wb') as dst:
        src.seek(lba * 512)
        left = sectors * 512
        while left:
            b = src.read(min(left, 64 << 20))
            dst.write(b)
            left -= len(b)


def _disk_ok(nova):
    disk = os.path.join(nova.work, 'windows.img')
    parts = _gpt(disk)
    mib = 2048
    if not parts or len(parts) != 6:
        return f'the GPT has {len(parts or [])} partitions, not Windows\' four and NovaOS\'s two'
    if parts[0] != (ESP, 1 * mib, 100 * mib) or parts[1] != (MSR, 101 * mib, 16 * mib) or \
            parts[3] != (RECOVERY, 2517 * mib, 500 * mib):
        return f'Windows\' ESP, reserved or recovery partition moved: {[(p[1] // mib, p[2] // mib) for p in parts]} (MiB)'
    win = parts[2]
    if win[0] != BASIC or win[1] != 117 * mib or not 1024 * mib <= win[2] < 2400 * mib:
        return f'Windows\' volume is not where it was, smaller: {(win[1] // mib, win[2] // mib)} (MiB)'
    nesp, ndata = parts[4], parts[5]
    if nesp[0] != ESP or ndata[0] != BASIC or nesp[1] < win[1] + win[2] or ndata[1] != nesp[1] + nesp[2] or \
            ndata[1] + ndata[2] > 2517 * mib:
        return f'NovaOS\'s partitions are not an ESP and a data partition in the room made: {parts[4:]}'
    with open(disk, 'rb') as f:
        f.seek(101 << 20)
        msr = hashlib.sha256(f.read(16 << 20)).hexdigest()
        f.seek(2517 << 20)
        rec = hashlib.sha256(f.read(500 << 20)).hexdigest()
    esp_files = sys.modules['__main__'].esp_files                  # (tools/selftest.py)
    got = f'{msr}  (Microsoft reserved)\n{rec}  (recovery)\n' + esp_files(disk, 1 << 20)
    want = open(os.path.join(nova.work, 'windows.sha256')).read()
    if got != want:
        return f'Windows\' ESP, reserved or recovery partition changed: {got!r}, was {want!r}'[:600]
    vol, data = disk + '.ntfs', disk + '.data'
    _extract(disk, win[1], win[2], vol)
    _extract(disk, ndata[1], ndata[2], data)
    try:
        r = subprocess.run([sys.executable, os.path.join(SCRIPTS, 'ntfs-check.py'), vol], capture_output=True, text=True)
        if r.returncode:
            return 'ntfs-check.py: ' + ' / '.join(l for l in r.stdout.splitlines() if l.startswith('ERROR'))[:400]
        ntfsresize = shutil.which('ntfsresize') or '/usr/sbin/ntfsresize'
        if os.path.exists(ntfsresize):                              # (its own consistency check, read-only)
            r = subprocess.run([ntfsresize, '--info', '--force', '--no-progress-bar', vol], capture_output=True, text=True)
            if r.returncode or 'inconsistent' in r.stdout + r.stderr:
                return 'ntfsresize --info: ' + (r.stdout + r.stderr)[-400:]
        ntfscat = shutil.which('ntfscat') or '/usr/bin/ntfscat'
        if os.path.exists(ntfscat):
            for line in open(os.path.join(nova.work, 'windows-files.sha256')):
                want, name = line.split()
                r = subprocess.run([ntfscat, vol, name], capture_output=True)
                if r.returncode or hashlib.sha256(r.stdout).hexdigest() != want:
                    return f'Windows\' file {name} changed or is missing after the shrink'
            r = subprocess.run([ntfscat, data, 'Users/Kept/keep.txt'], capture_output=True)
            if r.returncode or not r.stdout.startswith(KEEP.encode()):
                return f'the kept file on NOVADATA reads {r.stdout[:80]!r}'
    finally:
        os.unlink(vol)
        os.unlink(data)
    return None


TESTS = [
    Test('windows fills the disk', 'install',
         [r'nvme0n1  3072 MiB', r'Windows \(NTFS\): 2400 MiB at 117 MiB', r'Windows recovery: 500 MiB at 2517 MiB',
          r'NovaOS can go beside Windows by shrinking its volume from 2400 MiB to 1\d\d\d MiB \(\d+ MiB used\)'],
         builtin=True, boot_expect=[r'\[SETUP\] Running from the installation USB stick']),
    Test('setup shrinking windows', 'install',
         [r'nvme0n1  3072 MiB  [^\n]*NovaOS is installed on it', r'Windows \(NTFS\): 1\d\d\d MiB at 117 MiB',
          r'NOVA_EFI \(EFI System\): \d+ MiB at 1\d\d\d MiB', r'NOVADATA \(NTFS\): \d+ MiB at \d+ MiB',
          r'Windows recovery: 500 MiB at 2517 MiB',
          r'NovaOS can be reinstalled there, keeping drive C: and your files \(install nvme0n1 /reinstall\)'],
         builtin=True, before=shrink_window, timeout=60,
         boot_expect=[r'\[SETUP\] Installing NovaOS beside the other systems, making room, on nvme0n1',
                      r'\[NTFS\] nvme0n1: shrinking "Windows" from 2399 to 1\d\d\d MiB: \d+ MiB of \d+ attributes? to move',
                      r'\[SETUP\] Shrank the NTFS volume at 117 MiB on nvme0n1 from 2400 MiB to 1\d\d\d MiB',
                      r'\[SETUP\] Beside the 4 partitions on nvme0n1: EFI System Partition at 1\d\d\d MiB',
                      r'\[SETUP\] 100% NovaOS is installed', CLEAN]),
    Test('windows file after the shrink', r'type D:\novaos-beside.txt',
         [r'Written by the NovaOS self-tests beside Windows'], builtin=True),
    Test('keep a file in users', rf'cmd /c "md F:\Users\Kept & echo {KEEP}> F:\Users\Kept\keep.txt & '
         r'type F:\Users\Kept\keep.txt"', [KEEP], settle=3,
         boot_expect=[r'\[DRIVES\] F: is NTFS volume "NOVADATA" on nvme0n1 \(read-write\)']),
    Test('start the stick again', 'shutdown /r',
         [r'\[SETUP\] Running from the installation USB stick', r'\[DRIVES\] F: is NTFS volume "NOVADATA" on nvme0n1'],
         reboot=True),
    Test('setup reinstall', r'type F:\Users\Kept\keep.txt', [KEEP], builtin=True, before=reinstall_window, timeout=60,
         boot_expect=[r'\[SETUP\] Installing NovaOS again, keeping drive C:, on nvme0n1',
                      r'\[SETUP\] Reinstalling NovaOS on nvme0n1: EFI System Partition at 1\d\d\d MiB \(\d+ MiB\) '
                      r'formatted again, NOVADATA at \d+ MiB \(\d+ MiB\) kept',
                      r'\[SETUP\] 80% Keeping drive C: \(NOVADATA\)',
                      r'Added the firmware boot entry Boot[0-9A-F]{4} "NovaOS" for nvme0n1',
                      r'\[SETUP\] 100% NovaOS is installed']),
    Test('files kept', r'cmd /c "type F:\Users\Kept\keep.txt & type D:\novaos-beside.txt"',
         [KEEP, r'Written by the NovaOS self-tests beside Windows'], check=_disk_ok),
]
