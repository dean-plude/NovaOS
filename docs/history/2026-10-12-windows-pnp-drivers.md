## Plug and Play drivers on real hardware resources

Step 30.3 of the Windows driver model (roadmap Phase 30,
[design/windows-drivers.md](../design/windows-drivers.md)): a Windows
function driver installed from its `.inf` drives a PCI device, with the
device's memory, interrupt and DMA handed to it the way Windows hands them.

- **The PnP manager** (`kernel/nt/pnp.c`): a physical device object (PDO)
  for each PCI device a Windows driver is installed on, answering the bus
  driver's IRPs (`QUERY_ID`, `QUERY_CAPABILITIES`, `QUERY_INTERFACE` with
  the PCI bus interface, config space reads and writes).  It loads the
  driver the device's `Enum` key names, calls `AddDevice`, and sends
  `IRP_MN_START_DEVICE` with the device's `CM_RESOURCE_LIST`, raw and
  translated: the memory and I/O BARs and the interrupt (the ACPI `_PRT`
  routing, a vector from 0x60 and its IRQL).  Removal is
  `IRP_MN_QUERY_REMOVE_DEVICE` (a driver may veto it), then
  `IRP_MN_REMOVE_DEVICE`; the driver unloads when its last device goes.
  A driver that fails shows Windows' problem codes (28, 10, 31, 39, 52).
- **Interrupts** (`kernel/nt/intr.c`): `IoConnectInterrupt` and
  `IoConnectInterruptEx` (fully specified, line-based, and message-based
  falling back to line-based, since NovaOS gives Windows drivers no MSI
  yet), the ISR at the interrupt's IRQL under its spin lock,
  `KeSynchronizeExecution`.
- **Device memory and DMA** (`kernel/nt/dma.c`): `MmMapIoSpace(Ex)`,
  contiguous memory, and DMA adapters (`IoGetDmaAdapter`, version 3):
  common buffers, `AllocateAdapterChannel` with `MapTransfer`, and
  scatter/gather lists, through bounce buffers below the device's address
  width when the memory is out of its reach.  The physical page allocator
  sets aside 2 MiB just below 16 MiB at boot for such buffers, used only
  when nothing free lies under the device's limit, so short-reach devices
  still get memory after the low pages are taken.
- **Device interfaces**: `IoRegisterDeviceInterface` and
  `IoSetDeviceInterfaceState`; the interface's `\\?\` path opens the driver's
  device and `CM_Get_Device_Interface_List` lists it.
- **Installing on devices**: `pnputil /add-driver x.inf /install` and
  newdev's `UpdateDriverForPlugAndPlayDevices` install a package on each
  device one of its models names that no NovaOS driver runs (setupapi
  carries out the model's install section: files, services, `.HW` registry
  lines, the device's `Enum` and driver keys) and start it at once; it
  starts again at every boot.  `pnputil /disable-device`, `/enable-device`
  and `/restart-device` (with `/deviceid`), and cfgmgr32's
  `CM_Disable_DevNode` and `CM_Enable_DevNode`.
- **`edutest.sys`**, a WDM driver for QEMU's `edu` device, exercises it in
  the devices suite's new "edu" boot: a factorial through the device's
  interrupt, ISR and DPC, DMA three ways within the device's 28 bits, a
  removal vetoed while a program has it open, disable, enable and a start
  at boot.  NovaOS has no PCI hot-unplug yet, so surprise removal is
  handled but not tested.
