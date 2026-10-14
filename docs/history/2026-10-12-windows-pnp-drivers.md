## Plug and Play drivers on real hardware resources

Step 30.3 of the Windows driver model (roadmap Phase 30,
[design/windows-drivers.md](design/windows-drivers.md)): a Windows
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
- **Message-signalled interrupts** (`kernel/nt/intr.c`, `pnp.c`): a driver
  whose INF sets `MSISupported` under `Interrupt Management\MessageSignaledInterruptProperties`
  (as virtio-win's do; `MessageNumberLimit` caps the count) on a device
  with an MSI or MSI-X capability is started with a message interrupt
  descriptor (raw: the message count; translated: the first vector of an
  aligned block reserved from the 0x60 to 0xBF vectors) in place of the
  line.  As Windows' PCI driver does, starting the device writes the
  capability (address and data of a write to the boot CPU's local APIC;
  MSI-X also fills the table) and turns it on; a driver connects with
  `IoConnectInterruptEx` message-based (the `IO_INTERRUPT_MESSAGE_INFO`
  table and a message routine per vector) or, as KMDF does, each vector
  fully specified.  Without the opt-in, or on a device without the
  capability, the connect falls back to the line; the Storport host keeps
  the line.  The edu test shows both modes (`edutest msi 1`, restart,
  `edutest message`), and virtio-win's `vioser.sys` now runs on MSI-X.
  Two bugs found on the way: `HKR` lines of an INF's AddReg were never
  applied (setupapi read a line without `=` as an empty key), and
  `IoReleaseRemoveLockAndWait` released the lock once, not twice, so
  removing a device whose driver used it (`pnputil /disable-device` on the edu device)
  hung.
