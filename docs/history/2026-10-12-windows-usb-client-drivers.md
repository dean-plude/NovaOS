## Windows USB client drivers: FTDI's serial driver as COM3

Step 30.8 of the Windows driver model (roadmap Phase 30,
[design/windows-drivers.md](../design/windows-drivers.md)): a vendor's
Windows driver runs a USB device, through a USB host interface NovaOS
writes itself (Microsoft's `usbhub3.sys` and `usbccgp.sys` are never
shipped).

- **The USB bus interface** (`kernel/nt/usbd.c`): a USB device no NovaOS
  driver takes gets a physical device object that answers as Windows' hub
  driver does: the URBs of `IOCTL_INTERNAL_USB_SUBMIT_URB` (select
  configuration and interface, control, bulk and interrupt transfers,
  abort and reset pipe, frame number), the port requests, selective
  suspend (held until cancelled), `USB_BUS_INTERFACE_USBDI` versions 0-3
  and `usbd.sys`'s exports.  Transfers run in order on a thread per pipe
  over NovaOS's USB core; a bulk read waits for data a millisecond at a
  time, and on xHCI a timed-out transfer is stopped and its ring moved on.
- **Bus drivers' children** (`kernel/nt/pnp.c`): a started device is asked
  for its children (`IRP_MN_QUERY_DEVICE_RELATIONS`); each becomes a node
  named by its IDs and starts once a driver is installed on it.
  `IoInvalidateDeviceRelations` asks again; unplugging sends
  `IRP_MN_SURPRISE_REMOVAL`.  `IoOpenDeviceRegistryKey` opens the device's
  `Device Parameters` and driver keys.
- **The rest of the kernel API FTDI's drivers import**: the power
  framework (`PoFx*`, `kernel/nt/po.c`), security descriptors and
  `SeExports` (`kernel/nt/se.c`), `ZwEnumerateKey`, `ZwEnumerateValueKey`,
  `ZwQueryKey`, a real `RtlQueryRegistryValues`, `RtlWriteRegistryValue`,
  WMILIB, `KeWaitForMultipleObjects`, mutexes and string routines;
  `tools/ci/check-driver-imports.py` now checks `ftdibus.sys` and
  `ftser2k.sys` too.
- **COM ports**: setupapi names a Ports-class device `COMn` (first free
  from COM3, the `ComDB` arbiter) when its driver is installed;
  kernel32 opens `COM3` as the driver's `\DosDevices\COM3` and its
  communications functions (`comm.c`) send the `IOCTL_SERIAL_*` requests.
- **`pnputil /scan-devices /search-online /install`** installs what it
  finds and searches again for the devices the new drivers bring.

The new `usbserial` devices boot puts QEMU's `usb-serial` (an FTDI FT232)
on xHCI with its line looped back: pnputil downloads FTDI's
Microsoft-signed package by hardware ID (recorded Catalog pages, the real
cabinet), `ftdibus.sys` starts and reports the COM port, `ftser2k.sys`
makes it COM3, and `serialtest COM3` loops 4 KiB through it at 115200 8N1.
