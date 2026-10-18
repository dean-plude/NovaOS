# switchtest: Settings' Accounts page and fast user switching (kernel/apps/settings.c, wm/logon.c,
# um/um_accounts.c; tools/selftest.py's core suite).  The test does what a person would: it adds
# the account in Settings (Start, "add an account", the name and its password twice), locks the
# screen and switches to it (a wrong password first, refused), then switches back to the first
# account, whose switchtest kept running throughout.  The screenshot is the sign-in screen with
# both accounts, as the test found it.
import time

PASSWORD = 'Switch-33'


def add_in_settings(nova):
    """Start, "add an account": Settings opens on its Create-a-user form"""
    nova.qmp.key('meta_l', 's')
    time.sleep(2)
    nova.qmp.type('add an account')
    time.sleep(2)
    nova.qmp.key('ret')
    time.sleep(5)
    nova.qmp.type('novaswitch\t' + PASSWORD + '\t' + PASSWORD + '\n')
    time.sleep(8)                               # (made: hashing the password takes a moment)
    nova.qmp.key('alt', 'f4')                   # (Settings closes: the keys are the Terminal's again)


def to_second(nova):
    """Win+L, the other account (the last one, by RID: Up from the first), a wrong password"""
    nova.qmp.key('meta_l', 'l')
    time.sleep(3)
    nova.qmp.key('up')
    time.sleep(1)
    nova.qmp.type('wrong-one\n')


def right_password(nova):
    time.sleep(4)                               # (a second after a wrong one)
    nova.qmp.type(PASSWORD + '\n')


def back_to_first(nova):
    """Win+L on the second account's desktop, the first account (Down wraps to it), Sign in"""
    time.sleep(3)
    nova.qmp.key('meta_l', 'l')
    time.sleep(3)
    nova.qmp.key('down')
    time.sleep(1)
    nova.qmp.key('ret')


DOC = ('`switchtest` (Settings, Accounts: an account added there by the test, typing as a person would; '
       'fast user switching from the lock screen to it, a wrong password refused, while the first account\'s '
       'program keeps running; its profile and hive; back to the first account; signing the second out)')
TESTS = [
    Test('switchtest', 'switchtest', [r'switchtest: \d+ passed, 0 failed'],
         timeout=420, shot=r'switchtest: the sign-in screen is up',
         acts=[(r'switchtest: add the account in Settings', add_in_settings),
               (r'switchtest: switch to novaswitch', to_second),
               (r'switchtest: the sign-in screen is up', right_password),
               (r'switchtest: switch back', back_to_first)],
         boot_expect=[r'\[SETTINGS\] Added account novaswitch \(standard user\)',
                      r'\[LOGON\] Wrong password for novaswitch',
                      r'\[LOGON\] Switched to novaswitch \(.+ stays signed in\)',
                      r'\[LOGON\] novaswitch signed out']),
]
