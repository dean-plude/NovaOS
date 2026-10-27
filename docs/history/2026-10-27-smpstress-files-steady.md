## SMP stress: a low file sample gets a second look, and every sample is logged

Three main runs on 2026-10-27 (99a22d7, 1762f6f, 6f5403d) failed the
performance budget on one figure: `smpstress` file access scaled 1.07x,
1.23x and 1.07x against 2.6x for bare system calls, with the registry
(2.4x) in the same run fine; each passed on a re-run.  The best of three
four-thread samples was already used, so a stall that long outlasted all
three, and the log kept only the best figure, which cannot say what the
other samples did.  `smpstress` now prints every sample's operations a
second, takes the best of three for the one-thread figure as well as the
all-CPU one, and when the result is below half the bare system calls' (or
the target) it pauses three seconds and samples again before judging.  A
lock that serialises the work stays near 1x in every sample, before and
after the pause, and still fails; the logged samples show the next time
whether the stall is a few seconds of something else running or a state
that lasts.

The same day the file figure stayed at 0.99x on the re-run of one commit, so
it was not only a passing stall.  A file's contents are read and written
under one of 64 locks chosen by the file's address; but a file's node is a
512-byte slab object, so the address divided by 64 takes only 8 values
within a page, and two files shared a lock about one time in two.  The
node lock is now one of 256, picked by a hash of the address, so two files
share one about once in 256 and `smpstress`'s four, each on a file of its
own, do not queue behind each other.

The hashed node lock did not move the figure (0.93x on its own run), so the
cause is elsewhere.  When the files scale too little, `smpstress` now also
measures the parts of one file operation alone, opening and closing a file,
and reading and writing an open one, each on one thread and on all CPUs,
and the performance budget prints the log's tail in the job output when
smpstress fails.  Whichever part stays near 1x is the one to fix.
