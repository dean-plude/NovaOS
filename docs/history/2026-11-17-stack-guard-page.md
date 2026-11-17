## .NET's unhandled exceptions: a guard page on every thread's stack

An unhandled .NET exception printed only "Stack overflow." on NovaOS,
whatever the exception was, and the process ended.  Before it prints an
unhandled exception the .NET runtime checks that its thread still has a
stack guard page (`Thread::DetermineIfGuardPagePresent`: `VirtualQuery`
over the stack looking for a `PAGE_GUARD` page); if there is none, it
takes the exception to be a stack overflow and says so.  NovaOS committed
a thread's whole stack (lazily, so the pages cost nothing until touched)
and never put a guard page on it, so the check always failed.

- **A guard page on every stack** (`kernel/um/um.c`): a new thread's stack
  has a committed read/write `PAGE_GUARD` page 32 KB below its base
  (`UM_STACK_COMMIT`), and the TEB's `StackLimit` sits on top of it, as
  on Windows.  The pages below stay committed lazily, so a frame that
  skips the guard still has its memory.
- **The guard moves down as the stack grows**: touching it lifts the
  guard, and the next page down that has not been used yet becomes the
  guard (it was already committed, which used to stop the guard from
  being renewed), with `StackLimit` following.  Reaching the lowest page
  is `STATUS_STACK_OVERFLOW`, as before.
- **Checked by** the core suite's `stacktest` (64- and 32-bit: one guard
  page on the first thread's and a new thread's stack, below the running
  frame, with `StackLimit` on top; still one after the stack grew 128 KB,
  lower down) and the app corpus's .NET entry, whose new
  `exceptiontest.dll` prints "Unhandled exception.
  System.InvalidOperationException: exceptiontest unhandled" and its stack
  trace, as on Windows.
