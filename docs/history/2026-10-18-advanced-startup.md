## Advanced startup: safe mode, startup repair and the previous version

Third part of roadmap step 33.3 ([design](../design/recovery.md)), which
it finishes. The boot loader has an Advanced startup menu
(`bootloader/src/recovery.c`), the counterpart of Windows' Advanced Boot
Options and recovery environment: F8 as it starts, `shutdown /r /o` (a
mark on the ESP; advapi32's `InitiateShutdown` with
`SHUTDOWN_RESTART_BOOTOPTIONS`), or by itself after two starts in a row
that did not reach the desktop (the loader counts starts in
`\EFI\NOVA\starts.cnt`, the kernel deletes it at the desktop, as Windows'
`bootstat.dat`), when it runs startup repair after 30 seconds.

It offers the previous version (an update now keeps the loader it replaces
as `bootx64.old` beside `kernel.old`, and the menu starts that loader,
which starts `kernel.old` once), safe mode (no Windows driver package's
`.sys` loads, the firmware's framebuffer instead of a display driver, a
black desktop with "Safe Mode" in its corners) and startup repair (the
ESP's file system checked, `BOOTX64.EFI` and `kernel.elf` put back from the
known-good set, an unfinished update removed, a report in
`C:\Windows\System32\LogFiles\Srt\SrtTrail.txt`). A `kernel.elf` that is
not a kernel starts `kernel.old` with startup repair by itself.

The devices suite's new `rollbacktest` boot starts each line from the menu
and checks the menu comes up by itself after two failed starts.
