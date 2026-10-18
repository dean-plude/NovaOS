/*
 * recovery.h — the boot loader's Advanced startup menu (recovery.c)
 */
#pragma once

#include "../include/efi.h"

#define RECOVERY_NORMAL    0
#define RECOVERY_PREVIOUS  1     /* start \EFI\NOVA\kernel.old */
#define RECOVERY_SAFE_MODE 2
#define RECOVERY_REPAIR    3

/* On an installed disk, before the kernel is chosen: count this start,
 * show the menu when it is asked for or the last starts failed, and
 * return the line chosen with its BOOT_FLAG_* in *@flags.  For the
 * previous version it first starts that version's own boot loader
 * (bootx64.old), which does not come back when it starts. */
int recovery_start(EFI_HANDLE image, EFI_SYSTEM_TABLE *st, EFI_HANDLE dev, UINT64 *flags);
