## Hyper-V integration services

The first step of full Hyper-V Generation 2 support (a new roadmap item,
one synthetic device per step).  NovaOS now answers three of Hyper-V's
integration services over VMBus: **shutdown** (the host's Shut Down and
Restart save drive C: and power off or restart NovaOS, as the Start menu
does), **heartbeat** (Hyper-V Manager shows "Heartbeat: OK") and **time
sync** (the system time Windows programs read is set from the host's
clock at start and whenever it drifts more than a second).  Each agrees
on a version with the host first, as Linux's and FreeBSD's drivers do.
VMBus drivers now get each packet's transaction ID, so they can answer
the host's requests, and up to sixteen channels open.  A host test checks
all three protocols ([hyperv.md](hyperv.md)).
