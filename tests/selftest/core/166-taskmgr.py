# Task Manager (userland/programs/taskmgr.c): `start winhello` runs a known
# GUI program, then Ctrl+Shift+Esc opens Task Manager, which must list it
# among the processes (SystemProcessInformation); typing its name selects
# it and Delete ends it (OpenProcess + TerminateProcess), after which
# winhello must exit and drop out of the list.  Ctrl+Tab goes on to
# Performance, which must report the CPU (each logical processor, from
# SystemProcessorPerformanceInformation) and memory; Details and Services
# follow.  The screenshots of Processes and Performance are the reference
# pictures (docs/screenshots/task-manager.png and task-manager-performance.png);
# Alt+F4 closes it and gives the keyboard back to the Terminal.
import os, re, sys, time

DOC = ('Task Manager opened with Ctrl+Shift+Esc: `winhello` listed, selected by typing its name and ended '
       'with Delete; the Performance, Details and Services views')

state = {'why': 'Task Manager was not driven'}


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


def drive(nova):
    try:
        start = len(_log(nova))
        nova.qmp.type('start winhello\n')
        if not _wait(nova, r'\[UM\] Started winhello\.exe', start):
            raise AssertionError('winhello did not start')
        time.sleep(3)
        at = len(_log(nova))
        nova.qmp.key('ctrl', 'shift', 'esc')
        if not _wait(nova, r'\[APPS\] Ctrl\+Shift\+Esc: Task Manager', at, 10):
            raise AssertionError('Ctrl+Shift+Esc did nothing')
        if not _wait(nova, r'taskmgr: open \(\d+ logical processors', at):
            raise AssertionError('Task Manager did not open')
        m = _wait(nova, r'taskmgr: listed winhello\.exe \(PID (\d+)\)', at)
        if not m:
            raise AssertionError('Task Manager did not list winhello.exe')
        pid = m.group(1)
        time.sleep(3)                                   # (a few samples: CPU figures)
        nova.qmp.type('winhello')
        if not _wait(nova, r'taskmgr: selected winhello\.exe \(PID %s\)' % pid, at, 10):
            raise AssertionError('typing its name did not select winhello.exe')
        time.sleep(1)
        _shot(nova, 'task-manager.png')
        nova.qmp.key('delete')
        if not _wait(nova, r'taskmgr: ended winhello\.exe \(PID %s\)' % pid, at, 10):
            raise AssertionError('Delete did not end winhello.exe: ' +
                                 (_wait(nova, r'taskmgr: could not end[^\n]*', at, 1) or [''])[0])
        if not _wait(nova, r'\[UM\] winhello\.exe \(PID %s\) exited' % pid, at, 10):
            raise AssertionError('winhello.exe did not exit')
        if not _wait(nova, r'taskmgr: winhello\.exe \(PID %s\) is gone' % pid, at, 10):
            raise AssertionError('winhello.exe stayed in the list')
        time.sleep(3)
        nova.qmp.key('ctrl', 'tab')
        m = _wait(nova, r'taskmgr: performance: CPU (\d+)% on (\d+) logical processors, memory (\d+) MB of (\d+) MB in use', at, 10)
        if not m:
            raise AssertionError('Ctrl+Tab did not show Performance')
        if int(m.group(2)) < 1 or int(m.group(4)) < 64 or int(m.group(3)) < 1:
            raise AssertionError('Performance figures are wrong: ' + m.group(0))
        time.sleep(4)
        _shot(nova, 'task-manager-performance.png')
        nova.qmp.key('ctrl', 'tab')
        if not _wait(nova, r'taskmgr: view Details', at, 10):
            raise AssertionError('Ctrl+Tab did not show Details')
        nova.qmp.key('ctrl', 'tab')
        if not _wait(nova, r'taskmgr: \d+ services', at, 10):
            raise AssertionError('Ctrl+Tab did not show Services')
        state['why'] = None
    except AssertionError as e:
        state['why'] = str(e)
    nova.qmp.key('alt', 'f4')                          # (Task Manager: the keyboard back to the Terminal)
    _wait(nova, r'taskmgr: closed', 0, 10)
    time.sleep(2)


TESTS = [
    Test('taskmgr', 'echo task manager driven', [r'task manager driven'], builtin=True, before=drive,
         check=lambda nova: state['why']),
]
