## Threads stop queueing on their process's lock

Every handle lookup takes its process's lock shared, and three hot paths
took it exclusive, so each of them stopped all of the process's other
threads.  A profile of Firefox's start (`profile on 50 6000`, QEMU TCG,
2 CPUs, with the critical-section fix) put 23% of all samples on threads
waiting for that lock.  Timing who held it exclusive showed three holders:

- `NtAlertThreadByThreadId`, which wakes a thread parked on an SRW lock,
  condition variable or `WaitOnAddress`, took it exclusive only to find the
  thread.  It takes it shared now; the alert is set under the object lock.
- Opening or creating an object handle took it a second time, exclusive,
  to record the access the handle was granted.  The access is now filled in
  with the handle, before the handle can be seen.
- `LoadLibrary` held it while copying the DLL, zeroing all of it first, and
  hashing every read-only page to share it with other processes.  xul.dll
  is 160 MB, loaded in every Firefox process.  The copy and the hashing run
  without the process lock now (the loader lock still keeps other loads
  out), and only the parts of the image the file does not fill are zeroed.

In the same 60-second window, time spent waiting for the process lock fell
from 23% to 8% of samples, time the loader held it exclusive fell 25-fold
(124,533 to 4,898 million cycles over about 180 loads), and programs got 33%
of the CPU against 24%.  dlltest, crtthreads, wait migration, tlsslots
(x64 and x86) and smpstress (4 CPUs) pass.
