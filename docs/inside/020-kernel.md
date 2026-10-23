- **Kernel** (`kernel/`): an NT-style executive written from scratch.
  - Object manager and handles, processes and threads, an I/O manager, the
    registry (saved to disk), security tokens (restricted tokens,
    impersonation) and security descriptors, checked when named objects
    and files on drive C: are opened.
  - Virtual memory with sections and guard pages. The read-only pages of a
    loaded DLL are shared by every process that loads the same bytes, as
    Windows shares image sections.
  - The NT system-call table at Windows 10 1903 x64 numbers; NovaOS's own
    services are numbered from 0x200 up.
  - SMP: a run queue per CPU, fine-grained locks for the busy paths and a
    big kernel lock for the rest ([design/architecture.md](design/architecture.md)).
  - Timers are the local APIC's, one-shot or TSC-deadline, calibrated
    against the HPET (or CPUID leaf 0x15, or the PIT). `Sleep(1)` and wait
    timeouts end within a fraction of a millisecond even with every CPU
    busy.
  - NT's priorities: base and dynamic priorities, priority classes,
    wake-up boosts (+1 to +6), the foreground boost and a three times
    longer time slice (60 ms against 20 ms) for the active window's
    process, and a balance set that lifts threads starved for 3 s. Kernel
    threads run at 16 to 19, above anything a program can ask for.
  - The Multimedia Class Scheduler (`AvSetMmThreadCharacteristics`) runs
    audio threads at real-time priority 18, capped at 80% of a CPU as on
    Windows. NovaOS's own sound threads (waveOut, waveIn, DirectSound,
    WASAPI) use it too.
  - CPU time is measured with the TSC: each switch and each timer tick
    charges the running thread, its process and its CPU (idle, user
    programs or the kernel), which `NtQuerySystemInformation`
    (`SystemProcessInformation`, `SystemProcessorPerformanceInformation`),
    `GetSystemTimes`, `GetProcessTimes` and `GetThreadTimes` report and
    Task Manager graphs.
  - Drive C: is a RAM disk, saved to an NTFS or FAT32 volume once it has
    been quiet for a second and restored at boot; once saved, it reports
    that volume's file system, size and free space (Disk Management,
    `GetVolumeInformation`, `GetDiskFreeSpaceEx`).
