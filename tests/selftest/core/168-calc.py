# Calculator (userland/programs/calc.cpp, on Windows Calculator's own
# CalcManager engine in third_party/calcmanager): started from the Start
# menu (Win+S, "Calculator", Enter), it must compute typed expressions as
# Windows' Calculator does.  Standard executes each operator at once
# (2+3*4 = 20); Scientific keeps precedence (2+3*4 = 14), raises powers
# and takes 25! exactly (Ratpack's rational arithmetic); Programmer shows
# 255 as FF in hexadecimal and ANDs it with F.  The screenshots of the
# three calculators are the reference pictures (docs/screenshots/
# calculator*.png); Alt+F4 closes it and gives the keyboard back to the
# Terminal.
import os, re, sys, time

DOC = ('Calculator (Windows Calculator\'s CalcManager engine) from the Start menu: Standard, Scientific '
       '(precedence, x^y, 25!) and Programmer (hexadecimal, AND) answers to typed expressions')

state = {'why': 'Calculator was not driven'}


def _log(nova):
    return open(nova.serial_path, 'rb').read().decode('latin-1')


def _wait(nova, pattern, after=0, timeout=30):
    for _ in range(int(timeout / 0.25)):
        m = re.search(pattern, _log(nova)[after:])
        if m:
            return m
        time.sleep(0.25)
    return None


def _shot(nova, name):
    nova.shot(os.path.join(getattr(sys.modules['__main__'], 'OUT', '.'), name))


def _expect(nova, keys, pattern, what):
    at = len(_log(nova))
    nova.qmp.type(keys)
    if not _wait(nova, pattern, at, 20):
        last = re.findall(r'calc: [^\n]*display[^\n]*', _log(nova)[at:])
        raise AssertionError(f'{what}: expected /{pattern}/, Calculator showed ' +
                             (last[-1] if last else 'nothing'))


def drive(nova):
    try:
        start = len(_log(nova))
        nova.qmp.key('meta_l', 's')
        time.sleep(2)
        nova.qmp.type('Calculator')
        time.sleep(2)
        nova.qmp.key('ret')
        if not _wait(nova, r'\[UM\] Started calc\.exe', start, 60):
            raise AssertionError('the Start menu did not start calc.exe')
        if not _wait(nova, r'calc: open', start, 60):
            raise AssertionError('Calculator did not open')
        time.sleep(3)                                   # (its window up and focused)
        _expect(nova, '12+30=', r'calc: Standard display 42 after 12 \+ 30 =', 'Standard 12+30=')
        nova.qmp.key('esc')
        _expect(nova, '2+3*4=', r'calc: Standard display 20 after ', 'Standard 2+3*4= (no precedence)')
        nova.qmp.key('esc')
        _expect(nova, '1/7=', r'calc: Standard display 0\.1428571428571429 after ', 'Standard 1/7=')
        time.sleep(1)
        _shot(nova, 'calculator.png')

        at = len(_log(nova))
        nova.qmp.key('alt', '2')
        if not _wait(nova, r'calc: mode Scientific', at, 10):
            raise AssertionError('Alt+2 did not open Scientific')
        _expect(nova, '2+3*4=', r'calc: Scientific display 14 after 2 \+ 3 ', 'Scientific 2+3*4= (precedence)')
        nova.qmp.key('esc')
        _expect(nova, '2^10=', r'calc: Scientific display 1,024 after 2 \^ 10 =', 'Scientific 2^10=')
        nova.qmp.key('esc')
        _expect(nova, '25!', r'calc: Scientific display 15,511,210,043,330,985,984,000,000', 'Scientific 25!')
        time.sleep(1)
        _shot(nova, 'calculator-scientific.png')

        at = len(_log(nova))
        nova.qmp.key('alt', '4')
        if not _wait(nova, r'calc: mode Programmer', at, 10):
            raise AssertionError('Alt+4 did not open Programmer')
        _expect(nova, '255', r'calc: Programmer display 255', 'Programmer 255')
        at = len(_log(nova))
        nova.qmp.key('f5')
        if not _wait(nova, r'calc: Programmer display FF', at, 10):
            raise AssertionError('F5 did not show 255 as FF')
        _expect(nova, '&F=', r'calc: Programmer display F after FF AND F =', 'Programmer FF AND F')
        nova.qmp.type('FF')
        time.sleep(1)
        _shot(nova, 'calculator-programmer.png')
        state['why'] = None
    except AssertionError as e:
        state['why'] = str(e)
    nova.qmp.key('alt', 'f4')                          # (Calculator: the keyboard back to the Terminal)
    _wait(nova, r'calc: closed', 0, 10)
    time.sleep(2)


TESTS = [
    Test('calc', 'echo calculator driven', [r'calculator driven'], builtin=True, before=drive,
         check=lambda nova: state['why']),
]
