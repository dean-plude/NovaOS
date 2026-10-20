## Idle CPUs back at PASSIVE_LEVEL

A main-health fix for the devices self-tests, whose "after boot" checks
(`vioser`, `viostor`) timed out under KVM after a restart with the driver
starting at boot.

- **What the logs showed:** from the first switch after "Entering kernel
  main loop", CPU 0's idle thread switched out at IRQL 6 thousands of
  times (`IRQL_NOT_LESS_OR_EQUAL: thread 'idle' (TID 1) waits at IRQL 6`).
  `CR8` is the local APIC's task priority, so while CPU 0 idled it kept
  out its clock tick and every device interrupt up to class 6: the input
  poll and the desktop, which wait on CPU 0's tick, stalled, and so did a
  viostor read whose interrupt went to CPU 0 ("SCSI command 0x28 was never
  completed").  The program dumps (plugplay, services waiting in
  advapi32) were all on CPU 1 and only waiting.
- **Fix:** an idle thread about to halt above `PASSIVE_LEVEL` is put back
  and logged once per CPU (`kernel/ke/smp.c`).
- **Where it comes from:** not found yet (the container has no KVM).  The
  first thread switched out at a raised IRQL now prints a backtrace
  (`kernel/ke/scheduler.c`), and a Windows driver's interrupt taken at an
  IRQL that masks it is logged once (`kernel/nt/intr.c`), so the next log
  names the code that left the IRQL raised.
