## Hard links on NTFS volumes, checked; FindFirstFileNameW

Hard links on a mounted NTFS volume (drive D:, ...) are now checked on
every pull request, and a file's names can be listed as on Windows.

- **A file's names** (`kernel/um/um_syscall.c`, `kernel32/compat.c`):
  `NtQueryInformationFile(FileHardLinkInformation)` lists each name with
  its folder's file id (`STATUS_BUFFER_OVERFLOW` and `BytesNeeded` when
  the buffer is short), `NtCreateFile` opens a file or folder by its id
  (`FILE_OPEN_BY_FILE_ID`, beside a handle on the same drive), and
  kernel32's `FindFirstFileNameW`/`FindNextFileNameW` give each name's
  path on its volume (`\LinkTest\a.txt`, as Windows does).
- **Names not read yet** (`kernel/fs/ntfs.c`, `ramfs.c`): on a mounted
  NTFS volume a file's names in folders NovaOS has not listed yet were
  missed, so after a restart its link count could read 1.  `NtfsNames`
  walks a record's `$FILE_NAME`s up to the root, and `RamfsGatherLinks`
  loads those names and joins them to the file before its link count or
  names are given.
- **Checks**: `linktest` lists names by each of them and leaves a third
  name in another folder; `linktest restarted [DIR]` checks all three
  after the restart.  The devices suite's new "ntfs" boot attaches a
  fresh `mkntfs` disk as D:, runs `linktest D:\LinkTest`, restarts,
  runs `linktest restarted D:\LinkTest`, and once QEMU has quit checks
  the volume with `scripts/ntfs-check.py` (records, bitmaps, indexes,
  link counts against names) and reads the third name with `ntfscat`.
