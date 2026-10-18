# accounttest: local accounts and sign-in (kernel/um/um_accounts.c, wm/logon.c; tools/selftest.py's core suite).
# Half-way it locks the screen with a password on the first account: the test types a wrong one
# (refused, "The password is incorrect"), then the right one, which unlocks it.  The screenshot is
# the lock screen, as the test found it.
import time


def unlock(nova):
    """At the lock screen: a wrong password, then (after the second it waits) the right one"""
    time.sleep(1)
    nova.qmp.type('wrong-one\n')
    time.sleep(4)
    nova.qmp.type('Unlock-33\n')


DOC = ('`accounttest` (local accounts: NetUserAdd/Enum/GetInfo/SetInfo/ChangePassword/Del, LogonUser refusing a '
       'wrong password and signing in with the right one, the account\'s own SID, token, profile in C:\\Users, '
       'HKEY_CURRENT_USER and environment, CreateProcessAsUser, DeleteProfile; the lock screen refusing a wrong '
       'password and unlocking with the right one)')
TESTS = [
    Test('accounttest', 'accounttest', [r'accounttest: \d+ passed, 0 failed'],
         timeout=300, shot=r'accounttest: the screen is locked',
         acts=[(r'accounttest: the screen is locked', unlock)],
         boot_expect=[r'\[LOGON\] Wrong password for ', r'\[LOGON\] .+ unlocked the screen']),
]
