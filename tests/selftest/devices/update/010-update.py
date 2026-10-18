# Updating NovaOS (kernel/fs/update.h): the boot disk is build/nova.img, an
# installed NovaOS of this build's version, with a network on which
# tools/mkupdate.py's update channels are served (update_boot in
# tools/selftest.py): v1/ is this very build stamped one version newer (a
# "0.1.1-test" for a 0.1.0 build), v2/ one newer again.  NovaOS finds v1's
# update, downloads and stages it, restarts into it (the boot loader starts
# kernel.new once and the new kernel makes it the installed one), and
# restarts cleanly again.  Then v2's update is staged, and the machine is
# reset while the new kernel is still starting: the next start goes back
# to the v1 kernel and throws the update away.  Last, broken/ is a v3
# whose kernel is stamped to stop before it reaches the desktop
# (tools/mkupdate.py --fail-boot panic): restarting into it, the machine
# restarts on its own after the stop and comes back as v1, with no reset
# from outside (docs/design/recovery.md).  hung/ is a v4 that hangs there
# with interrupts off (--fail-boot hang): the chipset's watchdog (QEMU's
# ICH9 TCO timer) resets it.  stalled/ is a v5 whose desktop never starts
# (--fail-boot stall): the update's watching thread restarts it once it
# has made no progress for UPDATE_STALL_SECONDS.  Both come back as v1.  Every channel is signed with the
# self-tests' key (005-signature.py).
#
# Version order around the rolling build's channel (docs/updates.md): first
# a channel of this very version (same/) is up to date and a development
# build of it (dev/, "0.1.0+dev.<time>" as .github/workflows/ci.yml stamps
# main's) is newer; once v1 ("0.1.1-test") is installed, dev/ is older.
import os, re, time

DOC = ('an installed NovaOS updating itself from an update channel to a newer test build, restarting into it '
       'twice, and going back to that version when the next update is reset while it first starts, or stops, '
       'hangs or stalls while it first starts')


