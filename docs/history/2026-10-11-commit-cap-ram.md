## Commit cap: a program may commit the whole RAM

A program could commit 7/8 of the machine's RAM.  Firefox's main process
at 2 GB reached that once the 160 MB of `xul.dll` it shares counted
towards it; since the image cache, shared module pages are not charged,
and the main process had about 170 MB of room left.

- **The cap is the RAM** (`kernel/um/um_syscall.c`).  A commit counts a
  program's own memory only, so it may promise all of the RAM; a commit
  past it still fails up front with `STATUS_NO_MEMORY`.  At 2 GB that is
  256 MB more room for Firefox's main process.
- **A touched page waits for memory** (`kernel/um/um.c`, `UmDemandWait`).
  Commit is not backed by a page file, so a promised page can find no
  free memory when it is first touched.  The thread then runs the
  instruction again for up to 3 seconds, while the saved files of C: the
  "persist" thread lets go of when memory is short, and programs ending,
  give memory back.  Only then is it an access violation, with
  `[UM] NAME: no memory for a committed page at ADDRESS (N MB free)` on
  the serial log.
- **Test.**  The 64-bit `imgsharetest` commits fifteen sixteenths of the
  RAM at once, touches its first, middle and last pages, and must be
  refused a commit larger than the RAM.
