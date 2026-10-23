# diskc: drive C: on its disk, as on an installed system (docs/design/disk-c.md).
# The data disk is an NTFS volume labelled NOVADATA (diskc_boot), so NovaOS
# uses it in place as drive C:, with no RAM disk, and writes the system files
# to it at the first start. linktest runs on C: and leaves kept-times.txt
# with times, the hidden attribute and a DACL of its own. NovaOS restarts and
# finds the system files current (they are not written again), and
# `linktest restarted` finds the links, the times, the attribute and the DACL
# as they were. Once QEMU has quit, scripts/ntfs-check.py looks the volume
# over as chkdsk would, and ntfs-3g reads the system-file stamp and a DLL.
import os
import subprocess
import sys

SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'scripts')


def _volume_ok(nova):
    vol = os.path.join(nova.work, 'data.img')
    r = subprocess.run([sys.executable, os.path.join(SCRIPTS, 'ntfs-check.py'), vol], capture_output=True, text=True)
    if r.returncode:
        return 'ntfs-check.py: ' + ' / '.join(l for l in r.stdout.splitlines() if l.startswith('ERROR'))[:400]
    for name, start in (('Windows/NovaOS-system-files.txt', b'NovaOS system files\r\n'),
                        ('Windows/System32/kernel32.dll', b'MZ')):
        r = subprocess.run(['ntfscat', vol, name], capture_output=True)
        if r.returncode or not r.stdout.startswith(start):
            return f'ntfscat {name} read {r.stdout[:40]!r}'
    return None


TESTS = [
    Test('links on C:', 'linktest', [r'linktest: \d+ passed, 0 failed'], settle=2,
         boot_expect=[r'\[PERSIST\] Drive C: is \S+ NTFS "NOVADATA".*, in place \(not a RAM disk\)',
                      r'\[UM\] Wrote the system files to drive C:']),
    Test('restart', 'shutdown /r', [r'\[UM\] User-mode subsystem ready: the \d+ system files on drive C: are current'],
         reboot=True),
    Test('links restarted', 'linktest restarted', [r'linktest restarted: \d+ passed, 0 failed'], settle=3,
         check=_volume_ok),
]
