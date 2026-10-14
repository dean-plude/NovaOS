- **Program support**: what unmodified Windows programs rely on beyond
  the DLLs themselves.
  - The PE loader: imports, forwarders, API sets, TLS callbacks and
    `DllMain` run in user mode by `ntdll`, in dependency order.
  - 64- and 32-bit programs side by side: 32-bit code runs in compatibility
    mode through a 32-bit `ntdll` that converts each system call, as WoW64
    does, and Windows' segment selectors let 64-bit code far-jump into
    32-bit code.
  - x64 and x86 structured exception handling, with `CONTEXT` and the AVX
    state as Windows lays them out, and Windows' fix-up of misaligned SSE
    moves.
  - Consoles: screen buffers on libvterm, and pseudo consoles (ConPTY) for
    terminal hosts running `cmd.exe`. GUI programs start without a console
    window.
  - COM in-process and local (`LocalServer32`) servers, proxy/stub DLLs
    and calls between processes; drag and drop; a shared clipboard; `.lnk`
    shortcuts; Windows Installer packages.
  - Services (`advapi32`'s service control manager), scheduled tasks (Task
    Scheduler 2.0, kept in `C:\Windows\System32\Tasks`), the Data
    Protection API, and event tracing that answers as Windows does with no
    logging session running.
