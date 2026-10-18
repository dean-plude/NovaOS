# Accounts and sign-in, the Windows way

Status: roadmap step 33.1, first part (2026-10-18).  Local accounts, their
tokens and profiles, `LogonUser`, the `NetUser*` calls, the sign-in screen
at boot and the lock screen are in.  The Settings page for accounts,
sign-out, fast user switching and moving the first account's files to
`C:\Users` are the next parts (below).  UAC's consent prompt is step 33.2
([uac.md](uac.md)).

**The decision.**  NovaOS keeps accounts where Windows keeps them and hands
programs the same objects Windows does: a SAM database in the registry, a
token per signed-in account with the account's SID and groups, a profile
under `C:\Users\<name>` with its own `HKCU` hive, and the documented Win32
calls on top (`LogonUser`, `NetUserAdd` and the rest of `netapi32`,
`LoadUserProfile`, `CreateEnvironmentBlock`, `LockWorkStation`).  A program
that manages accounts, starts processes as another user or reads its own
profile path works unchanged.

## Where accounts live

| What | Where (as on Windows) |
|---|---|
| Accounts | `HKLM\SAM\SAM\Domains\Account\Users\<RID as 8 hex digits>`: `Name`, `FullName`, `Flags` (administrator, disabled), `Hash` |
| Next RID | `HKLM\SAM\SAM\Domains\Account`, value `NextRid` (the first account is 1001) |
| Profiles | `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList\<SID>`, value `ProfileImagePath` |
| Last account signed in | `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\LogonUI` |
| A user's hive | `HKEY_USERS\<SID>` (`HKCU` for that user's processes) |

The SAM key cannot be opened from user mode by anyone, administrators
included; only the kernel's account code reads it, as on Windows only LSA
does.  A password is stored as an Argon2id hash (Monocypher, already in the
tree; 2 MiB, 3 passes, a 16-byte random salt), never in clear.  Account SIDs
are `S-1-5-21-1000-2000-3000-<RID>`, under the machine SID NovaOS already used.

The first account (RID 1001) is made from the name typed in Setup (the
Welcome app), is an administrator, and has no password until one is set.
Its profile stays at `C:\` for now, where the files of every install so far
are; accounts created after it get `C:\Users\<name>` with `Desktop`,
`Documents`, `Downloads`, `Music`, `Pictures`, `Videos` and `AppData`
(`Roaming`, `Local\Temp`, `LocalLow`), a protected DACL (SYSTEM,
Administrators and the user; the user owns it) and their own hive.

## Tokens

Every process gets its token from the account it runs as: the account's SID,
Everyone, Users, Interactive, Authenticated Users and a logon SID
(`S-1-5-5-0-<n>`).  An administrator's normal token has Administrators as a
deny-only group (Windows' filtered token, elevation type limited); a program
whose manifest asks for `requireAdministrator` gets the linked elevated
token once UAC's prompt agrees ([uac.md](uac.md)).  A standard user's token has elevation type default and
no Administrators group at all.  Creating, deleting and resetting another
account need an elevated administrator.

The desktop starts programs with the token of the account signed in (the
"session" account).  `LogonUser` returns a token for any account whose
password matches; `CreateProcessAsUser` with it starts a process as that
account, with its environment (`USERNAME`, `USERPROFILE`, `APPDATA`,
`LOCALAPPDATA`, `TEMP`) and `HKCU`.

## The sign-in and lock screen

Drawn by the shell over everything (Windows' LogonUI): the screen behind it
frosted, the time and date, the account's initial and name, and the
password box, or a Sign in button for an account without a password.  At
boot it shows only when it is needed: when the last account signed in has a
password, or when there is more than one account to choose from; a single
account without a password goes straight to the desktop, as every install
so far does.  Services (`services.exe`) wait for
the first sign-in.

Win+L and `LockWorkStation` lock the screen for the account signed in.
While it shows, no program gets the keyboard or the mouse.  A wrong
password says "The password is incorrect. Try again." and refuses the next
try for a second.

The kernel entry point is NovaOS's private `NtNovaAccount` call (list, get,
find, add, delete, set or change a password, logon, load or delete a
profile, lock, session); `advapi32`, `netapi32`, `userenv` and `user32`
build the Windows calls on it.

## Next parts

1. Settings, Accounts: add, remove and rename accounts, set a password or a
   PIN (the "Done when" creates the second account there).
2. Sign-out and fast user switching: a session per signed-in account, each
   with its own desktop and programs, the first account's apps still
   running while the second is signed in.
3. Per-user desktop settings (wallpaper, theme, Start pins) from the
   account's hive instead of one machine-wide set, and `NTUSER.DAT` per
   profile.
4. Move the first account's files to `C:\Users\<name>` with a junction at
   the old paths.

Step 33.2 adds the consent prompt on a secure desktop and integrity
levels to the tokens above ([uac.md](uac.md)); nothing here assumes the
user is an administrator.
