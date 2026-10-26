## A failed update comes back on its own

First part of roadmap step 33.3 ([design](design/recovery.md)).  An
update whose kernel stopped during its first start (a panic, a failed
assertion, a fatal CPU exception) used to print its report and halt, and
the PC stayed there until someone pressed reset; only then did the boot
loader go back to the previous kernel.  Now every fatal stop calls
`KeFatalStop` (`kernel/fs/update.c`), which on an unfinished trial start
logs that the update stopped, waits three seconds and resets the machine,
as Windows restarts after a stop error; the trying mark is still on the
ESP, so the next start is the previous version, which removes the update.
A stop on any other start still halts.

To test it without a broken build, a kernel image has a second marked
field beside its version stamp, empty in every build:
`tools/mkupdate.py --fail-boot panic` stamps a copy that stops just before
it counts as started.  The devices suite's `update` boot stages one as a
v3 and checks that the machine comes back as v1 with no reset from outside.
