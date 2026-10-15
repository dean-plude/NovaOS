- ACPI beyond the MADT: ~~shutdown, reboot, sleep, batteries, thermal
  throttling, GPE blocks, PCI bridge routing~~ Done:
  power-off (S5), sleep (S3), reset and the fixed power button from the
  FADT; the AML interpreter (uACPI) for batteries, AC adapters,
  control-method power buttons and `_PTS`/`_WAK`; the lid (closing it
  sleeps), thermal zones (passive `_PSL` processors throttled through
  `_PTC`/`_TSS`, honoring the `_TPC` platform throttle level and `_TSP`;
  sleep at `_HOT`,
  shutdown at `_CRT`), wake devices from `_PRW` (including GPE-block
  references), ACPI0006 GPE blocks, the SCI as a real interrupt through
  the I/O APIC and PCI interrupt routing from `_PRT` on root and
  downstream buses (including bridge pin swizzling).  (USB wake from S3
  is confirmed on a real PC; QEMU can't emulate it.)
  (Display modes after S3 are set again on every adapter NovaOS drives:
  the VBE ones, QXL, virtio-vga, VMware SVGA and Cirrus.  Physical GPUs,
  such as the T14's Intel Iris Xe, have no driver yet; see step 25.3.)
