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
`pnputil`, a setup program or an `.msi` puts it in the driver store.  But
the `.sys` is a Windows kernel-mode driver: it calls `ntoskrnl.exe`'s
kernel API (the I/O manager, IRPs, WDF through `Wdf01000.sys`, NDIS for
network cards, the WDDM `dxgkrnl.sys` interface for GPUs), which NovaOS's
kernel does not offer yet, so today a staged package is not started.

That is changing: NovaOS will run Windows drivers the way Windows 11 does,
through the NT kernel-mode driver interface, with KMDF and UMDF from
Microsoft's own MIT-licensed source, packages matched by hardware ID from
the driver store and downloaded by hardware ID when none is there.  The
design and its steps (roadmap Phase 30) are in
[design/windows-drivers.md](design/windows-drivers.md).  Until those
steps land, the default set is the built-in drivers and the firmware
below.

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

    pnputil /scan-devices /search-online [/instanceid <device instance ID>]

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
package is staged, not started: starting Windows drivers on devices comes
with roadmap step 30.3, so for now the device keeps problem code 28.

Nothing Microsoft or a vendor publishes is put in the NovaOS image: the
package comes from its publisher to the user's own machine, as on
Windows.  `/source <url>` points the search at another server that answers
as the Catalog does; the `driversearch` self-test uses that with
[`tools/drivertest/catalog_fixture.py`](../tools/drivertest/catalog_fixture.py),
which serves pages recorded from the Catalog
(`tests/selftest/devices/driversearch/catalog/`) so CI needs no network.
Vendor feeds for packages published outside the Catalog
(`userland/drivers/feeds.json` in the design) and Device Manager's
"Search automatically for drivers" are still to come.
