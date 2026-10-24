## Setup installs drive C: on NTFS only

Since step 2 of the on-disk drive C: plan, an installed system's C: is
its NTFS volume, used in place. Setup could still format drive C: as
FAT32, and such an install kept C: in a RAM disk saved to the volume. Now
Setup formats drive C: as NTFS only, so after any install the RAM disk is
used only by the installation media (docs/design/disk-c.md, step 3).

- **Setup's window** (`kernel/apps/setup.c`): the confirm page has no
  NTFS/FAT32 choice any more; it says drive C: is a new NTFS volume used
  in place.
- **The Terminal's `install`** (`kernel/apps/terminal.c`): `/fat` is
  refused with a message, and the usage line no longer offers it.
- **`SetupStart`** (`kernel/fs/setup.c`) lost its file-system argument
  and the FAT32 drive C: format; the 1 TiB cap a FAT32 drive C: had's size
  went with it (`tests/tools/test_installer_layout.py`).
- **An older FAT32 drive C:** (`SetupDisk.fat_c`): reinstalling cannot
  keep it, as drive C: is NTFS now. The disk list says "an older NovaOS
  (drive C: on FAT32)", `install` explains it, `/reinstall` is refused,
  and Setup itself fails the reinstall if it finds one. Such an install
  still starts as before.
- **Docs**: install-and-power.md, user-guide.md, building.md,
  design/installer.md, design/disk-c.md (with what still uses the RAM
  disk), the README and the NTFS roadmap item.
