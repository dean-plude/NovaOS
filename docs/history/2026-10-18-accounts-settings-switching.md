## Accounts in Settings, and fast user switching

Second part of roadmap step 33.1 ([design](../design/accounts.md)).
Accounts could be made only by programs (`NetUserAdd`), and the lock
screen offered only the account that locked it.  Now Settings has
Windows 11's Accounts page: your info with your picture, type and
password (change it, or pick a picture from the wallpaper folder), and
the other users, where an administrator adds an account (name, password
twice, Standard user or Administrator), changes an account's type or
removes it with its files.  The Start menu's account button opens a menu
with "Change account settings", Lock, Sign out and the other accounts.

Every program now runs in the sign-in of the account it started under,
and every window belongs to its program's sign-in.  Signing in as a
second account from the lock screen or the sign-in screen switches: the
first account's windows are put away with their programs still running,
and come back with their focus when it signs in again.  Sign-out (Start,
or `ExitWindowsEx` with `EWX_LOGOFF`) ends an account's programs and
closes its windows; an account that is signed in cannot be deleted.
Services run in no account's sign-in, so a sign-out leaves them alone.

The new core self-test `switchtest` does it as a person would: it adds
an account in Settings by typing into the form, switches to it from the
lock screen (a wrong password refused first) while its own program keeps
running, checks the new profile and hive, switches back and signs the
second account out.
