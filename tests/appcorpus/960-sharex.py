# ShareX 19.0.1 (screen capture, 64-bit, Windows Forms on .NET 9) installed
# the way a user gets it: the App Store's portable zip, which carries its
# own .NET 9 (self-contained: no runtime from the Store), unpacked into
# C:\Programs\ShareX.  ShareX registers Print Screen as a hot key
# (RegisterHotKey); once its main window is up the test presses Print
# Screen, and ShareX captures the whole screen and saves the picture as a
# PNG under its portable folder (C:\Programs\ShareX\ShareX\Screenshots\
# YYYY-MM); the main window, its list showing the new capture, must match
# tests/reference/sharex.png.  Alt+F4 sends ShareX to the tray, so it is
# stopped when the test moves on, and the PNG must be there.  ShareX's
# default upload of a capture fails: the corpus VM has no network.
# Windowed, takes the keyboard.
import time


def print_screen(nova, echo):
    """Print Screen, ShareX's hot key for a capture of the whole screen"""
    nova.keys('print')
    time.sleep(60)                      # the capture, saved and listed
    return None


DOC = 'ShareX (Windows Forms on .NET 9: captures the screen to a PNG)'
SHAREX = r'C:\Programs\ShareX'

APP = App('ShareX', '19.0.1',
          'https://github.com/ShareX/ShareX/releases/download/v19.0.1/ShareX-19.0.1-portable.zip',
          'ShareX', [Test('install from the App Store', 'store install ShareX', store='ShareX', timeout=1200),
                     Test('capture the screen', rf'start {SHAREX}\ShareX.exe', timeout=420),
                     Test('saved capture', rf'cmd.exe /c "dir /s /b {SHAREX}\ShareX\Screenshots"',
                          [r'Screenshots\\\d{4}-\d{2}\\[^\r\n]+\.png'])],
          store='ShareX', gui=True, interact=print_screen)
