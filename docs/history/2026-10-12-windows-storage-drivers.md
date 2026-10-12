## Windows storage drivers on NovaOS's Storport host

Step 30.11 of the Windows driver model (roadmap Phase 30,
[design/windows-drivers.md](../design/windows-drivers.md)): a real Windows
storage driver, virtio-win's signed `viostor.sys`, drives QEMU's
virtio-blk card, and NovaOS mounts the volume on its disk.

- **NovaOS's own Storport host** (`kernel/nt/storport.c`,
  `kernel/nt/storport.h`): the `storport.sys` exports a Storport miniport
  links against (Microsoft's `storport.sys` is never shipped).
  `StorPortInitialize` makes Storport the driver's `AddDevice` and
  dispatch, as on Windows; the adapter starts with `HwFindAdapter` on a
  `PORT_CONFIGURATION_INFORMATION` filled from the controller's
  translated resources (its access ranges, mapped with
  `StorPortGetDeviceBase`), `HwInitialize` under the interrupt lock and
  the miniport's passive initialization routine.  Requests are
  `STORAGE_REQUEST_BLOCK`s with a 16-byte CDB, sent through `HwBuildIo`
  and `HwStartIo` with a scatter/gather list; the miniport's line-based
  interrupt, its DPCs (`StorPortNotification` codes for DPCs, spin locks
  and request completion) and `StorPortGetUncachedExtension`,
  `StorPortGetPhysicalAddress` and `StorPortGetBusData` work as on
  Windows.  One request at a time, through a 64 KiB bounce buffer.
- **The block layer's disk**: the host asks the adapter's LUN 0 for its
  INQUIRY and READ CAPACITY and hands the disk to the block layer as
  `stor0` (reads and writes become READ/WRITE(10), or (16) past 2 TiB),
  so its volumes mount as drive letters like any disk's.
- **The PnP manager** answers a PDO's PCI configuration space and device
  instance for the host (`NtPnpConfig`, `NtPnpInstanceOf`).
- **The import check** (`tools/ci/check-driver-imports.py`) covers
  `storport.sys` imports, with `viostor.sys`'s list as a fixture.
- **The devices suite's "viostor" boot** has a second disk on a virtio-blk
  card with an NTFS volume labelled VIOSTOR, installs the package
  (`tools/drivertest/viostor.py` takes it from virtio-win 0.1.302's guest
  tools installer) with `pnputil`, reads a known file from D:, writes a
  new one, and after a restart finds viostor started at boot and both
  files there.
