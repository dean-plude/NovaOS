## Programs ask before they run as administrator

First part of roadmap step 33.2 ([design](design/uac.md)).  A program
whose manifest asks for administrator, and ShellExecute's `runas`, used to
get the elevated half of the administrator's token at once, as if UAC
were set to "never notify".  Now the screen dims and User Account
Control's prompt asks first (`kernel/wm/consent.c`, Windows'
`consent.exe`): an administrator answers Yes or No, a standard user types
an administrator's user name and password, and a wrong one is refused.
No fails `runas` with `ERROR_CANCELLED` and an installer's start with
`ERROR_ELEVATION_REQUIRED`; the policy values Windows uses
(`ConsentPromptBehaviorAdmin`, `ConsentPromptBehaviorUser`, `EnableLUA`)
decide whether it asks.  Only the keyboard and the mouse reach the
prompt, never a program's input, as on the secure desktop.

Tokens now say their integrity level (system, high, medium), and a
process of a lower level cannot open one of a higher level for more than
waiting on it and asking its exit code: an elevated program cannot be
written, read or ended from an unelevated one.  The self-test harness
presses Alt+Y at each prompt for the tests that start installers, as the
person at the keyboard would; the new `uactest` answers its prompts
itself.
