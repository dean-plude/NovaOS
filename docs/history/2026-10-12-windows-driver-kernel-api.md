## The kernel API Windows drivers live on

Step 30.2 of the Windows driver model (roadmap Phase 30,
[design/windows-drivers.md](../design/windows-drivers.md)), first part:
the calls nearly every Windows driver makes besides its I/O.

- **IRQL in `CR8`** (`kernel/ke/irql.h`), as on Windows x64, where the
  WDK's `KeRaiseIrql` and `KeLowerIrql` write `CR8` inline.  The scheduler
  never switches a thread out at `DISPATCH_LEVEL` or above: it asks for
  the DPC interrupt, which the local APIC delivers when the IRQL drops,
  and switches then.  A thread waiting at `APC_LEVEL` keeps its IRQL.
- **DPCs** (`kernel/nt/ke.c`): a queue per CPU, run by the DPC interrupt
  (vector 0x2F, Windows' own) at `DISPATCH_LEVEL`, keeping the
  interrupted code's SSE registers.
- **Spin locks** (plain and in-stack queued), the interlocked list calls,
  **events, semaphores, timers** (with DPCs, on the clock tick),
  **fast mutexes** and `KeWaitForSingleObject`, `KeDelayExecutionThread`,
  the performance counter and `KeBugCheckEx`.
- **Pool** (`kernel/nt/ex.c`): `ExAllocatePool2`, `ExAllocatePoolWithTag`
  and the rest on the kernel heap, each block's tag and owner recorded: a
  wrong tag or double free is logged as `BAD_POOL_CALLER`, and the blocks a
  driver never freed are named when it unloads.
- **MDLs and direct I/O** (`kernel/nt/mm.c`, `io.c`): `IoAllocateMdl`,
  `MmProbeAndLockPages`, `MmBuildMdlForNonPagedPool`,
  `MmMapLockedPagesSpecifyCache` (frames mapped at consecutive kernel
  addresses, `kernel_map_frames` in `mm/vmm.c`), `MmGetPhysicalAddress`;
  devices with `DO_DIRECT_IO` and `METHOD_IN_DIRECT`/`OUT_DIRECT` control
  codes get the caller's buffer in `MdlAddress`.
- **Pending IRPs**: a dispatch routine may return `STATUS_PENDING` and
  complete the IRP later; the requester sleeps on its event.  The cancel
  spin lock and `IoCancelIrp`; a dispatch routine returning above
  `PASSIVE_LEVEL` is logged and put back.
- **`tools/sys_imports.py`**: the imports of a `.sys` file NovaOS does not
  export yet.
- **`wdmtest.sys`** exercises all of it in the devices suite's "wdm" boot.

Still to come in 30.2: kernel-mode SEH (`__try` in drivers), work items
and system threads, `Zw*` registry and file calls, ETW/WMI stubs.
