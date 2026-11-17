## Packaged apps in the shell, third part: suspended apps frozen

A follow-up to item 2 of 0.2.3 "Nebula" ([plan](releases/0.2.3.md)).
Once a UWP app's `Suspending` handlers are done (their deferrals completed
or the deadline passed) and its window is still minimised, NovaOS now
freezes the app, as Windows' process lifetime manager does: every one of
its threads stops at its next return to user mode and sleeps there, so a
suspended app uses no processor time at all.  `twinapi.appcore` asks for
it with `NtNovaGuiCtl`'s new `CTL_FREEZE` (`kernel/um/um_gui.c`), which
refuses a window that is no longer minimised; the threads park in
`UmReturnToUser` (`kernel/um/um.c`), sleeping a few ticks at a time
rather than yielding.  Restoring the window thaws it: from the taskbar or
Alt+Tab the desktop does it before the window's `WM_SIZE` goes out, and
another program's `ShowWindow` of the window does it in `user32` before
its message is sent (nothing in a frozen app would take it).  The app
then resumes (`Resuming`, `LeavingBackground`) as before.  Ending the
process (Settings > Apps, Task Manager) still ends a frozen app.  The
serial log says `frozen: suspended` and `thawed (...)` for each.

- **Tests:** `coreapptest` (core suite, 64- and 32-bit) starts a second
  copy of itself that watches a counter one of the app's threads moves
  every 10 ms through a shared section: once the app is suspended the
  counter stands still, the second process restores the window, the
  counter moves again, and the app gets `Resuming` and
  `LeavingBackground`.
- **Left:** XAML's own `Application.Suspending` and `Resuming` (its
  process lifetime handler, `PLMIntegration.cpp`, is not built yet).
