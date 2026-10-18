# The recovery boot (docs/design/recovery.md, roadmap step 33.3's boot test
# rollbacktest): the boot disk is a copy of build/nova.img, an installed
# NovaOS, with the known-good set an update leaves beside it (kernel.old,
# this build stamped 0.0.9-test, and bootx64.old) and the files of an
# update that never finished (rollbacktest_boot in tools/selftest.py).
# shutdown /r /o restarts into the boot loader's Advanced startup menu
# (bootloader/src/recovery.c), which starts safe mode (NovaOS's own drivers
# and the firmware's framebuffer only), startup repair (which removes the
# unfinished update and writes its report where Windows does) and the
# previous version (its own boot loader starting kernel.old, once).  Last,
# two starts are reset before they reach the desktop, and the third shows
# the menu by itself with startup repair chosen.  (A broken update rolling
# back by itself is the update boot's: tests/selftest/devices/update.)
# QEMU hands NovaOS an ACPI WDAT table describing the chipset's TCO
# watchdog (tools/mkwdat.py), as firmware does: `watchdog test` starts it
# through the table and lets it run out, and it resets the PC.  Last,
# Reset this PC (systemreset /keep, Settings > System > Recovery): a folder
# outside the profiles and a changed setting go, a file in Documents stays.
import os, re, sys, time

DOC = ("the boot loader's Advanced startup menu: safe mode, startup repair and the previous version, "
       'asked for with shutdown /r /o and coming up by itself after two failed starts; the firmware\'s WDAT watchdog '
       'resetting the PC; and Reset this PC keeping the files and accounts')

PREVIOUS = '0.0.9-test'
REPORT = r'C:\Windows\System32\LogFiles\Srt\SrtTrail.txt'


def _serial(nova):
    return open(nova.serial_path, 'rb').read().decode('latin-1')


def _wait(nova, text, timeout, since=0):
    end = time.time() + timeout
    while text not in _serial(nova)[since:]:
        if time.time() > end:
            raise RuntimeError(f'no "{text}" in {timeout} s')
        time.sleep(0.25)


def _menu(nova, keys, shot=None, ask=True):
    """Restart into Advanced startup (shutdown /r /o, unless the menu is
    coming by itself), press @keys there and wait for NovaOS to start"""
    nova.sr.read_new()
    seen = len(_serial(nova))
    if ask:
        nova.qmp.type('shutdown /r /o\n')
    _wait(nova, 'Choose how to start', 300, seen)
    time.sleep(1)
    if shot:
        nova.shot(os.path.join(getattr(sys.modules['__main__'], 'OUT', nova.work), shot))
    for k in keys:
        nova.qmp.key(k)
        time.sleep(0.5)
    nova.start(300)


def safe_mode(nova):
    _menu(nova, ('3', 'ret'), shot='advanced-startup.png')


def startup_repair(nova):
    seen = len(_serial(nova))
    _menu(nova, ('4', 'ret'))
    _wait(nova, '[REPAIR] Startup repair finished', 60, seen)


def previous_version(nova):
    _menu(nova, ('2', 'ret'))


def failed_starts(nova):
    """Two starts reset as soon as the kernel runs (they never reach the
    desktop), then the third shows the menu by itself: Enter takes the
    line it chose, startup repair"""
    nova.sr.read_new()
    nova.qmp.type('shutdown /r\n')
    for _ in range(2):
        seen = len(_serial(nova))
        _wait(nova, '=== Phase 1: Memory Manager ===', 300, seen)
        nova.qmp.cmd('system_reset')
    seen = len(_serial(nova))
    _menu(nova, ('ret',), ask=False)
    _wait(nova, '[REPAIR] Startup repair finished', 60, seen)


def watchdog_runs_out(nova):
    """The watchdog started through the WDAT table and never reloaded: the
    PC resets by itself"""
    nova.sr.read_new()
    seen = len(_serial(nova))
    nova.qmp.type('watchdog test\n')
    _wait(nova, '[WDAT] Watchdog running', 60, seen)
    nova.sr.read_new()
    nova.start(300)


