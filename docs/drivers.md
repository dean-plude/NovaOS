# Drivers and firmware

NovaOS gets drivers for devices the way Windows 11 does. A device has
hardware IDs; the driver store is searched for a Windows driver package
(an `.inf` with its `.sys` and `.cat` files) that names one of them; if
none is there, the package can be downloaded by hardware ID from the
Microsoft Update Catalog. The package's kernel-mode driver then runs
unchanged on NovaOS's NT kernel-mode driver interface (`kernel/nt/`).
This is roadmap Phase 30, whose steps 30.1 to 30.11 are all merged. The
design is in [design/windows-drivers.md](design/windows-drivers.md).

NovaOS also has its own built-in drivers (`kernel/drivers/`,
`kernel/hal/`). They play the part of Windows' inbox drivers: they are on
every image and run the disks, network cards, display, sound and USB of
QEMU and common PCs from the first boot, before any package is installed.
[hardware.md](hardware.md#devices-novaos-drives) lists them by device.

Firmware, the programs a driver loads into the device itself, is the
third part. Windows driver packages carry their own. For the built-in
drivers, NovaOS can ship a pinned set of files from linux-firmware and the
audio DSP's Sound Open Firmware ([below](#the-default-firmware-set)).

All of the Windows driver support has been tested only in QEMU, in CI's
devices suite. Nothing has run yet on the reference ThinkPad T14 Gen 4 or
on any other real PC.

## How a device gets its driver

| Situation | What happens |
|---|---|
| A built-in NovaOS driver takes the device at boot | The device runs on it. A Windows package that matches is listed by `pnputil /enum-devices /drivers` as outranked by the built-in driver, and is not installed on it |
| A built-in driver that can step aside (the Bochs VBE display driver, as on QEMU's standard VGA, and the virtio-net driver) | `pnputil /add-driver x.inf /install` installs the Windows driver on the device; the built-in driver steps aside (`[PCI] 00:03.0: virtio-net steps aside for an installed Windows driver`) and the device keeps the Windows driver at every boot after |
| No built-in driver (problem code 28) | `pnputil /add-driver x.inf /install`, a setup program's `UpdateDriverForPlugAndPlayDevices` or `DiInstallDriver` installs a matching package and starts its driver at once and at every boot after; `pnputil /scan-devices /search-online /install` finds, downloads and installs one by hardware ID |

Devices a Windows driver can be started on: PCI functions, USB devices
and functions, and the child devices a Windows bus driver reports
(`IoInvalidateDeviceRelations`). A non-Plug-and-Play kernel driver can
also be installed as a service and started by hand (below).

### pnputil

`pnputil` is Windows' driver package tool, with one NovaOS addition
(`/search-online`). Installing, disabling and enabling need an elevated
administrator (`SeLoadDriverPrivilege`).

| Command | What it does |
|---|---|
| `pnputil /add-driver x.inf [/subdirs] [/install]` | stages the package in the driver store as `oemN.inf` (`*.inf` works); `/install` also installs it on every connected device one of its models names and that no NovaOS driver runs |
| `pnputil /delete-driver oemN.inf [/uninstall] [/force]` | removes a staged package |
| `pnputil /enum-drivers` | the staged packages, with their signer and, for downloaded ones, their source |
| `pnputil /enum-devices [/connected \| /disconnected] [/class C] [/instanceid ID] [/deviceid ID] [/ids] [/drivers]` | the Plug and Play device tree, with each device's status and driver, its hardware and compatible IDs (`/ids`) and the staged packages that match it (`/drivers`) |
| `pnputil /disable-device`, `/enable-device`, `/restart-device` (an instance ID, or `/deviceid ID`) | take a device's Windows driver away (query-remove, then remove; a driver may refuse while a program has the device open), start it again, or both; a disabled device stays off across restarts |
| `pnputil /scan-devices [/instanceid ID] [/search-online [/source URL] [/install]]` | rescan; `/search-online` downloads packages by hardware ID ([below](#downloading-drivers-by-hardware-id)) |

There is no Device Manager app yet; `pnputil` and programs that call
setupapi, newdev and cfgmgr32 do its work.

### Kernel drivers as services

A kernel driver that is not a Plug and Play driver is installed as a
service and started by hand, as on Windows:

```
sc create NAME type= kernel binPath= C:\path\driver.sys
sc start NAME
sc stop NAME
```

The kernel loads the `.sys` into kernel memory and calls its
`DriverEntry`. The devices and `\DosDevices` links it creates open from
programs as `\\.\NAME`, and their `ReadFile`, `WriteFile` and
`DeviceIoControl` calls reach the driver's dispatch routines as Windows
IRPs. A driver with files still open unloads when the last one closes;
one without a `DriverUnload` routine cannot be stopped.

## What a Windows driver can use

### The NT kernel API

NovaOS's kernel exports what drivers import from `ntoskrnl.exe`,
`hal.dll` and the port drivers below. The list is one file,
[`kernel/nt/exports.h`](../kernel/nt/exports.h) (about 480 exports:
`ntoskrnl.exe`, `hal.dll`, `ndis.sys`, `storport.sys`, `usbd.sys`,
`cng.sys`, `wmilib.sys`). The main groups:

- IRQL kept in `CR8`, as on Windows x64; spin locks, DPCs, timers,
  events, semaphores, mutexes, fast mutexes and executive resources;
  work items and system threads.
- The pool (`ExAllocatePool2`, `ExAllocatePoolWithTag`), with tags, and
  MDLs with direct I/O. Pool blocks a driver never frees are named in the
  log when it unloads (`[NT] x.sys: 2 pool blocks (96 bytes, tags 'Abcd')
  never freed`).
- IRPs that complete later: a dispatch routine may return
  `STATUS_PENDING` and complete the request from a DPC or another thread;
  cancellation works.
- `Zw*` registry and file calls, kernel-mode SEH, the `/GS` cookie, ETW
  and WMI registration stubs, and `KeBugCheckEx`.
- Hardware: `MmMapIoSpace`, `IoConnectInterruptEx` (the device's line,
  or MSI and MSI-X messages when the driver's INF sets `MSISupported` and
  the device has the capability), DMA adapters (`IoGetDmaAdapter`,
  scatter/gather, common buffers, the version 3 calls KMDF uses) and
  device interfaces that programs open.

A driver that imports something missing does not load, and the log names
what it missed (`[NT] x.sys: ntoskrnl.exe!KeFoo not implemented`). To see
in advance what a driver needs that NovaOS does not export yet:

```bash
python3 tools/sys_imports.py x.sys        # --all lists every import
```

### Plug and Play

When a device gets a driver, NovaOS's PnP manager (`kernel/nt/pnp.c`)
does what Windows does: it gives the device a physical device object
(PDO), loads the driver and calls its `AddDevice`, and sends
`IRP_MN_START_DEVICE` with the device's memory, I/O ports and interrupt
as raw and translated resource lists. A bus driver's children (asked for
with `IRP_MN_QUERY_DEVICE_RELATIONS`) get device nodes from their
`IRP_MN_QUERY_ID` answers and start with their own drivers, or raw when
they allow it; a child no longer reported is removed. Unplugging a USB
device sends `IRP_MN_SURPRISE_REMOVAL` and `IRP_MN_REMOVE_DEVICE`,
children first. A driver that fails to load shows problem code 39, one
whose signature fails problem code 52.

`pnputil /enum-devices /deviceid ID` shows the device started with its
driver.

### KMDF

NovaOS ships Microsoft's Kernel-Mode Driver Framework, `Wdf01000.sys`,
built from Microsoft's MIT-licensed
[Windows-Driver-Frameworks](https://github.com/microsoft/Windows-Driver-Frameworks)
source, and its own KMDF loader, `WdfLdr.sys` (Microsoft did not publish
its own). Both are in `C:\Windows\System32\drivers`, as on Windows, built
by `tools/kmdf/build_kmdf.py`. A KMDF driver imports `WdfLdr.sys`, which
loads `Wdf01000.sys` and binds the driver to KMDF 1.33, the version
Windows 11 ships. The framework's messages go to the log as `[DbgPrint]
Wdf01000: ...` lines.

Limits today:

- The framework and the loader are test-signed by NovaOS's build, so
  every KMDF driver needs test signing on ([below](#driver-signing)),
  even one Microsoft signed, until NovaOS has a trust anchor for its own
  system files.
- Class-extension drivers (those that bind with `WdfVersionBindClass`) do
  not load yet.

### UMDF 2

User-mode drivers run too: a DLL whose package's INF has a `.Wdf` section
(`UmdfService`, `UmdfServiceOrder`, `UmdfLibraryVersion`) and installs
the reflector, `WUDFRd.sys`, as the device's function driver. NovaOS
ships Microsoft's user-mode framework, `Wudfx02000.dll` (from the same
MIT source), and its own driver host, `WUDFHost.exe`, and reflector
(`tools/umdf/build_umdf.py`). When the device starts, the UMDF service
(`wudfsvc`) starts a host for it, which loads the framework and the
driver and runs its `DriverEntry`, `EvtDriverDeviceAdd`,
`EvtDevicePrepareHardware` and `EvtDeviceD0Entry`. A program's
`ReadFile`, `WriteFile` and `DeviceIoControl` on the driver's interface
travel through the reflector to its queues in the host and back.
Disabling the device removes it in the host, which exits.

Limits today:

- UMDF drivers built with Microsoft's WDK link Microsoft's UMDF stub,
  which binds through an interface Microsoft does not publish, so only
  drivers built with NovaOS's stub (`tools/umdf/stub.c`) run.
- I/O is buffered, at most 64 KiB a request.
- The host gives drivers no direct hardware access, interrupts, USB or
  remote I/O targets, or impersonation; one UMDF driver per device stack.
- The reflector is test-signed, so UMDF drivers need test signing on.

### USB client drivers

A Windows USB client driver runs a USB device that no NovaOS driver
took. NovaOS's USB core (`kernel/drivers/usb.c`, on xHCI, EHCI, UHCI or
OHCI) gives the device a PDO that answers as Windows' USB hub driver does
(`kernel/nt/usbd.c`):

- URBs sent with `IOCTL_INTERNAL_USB_SUBMIT_URB`: selecting a
  configuration and interface, control, bulk and interrupt transfers,
  aborting and resetting pipes. Isochronous transfers are not there yet.
- The port requests, the USB bus interface (`USB_BUS_INTERFACE_USBDI`,
  versions 0 to 3) and `usbd.sys`'s exports.
- Selective-suspend requests are accepted and held until cancelled;
  NovaOS does not suspend the device.

Each pipe's transfers run in order on their own thread, so a read waiting
for data does not hold up a write. A bus driver's children (FTDI's
`ftdibus.sys` reports a COM port) start with their own drivers once
installed.

A port (class Ports) is named when its driver is installed, as Windows'
ports installer does: the first free `COMn` from `COM3` (the `ComDB`
arbiter), stored in the device's `PortName` and friendly name. Programs
open it as `COM3` or `\\.\COM3` and use the communications functions
(`SetCommState`, `SetCommTimeouts`, `WaitCommEvent`, `PurgeComm` and the
rest), which send the driver the `IOCTL_SERIAL_*` requests Windows sends.
For example, with an FTDI USB serial converter plugged in,

```
pnputil /scan-devices /search-online /install
```

downloads, installs and starts FTDI's bus driver, then finds and installs
the port driver for the COM port it reports.

### Network drivers (NDIS)

A Windows NDIS 6 miniport runs a network card on NovaOS's own NDIS host
(`kernel/nt/ndis.c`; Microsoft's `ndis.sys` is never shipped). The
miniport registers with `NdisMRegisterMiniportDriver`; NovaOS starts the
card as Windows does (its PnP handlers, `MiniportInitializeEx` with the
card's resources, `MiniportRestart`, the packet filter) and hands the
running adapter to its network stack. DHCP, DNS and every program's
sockets then go through the Windows driver: frames to send become
`NET_BUFFER_LIST`s for `MiniportSendNetBufferLists`, and frames the
miniport indicates are copied to the stack. Interrupts are line-based,
DMA is scatter/gather, and the link state, MAC address and OID requests
work. NDIS filter and protocol drivers are not hosted yet.

virtio-win's `netkvm.sys` on QEMU's virtio-net card is the test driver;
NovaOS's own virtio-net driver steps aside for it.

### Storage drivers (Storport)

A Windows Storport miniport runs a disk controller on NovaOS's own
Storport host (`kernel/nt/storport.c`; Microsoft's `storport.sys` is
never shipped). The miniport registers with `StorPortInitialize`; NovaOS
starts the adapter as Windows does (`HwFindAdapter` with the controller's
resources, `HwInitialize`, the passive initialization routine), sends it
SCSI requests as `STORAGE_REQUEST_BLOCK`s through `HwBuildIo` and
`HwStartIo` with a scatter/gather list, and takes completions from
`StorPortNotification(RequestComplete)` in the miniport's interrupt and
DPCs. Each disk it finds becomes a block device `storN`, whose volumes
mount like any other disk's (`[DRIVES] D: is NTFS volume "..." on stor0`).

virtio-win's `viostor.sys` on QEMU's virtio-blk card is the test driver.
Limits today: one request at a time through a 64 KiB bounce buffer,
line-based interrupts only (no MSI-X), and a started adapter cannot be
removed. NovaOS still boots from its own built-in storage drivers.

### Wi-Fi: Intel's AX211 driver (partly)

Intel's Windows Wi-Fi driver for the AX211 in the ThinkPad T14 Gen 4,
`Netwtw14.sys` (package `Netwtw6e.inf`, version 24.40.0.4, for
`PCI\VEN_8086&DEV_51F1&SUBSYS_00948086`), downloads from the Update
Catalog by hardware ID, passes the signing check (its catalog carries
Microsoft's WHQL signature) and installs unchanged. It is a WDI
miniport: it registers with `NdisMRegisterWdiMiniportDriver`, and NDIS
rather than the driver owns the Wi-Fi state machine.

What works: the NDIS host calls `MiniportWdiAllocateAdapter` and
`MiniportWdiOpenAdapter`. In the open, the driver reads its firmware
(`netwfw14.dat`, through `IoGetDriverDirectory`, `ZwOpenFile` and
`ZwReadFile`), checks its signature (ECDSA P-384 and SHA-384 through
NovaOS's `cng.sys` exports over Mbed TLS) and probes its hardware.

What is missing: the WDI command layer (the `WDI_TASK_*` and `WDI_SET_*`
messages through `MiniportWdiStartOperation`, and the transmit and receive
path), scanning, connecting, WPA2/WPA3, and the adapter as the network
stack's. That is roadmap step 28.1, which needs the real card.

QEMU has no Intel Wi-Fi card, so the `ax211` self-test uses a PCI
stand-in: the QEMU firmware config file `opt/novaos/pci-standin`
(`VVVV:DDDD VVVV:DDDD SSSS:ssss CCSS`, here `1234:11e8 8086:51f1
8086:0094 0280`) makes `kernel/hal/pci.c` present QEMU's `edu` card with
the AX211's IDs (`[PCI] 00:03.0 1234:11e8 stands in for 8086:51f1,
subsystem 00948086 ...`). The test downloads Intel's real package through
a recorded Catalog (`tools/drivertest/ax211_catalog.py`), installs it with
test signing on (the driver is also a KMDF client), and checks that the
driver registers, reads its firmware, has its adapter allocated and
opened and reaches its hardware probe. The `edu` card is not an AX211, so
the probe fails there; that is where the stand-in stops. The probe on the
real card has not been tried.

### Display drivers (WDDM, display-only)

Windows display drivers are miniports of `dxgkrnl.sys`. NovaOS has the
display-only half of a stand-in (`kernel/nt/dxgkrnl.c`, step 1 of
[design/intel-gpu.md](design/intel-gpu.md)). A driver's
`DxgkInitializeDisplayOnlyDriver` opens `\Device\DxgKrnl` and registers
its `KMDDOD_INITIALIZATION_DATA`; the PnP manager starts it on its
adapter, and the host reads its outputs and modes through a VidPN. The
desktop then moves to the driver: what the desktop draws goes to its
`DxgkDdiPresentDisplayOnly`, and mode changes from Settings or
`ChangeDisplaySettings` become VidPNs the driver checks and commits. The
built-in Bochs VBE driver steps aside for it, and takes the screen back,
in the mode the driver left, when the driver is disabled or removed.

The test driver is `wddmdod.sys` (`tools/drivertest/wddmdod.c`), a
display-only driver for QEMU's standard VGA modelled on Microsoft's KMDOD
sample. Not there yet: render drivers (the full `DxgkInitialize`, which
Intel's GPU driver needs), interrupts and vertical blanks, power states,
and more than one source.

## Downloading drivers by hardware ID

Windows 11 finds a driver for a device that has none through Windows
Update, which offers the packages the
[Microsoft Update Catalog](https://www.catalog.update.microsoft.com)
serves, by hardware ID. NovaOS asks the same Catalog, when the user asks
it to:

    pnputil /scan-devices /search-online [/instanceid <device instance ID>] [/install]

For each device with problem code 28 (no driver), or the one named,
`pnputil`:

1. searches the Catalog (`Search.aspx?q=`) for the device's hardware IDs,
   most specific first, then its compatible IDs, until one has driver
   updates;
2. ranks them as a 64-bit Windows 11 PC would take them: Windows 11
   client before Windows 10, client before server, never ARM64, newest
   first;
3. asks the Catalog for the update's files (`DownloadDialog.aspx`, which
   gives each file's address, SHA-1 and SHA-256), downloads the cabinet to
   `C:\Windows\SoftwareDistribution\Download\Drivers\<update ID>\`, and
   refuses it unless its SHA-256 is the Catalog's;
4. expands it with `cabinet.dll` and looks for an INF with an x64 model
   (`[Models.NTamd64...]`) for one of the device's IDs, going on to the
   next update when there is none (up to three per ID);
5. stages the package in the driver store as `pnputil /add-driver` does
   (`oemN.inf`) and records where it came from under
   `HKLM\SYSTEM\DriverDatabase\DriverPackages\<package>`: `SourceName`
   ("Microsoft Update Catalog"), `SourceServer`, `SourceUpdateId`,
   `SourceTitle`, `SourceProducts`, `SourceVersion`, `SourceDate`,
   `SourceUrl`, `SourceSha256` (hex, checked), `SourceSha1` (base64, as
   the Catalog gives it), `SourceCabinet` (the kept cabinet) and
   `SourceHardwareId`.

A device whose package is already in the driver store is not searched
again. `pnputil /enum-drivers` shows a downloaded package's source, and
`pnputil /enum-devices /drivers` lists it as matching the device.

Without `/install` the package is only staged, and the device keeps
problem code 28 until `pnputil /add-driver x.inf /install` installs it.
With `/install`, each package found is installed on its device at once,
as Windows Update installs it, and the search goes round again for the
devices the new drivers bring (a USB serial converter's COM port), until
a round installs nothing new.

Nothing Microsoft or a vendor publishes is put in the NovaOS image or the
repository: the package comes from its publisher to the user's own
machine, as on Windows. `/source <url>` points the search at another
server that answers as the Catalog does. The self-tests use that so CI
needs no access to the Catalog:

| Self-test | Server | What it serves |
|---|---|---|
| `driversearch` | [`tools/drivertest/catalog_fixture.py`](../tools/drivertest/catalog_fixture.py) | pages recorded from the Catalog (`tests/selftest/devices/driversearch/catalog/`) |
| `usbserial` | [`tools/drivertest/ftdi_catalog.py`](../tools/drivertest/ftdi_catalog.py) | recorded pages and FTDI's real cabinet |
| `ax211` | [`tools/drivertest/ax211_catalog.py`](../tools/drivertest/ax211_catalog.py) | recorded pages and Intel's real cabinet |

The real cabinets are downloaded once from the Catalog's own address,
checked against the Catalog's SHA-256, and cached (`NOVA_DRIVER_CACHE`
names a folder to keep them in). Still to come: vendor feeds for packages
published outside the Catalog, and a Device Manager "Search automatically
for drivers" button.

### Driver signing

A kernel driver loads only if it is signed the way Windows 11 requires
(`kernel/nt/ci.c`), checked every time it starts:

- **Signed by Microsoft**: an Authenticode signature, embedded in the
  `.sys` or in the package's catalog (`.cat`) that `pnputil` installed,
  whose certificates chain to a Microsoft product root (Microsoft Root
  Certificate Authority 2010, or the 2001 root for older WHQL packages)
  and whose signing certificate is one for drivers: WHQL, attestation or
  Windows component signing. Packages on the Microsoft Update Catalog
  carry this, so they load as they come. Certificate expiry does not
  count, as on Windows. Cross-signed drivers (a vendor certificate under
  Microsoft's old cross-signing roots) are refused, as on Windows 11 with
  Secure Boot on.
- **Test-signed**: any other intact signature (a developer's or a test
  certificate) loads only with **test signing** on, NovaOS's equivalent
  of `bcdedit /set testsigning on`. Turn it on as an administrator and
  restart:

      reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f

  After the restart the log says `[NT] Test signing on` and
  `Control\SystemStartOptions` contains ` TESTSIGNING`, as on Windows.
  No "Test Mode" mark is drawn on the desktop yet.
- **Unsigned**, or changed after it was signed or installed: never, test
  signing or not.

A driver that fails the check does not start (Windows' error 577,
`ERROR_INVALID_IMAGE_HASH`; problem code 52 on a device), and the kernel
log says why: `[NT] x.sys: not loaded: it is not signed ...`, `... signed
by NAME (oem3.cat), which is not a Microsoft driver signature; it loads
only with test signing on`, or `... the image does not match its
signature`. Nothing is bypassed: a driver that needs more than this
(Secure Boot's measured state, HVCI, TPM attestation, as anti-cheat and
DRM drivers do) does not load.

When a package is staged (`pnputil /add-driver`, `DiInstallDriver`, a
setup program or a download from the Catalog), setupapi checks every file
against the package's catalog with `WinVerifyTrust`, as Windows does. A
file whose hash the catalog does not list is refused ("The hash for the
file is not present in the specified catalog file",
`ERROR_FILE_HASH_NOT_IN_CATALOG`) and nothing is staged. An intact
catalog is installed in `C:\Windows\System32\CatRoot` as `oemN.cat`,
where the kernel looks for it, and `pnputil /enum-drivers` shows its
signer. A package with no signed catalog, or one signed under a root
nobody trusts, is staged as unsigned (as Windows stages one after its
"Windows can't verify the publisher" warning; NovaOS does not ask), and
its drivers do not load.

Because NovaOS's own KMDF framework, KMDF loader and UMDF reflector are
test-signed, any KMDF or UMDF driver needs test signing on for now, even
a Microsoft-signed one such as `vioser.sys` or `Netwtw14.sys`. A
Microsoft-signed driver that does not use the frameworks, such as
`viostor.sys` (Storport) or `netkvm.sys` (NDIS), loads without it.

## Drivers checked in CI

Every one of these runs in the devices suite (`python3 tools/selftest.py
--suite devices`), on QEMU, in CI on every pull request that changes
code. Third-party packages are downloaded by the test (checked against pinned SHA-256s) and never
kept in the repository or the image.

| Boot | Driver | Device | What it shows |
|---|---|---|---|
| `wdm` | `wdmtest.sys`, `kmdftest.sys` (NovaOS's, test-signed) | none | the signing policy (unsigned, tampered and test-signed copies refused), `sc start`/`sc stop`, IRPs, the kernel API, KMDF on a control device |
| `edu` | `edutest.sys` (NovaOS's WDM driver) | QEMU's `edu` PCI device | `pnputil /add-driver /install`, memory BAR, interrupt, ISR and DPC, DMA, removal |
| `kmdfedu` | `kmdfedu.sys` (NovaOS's KMDF driver) | `edu` | KMDF resources, interrupt object (line, then MSI), DMA transactions, common buffer |
| `umdf` | `umdftest.dll` (NovaOS's UMDF 2 driver) | `edu` | the reflector and driver host, reads, writes and device controls |
| `vioser` | virtio-win's `vioser.sys` (KMDF) | virtio-serial | child devices, bytes exchanged with the host through `\\.\Global\<port name>` |
| `viostor` | virtio-win's `viostor.sys` (Storport) | virtio-blk | an NTFS volume mounted as D:, read and written, again after a restart |
| `netkvm` | virtio-win's `netkvm.sys` (NDIS) | virtio-net | the built-in driver stepping aside, DHCP, ping, a 32 MB download |
| `driversearch` | a NovaOS test package (`novaser.inf`) behind recorded Catalog pages | virtio-serial, with problem code 28 | `pnputil /scan-devices /search-online`: ranking, a bad SHA-256 refused, staging, no second download |
| `usbserial` | FTDI's `ftdibus.sys` and `ftser2k.sys` | QEMU's `usb-serial` (FTDI FT232) | download by hardware ID, the USB bus interface, COM3 |
| `ax211` | Intel's `Netwtw14.sys` (WDI) | `edu` standing in for the AX211 | download, signing, registration, firmware read, adapter open |
| `wddm` | `wddmdod.sys` (NovaOS's WDDM display-only driver) | QEMU's standard VGA | the dxgkrnl host, mode changes, the screen handed back on disable |

To run one boot's tests, pass their names to `--only` (`python3
tools/selftest.py --suite devices --list` prints them).

## The default firmware set

The built-in drivers' firmware comes from two places, both downloaded at
build time and never committed:

- [`tools/firmware.json`](../tools/firmware.json) names files from
  linux-firmware, grouped in sets by device, pinned to one linux-firmware
  tag (`20260916`) and each file's SHA-256. `tools/fetch_firmware.py`
  downloads them into `third_party/firmware`.
- `tools/fetch_sof_firmware.py` downloads the audio DSP's Sound Open
  Firmware (sof-bin v2026.09.1) into `third_party/sof-bin`.

`tools/build_userland.py` puts whatever was fetched into
`C:\Windows\Firmware`, keeping linux-firmware's own layout
(`i915/adlp_dmc.bin` becomes `C:\Windows\Firmware\i915\adlp_dmc.bin`),
with each set's licence file under `C:\Windows\Firmware\LICENSES`.

| Set | Device | Linux driver | Files | Used by NovaOS |
|---|---|---|---|---|
| `intel-graphics` | Intel Iris Xe and UHD graphics, Alder Lake-P and Raptor Lake-P (the T14 Gen 4) | `i915` | DMC `adlp_dmc.bin`, GuC `adlp_guc_70.bin`, HuC `tgl_huc.bin`; 1 MB | no. The chosen GPU design runs Intel's Windows driver, which carries its own firmware ([design/intel-gpu.md](design/intel-gpu.md)); the files are kept for the native fallback |
| `intel-wifi` | Intel Wi-Fi 6E AX211 | `iwlwifi` | `intel/iwlwifi/iwlwifi-so-a0-gf-a0-89.ucode` and its `.pnvm`; 1.7 MB | no. Intel's Windows driver uses its own `netwfw14.dat` from its package |
| `intel-bluetooth` | Intel AX211 Bluetooth | `btintel` | `intel/ibt-0040-0041.sfi` and `.ddc`; 0.7 MB | no (no Bluetooth driver) |
| `qualcomm-wifi` | Qualcomm WCN6855 Wi-Fi 6E (NFA725A, the T14 Gen 4's other card option) | `ath11k` | `ath11k/WCN6855/hw2.0/` `amss.bin`, `m3.bin`, `board-2.bin`, `regdb.bin`; 12 MB | no |
| `qualcomm-bluetooth` | Qualcomm WCN6855 Bluetooth | `btqca` | `qca/wcnhpbtfw21.tlv`, `qca/wcnhpnv21.bin`; 0.2 MB | no |
| Sound Open Firmware | Intel's audio DSP, Tiger Lake to Raptor Lake | `sof-audio-pci-intel-tgl` | `C:\Windows\Firmware\Intel\sof-ipc4\<platform>\sof-<platform>.ri` | yes: `kernel/drivers/sof.c` ([hardware.md](hardware.md#the-digital-microphones-behind-the-audio-dsp)) |

So today only the DSP firmware is loaded by anything. The five
linux-firmware sets date from before the Windows driver model was chosen,
when NovaOS planned native drivers for the reference laptop's GPU, Wi-Fi
and Bluetooth; they are still fetched for release images but no NovaOS
driver reads them. QEMU's, VMware's and Hyper-V's virtual devices run no
firmware, so a virtual machine needs none of it.

Every file's licence allows redistributing it unchanged: Intel's
graphics, Wi-Fi and Bluetooth licences (`LICENSE.i915`,
`LICENCE.iwlwifi_firmware`, `LICENCE.ibt_firmware`), Qualcomm's
(`LICENSE.QualcommAtheros_ath10k` with `NOTICE.qca`, which also limits
use to Qualcomm chipsets) and the SOF project's (BSD-3-Clause with
Intel's firmware licence, `LICENCE.Intel`). None may be modified or
reverse engineered, and NovaOS does neither.

### Building with it

```bash
python3 tools/fetch_firmware.py            # every set, and the DSP firmware
python3 tools/fetch_firmware.py --list     # the sets and their sizes
python3 tools/fetch_firmware.py --set intel-graphics --no-sof
python3 tools/fetch_firmware.py --from ~/linux-firmware   # a local checkout, no network
python3 tools/fetch_sof_firmware.py        # only the DSP firmware (Raptor Lake; --all for every platform)
```

then build as usual ([building.md](building.md)). `fetch_firmware.py`
downloads from git.kernel.org, falls back to the GitLab mirror, retries
with the pauses `tools/download_cache.py` uses, checks every SHA-256 and
keeps files it already has. Without it NovaOS builds and runs; the image
just carries no firmware, and the DSP driver logs that its file is
missing.

CI fetches firmware only for release builds (the build workflow's
"Fetch release firmware" step runs when `release` is set, which the tag
workflow does; see [releasing.md](releasing.md)). Pull requests and the
rolling "latest" build carry no firmware.

### Adding firmware

1. Find the files the device's Linux driver loads (its `MODULE_FIRMWARE`
   lines, or `WHENCE` in linux-firmware) and check that `WHENCE` says the
   licence allows redistribution. A file whose licence does not is not
   added.
2. Add a set to `tools/firmware.json` with each file's `path`, `sha256`
   and `size` at the pinned tag, and its licence file under `licences`.
3. Run `tools/fetch_firmware.py --set <id>` and list the set in the table
   above.

Moving to a newer linux-firmware tag means changing `tag` and every hash
together.
