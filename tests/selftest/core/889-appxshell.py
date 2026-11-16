# Packaged apps in the shell (0.2.3 "Nebula" item 2, part 1): appx installs the
# test package (C:\Tests\Appx\PackageTest.msix, tools/appx/mktest.py), whose
# application the Start menu lists from the package repository by its
# manifest's display name ("NovaOS Package Test", from resources.pri) with its
# tile logo; Win+S, its name and Enter start it, and it runs packaged (its
# program, appxtest.exe with no arguments, writes the application user model
# id it runs as to C:\Temp\appxtest-start.log).  Then Settings > Apps
# (ms-settings:appsfeatures) lists it and uninstalls it from the keyboard
# (Down chooses it, Delete asks, Enter confirms), which runs "appx remove".
# Near the end of the suite: it moves the keyboard focus between windows.
import re, time

DOC = ('packaged apps in the shell: `appx` installs the test package, the Start menu lists its application '
       'by the manifest\'s name and tile logo and starts it packaged (Win+S, the name, Enter), and Settings > Apps '
       'lists the package and uninstalls it (Down, Delete, Enter)')

AUMID = r'NovaOS\.PackageTest_rtg2wbn51qgvw!App'
FULL = r'NovaOS\.PackageTest_1\.0\.0\.0_x64__rtg2wbn51qgvw'


def _log(nova):
    return open(nova.serial_path, 'rb').read().decode('latin-1')


def _wait(nova, pattern, after, timeout=60):
    for _ in range(int(timeout / 0.25)):
        if re.search(pattern, _log(nova)[after:]):
            return True
        time.sleep(0.25)
    return False


def start_from_start(nova):
    """Win+S, "NovaOS Package", Enter: the packaged app starts in a Terminal
    of its own (a console program), which Alt+F4 closes again"""
    at = len(_log(nova))
    nova.qmp.key('meta_l', 's')
    time.sleep(2)
    nova.qmp.type('NovaOS Package')
    time.sleep(2)
    nova.qmp.key('ret')
    if _wait(nova, r'\[START\] NovaOS Package Test: a packaged app', at, 20):
        _wait(nova, r'\[UM\] appxtest\.exe \(PID \d+\) exited', at, 60)
        time.sleep(2)
        nova.qmp.key('alt', 'f4')                       # (its Terminal)
    else:
        nova.qmp.key('esc')                             # (nothing started: clear the search, close the menu)
        time.sleep(0.5)
        nova.qmp.key('esc')
    time.sleep(2)


def uninstall_in_settings(nova):
    """ms-settings:appsfeatures, Down, Delete, Enter; Alt+F4 gives the keyboard back to the Terminal"""
    at = len(_log(nova))
    nova.qmp.type('start ms-settings:appsfeatures\n')
    if _wait(nova, r'\[SETTINGS\] Page: Apps', at, 30):
        time.sleep(1)
        nova.qmp.key('down')
        time.sleep(0.5)
        nova.qmp.key('delete')
        if _wait(nova, r'\[SETTINGS\] Apps: uninstall ' + FULL + r'\?', at, 10):
            time.sleep(0.5)
            nova.qmp.key('ret')
            _wait(nova, r'\[SETTINGS\] Apps: ' + FULL + r' (not )?uninstalled', at, 90)
    time.sleep(1)
    nova.qmp.key('alt', 'f4')
    time.sleep(1)


TESTS = [
    Test('appx shell install', r'appx install C:\Tests\Appx\PackageTest.msix -AllowUnsigned',
         [r'Installed ' + FULL], elevated=True),
    Test('appx start menu', r'type C:\Temp\appxtest-start.log', [r'appxtest: started as ' + AUMID], builtin=True,
         before=start_from_start, timeout=60,
         boot_expect=[r'\[START\] NovaOS Package Test: a packaged app']),
    Test('appx settings uninstall', 'echo settings-apps-done', [r'settings-apps-done'], builtin=True,
         before=uninstall_in_settings, timeout=60,
         boot_expect=[r'\[SETTINGS\] Page: Apps', r'\[SETTINGS\] Apps: ' + FULL + r' uninstalled \(0\)']),
]
