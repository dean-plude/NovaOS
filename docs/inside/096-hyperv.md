- **Hyper-V**: in a Generation 2 VM, whose only input devices are on
  VMBus, NovaOS's VMBus driver (hypercalls, the SynIC, channel offers, ring
  buffers) runs Hyper-V's synthetic keyboard and mouse and answers its
  shutdown, heartbeat and time sync integration services (the host's Shut
  Down and Restart work). Its synthetic SCSI controller's disks are block
  devices, so NovaOS installs to a VHDX, its synthetic video changes
  resolution, and its synthetic network adapter (NVSP and RNDIS) is the
  network stack's adapter ([hyperv.md](hyperv.md)).
