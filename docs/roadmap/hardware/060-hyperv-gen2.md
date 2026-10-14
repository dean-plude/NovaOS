- [ ] Hyper-V Generation 2: every synthetic device a Gen 2 VM has, one step
  at a time, each leaving NovaOS bootable ([hyperv.md](hyperv.md)).
  Microsoft's own VMBus drivers are inbox and not redistributable, so
  these are NovaOS drivers written from the protocols as FreeBSD's
  BSD-licensed Hyper-V drivers speak them, each with a host test, since
  CI has no Hyper-V.  Done when a Gen 2 VM installs NovaOS to its virtual
  disk, reaches the network and changes resolution.
  - [x] Keyboard and mouse over VMBus (#255, #367)
  - [ ] Integration services: the host's Shut Down and Restart, heartbeat,
    time sync
  - [ ] storvsc: SCSI disks and DVD, so NovaOS installs to a VHDX and keeps
    drive C:
  - [ ] Synthetic video: a resolution chosen in Settings
  - [ ] netvsc: the network adapter (NVSP and RNDIS)
  - [ ] Hyper-V clock and timers (reference TSC page, synthetic timers),
    the SynIC on every CPU, channel interrupts instead of polling, devices
    added and removed while running
  - [ ] Key-value exchange (the VM's name and addresses in Hyper-V
    Manager)
