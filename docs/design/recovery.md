# Update rollback and recovery (roadmap step 33.3)

How an installed NovaOS gets back to a working system when an update, a
driver or a damaged disk stops it from starting, done the way Windows does
it: a known-good copy of the system kept beside the new one, automatic
rollback of an update that does not start, a restart after a stop error
instead of a halted PC, and a recovery boot with safe mode, startup repair
and reset. How updates are fetched and staged is in
[updates.md](../updates.md).

| Windows | NovaOS |
|---|---|
| The previous build kept for rollback (`Windows.old`, the servicing stack's pending.xml rollback) | `\EFI\NOVA\kernel.old`, the kernel an update replaced |
| Boot Manager's boot status: a boot that never reached logon counts as failed | the trying mark `\EFI\NOVA\update.try`, deleted only once the new kernel has started |
| "Automatically restart" after a stop error (on by default) | `KeFatalStop`: on an update's first start, a kernel stop restarts the machine |
| Rollback of a feature update that fails to start | the boot loader starts the previous kernel, which removes the update |
| Windows Recovery Environment (WinRE) after two failed boots, F8 / Shift+Restart menu | the recovery menu in `BOOTX64.EFI` (to do) |
| Safe mode, Last Known Good Configuration, Startup Repair, Reset this PC | recovery menu entries (to do) |

## Done: a failed update comes back on its own

Before this step a broken update was rolled back only when something reset
the PC while the new kernel started: a kernel that stopped (a panic, a
failed assertion, a fatal CPU exception) printed its report and halted, and
the PC waited there until someone pressed reset. Now:

1. The boot loader starts `kernel.new` once with `BOOT_FLAG_UPDATE_TRIAL`
   after writing the trying mark (unchanged, [updates.md](../updates.md)).
2. Every fatal stop in the kernel (`KPANIC`, `KASSERT` and the page-fault,
   double-fault and unhandled-exception paths of
   `kernel/arch/x86_64/idt.c`) calls `KeFatalStop()` (`kernel/fs/update.c`)
   just before halting. On a trial start that has not finished the update
   yet, it logs
   `[UPDATE] The updated NovaOS X stopped while starting: restarting to go back to the previous version`,
   leaves the report on the screen for three seconds and resets the machine
   (`AcpiReset`: the FADT reset register, then port 0xCF9, the 8042 and a
   triple fault). On any other start it returns and the kernel halts as
   before, so a stop on a working system still leaves its report readable.
3. The trying mark is still on the ESP, so the boot loader starts the
   previous `kernel.elf` with `BOOT_FLAG_UPDATE_FAILED`, which deletes the
   update's files and says so on the App Store's Updates page.

### Testing it without a broken build

A kernel image has a second marked field next to its version stamp
(`NovaTestFault`, `kernel/ke/version.c`), empty in every build.
`tools/mkupdate.py --fail-boot panic` writes `panic` into it on the copy it
puts in a channel; such a kernel stops with a kernel panic just before it
enables interrupts and counts as started (`kernel/ke/main.c`). The devices
suite's `update` boot serves one as `broken/` (a v3 of the test versions):
it stages it, restarts, and passes when the broken kernel's stop, the
automatic restart and the previous version's start all appear in the serial
log with no reset from the test ([updates.md](../updates.md#the-self-test)).

## To do (the next pieces of 33.3)

1. **Hangs.** A kernel that hangs instead of stopping still waits for a
   reset. A trial start should arm a hardware watchdog (the ICH9 TCO timer
   QEMU's q35 and Intel PCs have, or the ACPI WDAT table where firmware
   describes one) that resets the PC unless the update finishes, and the
   point where a start counts as good should move from "interrupts enabled"
   to "the desktop and shell are running", as Windows counts a boot as good
   at logon.
2. **The known-good set and the boot menu.** Keep the previous boot loader
   (`bootx64.old`) beside `kernel.old`, and give `BOOTX64.EFI` a menu
   (shown when a key is held at start, or by itself after two starts in a
   row that did not reach the desktop, as WinRE does) with "NovaOS", "the
   previous version", "Safe mode" and "Startup repair".
3. **Safe mode and startup repair.** Safe mode starts with only NovaOS's
   own drivers and the firmware framebuffer (no third-party `.sys` packages,
   the Windows driver model's equivalent of Last Known Good drivers);
   startup repair checks the ESP and drive C:, restores the boot files from
   the known-good set and clears a stuck update; reset reinstalls from the
   running kernel and keeps `C:\Users`. The boot test `rollbacktest` then
   starts each from the menu in QEMU, which is the step's "Done when".
