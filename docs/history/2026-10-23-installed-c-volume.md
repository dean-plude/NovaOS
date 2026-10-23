## An installed drive C: shows its disk, not a RAM disk

After installing NovaOS to a disk, Disk Management still listed drive
C: as a RAMFS volume whose capacity was the machine's memory (4.13 GB
on a 4 GB Hyper-V machine, on a 9.44 GB NTFS partition).  C: was saved
to that partition all along; only the answers about it were wrong:
`GetVolumeInformation("C:\")` always named RAMFS, and
`GetDiskFreeSpaceEx` and `NtQueryVolumeInformationFile` on C: gave the
machine's memory.

- **Kernel** (`um/um_syscall.c`, `fs/persist.c`): once C: is saved to a
  volume, its `FileFsSizeInformation`, `FileFsFullSizeInformation` and
  `FileFsAttributeInformation` are that volume's size, free space and
  file system (`PersistFileSystem`: FAT32 or NTFS; NTFS adds
  `FILE_PERSISTENT_ACLS`).  A C: kept in memory only answers as before.
- **`GetVolumeInformation`** (`kernel32/extra.c`): C:'s file system is
  the one its volume reports (`\\.\C:`): RAMFS in memory only, else FAT32
  or NTFS, as Disk Management shows it.  Its label and serial number stay
  NovaOS's, so files on C: still match their volume.
- **Self-tests**: `cachetest` checks that C: reports the data disk's
  FAT32 and size; it and `ramdisktest` measure free memory with
  `GlobalMemoryStatusEx` now that C:'s free space is the disk's.
