## Critical sections stop spinning for nothing

A thread that found a critical section taken incremented its lock count,
then spun `SpinCount` rounds before it slept on the section's event.  Once
counted in, it could never take the lock by spinning (the lock is handed to
the queue), so every round was wasted; and the count was used whole, flag
bits and all, so a section set up with the top bit (an old way to ask for
the event up front) spun about two billion rounds.  Profiling Firefox's
start (`profile on 50 6000`, QEMU TCG, 2 CPUs) put 33% of all samples on that
one loop in `RtlEnterCriticalSection`.  A contending thread now spins first,
taking the lock as soon as it is free with nobody queued, and only then
joins the queue and sleeps; it spins the low 24 bits of the count only, and
not at all on one CPU.  In the same 60-second window Firefox made 1.8 times
the system calls (61,896 against 33,891 of the busiest) with the loop at
0.2% of samples, and 100 seconds after `start browser` its Welcome page was
on screen, where before no window had appeared yet.  `smpstress` runs its
critical-section test again with a spin count (11 passed).

A profile with a LENGTH (`profile on DELAY LENGTH`) now also reports to the
serial log by itself when its window closes, so a program holding the
keyboard does not keep the report from being read.
