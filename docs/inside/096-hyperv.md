- **Hyper-V**: in a Generation 2 VM, whose only input devices are on
  VMBus, NovaOS's VMBus driver (hypercalls, the SynIC, channel offers, ring
  buffers) runs Hyper-V's synthetic keyboard and mouse. Its synthetic
  disks, network and video have no driver yet ([hyperv.md](hyperv.md)).
