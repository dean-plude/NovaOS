- Windows drivers the Windows 11 way (Phase 30, complete: all eleven
  steps are in; design in [design/windows-drivers.md](design/windows-drivers.md),
  what ships in [drivers.md](drivers.md)): unchanged Windows driver
  packages run on an NT kernel-mode driver interface, beside NovaOS's own
  built-in drivers.  A kernel PE loader and `ntoskrnl.exe`/`hal.dll`
  exports with Windows-exact structures load `.sys` images (30.1, 30.2);
  `pnputil /add-driver /install` puts a driver on a PCI device with no
  built-in driver, started with its memory, interrupts (line, MSI or
  MSI-X) and DMA (30.3); KMDF (`Wdf01000.sys`) and UMDF 2 (`WUDFHost.exe`,
  `Wudfx02000.dll`) are built from Microsoft's MIT-licensed WDF source
  (30.4, 30.10); `pnputil /scan-devices /search-online` downloads a
  device's package from the Microsoft Update Catalog by hardware ID
  (30.6); Windows' kernel-mode signing policy is enforced (30.7).  Third-
  party drivers checked in QEMU's devices suite: virtio-win's `vioser.sys`
  (30.5), `netkvm.sys` on NovaOS's NDIS host (30.9) and `viostor.sys` on
  its Storport host (30.11), and FTDI's USB serial driver on its USB bus
  interface (30.8).  Intel's AX211 Wi-Fi driver (`Netwtw14.sys`) installs
  and registers with the NDIS host on a stand-in device; joining a
  network on the T14's card is step 28.1.  The display-only half of a
  WDDM host (`dxgkrnl.sys`'s role) followed as step 1 of the Intel GPU
  design (step 25.3).
