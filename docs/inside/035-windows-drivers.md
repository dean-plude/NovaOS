- **Windows drivers** (`kernel/nt/`): real Windows driver packages (an
  `.inf` with its `.sys`, `.cat` and firmware files) run unchanged on an
  NT kernel-mode driver interface ([drivers.md](drivers.md)).
  - A kernel PE loader and `ntoskrnl.exe`/`hal.dll` exports in the
    Microsoft x64 ABI, with Windows' own structure layouts: IRQL, DPCs,
    spin locks, dispatcher objects, the pool, MDLs and DMA, IRPs,
    kernel-mode SEH, line-based and message-signalled (MSI, MSI-X)
    interrupts.
  - KMDF from Microsoft's MIT-licensed Windows Driver Frameworks source
    (`Wdf01000.sys` with NovaOS's `WdfLdr.sys`), and UMDF 2 drivers in
    `WUDFHost.exe` behind the `WUDFRd.sys` reflector.
  - Class hosts that vendor miniports plug into: NDIS (virtio-win's
    `netkvm.sys`), Storport (virtio-win's `viostor.sys`), the USB bus
    interface (FTDI's USB serial driver) and a display-only WDDM host in
    place of `dxgkrnl.sys`.
  - Drivers are installed with `pnputil` or `sc`, matched to devices by
    hardware ID from the driver store, downloaded by hardware ID from the
    Microsoft Update Catalog when none is there, and loaded only under
    Windows' kernel-mode signing policy.
