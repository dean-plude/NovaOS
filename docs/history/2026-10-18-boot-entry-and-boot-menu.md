## NovaOS starts first beside Windows, and offers Windows

Second part of roadmap step 33.4 ([design](../design/installer.md)).  A
PC with Windows already has "Windows Boot Manager" first in the
firmware's `BootOrder`, so NovaOS installed beside it was only reachable
through the firmware's boot menu.  Setup now finishes by adding a
"NovaOS" boot entry for its own EFI System Partition and putting it first,
with every other entry kept after it, as Windows Setup does.  The kernel
calls the firmware's variable services for that (`kernel/hal/efi.c`): the
boot loader hands over the runtime services table (boot protocol 6) and
each call runs in physical mode, on page tables that map the firmware's
memory one to one, with interrupts off and the FPU state kept.  The entry
is the short form `HD(...)/\EFI\BOOT\BOOTX64.EFI`, and the boot loader
recognises it instead of adding a second entry on the first start.

The boot loader then looks on the PC's other volumes for Windows'
`bootmgfw.efi` and, when it finds it, shows a boot menu for five seconds:
NovaOS and Windows Boot Manager, which it starts with `LoadImage` and
`StartImage`.  The devices suite's `beside` boot checks all of it, with a
small UEFI application standing in for Windows' boot manager.
