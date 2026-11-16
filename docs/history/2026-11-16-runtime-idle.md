## Battery life, second part: runtime idle and low-power idle states

The second part of step 28.4 ([install-and-power.md](install-and-power.md#processor-idle-states-and-device-power)),
the "deeper idle" item of 0.2.3 ([plan](releases/0.2.3.md)).

- **Runtime idle (PoFx):** the power framework (`kernel/nt/po.c`) now
  runs as Windows runs it instead of keeping every component active: a
  component whose last activation reference goes gets the idle condition,
  then moves to its deepest F-state; with every component idle for the
  device's idle timeout the driver hears that device power is not
  required, and may send its device to D3.  The next activation has device
  power required again (`PoFxReportDevicePoweredOn`), the component back in
  F0, then the active condition.  Device and F-state callbacks come from
  the power manager's thread; `PO_FX_FLAG_BLOCKING` waits for the active
  condition.
- **Fixed:** `PoFxRegisterDevice` read version 2 and 3 `PO_FX_DEVICE`s'
  `ComponentCount` from the wrong place (it follows `DeviceContext`
  there), so such a registration failed.
- **`_LPI`:** where the platform-wide `_OSC` agrees to
  platform-coordinated low-power idle, a processor's `_LPI` is used
  instead of `_CST` (`kernel/hal/cstate.c`), with its processor
  container's states flattened in as Linux does; a state's minimum
  residency decides when it is worth entering (`kernel/ke/idle.c`).
- **Tests:** `kmdfedu.sys` now asks KMDF for a system-managed idle timeout
  with a component of two F-states, so the devices suite's `kmdfedu` boot
  shows F1, device power not required, D3, then device power required, D0
  and F0; the `laptop` boot's second processor and its container describe
  their states in `_LPI` (`tests/acpi/laptop.asl`), which CPU 1 takes,
  while CPU 0 keeps its `_CST`.

Not yet: OS-coordinated container states, PoFx directed power-down and
latency tolerances, NovaOS's own drivers powering their devices down, and
the idle-drain measurement on the T14.
