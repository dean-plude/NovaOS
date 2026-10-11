# Teeworlds 0.7.5 joins a game on its own server: the App Store installs
# it (OpenGL 1.2 comes from the Mesa built into the image), its dedicated server
# (teeworlds_srv.exe, from the same zip) is opened from the Terminal and
# loads its first map (dm1) on UDP port 8303, over IPv4 alone since its
# IPv6 socket on [::] is IPv6-only (IPV6_V6ONLY, on by default as on
# Windows), and the game starts in full screen with "connect
# 127.0.0.1:8303" on its command line (Teeworlds runs its arguments as
# console commands).  The connection goes over NovaOS's loopback interface;
# the game loads the map from the server and puts its tee in it: the
# server must log "player has entered the game" (programs' console output
# reaches NovaOS's serial log) and neither end may drop the connection for
# the next 20 s.  Where the tee lands differs from run to run, so there is
# no reference screenshot (App(reference=False)): the screenshot is only
# kept.  The server must still be running when the game has closed
# (taskkill).  Under QEMU's TCG the join times out (Mesa compiles its
# shaders for longer than the ten seconds both ends wait); with KVM, as
# the nightly corpus runs, it does not.
import re
import time

GAME = r'C:\Programs\Teeworlds\teeworlds-0.7.5-win64'

DOC = "Teeworlds joining a game on its own server (teeworlds_srv.exe on the same machine, over the loopback interface) and staying in it"

ENTERED = re.compile(r'teeworlds_srv\.exe[^\n]*\[server\]: player has entered the game')
DROPPED = re.compile(r'teeworlds_srv\.exe[^\n]*\[server\]: client dropped[^\n]*|'
                     r'teeworlds\.exe[^\n]*\[client\]: offline error[^\n]*')


def serial(nova):
    """NovaOS's serial log so far (read without taking it from the corpus's
    own reading)"""
    with open(nova.sr.path, 'rb') as f:
        return f.read().decode('utf-8', 'replace')


def join(nova, echo):
    """The server takes the player into the game, and keeps it"""
    for _ in range(60):
        log = serial(nova)
        if ENTERED.search(log) or DROPPED.search(log):
            break
        time.sleep(2)
    entered = ENTERED.search(log)
    if not entered:
        dropped = DROPPED.search(log)
        return f'it did not join the server ({dropped.group(0).strip()})' if dropped else \
            'the server never took the player into the game'
    time.sleep(20)
    dropped = DROPPED.search(serial(nova), entered.end())
    return f'it left the game ({dropped.group(0).strip()})' if dropped else None


APP = App('Teeworlds (own server)', '0.7.5',
          'https://github.com/teeworlds/teeworlds/releases/download/0.7.5/teeworlds-0.7.5-win64.zip',
          'Teeworlds', [Test('install from the App Store', 'store install Teeworlds', store='Teeworlds', timeout=600),
                        Test('its folder', rf'cd {GAME}', builtin=True),
                        Test('open its server', 'open teeworlds_srv.exe', builtin=True, settle=15),
                        Test('join the server', 'start teeworlds.exe "connect 127.0.0.1:8303"', timeout=60),
                        Test('server still running', 'taskkill /IM teeworlds_srv.exe',
                             [r'(?i)SUCCESS: Sent termination signal to the process "teeworlds_srv\.exe"'], builtin=True),
                        Test('back to Documents', r'cd C:\Documents', builtin=True)],
          store='Teeworlds', gui=True, interact=join, reference=False)
