## Device Manager

Dean's ask of 2026-10-14 ("a device manager, a task manager, a disk
manager and an event manager"), the Device Manager part: `devmgmt.exe`,
"Device Manager" in Start.

- **The app.**  The PC's devices by type (class nodes such as "Display
  adapters", the devices under them) or by connection, with View > Show
  hidden devices for unplugged ones.  The pane beside the tree shows
  what Windows' Properties dialog shows for the selected device: status
  and problem code in Windows' words, driver provider, version, date and
  INF, the instance path, hardware and compatible IDs, and resources.
  F5 and any `WM_DEVICECHANGE` list the devices again.
- **Only public APIs.**  setupapi (`SetupDiGetClassDevs`, the new
  `SetupDiGetClassDescription` and `SetupDiClassNameFromGuid`, registry
  and `DEVPKEY` properties) and cfgmgr32 (status, `CM_Get_Child`/`Sibling`
  and the new `CM_Get_First_Log_Conf`, `CM_Get_Next_Res_Des`,
  `CM_Get_Res_Des_Data(_Size)` and `CM_Free_*_Handle`), so programs that
  read devices the same way now get the same answers.
- **Driver properties.**  `DEVPKEY_Device_DriverDate`, `DriverVersion`,
  `DriverDesc`, `DriverInfPath`, `DriverInfSection` and `DriverProvider`
  come from the software key a device install wrote; a device on a
  NovaOS driver has NovaOS as provider and the system version.
- **Resources.**  The kernel sizes every PCI function's BARs once at boot,
  before any driver runs (`PciRanges`), and gives a node's ranges and its
  ACPI-routed interrupt to programs (`PnpResources`, CTL_PNP operation 6,
  `nova_pnp_resources`).
- **Test.**  The devices suite's new "devmgr" boot (an e1000e card, a USB
  keyboard and tablet, the driverless edu device) opens Device Manager
  from the Start menu, then `devmgmt --check` checks setupapi's list
  against a walk of cfgmgr32's tree, logs every device's class and
  status and each PCI device's resources, and takes the reference
  screenshot (`docs/screenshots/device-manager.png`).

Not yet: device icons, the Properties dialog with its Driver tab buttons
(update, disable, uninstall), and Events (Event Viewer is its own step).
