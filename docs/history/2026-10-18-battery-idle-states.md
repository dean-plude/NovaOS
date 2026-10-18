## Idle CPUs in C-states, a stopped tick, idle devices in D3

First part of roadmap step 28.4, battery life
([install-and-power.md](../install-and-power.md#processor-idle-states-and-device-power)).
An idle CPU used to execute HLT and wake every 10 ms; a Windows driver
that asked for its device to be powered down got `STATUS_NOT_IMPLEMENTED`.

- **C-states** (`kernel/ke/idle.c`, `kernel/hal/cstate.c`): each
  processor object hears what NovaOS supports through `_OSC` (Intel's
  processor UUID) or `_PDC`, then its `_CST` is read: states entered with
  MWAIT and a hint, a read of a P_LVLx port, or HLT.  Without `_CST`, the
  MWAIT sub-states CPUID leaf 5 lists, else HLT.  The idle CPU picks the
  deepest state whose exit latency is well inside the time until its
  timer is due.  MWAIT runs with interrupts enabled and is followed by
  HLT when it returned with no interrupt and no wake-up (QEMU's TCG runs
  MWAIT as PAUSE with several CPUs).  Deep states need ARAT and an
  invariant TSC on bare metal; C3-type I/O states are left out.
- **Tickless idle** (`kernel/ke/scheduler.c`): a CPU other than the
  first stops its 10 ms tick while idle, arming its timer for its next
  timed sleeper; any interrupt starts it again, and a CPU with threads
  waiting kicks an idle one, as the idle CPU's tick used to come looking.
- **Statistics as Windows gives them**: `SystemProcessorIdleInformation`
  (time and entries per C-state type), `MaxIdleState` and
  `CurrentIdleState` in `ProcessorInformation`, and each CPU's
  `InterruptCount` in `SystemProcessorPerformanceInformation`.
- **Device power IRPs** (`kernel/nt/po.c`, `io.c`, `pnp.c`):
  `PoRequestPowerIrp` sends `IRP_MN_SET_POWER`/`QUERY_POWER`/`WAIT_WAKE`
  down the device's stack from an `nt-power` thread and calls the
  requester back; `PoSetPowerState` keeps the state in
  `DEVOBJ_EXTENSION.PowerFlags`; `PoRegisterDeviceForIdleDetection` and
  `PoSetDeviceBusyEx` power a WDM driver's idle device down after its
  timeout; the PCI bus driver moves a function with PCI power management
  between D0 and D3hot through its PMCSR, restoring its configuration
  when D3hot reset it.
- **Tests**: the devices suite's `laptop` boot gives its processors
  `_PDC` and a three-state `_CST` (`tests/acpi/laptop.asl`) on a CPU with
  MWAIT, and `idletest` checks the states, both CPUs entering the deepest,
  and CPU 1's tick stopping (the keyboard and mouse poll thread now stays
  on CPU 0, whose tick wakes it anyway, so it no longer keeps CPU 1's
  tick running).  `kmdfedu.sys` now asks KMDF for S0 idle
  after 2 s: `edutest idle` sees the framework power the device to D3
  (`EvtDeviceD0Exit`, the PDO's D3), and the next request brings it back.

Not yet: PoFx runtime idle, `_LPI`, NovaOS's built-in drivers' own
device power, the first CPU's tick, and the battery measurement on the
T14 that finishes step 28.4.
