## Threads waiting for a lock sleep instead of spinning

With Firefox and File Explorer open together the machine crawled: on two
CPUs (QEMU TCG), 60-90% of all time went to threads spinning for one of
Firefox's process locks, and the desktop drew 25 frames a minute.  Every
handle lookup takes its process's lock shared; while a thread held it
exclusively (mapping a view, duplicating a handle) and then waited for a
CPU, the process's other threads spun, yielded to each other and spun
again, keeping the holder off the CPU they were waiting on.

The kernel's process, desktop and file-system locks now spin a moment (the
holder is usually running on another CPU) and then sleep on a wait queue
until the holder lets go: releasing a lock that has sleepers wakes them.
A writer waiting for readers to leave sleeps the same way, woken by the
last one out.

Firefox and File Explorer started together (`profile on 50 6000`, 4 GB,
2 CPUs): programs got 38% of the CPU against 20%, and samples spinning on a
process lock fell from about 6% to under 1%.  A minute later, with both up,
the CPU was 90% idle against 91% busy spinning (`profile on 9000 6000`).
