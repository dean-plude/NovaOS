- Windows drivers the Windows 11 way (roadmap Phase 30, design in
  [design/windows-drivers.md](design/windows-drivers.md)): run Windows
  driver packages unchanged on an NT kernel-mode driver interface (a
  kernel PE loader and `ntoskrnl.exe`/`hal.dll` exports, KMDF and UMDF
  built from Microsoft's MIT source, then USB, NDIS and Storport hosts),
  install them on devices matched by hardware ID from the driver store,
  download them by hardware ID when none is there, and enforce Windows'
  kernel-mode signing policy.  Steps 30.1 to 30.11, from loading a test
  driver to virtio-win's serial, network and disk drivers.  Step 30.1 is in: a
  kernel-mode test driver loads with `sc start`, serves a program's
  `ReadFile` and `DeviceIoControl` as IRPs and unloads with `sc stop`.  Step 30.7's
  signing policy is in: a kernel driver loads only with a Microsoft driver
  signature (embedded or in its installed catalog), a test-signed one only
  with test signing on, an unsigned or tampered one never, and a package
  whose files do not match its catalog is not staged.
