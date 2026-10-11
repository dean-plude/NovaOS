- ~~NTFS read~~ ~~NTFS write~~ Done: NTFS volumes mount as drives D:, E:,
  ...; files on them are written, created, renamed and deleted (volumes
  Windows left hibernated or unclean stay read-only).  ~~NTFS as drive C:~~
  Done: the installer formats C: as NTFS (or FAT32), and files there keep
  security descriptors that opening, deleting and renaming obey.  ~~Hard
  links and persistence~~ Done (Phase 17.5; `linktest` and `linktest
  restarted`; on a mounted NTFS volume across a restart, with
  `FindFirstFileNameW` and an offline volume check, in the devices
  suite's "ntfs" boot).  Interrupted writes and a full volume are still
  to be tested on a disposable disk.  ~~NVMe~~ Done: NVMe disks, installed to and booted from.
