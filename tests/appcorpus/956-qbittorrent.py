# qBittorrent 5.0.2 (BitTorrent client, 64-bit, Qt 6) installed the way a
# user gets it: the App Store's download (its NSIS installer, put in
# C:\Downloads) run silently, as the Store's Install would run it with its
# defaults, into C:\Programs\qBittorrent.  qBittorrent then opens
# C:\Apps\data\corpus.torrent (a one-file torrent the corpus makes, no
# tracker).  Its first start shows the Legal Notice, which Enter accepts
# (OK); then the Add torrent dialog (the torrent's name, size and file
# list) comes up over the main window; the screenshot must match
# tests/reference/qbittorrent.png.  Nothing is downloaded: the corpus VM
# has no peers to reach.  Alt+F4 closes the dialog, a second one
# qBittorrent.  Windowed, takes the keyboard.
import time

SETUP = r'C:\Downloads\qbittorrent_5.0.2_x64_setup.exe'
QBT = r'C:\Programs\qBittorrent\qbittorrent.exe'


def accept_notice(nova, echo):
    """OK on the first start's Legal Notice, then time for the Add torrent
    dialog"""
    nova.keys('ret')
    time.sleep(90)
    return None


DOC = 'qBittorrent (a Qt 6 program: opens a .torrent)'

APP = App('qBittorrent', '5.0.2',
          'https://sourceforge.net/projects/qbittorrent/files/qbittorrent-win32/qbittorrent-5.0.2/qbittorrent_5.0.2_x64_setup.exe/download',
          'qBittorrent', [Test('install', rf'{SETUP} /S', timeout=600),
                          Test('open a .torrent', rf'start {QBT} {A}\data\corpus.torrent', timeout=240)],
          store='qBittorrent', gui=True, interact=accept_notice, closes=2)
