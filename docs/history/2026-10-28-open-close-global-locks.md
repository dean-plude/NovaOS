## Opening and closing files stop queuing for two process-wide locks

The per-CPU DACL buffer (#568) did not move `smpstress`'s file figure: on
main it still failed with opening and closing a file alone at 0.25x on four
CPUs (884,000 a second on one thread, 224,000 on four), while reading and
writing open files scaled 2.6x and the registry 2.3x.  Two process-wide
locks sat on that path in kernel32 and the registry's path did not touch
them:

- `CreateFileA` built the NT path with `full_path`, which asked for the
  current directory first, whether or not the name needed it.  That takes
  ntdll's `FastPebLock` critical section and a counted reference on the
  shared current-directory record.  An absolute name (`C:\dir\file`) now
  never asks.
- `CloseHandle` called `k32_forget_handle`, which took an SRW lock around
  three lists (emulated file information, completion ports and timers) to
  find nothing.  It now returns at once while the three lists are empty.
