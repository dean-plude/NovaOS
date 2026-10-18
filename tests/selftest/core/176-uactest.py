# uactest: standard users and UAC's prompt (kernel/um/um_security.c, wm/consent.c; tools/selftest.py's core suite).
# The test answers the prompts itself, as the person at the keyboard: No twice, then Yes, then for the standard
# user's installer a wrong administrator password (refused) and the right one, the right one again for its
# "runas", and Yes once more.  The screenshot is taken as the first prompt shows.
import re
import time


def _log(nova):
    with open(nova.serial_path, 'rb') as f:
        return f.read().decode('latin-1')


def _after(nova, start, pattern, timeout=180):
    """Where @pattern ends in the serial log, after offset @start (once it shows)"""
    rx, end = re.compile(pattern), time.time() + timeout
    while time.time() < end:
        m = rx.search(_log(nova), start)
        if m:
            return m.end()
        time.sleep(0.25)
    print(f'uactest: no {pattern!r} in the serial log', flush=True)
    return None


_seen = [None]          # where the last prompt this test answered was answered (the serial log's offset)


def _start(nova):
    """Where to look for the next prompt: after the last one answered (the
    program's "prompt N" line can reach the log after the kernel's prompt line)"""
    if _seen[0] is None or _seen[0] > len(_log(nova)):
        _seen[0] = max(0, _log(nova).rfind('command line: uactest'))
    return _seen[0]


def _answer(keys):
    def act(nova):
        at = _after(nova, _start(nova), r'\[UAC\] Consent prompt for [^\n]*(uactest|setupadmin)')
        if at is None:
            return
        time.sleep(1)
        nova.qmp.key(*keys)
        _seen[0] = _after(nova, at, r'\[UAC\] [^\n]*: (allowed|refused)\b') or at
    return act


def _standard_user(nova):
    """Yes for "uactest admin"; then the standard user's two credential prompts"""
    steps = [(r'\[UAC\] Consent prompt for [^\n]*uactest', lambda: nova.qmp.key('alt', 'y')),
             (r'\[UAC\] Credential prompt for [^\n]*setupadmin', lambda: nova.qmp.type('uacadmin\tWrong-Pass-1\n')),
             (r'\[UAC\] Wrong administrator name or password', lambda: (time.sleep(1), nova.qmp.type('Admin-Pass-33\n'))),
             (r'\[UAC\] Credential prompt for [^\n]*uactest', lambda: nova.qmp.type('uacadmin\tAdmin-Pass-33\n')),
             (r'\[UAC\] uacadmin approved uactest', lambda: None)]
    at = _start(nova)
    for pattern, do in steps:
        at = _after(nova, at, pattern)
        if at is None:
            return
        time.sleep(1)
        do()
    _seen[0] = at


DOC = ('`uactest` (standard users and UAC: the consent prompt on the secure desktop for runas and for an installer\'s '
       'manifest, No refused, Yes elevating to high integrity; a standard user\'s credential prompt refusing a wrong '
       'administrator password and running the installer and runas as the administrator; a medium-integrity process '
       'kept from writing, reading or ending a high-integrity one; programs that are not elevated refused changes to '
       '`C:\\Windows`, `C:\\Programs` and `HKLM`, which they read, and the elevated one making them)')
TESTS = [
    Test('uactest', 'uactest', [r'uactest: \d+ passed, 0 failed'],
         timeout=600, uac_auto=False, shot=r'uactest: prompt 1',
         acts=[(r'uactest: prompt 1', _answer(('alt', 'n'))),
               (r'uactest: prompt 2', _answer(('alt', 'n'))),
               (r'uactest: prompt 3', _standard_user),
               (r'uactest: prompt 4', _answer(('alt', 'y')))],
         boot_expect=[r'\[UAC\] Wrong administrator name or password', r'\[SEC\] Integrity: uactest\.exe',
                      r'\[SEC\] Registry: uactest\.exe \(not elevated\) may not change machine key']),
]
