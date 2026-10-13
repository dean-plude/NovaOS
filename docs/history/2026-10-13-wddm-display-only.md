## A Windows display driver on the boot display

Step 1 of the Intel GPU design (roadmap 25.3,
[design/intel-gpu.md](../design/intel-gpu.md)): Windows display drivers
are miniports of `dxgkrnl.sys`, and NovaOS now has the display-only half
of it.  A WDDM display-only driver installed with `pnputil` takes the
desktop over from the built-in driver, changes mode when Settings asks,
and gives the screen back when it is removed.

- **`\Device\DxgKrnl`** (`kernel/nt/dxgkrnl.c`, structures in
  `kernel/nt/dxgk.h` in Windows' x64 layout): a driver's
  `DxgkInitializeDisplayOnlyDriver` (displib.lib's, linked into the
  driver) opens it with `IoGetDeviceObjectPointer`, which now finds named
  devices, and asks with `IOCTL_VIDEO_DDI_FUNC_REGISTER` for dxgkrnl's
  initialisation function; that takes the driver's
  `KMDDOD_INITIALIZATION_DATA` and makes the driver object's AddDevice,
  IRP dispatch and unload the host's.  The PnP manager then starts the
  adapter as any Phase 30 device: the host's AddDevice puts an FDO on the
  PCI function and calls `DxgkDdiAddDevice`; `IRP_MN_START_DEVICE` goes
  down first, then `DxgkDdiStartDevice` gets the `DXGKRNL_INTERFACE`
  callbacks (device information and resources, PCI configuration space,
  `DxgkCbMapMemory`, DPCs, `DxgkCbAcquirePostDisplayOwnership`,
  `DxgkCbQueryVidPnInterface`), and the host asks for the driver's caps,
  outputs (`DxgkDdiQueryChildRelations`/`QueryChildStatus`, the EDID) and
  modes.
- **The VidPN manager**: VidPNs with their topology (paths from source 0
  to an output) and source and target mode sets, behind the interfaces
  Microsoft documents (`pfnGetTopology`, `pfnCreateNewSourceModeSet`,
  `pfnAddMode`, `pfnAcquirePinnedModeInfo` and the rest).  The driver's
  modes are what `DxgkDdiEnumVidPnCofuncModality` puts in source 0's set
  for a VidPN that pins nothing; a mode change is a VidPN with that mode
  pinned, checked with `DxgkDdiIsSupportedVidPn` and committed with
  `DxgkDdiCommitVidPn`.
- **The display layer's WDDM back end** (`kernel/hal/display.c`): the
  started driver offers itself (`DisplayWddmOffer`) and the desktop loop
  hands it the screen between two frames: the desktop draws into a
  picture in memory, each drawn rectangle goes to
  `DxgkDdiPresentDisplayOnly`, and `DisplaySetMode` (which Settings'
  resolution chips and `ChangeDisplaySettings` call) commits a VidPN.
  The built-in Bochs VBE driver steps aside for the installed driver as
  the virtio-net one does for netkvm.  On removal the driver's
  `DxgkDdiStopDeviceAndReleasePostDisplayOwnership` says what it left on
  the screen, and the built-in driver takes it back in that mode, with
  the desktop's last picture.
- **Plug and Play fixes on the way**: at boot the PnP manager no longer
  takes a built-in driver's name, which plugplay records as the device
  key's `Service`, for an installed Windows service (it tried to load
  "Bochs VBE.sys"), and a device whose installed service does not load is
  left to its built-in driver, so a later install still has that driver
  step aside.  The built-in display driver turns the adapter's PCI
  decoding back on when it takes the screen back, since removing the
  Windows driver's device turns it off.
- **`wddmdod.sys`** (`tools/drivertest/wddmdod.c`, `wddmdod.inf`, built
  by `tools/drivertest/mksys.py` into `C:\Tests\Wddm`): a display-only
  driver for QEMU's standard VGA (PCI 1234:1111, the Bochs DISPI
  registers), written as Microsoft's MIT-licensed KMDOD sample is, with
  its own copy of what displib's `DxgkInitializeDisplayOnlyDriver` does.
  It offers the common modes that fit the adapter's memory, programs
  DISPI on commit, and copies dirty rectangles to the framebuffer.
- **The test** (`tests/selftest/devices/wddm`, the devices suite's
  "wddm" boot): with test signing on, `pnputil /add-driver wddmdod.inf
  /install` starts the driver and the desktop moves to it; `disptest`
  runs its mode checks through it and `disptest 1024 768` saves a mode;
  `pnputil /disable-device` stops it, the built-in driver draws again at
  1024x768 and changes mode itself; `/enable-device` starts the driver
  again, and after a restart it starts at boot and still changes mode.

Not yet: the full `DxgkInitialize` path of a render driver (Intel's, on
the T14: step 2), interrupts and vertical blanks, power states, more than
one source or output, and monitor interfaces.
