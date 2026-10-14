## Task Manager

Dean asked for a Task Manager (with a Device Manager, a Disk Manager and
an Event Viewer, built separately).  `taskmgr.exe` is a Windows 11-style
Task Manager in `C:\Windows\System32`, opened with Ctrl+Shift+Esc, from
the Start button's right-click menu or as `taskmgr` in the Terminal.  It
uses only public APIs, so making it work meant making those APIs real:

- **CPU time accounting**: the scheduler now charges each thread the
  TSC cycles it ran, at every switch and every tick, into the thread, its
  process and the CPU's user or idle total.  `NtQuerySystemInformation`
  class 8 (per-CPU idle, kernel and user time), `ProcessTimes`,
  `ThreadTimes`, `GetSystemTimes`, `GetProcessTimes` and `GetThreadTimes`
  returned zeros or estimates before.
- **SystemProcessInformation** carries each process's threads, handle
  count, parent, creation and CPU times, and the right `PeakVirtualSize`
  offset.
- **SystemPerformanceInformation** had its offsets after
  `IoOtherOperationCount` four bytes off (`AvailablePages` is at 0x2C);
  its I/O counters now come from new counters in the block layer.
- **Network counters**: the network stack counts bytes and frames in and
  out, and iphlpapi's `GetIfTable2` and `GetIfEntry2` return a real row
  for the Ethernet adapter.

Processes lists every process with its CPU and memory and ends the
selected one with End task or Delete (`OpenProcess` +
`TerminateProcess`); Performance draws CPU (a graph per logical
processor), memory, disk and Ethernet; Details is the classic table; and
Services lists the Service Control Manager's services.  The OS test
`taskmgr` starts `winhello`, finds it in Task Manager, ends it and checks
it is gone.
