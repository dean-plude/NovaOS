## SMP stress: scaling is the best of three samples

The nightly of 2026-10-14 (12:22) went red on one figure: `smpstress`
file access scaled 1.05x against 2.59x for bare system calls, while
computing (3.94x) and the registry (2.41x) in the same run were fine; the
performance budget had shown the same single low sample once before.  The
four-thread throughput was one 2-second sample, so a moment in which the
shared runner did not run the emulated CPUs side by side counted as
"files do not scale".  `smpstress` now takes the best of three samples for
the all-CPU figure; a lock that serialises the work holds it near 1x in
every sample and still fails.
