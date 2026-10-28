## Opening a file no longer queues for the page allocator

`smpstress`'s file figure kept failing the performance budget on main, and
the parts the test had been taught to log said where: reading and writing
open files scaled 2.6x on four CPUs, registry keys 2.3x, but opening and
closing a file scaled 0.25x (824,000 a second on one thread, 206,000 on
four).  Every open checks the file's security descriptor
(`FsecAccess`), which asked twice for a 4096-byte buffer for the inherited
DACL.  That is not a slab size, so each was a page allocation (two pages
with the header) from the physical memory manager's one lock, and threads
opening files side by side queued for it.  Each CPU now keeps one such
buffer to reuse.
