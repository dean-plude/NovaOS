## An update that hangs or stalls comes back too

Second part of roadmap step 33.3 ([design](../design/recovery.md)).  An
update's first start used to count as good as soon as the kernel enabled
interrupts, and a kernel that hung during that start waited for someone
to press reset.  Now the start counts as good only once the desktop has
answered for three seconds with an account signed in or the sign-in
screen showing, as Windows counts a boot as good at logon, and until then
it is watched twice over: the chipset's TCO watchdog timer
(`kernel/hal/tco.c`, Intel's ICH9 family, which QEMU's q35 machine has)
resets the PC a minute after the kernel last reloaded it, and the
`update watch` thread, which reloads it every second, restarts the PC
itself when the start has made no step forward for 90 seconds.  Either
way the trying mark is still on the ESP, and the next start is the
previous version.

Two more test faults check it (`tools/mkupdate.py --fail-boot hang` and
`stall`): the devices suite's `update` boot stages a v4 that hangs with
interrupts off and a v5 whose desktop never starts, and checks that each
comes back as v1 with no reset from outside.
