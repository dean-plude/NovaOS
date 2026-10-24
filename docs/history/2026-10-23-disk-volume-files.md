## Drives on disk keep attributes, times and permissions

The first of three steps toward an installed drive C: that is the disk's
own NTFS or FAT volume, with the RAM disk kept for live and setup boots
only: drives mounted from disk (D:, E:, ...) now do what C: relies on.

- **Attributes and times** set with `SetFileAttributes` and `SetFileTime`
  are written to the volume at once (NTFS's standard information and
  directory entries, FAT's directory entry), and times set on a file
  before its new contents reach the disk are given again after them, as
  an extractor sets an archive's times before closing.
- **Permissions on NTFS:** a file's or folder's security descriptor is
  read from `$Secure` when first wanted, and one a program sets is
  written there; a file whose descriptor is the root's inherits from its
  folder, as drive C: saves them, and new files get that one.
- **FAT moves:** files move between folders, and folders are renamed
  (also in case only) and moved, keeping their times; a moved folder's
  `..` names its new parent.
- **Change notifications** already reached these drives' folders; they
  are now tested there.
- **kernel32:** `FindFirstChangeNotificationA`, `GetCompressedFileSizeA`
  and `SearchPathA` passed their path on unterminated (the conversion
  writes no NUL), so a watch could fail with "path not found" on any
  drive.
- **Test:** the devices suite's "ntfs" boot gains a FAT32 disk as E:, and
  `volfiletest` checks all of the above on D: and E:, then again after a
  restart, read back from the volumes.
- **Next:** step 2 mounts an installed C: straight from its volume (no
  restore into memory at boot, no saving behind), with system files kept
  in `C:\Windows` and refreshed when an update changes them.
