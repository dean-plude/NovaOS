# beside: a PC with Windows on its NVMe disk, laid out as Windows Setup
# leaves one (tools/selftest.py windows_disk: an ESP "SYSTEM" holding
# \EFI\Microsoft\Boot\bootmgfw.efi, a Microsoft reserved partition, an NTFS
# volume "Windows" with a file on it, then 1.9 GiB of free space), and
# nova.iso on a USB stick.  NovaOS starts live from the stick; `install`
# shows the disk's partitions and its free space; then Setup's window,
# driven from the keyboard, installs NovaOS beside Windows (welcome, the
# disk, "Where should NovaOS go?" with the partition map, confirm, the
# progress, done: a screenshot of each page in --out), and
# NovaOS starts from the disk.  Windows' file is still on its volume, and
# once QEMU has quit the host checks the GPT (Windows' three partitions
# where they were, NovaOS's ESP and data partition in the free space),
# that the reserved partition is unchanged byte for byte and the ESP's
# files are (QEMU's firmware adds its NvVars there, as no PC's does),
# and Windows' NTFS volume with scripts/ntfs-check.py and ntfscat.
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import time

DOC = ('a PC with Windows on its NVMe disk (ESP, Microsoft reserved, NTFS, free space) and nova.iso on a USB '
       'stick: Setup\'s window installs NovaOS beside Windows in the free space, NovaOS starts from the disk, '
       'and Windows\' partitions and files are checked unchanged from the host')

SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'scripts')
ESP = bytes.fromhex('28732ac11ff8d211ba4b00a0c93ec93b')       # (the GUIDs as stored)
MSR = bytes.fromhex('16e3c9e35c0bb84d817df92df00215ae')
BASIC = bytes.fromhex('a2a0d0ebe5b9334487c068b6b72699c7')
NAME, TEXT = 'novaos-beside.txt', 'Written by the NovaOS self-tests beside Windows\n'


def _serial(nova):
    return open(nova.serial_path, 'rb').read().decode('latin-1')


def _page(nova, name, keys=('ret',)):
    """Screenshot Setup's page as setup-@name.png in --out, then press @keys"""
    time.sleep(2)
    nova.shot(os.path.join(getattr(sys.modules['__main__'], 'OUT', nova.work), f'setup-{name}.png'))
    for k in keys:
        nova.qmp.key(k)
        time.sleep(1)


def setup_window(nova):
    """Install beside Windows through Setup's window: it opened by itself at
    the start; "start setup" brings it to the front over the Terminal"""
    nova.run('start setup', 30)
    _page(nova, '1-welcome')
    _page(nova, '2-disk')                          # (the NVMe disk is chosen: the first one Setup can use)
    _page(nova, '3-where')                         # (beside Windows: chosen when it fits)
    _page(nova, '4-confirm')
    end = time.time() + 900
    while time.time() < end and not re.search(r'\[SETUP\] (100% NovaOS is installed|Failed)', _serial(nova)):
        time.sleep(1)
    _page(nova, '5-done', keys=('esc',))           # (closes the window: the Terminal is in front again)
    time.sleep(1)


def _gpt(disk):
    """The disk's GPT entries: [(type, first sector, sectors)]"""
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


def _disk_ok(nova):
    disk = os.path.join(nova.work, 'windows.img')
    parts = _gpt(disk)
    mib = 2048
    want = [(ESP, 1 * mib, 100 * mib), (MSR, 101 * mib, 16 * mib), (BASIC, 117 * mib, 1024 * mib)]
    if not parts or parts[:3] != want:
        return f'Windows\' partitions moved or changed: {[(p[1] // mib, p[2] // mib) for p in parts or []]} (MiB)'
    if len(parts) != 5 or parts[3][0] != ESP or parts[4][0] != BASIC or parts[3][1] < 1141 * mib or \
            parts[4][1] != parts[3][1] + parts[3][2]:
        return f'NovaOS\'s partitions are not an ESP and a data partition in the free space: {parts[3:]}'
    with open(disk, 'rb') as f:
        f.seek(101 << 20)
        msr = hashlib.sha256(f.read(16 << 20)).hexdigest()
        f.seek(parts[3][1] * 512 + 71)
        label = f.read(11)
    esp_files = sys.modules['__main__'].esp_files                  # (tools/selftest.py)
    got = f'{msr}  (Microsoft reserved)\n' + esp_files(disk, 1 << 20)
    want = open(os.path.join(nova.work, 'windows.sha256')).read()
    if got != want:
        return f'Windows\' ESP or Microsoft reserved partition changed: {got!r}, was {want!r}'[:600]
    if label != b'NOVA_EFI   ':
        return f'NovaOS\'s ESP is labelled {label!r}'
    vol = disk + '.ntfs'
    with open(disk, 'rb') as src, open(vol, 'wb') as out:
        src.seek(117 << 20)
        out.write(src.read(1024 << 20))
    try:
        r = subprocess.run([sys.executable, os.path.join(SCRIPTS, 'ntfs-check.py'), vol], capture_output=True, text=True)
        if r.returncode:
            return 'ntfs-check.py: ' + ' / '.join(l for l in r.stdout.splitlines() if l.startswith('ERROR'))[:400]
        ntfscat = shutil.which('ntfscat') or '/usr/bin/ntfscat'
        if os.path.exists(ntfscat):
            r = subprocess.run([ntfscat, vol, NAME], capture_output=True)
            if r.returncode or r.stdout != TEXT.replace('\n', '\r\n').encode():
                return f'ntfscat {NAME} read {r.stdout[:80]!r}'
    finally:
        os.unlink(vol)
    return None


TESTS = [
    Test('disk with windows', 'install',
         [r'nvme0n1  3072 MiB', r'SYSTEM \(EFI System\): 100 MiB at 1 MiB', r'Microsoft reserved: 16 MiB at 101 MiB',
          r'Windows \(NTFS\): 1024 MiB at 117 MiB', r'Free space: 1\d\d\d MiB at 1141 MiB',
          r'NovaOS can go beside Windows in 1\d\d\d MiB of free space \(install nvme0n1 /beside\)'],
         builtin=True, boot_expect=[r'\[SETUP\] Running from the installation USB stick']),
    Test('setup beside windows', 'install',
         [r'nvme0n1  3072 MiB  [^\n]*NovaOS is installed on it', r'Windows \(NTFS\): 1024 MiB at 117 MiB',
          r'NOVA_EFI \(EFI System\): \d+ MiB at 1141 MiB', r'NOVADATA \(NTFS\): \d+ MiB at \d+ MiB'],
         builtin=True, before=setup_window, timeout=60,
         boot_expect=[r'\[SETUP\] Installing NovaOS beside the other systems on nvme0n1',
                      r'\[SETUP\] Beside the 3 partitions on nvme0n1: EFI System Partition at 1141 MiB',
                      r'\[SETUP\] 100% NovaOS is installed']),
    Test('start from the disk', 'shutdown /r', [r'\[SETUP\] Added the firmware boot entry "NovaOS" for this disk'],
         reboot=True),
    Test('windows files kept', r'type D:\novaos-beside.txt', [r'Written by the NovaOS self-tests beside Windows'],
         builtin=True, check=_disk_ok),
]
