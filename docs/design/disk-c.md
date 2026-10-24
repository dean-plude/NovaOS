# Drive C: on its disk

Asked for on 2026-10-23: once NovaOS is installed, drive C: is the disk
volume it was installed to, as on Windows. The RAM disk is only for the
installer and the live system.

## Before

C: was always the RAM disk (`fs/ramfs.c`). On an installed system,
`fs/persist.c` restored the tree from the disk at every start. File
contents were read only when wanted, and when memory ran short the
saved ones were let go of again. Changes were saved about a second
after the last one. The system DLLs and programs, which are built into
`kernel.elf`, were unpacked into memory at every start and never
written to the disk.

Drives D:, E:, ... already work the way C: should: they are volumes
mounted into the same tree (`RamfsMountDrive`, a `RamfsSource` that
lists, reads, creates, deletes, renames, links and writes). A folder's
entries are read the first time something looks in it, and a file's
contents when it is opened. The contents are written back when nothing
holds the file any more.

## After

- **Live system and installer:** C: is the RAM disk, as before. A live
  stick with a NOVADATA FAT volume still saves C: there.
- **Installed system (NTFS):** C: is the NTFS volume. `PersistInit`
  mounts it at the root of the tree as a volume like D:, so there is no
  restore at start, no saving behind the scenes and no letting go.
  Files are held in memory only while open.
- **System files** live on the disk in `C:\Windows` and `C:\Programs`.
  Setup writes them. At each start, a stamp of the kernel build (and of
  the files beside it) decides whether they are written again, which is
  how an update brings new ones.
- **Security descriptors** of C: are the volume's own (`$Secure`), as
  on any NTFS drive since step 1.
- **FAT32:** Setup installs on NTFS only (the window has no file
  system choice, and the Terminal's `install /fat` is refused). An
  existing FAT32 install, from an older NovaOS, keeps the old way (RAM
  disk saved to `\NOVA\C`); Setup cannot keep that drive C: when
  reinstalling, so the disk is erased and installed again on NTFS.

## Steps (one pull request each)

1. Drives on disk keep what C: relies on: times and attributes
   (`RamfsSource.set_info`), security descriptors on NTFS
   (`RamfsSource.security`/`set_security`, `RamfsLoadSecurity`,
   `RamfsSecurityChanged`), and moves on FAT.
2. Installed C: on its disk: the mount (`RamfsMountC`, `PersistInit`),
   the system-file stamp (`UmInit`, `g_userland_digest` from
   `tools/build_userland.py`, `BootFilesDigest`), and the devices boot
   `diskc`.  `Reset this PC` removes the system files too,
   and the restart writes them again.  Setup refuses to install over the
   disk the running system's C: is on.
3. Setup installs on NTFS only (`SetupStart` lost its file-system
   argument; `SetupDisk.fat_c` marks an older FAT32 drive C:, which a
   reinstall refuses to keep), and the docs.  Settings, Disk Management
   and the Terminal's `mem` already describe the on-disk C: since step 2.

## What still uses the RAM disk

- The installation media (live), with or without a `NOVADATA` volume
  to save to.
- An installed system whose NTFS volume could not be mounted in place
  (the boot log says so): C: falls back to the RAM disk, saved to the
  volume, rather than not starting.
- An install on FAT32 made by an older NovaOS.
