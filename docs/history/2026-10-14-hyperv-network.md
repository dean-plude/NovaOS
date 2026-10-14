## Hyper-V network adapter

The fourth step of Hyper-V Generation 2 support.  A Gen 2 VM's network
adapter is a VMBus channel, so NovaOS had no network there.  NovaOS now
drives it (`kernel/drivers/netvsc.c`, NVSP and RNDIS as FreeBSD's
BSD-licensed `hn` driver speaks them): it agrees on an NVSP version (5,
4, 2 or 1), gives the host a receive buffer, reads the MAC address and
the link through RNDIS, sends frames in GPA-direct packets and takes the
host's from the receive buffer.  The network stack uses it when there is
no Intel or virtio adapter, so DHCP, DNS and the App Store work in a
Hyper-V VM.  VMBus can now give the host buffers larger than one message
lists (GPADL_BODY), deliver transfer-page packets to a driver and send
completion packets.  A fake Hyper-V host in a host test checks the
driver ([hyperv.md](hyperv.md)).
