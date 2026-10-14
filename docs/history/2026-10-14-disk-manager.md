## Disk Management

Dean asked for a Disk Manager (with a Device Manager, a Task Manager and
an Event Viewer, built separately).  `diskmgmt.exe` is Windows 11's Disk
Management layout in `C:\Windows\System32`, found by Start menu search:
the volumes on top (letter, layout, file system, status, capacity, free
space) and each disk drawn below as a row of its partitions and
unallocated space.  It uses only public APIs, so NovaOS grew them:

- **Volumes, Windows' way** (`kernel/fs/drives.c`, `partition.c`): every
  partition of every disk is a volume with a number
  (`\Device\HarddiskVolumeN`, drive C: is 1) and a GUID name
  (`\\?\Volume{...}\`: the GPT partition's own GUID, or one made from an
  MBR disk's signature and the partition's offset).  Drive letters are
  kept in `HKLM\SYSTEM\MountedDevices` with Windows' unique IDs
  (`DMIO:ID:` and the GUID on GPT, signature and offset on MBR) and are
  given back at the next boot.  FAT volumes on fixed disks are mounted
  read-write now (sticks stay read-only).  MBR and GPT tables are
  written as well as read.
- **Disk and volume handles** (`kernel/um/um_disk.c`): `\\.\PhysicalDriveN`,
  `\\.\X:` and the volume names open with `CreateFile` and answer
  `IOCTL_STORAGE_QUERY_PROPERTY`, `IOCTL_STORAGE_GET_DEVICE_NUMBER`,
  `IOCTL_DISK_GET_DRIVE_GEOMETRY(_EX)`, `IOCTL_DISK_GET_LENGTH_INFO`,
  `IOCTL_DISK_GET/SET_DRIVE_LAYOUT_EX`, `IOCTL_DISK_CREATE_DISK`,
  `IOCTL_DISK_GET_PARTITION_INFO(_EX)`,
  `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` and
  `IOCTL_MOUNTDEV_QUERY_DEVICE_NAME`; `NtQueryVolumeInformationFile`
  works on a volume with no letter.  Where Windows uses private
  interfaces (the mount manager, fmifs's insides), NovaOS has three
  IOCTLs of its own (`IOCTL_NOVA_VOLUME_*` in `winioctl.h`).
- **kernel32** (`userland/kernel32/volume.c`): `FindFirstVolume`,
  `FindNextVolume`, `GetVolumeNameForVolumeMountPoint`,
  `GetVolumePathNamesForVolumeName`, `SetVolumeMountPoint`,
  `DeleteVolumeMountPoint` and `QueryDosDevice` are real (they were
  stubs), and `GetVolumeInformation`, `GetDiskFreeSpaceEx`,
  `GetVolumePathName` and `GetDriveType` take volume names.
- **fmifs.dll**: `FormatEx` (NTFS or FAT32, quick) and
  `QueryAvailableFileSystemFormat`, as format programs call them.

The actions are Initialize Disk, New Simple Volume, Format (with
Windows' "Formatting this volume will erase all data on it" question)
and Change Drive Letter and Paths.  NovaOS's own volumes are shown but
refused.  The OS test `diskmgr` (devices suite) attaches a blank 256 MB
disk: Disk Management opens from the Start menu, then initializes the
disk as GPT, makes a 128 MB NTFS volume and a FAT32 volume from the rest,
writes files on both and moves the FAT32 one to T:, which it still is
after a restart.  The host then checks the disk's GPT, both volumes
(`scripts/ntfs-check.py`, `fsck.fat`) and the files on them.  Deleting
and resizing partitions are left for later.
