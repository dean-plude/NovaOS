/*
 * recovery.h — safe mode, startup repair and the failed-start count
 * (roadmap step 33.3, docs/design/recovery.md)
 *
 * The boot loader's Advanced startup menu (bootloader/src/recovery.c)
 * comes up when F8 is held, when NovaOS asked for it (shutdown /r /o:
 * RecoveryRequestMenu), or by itself after BOOT_FAILED_STARTS starts in a
 * row that did not reach the desktop.  It counts each start in
 * \EFI\NOVA\starts.cnt; RecoveryStartGood deletes the file once the
 * desktop is up, as Windows' boot manager counts boots that never reached
 * logon.  What the menu chose arrives as boot flags:
 *
 *   - BOOT_FLAG_SAFE_MODE: only NovaOS's own drivers (no Windows driver
 *     package's .sys loads: kernel/nt/ldr.c) and the firmware's
 *     framebuffer (no display adapter driver: kernel/hal/display.c), on a
 *     black desktop that says "Safe Mode" in its corners;
 *   - BOOT_FLAG_STARTUP_REPAIR: before the start goes on, check the EFI
 *     System Partition and its boot files and mend what can be mended
 *     (RecoveryBootDone), with a report in
 *     C:\Windows\System32\LogFiles\Srt\SrtTrail.txt, Windows' own path;
 *   - BOOT_FLAG_PREVIOUS: this is the previous version (kernel.old),
 *     started once.
 */
#pragma once

#include "../include/types.h"
#include "../include/boot_protocol.h"

/* Early boot: the menu's flags */
void RecoveryBootInfo(const BootInfo *info);
/* Whether this start is in safe mode */
bool RecoverySafeMode(void);
/* The start reached the desktop (fs/update.h): clear the failed-start count */
void RecoveryStartGood(void);
/* Interrupts are on: run startup repair, if it was chosen (a thread) */
void RecoveryBootDone(void);
/* Show the Advanced startup menu at the next start (shutdown /r /o);
 * false if the EFI System Partition was not found */
bool RecoveryRequestMenu(void);
