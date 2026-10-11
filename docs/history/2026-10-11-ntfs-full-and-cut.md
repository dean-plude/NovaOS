## NTFS drives: a full volume, and writing cut off half-way

A mounted NTFS volume (drive D:, ...) now says when it is full, and one
NovaOS was writing to when the power went comes back read-only with
nothing lost, as Windows would have chkdsk look it over.

- **Marked dirty while writing** (`kernel/fs/ntfs.c`, `drives.c`): there
  is no journal, so before the first change NovaOS sets `$Volume`'s dirty
  flag and flushes it to the disk, and clears it once the changes are on
  the disk and a second has passed with none (and at shutdown and
  unmount).  A volume cut off mid-change is found dirty at the next
  mount and stays read-only ("stays read-only: marked dirty"), and
  Windows runs chkdsk on it.  Drive C:, which NovaOS saves itself, is not
  marked.  A file rewritten on disk already took new clusters before it
  let go of its old ones, so a cut leaves it whole, old or new.
- **Full volumes** (`kernel/fs/ramfs.c`, `um/um_syscall.c`): a file on
  a mounted volume lives in memory until it is closed, so it could grow
  past the free space and then fail to reach the disk unseen.  Now a
  write or `SetEndOfFile` that would take it past the volume's free
  clusters fails with `ERROR_DISK_FULL` and writes nothing, as on
  Windows (`RamfsSource.fits`, `NtfsFits`).
- **Checks**: the devices suite's "ntfs" boot goes on after the hard-link
  tests: `ntfstest fill D:\Full` fills the volume, `ntfstest churn
  D:\Churn` rewrites four files until the test resets the machine (the
  self-tests' new `cut=` option), and in the next boot D: must be
  read-only and `ntfstest cut D:\Churn` must find every file whole.
  Offline, `scripts/ntfs-check.py` passes the disk after filling and,
  with the new `--after-cut` (only what chkdsk mends without loss), the
  disk after the reset, which `scripts/ntfs-dirty.py` must find dirty.
