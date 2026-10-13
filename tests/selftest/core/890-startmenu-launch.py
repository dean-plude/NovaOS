# Programs started from the Start menu: winhello (a GUI program, PE
# subsystem 2) starts on its own with no Terminal window beside it, as
# Explorer starts one on Windows, and hello (a console program) still gets
# a Terminal for its console.  Both are found by typing their name after
# Win+S and started with Enter, then closed with Alt+F4.  Last in the
# suite: it moves the keyboard focus between windows.
import re, time

DOC = ('programs started from the Start menu (Win+S, the name, Enter): `winhello`, a GUI program, '
       'with no Terminal window, and `hello`, a console program, in a Terminal')


def _log(nova):
    return open(nova.serial_path, 'rb').read().decode('latin-1')


def _wait(nova, pattern, timeout=90):
    for _ in range(int(timeout / 0.25)):
        if re.search(pattern, _log(nova)):
            return True
        time.sleep(0.25)
    return False


def _start(nova, name):
    nova.qmp.key('meta_l', 's')
    time.sleep(2)
    nova.qmp.type(name)
    time.sleep(2)
    nova.qmp.key('ret')


def launch(nova):
    _start(nova, 'winhello')
    if _wait(nova, r'\[UM\] Started winhello\.exe'):
        time.sleep(4)                                   # (its window up and focused)
        nova.qmp.key('alt', 'f4')
        _wait(nova, r'\[UM\] winhello\.exe \(PID \d+\) exited', 30)
    time.sleep(2)
    _start(nova, 'hello')
    if _wait(nova, r'\[UM\] hello\.exe \(PID \d+\) exited'):
        time.sleep(2)
        nova.qmp.key('alt', 'f4')                       # (its Terminal)
    time.sleep(2)


TESTS = [
    Test('start menu launch', 'echo start-menu-launch-done', [r'start-menu-launch-done'], builtin=True,
         before=launch, timeout=60,
         boot_expect=[r'\[APPS\] winhello\.exe: a GUI program, started without a console',
                      r'\[UM\] Started winhello\.exe',
                      r'\[APPS\] hello\.exe: a console program, in a Terminal',
                      r'\[UM\] Started hello\.exe']),
]
