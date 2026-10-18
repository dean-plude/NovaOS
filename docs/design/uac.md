# Standard users and UAC, the Windows way (roadmap step 33.2)

Status: done (2026-10-18), in two parts.  The consent prompt on the
secure desktop, the policy that decides whether it asks and integrity
levels on tokens and processes came first; the second part keeps
programs that are not elevated from changing `C:\Windows`, `C:\Programs`
and `HKLM` and adds the Terminal's `sudo`.  Windows' virtualization of
those writes for old programs and the rest under "Next parts" follow.
Accounts, tokens and sign-in are in [accounts.md](accounts.md).

**The decision.**  NovaOS elevates exactly where Windows does and asks
the way Windows asks: a program runs as administrator only after User
Account Control's prompt agreed, the prompt takes the screen so that no
program can answer it, and a process carries its token's integrity level
so that an unelevated one cannot reach into an elevated one.  Installers,
their UAC plug-ins and programs that check `TokenElevation` or
`TokenIntegrityLevel` see what they see on Windows.

## When it asks

| Windows | NovaOS |
|---|---|
| `ShellExecute(Ex)` with the `runas` verb (AppInfo service, `consent.exe`) | `shell32` starts the program suspended and calls `NtNovaAccount(NOVA_ACCOUNT_ELEVATE)`: the kernel asks, gives the process the elevated token on Yes and never hands the token to the caller.  No: `ERROR_CANCELLED` |
| A manifest's `requestedExecutionLevel` `requireAdministrator` | asks when the program starts (`kernel/um/um.c`, `um_spawn_image`).  No: `ERROR_ELEVATION_REQUIRED` |
| `highestAvailable` | asks an administrator; a standard user's program runs as it is |
| `ConsentPromptBehaviorAdmin` (5 by default: Yes or No; 0: no prompt; 1 and 3: credentials) | the same values, `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System` |
| `ConsentPromptBehaviorUser` (3 by default: an administrator's credentials; 0: refuse) | the same values |
| `EnableLUA` 0 (UAC off) | administrators elevate without a prompt |

An administrator's Yes gives the program the elevated half of its own
split token.  A standard user types an administrator's user name and
password; the program then runs as that administrator, elevated (a token
of its own, a new logon), as on Windows.  A wrong password, or one of an
account that is not an administrator, is refused with "The user name or
password is incorrect." and the next try waits a second.

Programs the App Store starts to install what it downloaded run as
administrator without a prompt, as before: the Store starts them from the
kernel, where nobody is asked (Windows' Store installs as the system too).

## The prompt and the secure desktop

`kernel/wm/consent.c` draws it over everything, as the sign-in screen is
drawn (`wm/logon.c`): the screen behind dimmed, "User Account Control",
"Do you want to allow this app to make changes to your device?", the
program's name, "Publisher: Unknown" (no signature is checked yet) and its
location, then Yes and No, or the user name and password boxes.  While it
shows, the desktop loop gives the keyboard and the mouse to it alone, and
only the real devices reach it: a program's `SendInput` goes to windows,
never to the prompt, which is what the secure desktop is for.  Alt+Y and
Alt+N answer, Esc is No, Tab moves between the boxes and buttons.

The asking thread (a program's `CreateProcess` or `ShellExecute`, or the
Terminal's worker starting a program) sleeps until the answer, with the
desktop lock let go.  The serial log says `[UAC] Consent prompt for
<path>` (or `Credential prompt`) once the dialog is on the screen and
`[UAC] <name>: allowed` or `refused` afterwards.  The self-test harness
(`tools/novarun.py`) presses Alt+Y at each consent prompt, as the person at
the keyboard would, for the tests that start installers; `uactest` answers
its own.

## Integrity levels

| Token | Level (`TokenIntegrityLevel`) |
|---|---|
| SYSTEM's (services, the kernel) | System, `S-1-16-16384` |
| An elevated administrator's | High, `S-1-16-12288` |
| An administrator's limited half, a standard user's | Medium, `S-1-16-8192` |

Every process has its token's level and Windows' mandatory label
`NO_WRITE_UP | NO_READ_UP`.  A thread of a lower level that opens a
process or a thread of a higher level (`NtOpenProcess`, `NtOpenThread`,
`NtDuplicateObject`) gets `ACCESS_DENIED` unless it asks for no more than
`SYNCHRONIZE` and `PROCESS_QUERY_LIMITED_INFORMATION`
(`THREAD_QUERY_LIMITED_INFORMATION`); such a handle can wait on the process
and read its exit code and basic information, and every other use (memory,
threads, ending it, its token's process, duplicating it into more) is
refused.  The serial log says `[SEC] Integrity: <program> (level 2000) may
not open process <pid> (level 3000) ...`.

## The system's folders and keys

A program that is not elevated (an administrator's limited half, or a
standard user's) may read `C:\Windows`, `C:\Programs` (Program Files)
and `HKEY_LOCAL_MACHINE` but not change them; an elevated program,
services (SYSTEM) and the kernel may.  The refusal is Windows' own:
`ACCESS_DENIED` (`ERROR_ACCESS_DENIED`), from the same checks.

| Windows | NovaOS |
|---|---|
| `C:\Windows`, `C:\Program Files`: owner TrustedInstaller; TrustedInstaller, SYSTEM, Administrators full control, Users read and execute, protected from C:\'s | the same DACL for `C:\Windows` and `C:\Programs` when they have no descriptor of their own (`kernel/fs/fsec.c`), inherited by everything in them; a limited token has Administrators only for denying, so only Users' read and execute apply |
| `C:\Windows\Temp`, `System32\Tasks`, `System32\spool\PRINTERS`: users may create files | the same, as C:\ has it (Authenticated Users change); also `System32\CertStore`, crypt32's stores, the user's ROOT store among them (Windows keeps it in HKCU) |
| `HKLM` keys: SYSTEM and Administrators full control, Users read | `kernel/um/um_registry.c`: an open or create asking for a write right (`KEY_SET_VALUE`, `KEY_CREATE_SUB_KEY`, `DELETE`, ...) below `\Registry\Machine` by a caller that is not elevated is refused and a missing key is not made; `MAXIMUM_ALLOWED` and read opens give a handle that cannot set or delete values, delete or rename the key |
| The Task Scheduler service writes its cache, lsass the machine's DPAPI key, the kernel the time zone (SeTimeZonePrivilege, every user's) | `taskschd.dll`, `crypt32` and `kernel32` write those keys in the program, so `...\Schedule\TaskCache`, `...\Cryptography\Protect` and `TimeZoneInformation` stay open to everyone |

The serial log says `[SEC] Registry: <program> (not elevated) may not
change machine key <name>` for each refused open.  `kernel32`'s
`RegOpenKey`, `RegCreateKey`, the predefined keys and the helpers that
open a subkey ask for `MAXIMUM_ALLOWED`, as Windows' do, so reading HKLM
works for everyone.  `oleaut32`'s `LoadTypeLib` still loads a library it
could not register (HKCR is HKLM's `SOFTWARE\Classes` here, with no
per-user merge yet); `advapi32`'s service queries read the state without
asking to write.

Who runs elevated without the person asking: the App Store's installers
and its 7-Zip unpacking (`UmSpawnElevated`, as Windows' Store installs as
the system).  An `.msi` opened from File Explorer asks first (Windows'
installer service elevates a per-machine install the same way), and so
does `msiexec.exe` typed in the Terminal: its manifest asks for
administrator, since every NovaOS install is per-machine (`ALLUSERS=1`)
and there is no installer service to elevate for it.  Windows' installer
detection is there too: a 32-bit program whose manifest states no
`requestedExecutionLevel` and whose name has `install`, `setup` or
`update` in it asks as if it said `requireAdministrator` (SuperTux's Inno
Setup 4 `setup.exe`; the serial log says `[UAC] NAME looks like an
installer`); NovaOS's own `setuptest` and `wvsetuptest` say `asInvoker`
to stay out of it.  A program the Store unpacks from an archive (a
portable one, such as ShareX, which keeps its settings beside it) gets a
folder in `C:\Programs` that users may change, as its installer's
users-modify permission would make it (`FsecShareFolder`).  In the
Terminal, `sudo <command>` asks and runs the command's program as
administrator in the same window, as Windows 11's `sudo` in its inline
mode; the self-tests that change the machine (`regtest`, the MSI,
service, printer, COM registration and driver tests, `reg add HKLM...`)
are typed that way (`Test(elevated=True)` in `tools/selftest.py`) and the
harness answers Yes, as one runs them on Windows.

## Next parts

1. Windows' UAC virtualization: an old 32-bit program (no
   `requestedExecutionLevel` in its manifest) that is not elevated has its
   writes to `C:\Windows`, `C:\Programs` and `HKLM\SOFTWARE` sent to
   `C:\Users\<name>\AppData\Local\VirtualStore` and
   `HKCU\Software\Classes\VirtualStore` instead of refused; HKCR merging
   `HKCU\Software\Classes` for per-user COM registration; security
   descriptors on registry keys (`RegSetKeySecurity`) in place of the
   fixed rule.
2. `TokenLinkedToken` at identification level for a caller that is not
   elevated (it is a usable token today; `shell32` no longer uses it, but
   `acltest` and `advapi32`'s service start do, and need a kernel path of
   their own first).
3. `CreateProcess` of a `requireAdministrator` program failing with
   `ERROR_ELEVATION_REQUIRED` without a prompt, as Windows does, with
   `cmd`, Explorer and the Terminal retrying through `ShellExecute`.
4. The publisher from the program's Authenticode signature
   (`kernel/nt/ci.c` checks driver signatures already), a blue header for a
   verified one, and Settings, Accounts choosing an account's type.
