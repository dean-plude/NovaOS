## Windows network drivers on NovaOS's NDIS host

Step 30.9, first half, of the Windows driver model (roadmap Phase 30,
[design/windows-drivers.md](../design/windows-drivers.md)): a real Windows
network driver, virtio-win's signed `netkvm.sys`, drives QEMU's
virtio-net card, and NovaOS's network stack uses it as its adapter.

- **NovaOS's own NDIS host** (`kernel/nt/ndis.c`, `kernel/nt/ndis.h`): the
  `ndis.sys` exports an NDIS 6 miniport links against (Microsoft's
  `ndis.sys` is never shipped).  `NdisMRegisterMiniportDriver` makes NDIS
  the driver's `AddDevice` and dispatch, as on Windows; the card starts
  with the miniport's PnP handlers, `MiniportInitializeEx` with the card's
  translated resources, `MiniportRestart` and the packet filter, and stops
  with `MiniportPause`, `MiniportHaltEx` and its remove handler.  Net
  buffer lists and their pools, scatter/gather DMA through the card's DMA
  adapter, shared memory, line-based interrupts (`MiniportInterrupt`,
  then `MiniportInterruptDPC` with a receive budget), timers, work items,
  read-write locks, OID requests, status indications (link state) and
  the adapter's configuration (its driver key, which setupapi now fills
  from the `.inf`'s `AddReg` lines).  `NdisGetVersion` reports 6.85.
- **The network stack's adapter**: the running miniport becomes lwIP's
  adapter (`NetAttach`); frames sent become `NET_BUFFER_LIST`s for
  `MiniportSendNetBufferLists`, and frames the miniport indicates are
  copied to the stack and their lists returned.  The adapter's MAC
  address comes from the driver, and DHCP starts over on it.
- **Built-in drivers step aside**: NovaOS's own virtio-net driver lets a
  Windows driver installed on its card take the card over (`pnputil
  /add-driver /install` stops it and starts the Windows driver), and the
  Windows driver keeps the card at every boot after.
- **ntoskrnl.exe**: `KeGetProcessorIndexFromNumber`,
  `KeGetProcessorNumberFromIndex`, `KeInitializeMutex`, `KeReleaseMutex`
  and `_vsnprintf_s`, which `netkvm.sys` also imports; `WmiTraceMessage`,
  `WmiQueryTraceInformation` and `PsGetVersion` for WPP software tracing
  (`MmGetSystemRoutineAddress` finds them; no trace session takes the
  messages); and `KUSER_SHARED_DATA` at `0xFFFFF78000000000`, where a
  driver reads the clocks (`KeQuerySystemTime` and `KeQueryTickCount` are
  macros that do).
- **Imports from NovaOS's own driver hosts**: a driver's imports from
  `ndis.sys` and `storport.sys` bind to NovaOS's export table, as those
  from `ntoskrnl.exe` and `hal.dll` do, instead of being taken for an
  export driver to load from `System32\drivers` (which stopped
  `viostor.sys` loading once KMDF's export drivers came in).
- **Driver spin locks and the big kernel lock**: a thread waiting for a
  driver's spin lock lets go of the big kernel lock while it waits (a
  download through `netkvm.sys` stopped the machine: one processor held
  the big lock and waited for the driver's lock, while the other held the
  driver's lock and took an interrupt that wanted the big lock).  A
  receive DPC indicates no more frames than the stack has room for, and
  waits for the stack when it has none.
- **Kernel faults in drivers** are logged before their `__except` handler
  is looked for (`[NT] x.sys+0x1234: exception 14 ...`), and a backtrace
  names driver addresses as `x.sys+offset` and shows where a call through
  a null pointer came from.
- **The import check** (`tools/ci/check-driver-imports.py`) covers
  `ndis.sys` imports, with `netkvm.sys`'s list as a fixture.
- **The devices suite's "netkvm" boot** installs the package
  (`tools/drivertest/netkvm.py` takes it from virtio-win 0.1.302's
  guest tools installer) with `pnputil`, then gets a DHCP lease, pings the
  host and downloads 4 x 32 MB through it, and finds netkvm started again
  after a restart.
