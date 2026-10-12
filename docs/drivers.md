# Drivers and firmware

Windows and Linux both come with a default set of drivers, so most PCs work
from the first boot.  NovaOS does the same in the way Linux does, because
of how its kernel is built:

- **Drivers** are NovaOS's own and built into the kernel (`kernel/drivers/`),
  as Linux's in-tree drivers are.  Every one is on the image; nothing has
  to be installed.  [hardware.md](hardware.md#devices-novaos-drives) lists
  them by device.
- **Firmware**, the programs a driver loads into the device itself (a
  GPU's display and power microcontrollers, a Wi-Fi card, a Bluetooth
  radio, an audio DSP), is not written by anyone but the vendor.  Linux
  distributions download it from the linux-firmware repository, where
  vendors publish it under licences that allow redistributing it
  unchanged.  NovaOS downloads its default firmware set from the same
  place (below).

## Windows driver packages

A Windows driver package (an `.inf` with `.sys` and `.cat` files)
installs on NovaOS as on Windows ([user-guide.md](user-guide.md#installing-programs)):
`pnputil`, a setup program or an `.msi` puts it in the driver store.  The
`.sys` is a Windows kernel-mode driver, and NovaOS is learning to run those
the way Windows 11 does, through the NT kernel-mode driver interface, with
KMDF and UMDF from Microsoft's own MIT-licensed source, packages matched
by hardware ID from the driver store and downloaded by hardware ID when
none is there.  The design and its steps (roadmap Phase 30) are in
[design/windows-drivers.md](design/windows-drivers.md).

What runs today (step 30.1): a kernel driver installed as a service
(`sc create NAME type= kernel binPath= C:\path\driver.sys`) starts with
`sc start NAME` and stops with `sc stop NAME`.  The kernel loads the
`.sys` into kernel memory and calls its `DriverEntry`; the devices and
`\DosDevices` links it creates open from programs (`\\.\NAME`), whose
`ReadFile`, `WriteFile` and `DeviceIoControl` reach the driver's dispatch
routines as Windows IRPs.  Since step 30.2 the driver can also use the
kernel API most drivers are written against: the IRQL (kept in `CR8`, as
on Windows x64), spin locks, DPCs, timers, events, semaphores, fast
mutexes, the pool (`ExAllocatePool2`, `ExAllocatePoolWithTag`) and MDLs
with direct I/O, and it may return `STATUS_PENDING` and complete a request
later, from a DPC.  Pool blocks a driver never frees are named in the log
when it unloads (`[NT] x.sys: 2 pool blocks (96 bytes, tags 'Abcd') never
freed`).  Only part of `ntoskrnl.exe` is there yet (the list is
`kernel/nt/exports.h`); a driver that imports anything else does not load,
and the log names what it missed (`[NT] x.sys: ntoskrnl.exe!KeFoo not
implemented`).  `tools/sys_imports.py x.sys` lists, before trying, the
imports of a driver that NovaOS does not have yet.

Since step 30.3 a Windows **Plug and Play driver** runs a PCI device that
has no NovaOS driver.  `pnputil /add-driver x.inf /install` (or a setup
program's `UpdateDriverForPlugAndPlayDevices`) installs the package on
each such device one of its models names, and the driver starts at once
and at every boot after: NovaOS calls its `AddDevice` and gives it the
device's memory, I/O ports and interrupt with `IRP_MN_START_DEVICE`, as
Windows does, and the driver can map the memory (`MmMapIoSpace`), connect
the interrupt (`IoConnectInterruptEx`; NovaOS gives Windows drivers no
MSI yet, so it is the line-based interrupt), use DMA (`IoGetDmaAdapter`)
and register a device interface that programs open.  `pnputil
/enum-devices /deviceid ID` shows the device started with its driver,
`pnputil /disable-device /deviceid ID` takes the driver away (it may
refuse while a program has the device open) and keeps the device off
across restarts, and `/enable-device` starts it again.  A device NovaOS
has its own driver for keeps it.  Since step 30.4 KMDF drivers load
too, on `Wdf01000.sys` and `WdfLdr.sys`; the rest follow in later steps,
and until then the default set is the built-in drivers and the firmware
below.

Since step 30.8 a Windows **USB client driver** runs a USB device that
has no NovaOS driver, the way a vendor's driver runs it on Windows.
NovaOS's USB core (`kernel/drivers/usb.c`, on xHCI, EHCI, UHCI or OHCI)
gives the device a physical device object that answers as Windows' USB
hub driver does (`kernel/nt/usbd.c`): the URBs a driver sends with
`IOCTL_INTERNAL_USB_SUBMIT_URB` (selecting a configuration and interface,
control, bulk and interrupt transfers, aborting and resetting pipes;
isochronous transfers are not there yet), the port requests, the USB bus
interface (`USB_BUS_INTERFACE_USBDI`, versions 0 to 3), selective-suspend
requests (held until cancelled: NovaOS does not suspend a device) and
`usbd.sys`'s exports.  Each pipe's transfers run in order on a thread of
its own, so a read waiting for data does not hold up a write.  A driver
that is a bus (FTDI's `ftdibus.sys` reports a COM port) has its children
added to the device tree, and their drivers start once installed.
Unplugging the device sends its drivers `IRP_MN_SURPRISE_REMOVAL` and
`IRP_MN_REMOVE_DEVICE`, children first.  A port (class Ports) is named
when its driver is installed, as Windows' ports installer names it: the
first free `COMn` from `COM3` (the COM name arbiter's `ComDB`), in the
device's `PortName` and its friendly name.  Programs open it as `COM3` or
`\\.\COM3` and set it up with the communications functions
(`SetCommState`, `SetCommTimeouts`, `WaitCommEvent`, `PurgeComm` and the
rest), which send the driver the `IOCTL_SERIAL_*` requests Windows sends.

    pnputil /scan-devices /search-online /install

downloads, installs and starts FTDI's driver for an FTDI USB serial
converter, then finds and installs the port driver for the COM port it
reports.

KMDF drivers run too (step 30.4): NovaOS ships Microsoft's framework,
`Wdf01000.sys`, built from Microsoft's MIT-licensed source, and its own
KMDF loader, `WdfLdr.sys`, both in `C:\Windows\System32\drivers` as on
Windows (`tools/kmdf/build_kmdf.py`).  A KMDF driver starting imports
`WdfLdr.sys`, which loads `Wdf01000.sys` and binds the driver to KMDF
1.33; the framework's error messages go to the log as `[DbgPrint]
Wdf01000: ...` lines.  For now the framework is test-signed,
so a KMDF driver needs test signing on (below) until NovaOS trusts its own
system files; class-extension drivers (those that bind with
`WdfVersionBindClass`) do not load yet.

The first third-party driver that runs unchanged is virtio-win's
**`vioser.sys`** (step 30.5), the Red Hat serial driver Microsoft signed,
for QEMU's `virtio-serial-pci`: `pnputil /add-driver vioser.inf /install`
starts it on the device, and each port the host offers becomes a child
device (`{6FDE7547-1B65-48ae-B628-80BE62016026}\VIOSerialPort\...`) that
programs open as `\\.\Global\<port name>`, or through its
`GUID_VIOSERIAL_PORT` interface, to exchange bytes with the host.  For
this NovaOS's PnP manager now enumerates **bus drivers**' children, as
Windows does: a driver that calls `IoInvalidateDeviceRelations` is asked
for its `BusRelations`, each new child gets a device node from its
`IRP_MN_QUERY_ID` answers and starts with its own driver, or raw (with the
bus driver alone) when it says it may, and a child no longer reported is
removed.  `vioser.sys` itself is Microsoft-signed, but it runs on the
KMDF framework above, so it too needs test signing on for now.

Since step 30.11 a Windows **storage driver** (a Storport miniport) runs a
disk controller, on NovaOS's own Storport host (`kernel/nt/storport.c`,
the `storport.sys` exports; Microsoft's `storport.sys` is never shipped).
The miniport registers with `StorPortInitialize`; NovaOS starts the
adapter as Windows does (`HwFindAdapter` with the controller's resources
in its `PORT_CONFIGURATION_INFORMATION`, `HwInitialize`, the passive
initialization routine), sends it SCSI requests as `STORAGE_REQUEST_BLOCK`s
through `HwBuildIo` and `HwStartIo` with a scatter/gather list, and takes
completions from `StorPortNotification(RequestComplete)` in the
miniport's interrupt and DPCs.  Each disk it finds (INQUIRY, READ
CAPACITY) joins the block layer as `storN`, and its volumes mount like any
other disk's (`[DRIVES] D: is NTFS volume "..." on stor0`).  virtio-win's
`viostor.sys` on QEMU's virtio-blk card is the first.  For now the host
sends one request at a time through a 64 KiB bounce buffer, interrupts
are line-based (no MSI-X), and a started adapter cannot be removed yet;
deeper queues, MSI-X and removal come later.

### Driver signing

A kernel driver loads only if it is signed the way Windows 11 requires
(step 30.7, `kernel/nt/ci.c`), checked every time it starts:

- **Signed by Microsoft**: an Authenticode signature, embedded in the
  `.sys` or in the package's catalog (`.cat`) that `pnputil` installed,
  whose certificates chain to a Microsoft product root (Microsoft Root
  Certificate Authority 2010, or the 2001 root for older WHQL packages)
  and whose signing certificate is one for drivers: WHQL, attestation or
  Windows component signing.  That is what the packages on the Microsoft
  Update Catalog carry, so they load as they come (checked on Red Hat's
  WHQL-signed `vioser.sys`).  Certificate expiry does not count, as on
  Windows.
- **Test-signed**: any other intact signature (a developer's or a test
  certificate) loads only with **test signing** on, NovaOS's
  `bcdedit /set testsigning on`.  Turn it on, as an administrator, and
  restart:

      reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f

  `Control\SystemStartOptions` then shows ` TESTSIGNING`, as on Windows.
- **Unsigned**, or changed after it was signed or installed: never, test
  signing or not.

A driver that fails starts with Windows' error 577
(`ERROR_INVALID_IMAGE_HASH`, `STATUS_INVALID_IMAGE_HASH`), and the kernel
log says why: `[NT] x.sys: not loaded: it is not signed`, `... signed by
NAME (oem3.cat), which is not a Microsoft driver signature; it loads only
with test signing on`, or `... the image does not match its signature`.
Nothing is bypassed: a driver that needs more than this (Secure Boot's
measured state, HVCI, TPM attestation, as anti-cheat and DRM drivers do)
does not load.

When a package is staged (`pnputil /add-driver`, `DiInstallDriver`, a
setup program or a download from the Update Catalog), setupapi checks
every file against the package's catalog with `WinVerifyTrust`, as
Windows does.  A file whose hash the catalog does not list is refused
("The hash for the file is not present in the specified catalog file",
`ERROR_FILE_HASH_NOT_IN_CATALOG`) and nothing is staged.  An intact
catalog is installed in `C:\Windows\System32\CatRoot` as `oemN.cat`,
where the kernel looks for it, and `pnputil /enum-drivers` shows its
signer.  A package with no signed catalog, or one signed under a root
nobody trusts, is staged as unsigned, as Windows stages one after its
"Windows can't verify the publisher" warning (NovaOS does not ask yet),
and its drivers do not load.

## The default firmware set

[`tools/firmware.json`](../tools/firmware.json) names the firmware NovaOS
ships, grouped in sets by device, pinned to one linux-firmware tag
(`20260916`) and each file's SHA-256.  `tools/fetch_firmware.py` downloads
it into `third_party/firmware` (not committed), and the build puts it in
`C:\Windows\Firmware` with linux-firmware's own layout, so
`i915/adlp_dmc.bin` becomes `C:\Windows\Firmware\i915\adlp_dmc.bin` and a
NovaOS driver finds a file by the same name its Linux counterpart asks
for.  Each set's licence file goes beside it under
`C:\Windows\Firmware\LICENSES`.

| Set | Device | Linux driver | Files | NovaOS driver |
|---|---|---|---|---|
| `intel-graphics` | Intel Iris Xe and UHD graphics, Alder Lake-P and Raptor Lake-P (the ThinkPad T14 Gen 4) | `i915` | display microcontroller (DMC) `adlp_dmc.bin`, GuC `adlp_guc_70.bin`, HuC `tgl_huc.bin`; 1 MB | not yet: the native Intel GPU driver is roadmap step 25.3 |
| `intel-wifi` | Intel Wi-Fi 6E AX211 (Alder Lake and Raptor Lake laptops, one of the T14 Gen 4's two cards) | `iwlwifi` | `intel/iwlwifi/iwlwifi-so-a0-gf-a0-89.ucode` and its `.pnvm`; 1.7 MB | not yet (Wi-Fi is "missing" in [hardware.md](hardware.md#devices)) |
| `intel-bluetooth` | Intel AX211 Bluetooth | `btintel` | `intel/ibt-0040-0041.sfi` and `.ddc`; 0.7 MB | not yet |
| `qualcomm-wifi` | Qualcomm WCN6855 Wi-Fi 6E (NFA725A, the T14 Gen 4's other card) | `ath11k` | `ath11k/WCN6855/hw2.0/` `amss.bin`, `m3.bin`, `board-2.bin`, `regdb.bin`; 12 MB | not yet |
| `qualcomm-bluetooth` | Qualcomm WCN6855 Bluetooth | `btqca` | `qca/wcnhpbtfw21.tlv`, `qca/wcnhpnv21.bin`; 0.2 MB | not yet |
| Sound Open Firmware | Intel's audio DSP, Tiger Lake to Raptor Lake | `sof-audio-pci-intel-tgl` | `C:\Windows\Firmware\Intel\sof-ipc4\<platform>\sof-<platform>.ri`, from the SOF project's own releases (`tools/fetch_sof_firmware.py`) | `kernel/drivers/sof.c` ([hardware.md](hardware.md#the-digital-microphones-behind-the-audio-dsp)) |

The first five sets are for the reference laptop's devices whose NovaOS
drivers come next, so those drivers find their firmware already on every
release image; until then nothing loads them.  QEMU's, VMware's and
Hyper-V's virtual devices run no firmware, so a virtual machine needs
none of it.

Every file's licence allows redistributing it unchanged: Intel's
graphics, Wi-Fi and Bluetooth licences (`LICENSE.i915`,
`LICENCE.iwlwifi_firmware`, `LICENCE.ibt_firmware`) and Qualcomm's
(`LICENSE.QualcommAtheros_ath10k` with `NOTICE.qca`, which also limits
use to Qualcomm chipsets).  None may be modified or reverse engineered,
and NovaOS does neither.

### Building with it

```bash
python3 tools/fetch_firmware.py            # every set, and the DSP firmware
python3 tools/fetch_firmware.py --list     # the sets and their sizes
python3 tools/fetch_firmware.py --set intel-graphics --no-sof
python3 tools/fetch_firmware.py --from ~/linux-firmware   # a local checkout, no network
```

then build as usual ([building.md](building.md)).  The script downloads
from git.kernel.org, falls back to the GitLab mirror, retries with the
pauses `tools/download_cache.py` uses, checks every SHA-256 and keeps
files it already has.  Without it NovaOS builds and runs; the image just
carries no firmware.  Release builds run it before building
([releasing.md](releasing.md)); pull requests and the "latest" build do
not, as with the DSP firmware before.

### Adding firmware

1. Find the files the device's Linux driver loads (its `MODULE_FIRMWARE`
   lines, or `WHENCE` in linux-firmware) and check `WHENCE` says the
   licence allows redistribution.  A file whose licence does not (some
   vendors' Windows-only packages) is not added: the user downloads it,
   and the NovaOS driver says in the boot log which file it is missing.
2. Add a set to `tools/firmware.json` with each file's `path`, `sha256`
   and `size` at the pinned tag, and its licence file under `licences`.
3. Run `tools/fetch_firmware.py --set <id>` and list the set in the table
   above.

Moving to a newer linux-firmware tag is changing `tag` and every hash
together.

## Downloading drivers by hardware ID

Windows 11 finds a driver for a device it has none for by the device's
hardware ID, through Windows Update, which offers the packages the
[Microsoft Update Catalog](https://www.catalog.update.microsoft.com)
serves.  NovaOS asks the same Catalog (roadmap step 30.6), when the user
asks it to:

    pnputil /scan-devices /search-online [/instanceid <device instance ID>] [/install]

For each device with problem code 28 (no driver), or the one named,
`pnputil`:

1. searches the Catalog (`Search.aspx?q=`) for the device's hardware IDs,
   most specific first, then its compatible IDs, until one has driver
   updates;
2. ranks them as a 64-bit Windows 11 PC would take them: Windows 11 client
   before Windows 10, client before server, never ARM64, newest first;
3. asks the Catalog for the update's files (`DownloadDialog.aspx`, which
   gives each file's address, SHA-1 and SHA-256), downloads the cabinet to
   `C:\Windows\SoftwareDistribution\Download\Drivers\<update ID>\`,
   and refuses it unless its SHA-256 is the Catalog's;
4. expands it with `cabinet.dll` and looks for an INF with an x64 model
   (`[Models.NTamd64...]`) for one of the device's IDs, going on to the
   next update when there is none (up to three per ID);
5. stages the package in the driver store as `pnputil /add-driver` does
   (`oemN.inf`), and records where it came from in its
   `HKLM\SYSTEM\DriverDatabase\DriverPackages\<package>` key:
   `SourceName` ("Microsoft Update Catalog"), `SourceServer`,
   `SourceUpdateId`, `SourceTitle`, `SourceProducts`, `SourceVersion`,
   `SourceDate`, `SourceUrl`, `SourceSha256` (hex, checked),
   `SourceSha1` (base64, as the Catalog gives it), `SourceCabinet` (the
   kept cabinet, whose signature the signing checks of step 30.7 will
   verify) and `SourceHardwareId`.

A device whose package is already in the driver store is not searched
again.  `pnputil /enum-drivers` shows a downloaded package's source, and
`pnputil /enum-devices /drivers` lists it as matching the device.  The
package is staged, not started, so the device keeps problem code 28 until
`pnputil /add-driver x.inf /install` installs it on the device (roadmap
step 30.3, above), unless the search was given `/install`: then each
package found is installed on its device at once, as Windows Update
installs it, and the search goes round again for the devices the drivers
it started bring (a USB serial converter's COM port, step 30.8), until a
round installs nothing new.

Nothing Microsoft or a vendor publishes is put in the NovaOS image: the
package comes from its publisher to the user's own machine, as on
Windows.  `/source <url>` points the search at another server that answers
as the Catalog does; the `driversearch` self-test uses that with
[`tools/drivertest/catalog_fixture.py`](../tools/drivertest/catalog_fixture.py),
which serves pages recorded from the Catalog
(`tests/selftest/devices/driversearch/catalog/`) so CI needs no network.
The `usbserial` self-test does the same with
[`tools/drivertest/ftdi_catalog.py`](../tools/drivertest/ftdi_catalog.py)
and FTDI's real cabinet, which it downloads once from the Catalog's
address (checked against the Catalog's SHA-256; `NOVA_DRIVER_CACHE` names
a folder to keep it in) and never puts in the repository or the image.
Vendor feeds for packages published outside the Catalog
(`userland/drivers/feeds.json` in the design) and Device Manager's
"Search automatically for drivers" are still to come.
