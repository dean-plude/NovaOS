# The installer beside Windows (roadmap step 33.4)

How NovaOS's Setup installs NovaOS on a PC that already has Windows,
without touching the Windows install, the way Windows Setup and the
installers of other systems that dual-boot with Windows do it. How a
whole disk is installed, the firmware boot entry and the T14 notes are in
[install-and-power.md](../install-and-power.md); the boot menu and the
recovery boot belong to step 33.3 ([recovery.md](recovery.md)).

| Windows Setup | NovaOS Setup |
|---|---|
| "Where do you want to install Windows?": each disk's partitions and unallocated space | "Where should NovaOS go?": the disk's partitions and free space drawn as a map, with what NovaOS would take |
| Installs into unallocated space, keeping the other partitions | "Beside Windows": NovaOS's partitions in the largest free space, every other partition kept |
| Shares the disk's ESP, adds its files under `\EFI\Microsoft` | An ESP of its own, `NOVA_EFI` (see below) |
| Adds "Windows Boot Manager" to the firmware's boot entries | Adds "NovaOS" to them, first in `BootOrder`, Windows' entry kept after it |
| Boot Manager lists the other systems | NovaOS's boot menu lists "Windows Boot Manager" and starts it |

## Done (part 1): installing into the free space

<img src="../screenshots/install-where.png" alt="Setup's Where page: Windows' partitions and NovaOS in the free space" width="640">
<img src="../screenshots/install-confirm.png" alt="Setup's confirm page for installing beside Windows" width="640">

`SetupListDisks` (`kernel/fs/setup.c`) reads each disk's partition table
(`DiskReadLayout`, `kernel/fs/partition.c`), names each partition from its
volume (`"Windows (NTFS)"`, `"SYSTEM (EFI System)"`) or its GPT type
(`"Microsoft reserved"`, `"Windows recovery"`), finds the free spaces
between them on 1 MiB boundaries, and says whether NovaOS fits beside them:
a GPT disk with enough free space (`SetupBesideBytes`: an ESP sized for
the system twice and the boot files, about 600 MB today, plus 512 MiB for
drive C:), that NovaOS is not already on
and is not running from. A Windows disk is one with a Microsoft reserved
partition or `\EFI\Microsoft\Boot\bootmgfw.efi` on an ESP.

Setup's window (`kernel/apps/setup.c`) has a page between choosing the
disk and confirming: "Where should NovaOS go?", with the disk's partitions
as a bar to scale and a line for each, NovaOS drawn in the free space it
would take, and two choices, "Beside Windows, in the X GB of free space"
(chosen when it fits) and "Erase the disk and give all of it to NovaOS".
The Terminal's `install <disk> /beside` does the same.

Installing beside the other systems:

1. The disk's GPT is read again and NovaOS's two partitions are added in
   its largest free space, in the first unused entries: an EFI System
   Partition (FAT32 `NOVA_EFI`, sized as for a whole disk: the system, a
   staged update and the boot files) and a basic data partition
   (`NOVADATA`, NTFS or FAT32) for drive C: with the rest, at least
   512 MiB. Every partition already there keeps its entry number, place,
   type, GUIDs, attributes and name, so Windows' Boot Manager (which finds
   its partitions by GUID and offset) is not affected.
2. Only the first sectors of the two new partitions are cleared; the
   table is written with the backup copy at the end of the disk
   (`DiskWriteLayout`). Nothing of the other partitions is read or written.
3. The rest is the whole-disk installation: format, copy the boot loader,
   kernel and boot files, format drive C:, copy this session's files.
4. The disk's other volumes are mounted again (Windows' NTFS volume as a
   drive letter, as it was in the live session).

**Why an ESP of its own.** Windows Setup makes a 100 MB ESP (260 MB on
4K-sector disks). NovaOS's boot files are larger than that once a staged
update is counted, and an update writes to the ESP while Windows is
installed beside it. The UEFI specification allows several ESPs on a disk
and firmware searches each one; Windows only uses the ESP its own boot entry points at.
So Windows' ESP is never written, and removing NovaOS later is deleting
two partitions.