def reset_pc(nova):
    """systemreset /keep: the next start resets, then restarts by itself"""
    nova.sr.read_new()
    seen = len(_serial(nova))
    nova.qmp.type('systemreset /keep\n')
    _wait(nova, '[RESET] Reset this PC finished: restarting', 600, seen)
    nova.sr.read_new()
    nova.start(300)


def E(v):
    return re.escape(v)


TESTS = [
    Test('safe mode', 'ver', [r'NovaOS \[Version'], builtin=True, before=safe_mode, timeout=60,
         boot_expect=[r'Advanced startup: Safe mode', r'\[RECOVERY\] Safe mode: only NovaOS',
                      r'\[DISPLAY\] Safe mode: no display adapter driver',
                      r'\[DISPLAY\] UEFI GOP framebuffer']),
    Test('startup repair', f'type {REPORT}',
         [r'The files of an unfinished update were removed\.', r'The boot loader \\EFI\\BOOT\\BOOTX64\.EFI: no problem found',
          r'The kernel \\EFI\\NOVA\\kernel\.elf: no problem found', r'Startup repair finished: 1 problem\(s\) found, 1 repaired'],
         builtin=True, before=startup_repair, timeout=60, boot_expect=[r'Advanced startup: Startup repair']),
    Test('previous version', 'ver', [rf'NovaOS \[Version {E(PREVIOUS)}\]'], builtin=True, before=previous_version,
         timeout=60,
         boot_expect=[r"Started as the previous version's boot loader: starting kernel\.old",
                      rf'\[RECOVERY\] Started the previous version of NovaOS \({E(PREVIOUS)}']),
    Test('this version again', 'shutdown /r', [r'\[version (?!0\.0\.9-test)'], reboot=True, timeout=600),
    Test('menu after failed starts', f'type {REPORT}',
         [r'Updates: nothing unfinished\.', r'The failed-start count was cleared\.',
          r'Startup repair finished: 0 problem\(s\) found'],
         builtin=True, before=failed_starts, timeout=60,
         boot_expect=[r"NovaOS didn't start correctly: the last 2 starts stopped before the desktop",
                      r'\[RECOVERY\] The last starts did not reach the desktop']),
    Test('count cleared', 'ver', [r'NovaOS \[Version'], builtin=True, timeout=60,
         before=lambda nova: _wait(nova, 'the failed-start count is cleared', 60)),
    Test('firmware watchdog', 'watchdog', [r'Hardware watchdog: ACPI WDAT: counts of 1200 ms, 4 to 1023, resets the PC'],
         builtin=True),
    Test('watchdog resets the PC', 'ver', [r'NovaOS \[Version'], builtin=True, before=watchdog_runs_out, timeout=60,
         boot_expect=[r'\[WDAT\] Watchdog running \(the firmware\'s WDAT table\): the PC resets 6\.0 s after']),
    Test('a file to keep', r'echo kept through the reset > C:\Documents\resetkeep.txt', [], builtin=True, settle=2),
    Test('a folder to remove', r'mkdir C:\ResetJunk', [], builtin=True, settle=2),
    Test('a setting to undo', 'update channel http://10.0.2.2:1/reset-test/novaos-update.txt',
         [r'Update channel: http://10\.0\.2\.2:1/reset-test/'], builtin=True, settle=2),
    Test('reset this PC', r'type C:\Windows\Logs\SystemReset\ResetReport.txt',
         [r'Reset this PC \(keep my files\)', r'Removed: [1-9]\d* file\(s\) and folder\(s\)'],
         builtin=True, before=reset_pc, timeout=60,
         boot_expect=[r'\[RESET\] Resetting this PC', r'\[REG\] Reset to the defaults, keeping the accounts']),
    Test('files kept', r'type C:\Documents\resetkeep.txt', [r'kept through the reset'], builtin=True),
    Test('apps removed', r'dir C:\ResetJunk', [r'The system cannot find the path specified'], builtin=True),
    Test('settings undone', 'update channel', [r'Update channel: (?!http://10\.0\.2\.2:1/)'], builtin=True),
]
