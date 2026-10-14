- **Hyper-V**: in a Generation 2 VM, whose only input devices are on
  VMBus, NovaOS's VMBus driver (hypercalls, the SynIC, channel offers, ring
  buffers) runs Hyper-V's synthetic keyboard and mouse and answers its
  shutdown, heartbeat and time sync integration services (the host's Shut
  Down and Restart work). Its synthetic disks, network and video have no
  driver yet ([hyperv.md](hyperv.md)).
