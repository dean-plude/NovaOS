## Intel's AX211 Wi-Fi driver loads on NovaOS's NDIS host

The Wi-Fi half of step 30.9 of the Windows driver model (roadmap Phase 30,
[design/windows-drivers.md](design/windows-drivers.md)): Intel's own
Windows driver for the AX211, `Netwtw14.sys` 24.40.0.4, downloads by
hardware ID, installs unchanged, registers with NovaOS's NDIS host and
reaches its hardware probe.

- **WDI miniports** (`kernel/nt/ndis.c`, `kernel/nt/ndis.h`):
  `NdisMRegisterWdiMiniportDriver` and its deregistration; a WDI driver's
  adapter starts as Windows starts it, `MiniportWdiAllocateAdapter` then
  `MiniportWdiOpenAdapter`, completed by `NdisWdiOpenAdapterComplete`, and
  stops with its close and free handlers.  No WDI command layer yet
  (scanning, connecting): roadmap step 28.1.
- **`cng.sys`** (`kernel/nt/cng.c`): the `BCrypt*` hashes (SHA-1, SHA-256,
  SHA-384, SHA-512) and signature checks (ECDSA P-256 and P-384, RSA
  PKCS #1 and PSS) a driver imports, over Mbed TLS; Intel's driver checks
  its firmware with ECDSA P-384.
- **Files from kernel mode** (`kernel/nt/zw.c`): `ZwCreateFile` opens and
  creates files and directories (`\??\`, `\SystemRoot\`, relative to a
  directory handle), with `ZwOpenFile`, `ZwReadFile`, `ZwWriteFile`,
  `ZwQueryInformationFile` and `IoGetDriverDirectory`, where the driver
  finds its firmware.
- **ntoskrnl.exe and ndis.sys**: the rest of what `Netwtw14.sys` imports:
  C run-time string functions, time conversions, timer objects,
  `NdisWriteEventLogEntry`, `IoBuildSynchronousFsdRequest`, the `Etw*`
  functions (already in `etw.c`, now exported), `NtBuildNumber` and
  stubs for performance counters, WMI and live kernel reports
  (`kernel/nt/win11.c`).
- **A PCI stand-in** (`kernel/hal/pci.c`, `kernel/hal/fwcfg.c`): QEMU has
  no Intel Wi-Fi card, so the firmware config file
  `opt/novaos/pci-standin` makes one QEMU device answer as another's
  vendor, device, subsystem and class.

The new `ax211` devices boot presents QEMU's `edu` card as a ThinkPad T14
Gen 4's AX211: pnputil downloads Intel's Microsoft-signed package from
recorded Catalog pages (the real cabinet), the driver loads (on KMDF, so
test signing is on for NovaOS's own `WdfLdr.sys`), reads and checks its
firmware, and its adapter is allocated and opened; the open fails at the
hardware probe, since `edu` is not an AX211.  The real card is checked on
the T14 (see [drivers.md](drivers.md)).
