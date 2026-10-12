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
routines as Windows IRPs.  Only the small part of `ntoskrnl.exe` such a
driver needs is there yet (the list is `kernel/nt/exports.h`); a driver
that imports anything else does not load, and the log names what it
missed (`[NT] x.sys: ntoskrnl.exe!KeFoo not implemented`).  Plug and Play
drivers for real devices, KMDF and the rest follow in later steps; until
then the default set is the built-in drivers and the firmware below.

NovaOS does not check driver signatures yet (step 30.7), so it loads
kernel drivers only with **test signing** on, as Windows does for drivers
not signed by Microsoft.  Turn it on, as an administrator, and restart:

    reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f

With it off, starting a kernel driver fails with Windows' error 577
(`ERROR_INVALID_IMAGE_HASH`), as an unsigned driver does on Windows.  A
kernel driver runs with full access to the machine, so only turn it on
for drivers you trust.

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
