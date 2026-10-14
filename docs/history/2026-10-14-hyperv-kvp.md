## Hyper-V key-value exchange

The sixth and last step of Hyper-V Generation 2 support.  Hyper-V
Manager showed no IP addresses for a NovaOS VM, because nothing answered
the Data Exchange integration service.  `kernel/drivers/hv_util.c` now
answers it in the kernel (Linux and FreeBSD use a user daemon): the host
enumerates the guest's own items and gets NovaOS's name, version,
processor architecture and IPv4 and IPv6 addresses, and the items the
host sets for the VM are kept in memory and read back.  A host test
checks every message ([hyperv.md](hyperv.md)).
