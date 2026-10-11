## Image cache: Firefox's processes share xul.dll's pages at load

Firefox starts about ten processes, and each loaded its own `xul.dll`
(164 MB): the kernel copied the whole file into a kernel buffer,
relocated it, bound its imports, then hashed and compared every
read-only page to find the frames other processes already shared.
Several processes doing that at once on a 2 GB machine held hundreds of
megabytes of transient copies, and Firefox did not get a window up in
170 seconds under TCG.

- **Image cache** (`kernel/um/um.c`).  After a module's first load at an
  address, its pages are kept: the shared frames of its read-only pages,
  and a copy of each writable page that is not all zeros.  Another
  process loading the same file (same node, contents and last write) at
  the same address maps those frames and copies those pages straight
  away, with no kernel copy of the image, no relocating and no hashing.
  Only its imports are bound again, read and written in the process; an
  import address that differs makes that one page the process's own
  copy, as any write to a shared image page does.  An entry lives while
  a process that loaded it runs (a running image cannot be replaced) and
  goes with the last one.  Modules whose import table has no separate
  name table (binding overwrites the names) and modules mapped as data
  are not kept.
- **Shared image pages are not charged** to a process's commit, as on
  Windows, where an image page is backed by its file.  Firefox's main
  process at 2 GB was refused memory (`MOZ_CRASH`) once its commit,
  160 MB of it the `xul.dll` it shares, passed the per-process cap.  A
  page is charged once it becomes the process's own.
- **`mem`** in the Terminal prints the modules kept, their size, and the
  loads that reused one.  `imgsharetest` (64- and 32-bit) changes its
  data and a read-only page, starts a copy of itself, and the copy must
  see its file's contents and work through its imports.

Firefox at 2 GB, QEMU TCG, 2 CPUs, 100 seconds after `start browser`:
1127 MB used before, 549 MB after (62 modules kept, 213 MB; 484 loads
reused one).  Before, no Firefox window by 170 seconds; after, the
Welcome page is up by 150 seconds.
