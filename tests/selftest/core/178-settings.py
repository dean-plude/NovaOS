# Settings (kernel/apps/settings.c): `start ms-settings:display` opens it on Display (Windows'
# ms-settings: names), Ctrl+F and "printer" + Enter in its sidebar's search box go to Printers,
# Alt+F4 closes it.  Then `settings check` draws every page through the window's own on_paint
# at three window sizes (400x300 with the slim sidebar, the default 860x580, 1200x720), at the
# page's top and scrolled to its end: each must draw something and its end must come into view,
# and every page must be found by its ms-settings: name and by searching for its name.
import re, time

DOC = ('Settings: `start ms-settings:display` opens Display, its search box (Ctrl+F, "printer", Enter) '
       'goes to Printers; `settings check` draws every page at three window sizes, at its top and '
       'scrolled to its end')


def _log(nova):
    return open(nova.serial_path, 'rb').read().decode('latin-1')


def _wait(nova, pattern, after, timeout=30):
    for _ in range(int(timeout / 0.25)):
        if re.search(pattern, _log(nova)[after:]):
            return True
        time.sleep(0.25)
    return False


def search_in_settings(nova):
    """ms-settings:display, then Ctrl+F, "printer", Enter; Alt+F4 gives the keyboard back to the Terminal"""
    start = len(_log(nova))
    nova.qmp.type('start ms-settings:display\n')
    if _wait(nova, r'\[SETTINGS\] Page: Display', start):
        time.sleep(1)
        nova.qmp.key('ctrl', 'f')
        time.sleep(0.5)
        nova.qmp.type('printer\n')
        _wait(nova, r'\[SETTINGS\] Page: Printers', start, 10)
    time.sleep(1)
    nova.qmp.key('alt', 'f4')
    time.sleep(1)


TESTS = [
    Test('settings search', 'echo settings searched', [r'settings searched'], builtin=True,
         before=search_in_settings,
         boot_expect=[r'\[SETTINGS\] Page: Display', r'\[SETTINGS\] Page: Printers']),
    Test('settings check', 'settings check', [r'settings check: \d+ passed, 0 failed'], builtin=True,
         timeout=240),
]
