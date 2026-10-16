## ACPI: processor throttling, GPE blocks and interrupts behind bridges

The three items left open under "ACPI beyond the MADT" since Phase 18.6
are done ([install-and-power.md](../install-and-power.md) has the
details):

- **Passive cooling throttles the processors.** A thermal zone at or
  above `_PSV` used to only report it. Now every `_TSP` it works out the
  performance its `_PSL` processors should give up from `_TC1` and
  `_TC2` (ACPI 6.5, 11.1.5), and `kernel/hal/throttle.c` puts each
  processor in the matching T-state: from `_PTC`, `_TSS` and `_TPC`
  (control and status registers in I/O or memory space, or Intel's
  `IA32_CLOCK_MODULATION` MSR, which each CPU writes on its own timer
  tick), or from `P_CNT` and the FADT's duty cycle field. `_TPC`
  changes (Notify 0x82) are followed, the states are written again after
  S3, and `CallNtPowerInformation(ProcessorInformation)` reports a
  throttled CPU's lower `MhzLimit` (it used to return a fixed 2000 MHz
  from powrprof); `ThermalInformation` now carries the zone's constants
  and processors.
- **GPE block devices.** Besides the FADT's `\_GPE` blocks, each
  `ACPI0006` device's block is set up from its `_CRS` (registers in I/O
  or memory space; the SCI, or an interrupt of its own on a new vector,
  `IRQ_GPE_BLOCK`, polled once a second like the SCI). uACPI runs its
  `_Lxx`/`_Exx` methods, so their Notify reaches the lid, buttons,
  batteries and thermal zones; a `_PRW` naming the block makes its GPE a
  wake event, and the wake report names the device.
- **Interrupts behind PCI bridges.** Routing used to cover bus 0 only, so
  a device behind a PCIe root port got the firmware's legacy IRQ number
  instead of its GSI. Now every root bridge's `_PRT` (by `_BBN`) and
  every bridge's own `_PRT` are read, and a bus without one is routed by
  the standard swizzle onto its bridge's pin, one bridge at a time; the
  Interrupt Line registers, the PnP manager's resources for Windows
  drivers and the inbox drivers' all use it.

Tests: `powertest` (core) checks throttling to the deepest T-state and
back through `ProcessorInformation`, with `tests/acpi/lid-thermal.asl`'s
T-states in pc-testdev's scratch memory and its lid's event and wake
coming through a GPE block device; the devices suite's `edu` boot puts
the edu device behind a PCIe root port, which QEMU gives no `_PRT`.