def _version():
    """The built kernel's stamped version (kernel/ke/version.c), and the next five"""
    try:
        data = open(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', 'build', 'kernel.elf'), 'rb').read()
        at = data.index(b'NovaOS-version-stamp:') + 21
        ver = data[at:at + 27].split(b'\0')[0].decode()
    except (OSError, ValueError):
        ver = '0.1.0'
    nums = [int(x) for x in re.match(r'[0-9.]+', ver).group(0).strip('.').split('.')]
    nums += [0] * (3 - len(nums))
    return (ver,) + tuple('.'.join(map(str, nums[:2] + [nums[2] + n])) + '-test' for n in (1, 2, 3, 4, 5))


VER, V1, V2, V3, V4, V5 = _version()
DEV = VER + '+dev.20261004125600'
CHANNEL = 'http://10.0.2.2:18090/{}/novaos-update.txt'


def _serial(nova):
    return open(nova.serial_path, 'rb').read().decode('latin-1')


def reset_while_trying(nova):
    """Restart into the staged update and reset the machine as soon as the
    new kernel says it is starting (before it reaches the desktop and
    finishes the update), then wait for the next start"""
    nova.sr.read_new()
    seen = len(_serial(nova))
    nova.qmp.type('shutdown /r\n')
    end = time.time() + 300
    while "This is the update's first start" not in _serial(nova)[seen:] and time.time() < end:
        time.sleep(0.05)
    nova.qmp.cmd('system_reset')
    nova.start()


def wait_serial(nova, regex, timeout, since=0):
    """Until the serial log after @since matches @regex (False: it didn't in time)"""
    end = time.time() + timeout
    while not re.search(regex, _serial(nova)[since:]):
        if time.time() > end:
            return False
        time.sleep(0.25)
    return True


def finished(nova):
    """The update's first start finishes it once the desktop has been up a
    moment (kernel/fs/update.h): wait for that before looking"""
    wait_serial(nova, r'\[UPDATE\] Updated NovaOS from', 60)


def stall_then_back(nova):
    """Restart into the staged update whose desktop never starts, wait for
    its watching thread to give up on it, then for the next start"""
    nova.sr.read_new()
    seen = len(_serial(nova))
    nova.qmp.type('shutdown /r\n')
    wait_serial(nova, r'made no progress for \d+ s while starting', 600, seen)
    nova.sr.read_new()
    nova.start()


def E(v):
    return re.escape(v)


TESTS = [
    Test('same channel', f'update channel {CHANNEL.format("same")}', [r'Update channel: http://10\.0\.2\.2:18090/same/'],
         builtin=True),
    Test('same version up to date', 'update', [rf'NovaOS {E(VER)} is up to date'], builtin=True, timeout=120),
    Test('dev channel', f'update channel {CHANNEL.format("dev")}', [r'Update channel: http://10\.0\.2\.2:18090/dev/'],
         builtin=True),
    Test('dev build newer', 'update', [rf'NovaOS {E(DEV)} is available \('], builtin=True, timeout=120),
    Test('update channel', f'update channel {CHANNEL.format("v1")}', [r'Update channel: http://10\.0\.2\.2:18090/v1/'],
         builtin=True),
    Test('update check', 'update', [rf'NovaOS {E(V1)} is available \(', r"Type 'update install'"], builtin=True, timeout=120,
         boot_expect=[r"\[UPDATE\] The channel's signature is good \(key 8cafb488b4fdb69e"]),
    Test('update install', 'update install', [rf'NovaOS {E(V1)} is ready: restart'], builtin=True, timeout=900,
         boot_expect=[rf'\[UPDATE\] NovaOS {E(V1)} is staged']),
    Test('restart into it', 'shutdown /r', [rf"\[UPDATE\] This is the update's first start \(NovaOS {E(V1)}",
                                             rf'\[version {E(V1)}\]'], reboot=True, timeout=600),
    Test('updated', 'ver', [rf'NovaOS \[Version {E(V1)}\]'], builtin=True, before=finished,
         boot_expect=[rf'\[UPDATE\] Updated NovaOS from {E(VER)} to {E(V1)}']),
    Test('restart again', 'shutdown /r', [rf'\[version {E(V1)}\]'], reboot=True, timeout=600),
    Test('up to date', 'update', [rf'NovaOS {E(V1)} is up to date'], builtin=True, timeout=120),
    Test('dev channel again', f'update channel {CHANNEL.format("dev")}',
         [r'Update channel: http://10\.0\.2\.2:18090/dev/'], builtin=True),
    Test('dev build older', 'update', [rf'NovaOS {E(V1)} is up to date'], builtin=True, timeout=120),
    Test('next update', f'update channel {CHANNEL.format("v2")}', [r'Update channel: http://10\.0\.2\.2:18090/v2/'],
         builtin=True),
    Test('stage it', 'update install', [rf'NovaOS {E(V2)} is ready: restart'], builtin=True, timeout=900),
    Test('reset while trying', 'ver', [rf'NovaOS \[Version {E(V1)}\]'], builtin=True, before=reset_while_trying,
         timeout=600,
         boot_expect=[r'\[UPDATE\] The boot loader started the previous kernel',
                      rf'\[UPDATE\] The update to NovaOS {E(V2)} did not finish starting, so NovaOS {E(V1)} started again']),
    Test('offered again', 'update', [rf'The update to NovaOS {E(V2)} did not start, so NovaOS went back to {E(V1)}',
                                     rf'NovaOS {E(V2)} is available \('], builtin=True, timeout=120),
    Test('broken channel', f'update channel {CHANNEL.format("broken")}',
         [r'Update channel: http://10\.0\.2\.2:18090/broken/'], builtin=True),
    Test('stage broken update', 'update install', [rf'NovaOS {E(V3)} is ready: restart'], builtin=True, timeout=900),
    # (the stop is a kernel panic on purpose: quoted, not this test's failure)
    Test('broken update rolls back', 'shutdown /r',
         [rf"\[UPDATE\] This is the update's first start \(NovaOS {E(V3)}", r'stamped with the test fault "panic"',
          rf'\[UPDATE\] The updated NovaOS {E(V3)} stopped while starting: restarting',
          rf'\[version {E(V1)}\]'], reboot=True, timeout=900, quotes_panic=True),
    Test('rolled back', 'ver', [rf'NovaOS \[Version {E(V1)}\]'], builtin=True,
         boot_expect=[rf'\[UPDATE\] The update to NovaOS {E(V3)} did not finish starting, so NovaOS {E(V1)} started again']),
    Test('hung channel', f'update channel {CHANNEL.format("hung")}',
         [r'Update channel: http://10\.0\.2\.2:18090/hung/'], builtin=True),
    Test('stage hung update', 'update install', [rf'NovaOS {E(V4)} is ready: restart'], builtin=True, timeout=900),
    Test('hung update rolls back', 'shutdown /r',
         [rf"\[UPDATE\] This is the update's first start \(NovaOS {E(V4)}", r'\[TCO\] Watchdog running',
          r'stamped with the test fault "hang"', rf'\[version {E(V1)}\]'], reboot=True, timeout=900),
    Test('back from the hang', 'ver', [rf'NovaOS \[Version {E(V1)}\]'], builtin=True,
         boot_expect=[rf'\[UPDATE\] The update to NovaOS {E(V4)} did not finish starting, so NovaOS {E(V1)} started again']),
    Test('stalled channel', f'update channel {CHANNEL.format("stalled")}',
         [r'Update channel: http://10\.0\.2\.2:18090/stalled/'], builtin=True),
    Test('stage stalled update', 'update install', [rf'NovaOS {E(V5)} is ready: restart'], builtin=True, timeout=900),
    Test('stalled update rolls back', 'ver', [rf'NovaOS \[Version {E(V1)}\]'], builtin=True, before=stall_then_back,
         timeout=900,
         boot_expect=[r'stamped with the test fault "stall"',
                      rf'\[UPDATE\] The updated NovaOS {E(V5)} made no progress for \d+ s while starting \(last step: ',
                      rf'\[UPDATE\] The update to NovaOS {E(V5)} did not finish starting, so NovaOS {E(V1)} started again']),
    Test('store updates', 'store updates', [r"Opened the App Store's updates"], builtin=True),
]
