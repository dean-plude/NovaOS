## Hyper-V SCSI disks

The second step of Hyper-V Generation 2 support.  A Gen 2 VM's virtual
hard disks sit on a synthetic SCSI controller on VMBus; NovaOS now drives
it (`kernel/drivers/storvsc.c`, the protocol as FreeBSD's BSD-licensed
`hv_storvsc` speaks it): it agrees on a protocol version, asks each of the
controller's locations what is there, and makes every virtual hard disk a
block device (`hv0`, `hv1`, ...), read and written through GPA-direct
packets that list a bounce buffer's pages.  So NovaOS installs to a VHDX
(`install hv0`, or the Setup app) and keeps drive C: there.  VMBus now
comes up before the disks are looked for, delivers completion packets to
its drivers, and lets a driver read its own channel while it waits for an
answer.  A fake Hyper-V host in a host test checks the driver.  The host
tests for the VMBus input and integration-service drivers were checking
nothing (the lwIP port's `assert.h` was on their include path); they
check again, and the integration-service test's expected lengths now
allow for the ring's 8-byte padding ([hyperv.md](hyperv.md)).
