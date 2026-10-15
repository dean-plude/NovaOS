- ~~ACPI beyond the MADT: shutdown, reboot, sleep, batteries~~ Done:
  power-off (S5), sleep (S3), reset and the fixed power button from the
  FADT; the AML interpreter (uACPI) for batteries, AC adapters,
  control-method power buttons and `_PTS`/`_WAK`; the lid (closing it
  sleeps), thermal zones (passive cooling reported, sleep at `_HOT`,
  shutdown at `_CRT`), wake devices from `_PRW` (the lid, power buttons,
  USB controllers, with USB keyboards set for remote wakeup), the SCI as
  a real interrupt through the I/O APIC and PCI interrupt routing from
  `_PRT` (Phase 18.6).  ~~CPU throttling for passive cooling; GPE blocks
  other than `\_GPE`; routing behind PCI bridges~~ Done (2026-10-15):
  T-states from `_PTC`/`_TSS`/`_TPC` (I/O, memory or the clock
  modulation MSR) or `P_CNT`, driven by each zone's `_PSL`, `_TC1`,
  `_TC2` and `_TSP` and reported as `MhzLimit`; GPE block devices
  (ACPI0006) on the SCI or their own interrupt, with their `_Lxx`/`_Exx`
  methods and wake GPEs; bridges' own `_PRT`, else the standard swizzle.
  (USB wake from S3 is confirmed on a real PC; QEMU can't emulate it.)
  (Display modes after S3 are set again on every adapter NovaOS drives:
  the VBE ones, QXL, virtio-vga, VMware SVGA and Cirrus.  Physical GPUs,
  such as the T14's Intel Iris Xe, have no driver yet; see step 25.3.)
