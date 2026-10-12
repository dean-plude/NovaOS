- Windows drivers the Windows 11 way (roadmap Phase 30, design in
  [design/windows-drivers.md](design/windows-drivers.md)): run Windows
  driver packages unchanged on an NT kernel-mode driver interface (a
  kernel PE loader and `ntoskrnl.exe`/`hal.dll` exports, KMDF and UMDF
  built from Microsoft's MIT source, then USB, NDIS and Storport hosts),
  install them on devices matched by hardware ID from the driver store,
  download them by hardware ID when none is there (step 30.6 is in:
  `pnputil /scan-devices /search-online` downloads a device's package from
  the Microsoft Update Catalog and stages it), and enforce Windows'
  kernel-mode signing policy.  Steps 30.1 to 30.11, from loading a test
  driver to virtio-win's serial, network and disk drivers.  Step 30.1 is in: a
  kernel-mode test driver loads with `sc start`, serves a program's
  `ReadFile` and `DeviceIoControl` as IRPs and unloads with `sc stop`.
  Step 30.2's first part is in: the IRQL in `CR8`, DPCs, spin locks,
  events, semaphores, timers, fast mutexes, the pool, MDLs and direct I/O,
  and IRPs completed later from a DPC; kernel-mode SEH, work items and
  system threads, `Zw*` registry and file calls and ETW stubs are next.  Step 30.7's
  signing policy is in: a kernel driver loads only with a Microsoft driver
  signature (embedded or in its installed catalog), a test-signed one only
  with test signing on, an unsigned or tampered one never, and a package
  whose files do not match its catalog is not staged.  Step 30.4's
  framework is in: `Wdf01000.sys`, built from Microsoft's MIT WDF source,
  and NovaOS's `WdfLdr.sys` load KMDF drivers
  (a test driver's control device and queue); the KMDF version of the
  `edu` PnP test driver, which finishes the step, follows 30.3.
  Step 30.3 is in: `pnputil /add-driver /install` puts a Plug and Play
  driver on a PCI device with no NovaOS driver, and the PnP manager starts
  it with the device's memory, interrupt and DMA (QEMU's `edu` device in
  the tests); line-based interrupts only, MSI comes later.
  Step 30.8 is in: a vendor's Windows USB client driver runs a USB device
  through NovaOS's own USB bus interface (URBs over its USB core): FTDI's
  driver, downloaded by hardware ID, makes QEMU's `usb-serial` converter
  COM3, which programs open with the communications functions.
