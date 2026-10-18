# The event log (userland/programs/eventlog.c, advapi32's eventlog.c,
# wevtapi): evtlogtest reports an event with ReportEvent from a registered
# source and reads it back with ReadEventLog and EvtQuery (with the
# boot, service, driver-failure and crash events NovaOS writes itself).
# Then Event Viewer is started from the Start menu (Win+S, "Event
# Viewer", Enter) and closed again, and "eventvwr --check" finds that
# event in the Application log, shows it (the screenshot is the reference
# picture, docs/screenshots/event-viewer.png), filters by level, saves the
# log as .evtx and .xml, and clears it.
import re, time

DOC = ('`evtlogtest` (ReportEvent, ReadEventLog, EvtQuery/EvtRender/EvtSubscribe/EvtExportLog; the boot, service, '
       'driver-failure and crash events) and Event Viewer from the Start menu finding its event')


def _wait(nova, pattern, timeout=60):
    for _ in range(int(timeout / 0.25)):
        if re.search(pattern, open(nova.serial_path, 'rb').read().decode('latin-1')):
            return True
        time.sleep(0.25)
    return False


def _from_start_menu(nova):
    nova.qmp.key('meta_l', 's')
    time.sleep(2)
    nova.qmp.type('Event Viewer')
    time.sleep(2)
    nova.qmp.key('ret')
    if _wait(nova, r'eventvwr: showing \d+ events in Application'):
        time.sleep(3)
        nova.qmp.key('alt', 'f4')
        _wait(nova, r'eventvwr: closed', 30)
    else:
        nova.qmp.key('esc')
        time.sleep(0.5)
        nova.qmp.key('esc')
    time.sleep(2)


TESTS = [
    Test('evtlogtest', 'evtlogtest', [r'evtlogtest: \d+ passed, 0 failed'], timeout=120, elevated=True),
    Test('event viewer start menu', 'eventvwr --check NovaEventTest 4321 "NovaOS event log test"',
         [r'eventvwr: \d+ passed, 0 failed',
          r'eventvwr: System: The Application log file was cleared\.'],
         timeout=120, before=_from_start_menu, shot=r'eventvwr: showing NovaEventTest event 4321',
         boot_expect=[r'command line: "C:\\Windows\\System32\\eventvwr\.exe"']),     # (the Start menu's launch)
]
