## A driver exception no longer leaks the kernel lock

A follow-up to the kernel API Windows drivers link against (Phase 30.2): a
ring-0 fault inside a driver that its `__try`/`__except` catches left the
big kernel lock held one level too deep.

`interrupt_dispatch` takes the big kernel lock for the exception and lets
it go when dispatch returns, but a handled `__except` resumes straight into
the handler and never returns there, so the release was skipped. The driver
ran on holding the lock for good; the other CPUs starved on it (the watchdog
reported it held for seconds while another thread waited), and the forced
release corrupted the kernel into a page fault. `NtKernelException`
(`kernel/nt/seh.c`) now drops that one lock level before a resume and takes
it back only if nothing handles the fault, so the `wdm` self-test's full
driver round trip — an access violation caught, a work item, a system
thread and a `Zw` registry value — completes instead of panicking.