**The test.** The devices suite's `beside` boot
(`tests/selftest/devices/beside`) builds the disk of a PC with Windows
from the host (`windows_disk` in `tools/selftest.py`: ESP `SYSTEM` with a
stand-in `bootmgfw.efi`, a Microsoft reserved partition, an NTFS volume
`Windows` with a file, and 1.9 GiB free on a 3 GiB NVMe disk), starts
nova.iso from a USB stick, drives Setup's window from the keyboard
(a screenshot of each page in the work directory), starts NovaOS from the
disk and reads Windows' file from its drive letter. Once QEMU has quit the
host checks the GPT (Windows' three partitions where they were, NovaOS's
ESP and data partition in the free space), compares Windows' reserved
partition and each file on its ESP with their hashes from before, and
checks the NTFS volume with `scripts/ntfs-check.py` and `ntfscat`. (The
ESP is compared file by file because QEMU's firmware, with no flash store
for its variables, writes them to `NvVars` on the first ESP it finds; a
PC's firmware does not.)

The test found that NovaOS itself wrote to every FAT volume it only
looked at: unmounting rewrote the FAT32 FSInfo sector, with the free
cluster count "unknown". `FatSync` (`kernel/fs/fat.c`) now updates FSInfo
only on a volume something was written to, so listing the disks in Setup
leaves Windows' ESP as it was.

## Done (part 2): the firmware's boot entry and the boot menu

**The firmware boot entry from Setup.** A real PC already has "Windows
Boot Manager" first in `BootOrder`, so NovaOS's ESP alone would not be
started. As the last step of an installation (beside Windows or on the
whole disk) Setup calls the firmware's runtime services, `GetVariable` and
`SetVariable` (`kernel/hal/efi.c`, as Windows'
`SetFirmwareEnvironmentVariable` and `bcdedit /set {fwbootmgr}` do): it
writes `Boot####` "NovaOS" for `\EFI\BOOT\BOOTX64.EFI` on `NOVA_EFI`
(reusing the entry that already names that partition on a reinstall, else
the first free number) and puts it first in `BootOrder` with every other
entry after it, Windows' included. The entry's path is the short form
`HD(entry, GPT, unique GUID, start, size)/\EFI\BOOT\BOOTX64.EFI`, which
firmware expands to the disk wherever it is attached (UEFI 2.10, 3.1.2);
the boot loader's own check for its entry (`ensure_boot_entry`,
`bootloader/src/main.c`) accepts that form as well as its full one, so
the first start from the disk finds Setup's entry instead of adding a
second.

How the kernel calls the runtime services: the boot loader hands over the
firmware's `EFI_RUNTIME_SERVICES` table (boot protocol 6). NovaOS never
calls `SetVirtualAddressMap`, so the services run in physical mode, as the
specification allows: for each call the kernel switches to page tables that
map the first 4 GiB and every runtime or MMIO region the firmware listed
one to one (2 MiB pages, built on the first call) and its own half of the
address space as it is, with interrupts off, one CPU at a time, and the
caller's x87/SSE/AVX state saved around the call (the firmware is free to
use SSE). A firmware that refuses the variable leaves the old behaviour:
the boot loader adds its entry on the first start from the disk, through
the firmware's boot menu (F12 on a ThinkPad).

<img src="../screenshots/boot-menu.png" alt="NovaOS's boot menu offering NovaOS and Windows Boot Manager" width="640">

**Windows in NovaOS's boot menu.** On an installed disk the boot loader
(`bootloader/src/bootmenu.c`) looks on every other volume the firmware can
read for `\EFI\Microsoft\Boot\bootmgfw.efi`. When it finds one it shows
the boot menu before anything else (the update and rollback choice comes
after it): "1. NovaOS" and "2. Windows Boot Manager", with "NovaOS starts
in 5 s"; Up, Down or a line's number choose and stop the countdown, Enter
starts. Windows' boot manager is started as the firmware's boot manager
would start an entry: `LoadImage` from its volume's device path and file,
the firmware's watchdog armed, `StartImage`. If it returns (or cannot be
loaded) the menu shows again. One "Windows Boot Manager" line is enough:
it lists every Windows itself. Step 33.3's recovery choices (previous
version, safe mode, startup repair) join the same menu.

**The test.** The `beside` boot's stand-in `bootmgfw.efi` is a small UEFI
application (`bootmgfw_standin` in `tools/selftest.py`, a PE32+ the host
writes byte by byte) that prints a line on the firmware's console and
returns. After Setup the log must show its `Added the firmware boot entry
Boot#### "NovaOS" for nvme0n1, first in BootOrder` (read back after
writing); the first start from the disk must say `The firmware has a
"NovaOS" boot entry for this disk` (Setup's entry kept by the firmware
across the restart, and recognised); then a restart through the boot menu
(screenshot `boot-menu.png`) chooses Windows Boot Manager, the stand-in's
line appears, the menu comes back, and NovaOS is chosen and starts. QEMU's
firmware copies its console to the serial port, where the test reads the
menu. The `laptop` boot checks the same boot entry after a whole-disk
install.

## To do (the next parts of 33.4)

1. **Shrinking Windows' NTFS volume.** "Make room for NovaOS" on the Where
   page when the disk has too little free space: the volume is checked as
   chkdsk would (clean, not hibernated, no Fast Startup), its clusters past
   the new end are moved down (NovaOS's NTFS driver, `kernel/fs/ntfs.c`,
   moving data runs and updating `$Bitmap`, `$MFT` runs and the boot
   sector's sector count, as ntfsresize does; ntfsresize itself is GPL,
   so the method is reimplemented, not its code), then the partition
   entry is made smaller.
2. **Reinstalling keeps the data.** On a disk where NovaOS is installed,
   "Reinstall NovaOS" replaces the ESP's system files and keeps the
   `NOVADATA` partition (drive C:, `C:\Users`).
3. **The step's screenshots**: the Where and confirm pages are in
   `docs/screenshots/` (`install-where.png`, `install-confirm.png`); the
   `beside` boot saves every page (`setup-*.png` in its output), and the
   rest join them once the pages above exist.
