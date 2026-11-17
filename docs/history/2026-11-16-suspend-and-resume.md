## Packaged apps in the shell, second part: suspend and resume

The second part of item 2 of 0.2.3 "Nebula" ([plan](releases/0.2.3.md)).
A UWP app (one that runs its view through `CoreApplication.Run`, which on
Windows only a packaged app does) now goes through the lifetime Windows'
process lifetime manager gives it.  When its window is minimised,
`twinapi.appcore` raises `CoreApplication.EnteredBackground`, and a
second later, if the window is still minimised, `Suspending`: its
`SuspendingOperation` gives deferrals and a deadline five seconds on, and
the suspension waits, pumping the window's messages so work the app posts
to its dispatcher still runs, until every deferral is completed or the
deadline passes.  Restored, the app gets `Resuming` and then
`LeavingBackground` (with a deferral too), before `VisibilityChanged`, as
on Windows.  A window minimised and restored within the second is never
suspended, and one restored while its app is still saving resumes as
soon as the saving ends.

- **Tests:** `coreapptest` (core suite, 64- and 32-bit) minimises its
  CoreWindow, completes the `Suspending` deferral from another thread
  300 ms later, restores the window and checks the events' order
  (EnteredBackground, Suspending, the deferral, Resuming,
  LeavingBackground) and the deadline.
- **Left:** NovaOS does not freeze a suspended app's threads (the app has
  saved its state, which is what `Suspending` asks of it); XAML's own
  `Application.Suspending` and `Resuming` (its process lifetime handler,
  `PLMIntegration.cpp`, is not built yet) are still never raised.
