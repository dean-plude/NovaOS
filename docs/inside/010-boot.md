- **Boot** (`bootloader/`): a UEFI bootloader (a PE32+ EFI application)
  loads the ELF kernel, `\EFI\NOVA\kernel.elf`, from the EFI System
  Partition of an installed disk, or from the ISO on a CD or written to a
  USB stick.
  - The ISO is both the live system and the installer. Started from a USB
    stick, NovaOS also writes its log to `\EFI\NOVA\bootlog.txt` on the
    stick.
  - On an installed disk the bootloader adds a "NovaOS" firmware boot
    entry, and starts a newly downloaded kernel once, falling back to the
    previous one if the new one never reaches the desktop
    ([updates.md](updates.md)).
  - It hands the kernel the memory map, the GOP framebuffer, the ACPI RSDP
    and a copy of the SMBIOS tables ([phase1-architecture.md](phase1-architecture.md)).
