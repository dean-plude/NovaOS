## GUI programs start without a Terminal window

Dean's report from a real boot: starting a program from the Start menu
opened a Terminal window beside it.  Every program the shell started
(the Start menu, the App Store's **Open** and **Run**, File Explorer,
the desktop and shortcuts) ran through `TerminalRun`, so even a GUI
program got a Terminal, which only waited for it to exit.

- **The shell reads the PE subsystem** (`kernel/apps/apps.c`,
  `UmIsGuiProgram` in `kernel/um/um.c`): a GUI program (subsystem 2,
  64- or 32-bit) starts on its own (`UmSpawnDetached`) by its full path in
  its own folder, as Explorer starts it, and logs
  `[APPS] NAME: a GUI program, started without a console`.  A console
  program still opens in a Terminal, its console.
- **`winhello` is a GUI program** (`"gui": true` in its
  `userland/programs/winhello.json` links it `/subsystem:windows`), so the
  self-test image has one.
- **Start menu search finds every program**: the menu kept only the first
  64 programs of `C:\Programs` (and the Start menu's shortcuts), so
  search missed the rest; it now keeps 256.
- **Test**: `tests/selftest/core/890-startmenu-launch.py` starts
  `winhello` and `hello` from the Start menu (Win+S, the name, Enter):
  the first with no Terminal, the second in one.
