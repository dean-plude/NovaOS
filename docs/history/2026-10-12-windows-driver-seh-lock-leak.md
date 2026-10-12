## A driver's caught exception resumes cleanly

A follow-up to the kernel API Windows drivers link against (Phase 30.2):
when a driver's `__try`/`__except` caught a ring-0 fault, the resume left
the driver in a bad state, and the `wdm` self-test's full round trip
kernel-panicked.

Two things were wrong in `NtKernelException` (`kernel/nt/seh.c`):

- **The stack pointer it resumed with was 16 bytes low.** In 64-bit mode
  the CPU pushes SS:RSP on every interrupt, a ring-0 fault included, so the
  interrupt frame's RSP is the faulting one; the code took the address of
  that field instead (an old comment in `arch/x86_64/idt.h` said RSP was
  pushed only on a ring change, now corrected). The `__except` body ran
  fine on its frame pointer, but the function's epilogue then popped its
  saved registers two slots off and returned into the caller's file object
  as if it were code.
- **It leaked a big-kernel-lock level.** `interrupt_dispatch` takes the
  lock for the exception and lets it go when dispatch returns, but a handled
  `__except` resumes straight into the handler and never returns there. The
  driver now drops that level before the resume and takes it back only if
  nothing handles the fault.

The whole driver round trip — an access violation caught, a work item, a
system thread and a `Zw` registry value — now completes.
