# Standard users and UAC, the Windows way (roadmap step 33.2)

Status: first part (2026-10-18).  The consent prompt on the secure
desktop, the policy that decides whether it asks, and integrity levels on
tokens and processes are in; protecting `C:\Windows`, `C:\Programs` and
`HKLM` from unelevated programs is the next part (below).  Accounts,
tokens and sign-in are in [accounts.md](accounts.md).

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

## Next parts

1. Protection of the system's folders and keys: `C:\Windows` and
   `C:\Programs` get Windows' DACLs (SYSTEM, Administrators and
   TrustedInstaller full control, Users read and execute), `HKLM` keys get
   security descriptors checked on open, and Windows' UAC virtualization
   sends an unelevated old 32-bit program's writes there to
   `C:\Users\<name>\AppData\Local\VirtualStore` and
   `HKCU\Software\Classes\VirtualStore` instead.  The step's "Done when"
   needs this.
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
