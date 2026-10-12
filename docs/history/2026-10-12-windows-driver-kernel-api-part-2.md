## The kernel API Windows drivers live on, completed

Step 30.2 of the Windows driver model (roadmap Phase 30,
[design/windows-drivers.md](../design/windows-drivers.md)), second part:
the rest of what a real Windows driver imports, so a `.sys` file links and
runs rather than failing to load on a missing export.

- **Kernel-mode SEH** (`kernel/nt/seh.c`): a driver's `__try`/`__except`
  works in kernel mode.  A fault in ring 0 inside a driver (the page-fault
  and general-protection handlers call `NtKernelException`) is dispatched
  by walking the driver's own `.pdata`/`.xdata` through
  `__C_specific_handler`, running the `__except` filter and unwinding to
  its handler; `ExRaiseStatus`, `RtlRaiseException`, `ProbeForRead`/`Write`
  and `RtlUnwindEx` raise and unwind the same way.  A driver built for this
  needs `clang -fasync-exceptions` (MSVC's `/EHa`), or the optimizer drops
  the handler.
- **Work items and system threads** (`kernel/nt/io.c`, `ps.c`):
  `IoAllocateWorkItem`/`IoQueueWorkItem` run a routine on a kernel worker
  thread at `PASSIVE_LEVEL`; `PsCreateSystemThread`/`PsTerminateSystemThread`
  give a driver a real NovaOS kernel thread, its handle an object a driver
  can wait on, reference by handle (`ObReferenceObjectByHandle`) and let go
  of (`ObfDereferenceObject`).
- **`Zw*`** (`kernel/nt/zw.c`): registry (`ZwCreateKey`, `ZwOpenKey`,
  `ZwSetValueKey`, `ZwQueryValueKey`, `ZwDeleteValueKey`, `ZwDeleteKey`),
  files on the RAM file system (`ZwCreateFile`, `ZwClose`), driver loading
  for the KMDF loader (`ZwLoadDriver`/`ZwUnloadDriver`), and
  `ZwQuerySystemInformation`'s module list, which a driver reads to find
  its own image base.
- **ERESOURCEs, lookaside lists, SLists, KMUTEXes and
  `KeWaitForMultipleObjects`** (`kernel/nt/ex.c`, `ke.c`), the fast
  allocators and locks the KMDF runtime leans on.
- **`MmMapIoSpaceEx`/`MmUnmapIoSpace`** (`kernel/nt/mm.c`) map a device's
  registers; the pageable-section and Driver-Verifier calls are honest
  no-ops (a NovaOS driver is never paged out and verification is a later
  step), and the string, bitmap and version run-time library and the
  compiler's own `_vsnprintf`/`_purecall` round it out.
- **ETW/WMI** (`kernel/nt/etw.c`): `EtwRegister`/`EtwWrite` and the WMI
  registration calls succeed and record nothing yet.
- **Import coverage** (`tools/ci/check-driver-imports.py`): a CI check that
  every symbol the drivers we target first — the VirtIO serial driver and
  the KMDF runtime and loader (`vioser.sys`, `Wdf01000.sys`, `wdfldr.sys`) —
  import from `ntoskrnl.exe` and `hal.dll` is an export `kernel/nt/exports.h`
  provides, against import lists kept beside the wdm self-test.

`wdmtest.sys` now also catches an access violation with `__try`/`__except`,
runs a work item, starts and joins a system thread and round-trips a
registry value through `Zw`, all checked in the devices suite's "wdm" boot.
