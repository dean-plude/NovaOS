## Unelevated programs cannot change Windows or HKLM

Second part of roadmap step 33.2, which it finishes
([design](../design/uac.md)).  A program that is not elevated used to
write anywhere in `C:\Windows`, `C:\Programs` and `HKEY_LOCAL_MACHINE`,
since the root's permissions reached everything and registry keys had
none.  Now those folders carry Windows' own DACL (owned by
TrustedInstaller; Administrators full control, Users read and execute)
and a machine key opened for writing by a program that is not elevated
is refused with `ACCESS_DENIED`, while reading both works as before.
Elevated programs, services and the App Store's installers still change
them, and `C:\Windows\Temp`, the Task Scheduler's folder and the print
spool stay open to every user, as on Windows.  Installers elevate as
they do there: `msiexec` asks for administrator, an old 32-bit
`setup.exe` with no execution level in its manifest is taken for an
installer, and a portable program the Store unpacks may write its own
folder.

The Terminal has `sudo <command>`, as Windows 11 does: UAC asks, then the
program runs as administrator in the same window.  The self-tests that
change the machine are typed that way, and `uactest` now checks the
refusals for the limited administrator and a standard user and the
writes for an elevated program.
