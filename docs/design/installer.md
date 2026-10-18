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
| Adds "Windows Boot Manager" to the firmware's boot entries | The boot loader adds "NovaOS" on its first start (to do: Setup adds it, below) |
| Boot Manager lists the other systems | NovaOS's boot menu lists "Windows Boot Manager" (to do, with 33.3's menu) |

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

In QEMU the firmware has no boot entries, finds no `\EFI\BOOT\BOOTX64.EFI`
on Windows' ESP and starts NovaOS's from the second ESP, and NovaOS's boot
loader then adds its "NovaOS" entry. A real PC already has a "Windows Boot
Manager" entry first in `BootOrder`, so until Setup adds NovaOS's entry
itself (below), the first start of NovaOS there is through the firmware's
boot menu (F12 on a ThinkPad: the disk's NovaOS partition, or "Boot from
file" `\EFI\BOOT\BOOTX64.EFI` on `NOVA_EFI`).

## To do (the next parts of 33.4)

1. **The firmware boot entry from Setup.** NovaOS calls the UEFI runtime
   services' `SetVariable` (as Windows' `SetFirmwareEnvironmentVariable`
   and `bcdedit /set {fwbootmgr}` do) to add "NovaOS" for
   `\EFI\BOOT\BOOTX64.EFI` on `NOVA_EFI` and put it first in `BootOrder`,
   keeping "Windows Boot Manager" in the list.
2. **Windows in NovaOS's boot menu.** The boot loader finds
   `\EFI\Microsoft\Boot\bootmgfw.efi` on the disk's other ESPs and lists
   "Windows Boot Manager" in step 33.3's boot menu, starting it with
   `LoadImage`/`StartImage`; the test boot then checks the stand-in is
   offered (the step's "offers the other system in the boot menu").
3. **Shrinking Windows' NTFS volume.** "Make room for NovaOS" on the Where
   page when the disk has too little free space: the volume is checked as
   chkdsk would (clean, not hibernated, no Fast Startup), its clusters past
   the new end are moved down (NovaOS's NTFS driver, `kernel/fs/ntfs.c`,
   moving data runs and updating `$Bitmap`, `$MFT` runs and the boot
   sector's sector count, as ntfsresize does; ntfsresize itself is GPL,
   so the method is reimplemented, not its code), then the partition
   entry is made smaller.
4. **Reinstalling keeps the data.** On a disk where NovaOS is installed,
   "Reinstall NovaOS" replaces the ESP's system files and keeps the
   `NOVADATA` partition (drive C:, `C:\Users`).
5. **The step's screenshots**: the Where and confirm pages are in
   `docs/screenshots/` (`install-where.png`, `install-confirm.png`); the
   `beside` boot saves every page (`setup-*.png` in its output), and the
   rest join them once the pages above exist.
