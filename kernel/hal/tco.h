/*
 * tco.h — the chipset's hardware watchdog (the Intel ICH9 family's TCO timer)
 *
 * The TCO timer counts down in 0.6 s ticks.  The first time it runs out
 * it reloads and sets a status bit; the second time the chipset resets
 * the PC, unless software reloaded it in between.  QEMU's q35 machine has
 * it (ICH9-LPC, with -watchdog-action reset by default), as Intel desktop
 * chipsets of that family do.  NovaOS arms it while an update's kernel
 * starts for the first time (kernel/fs/update.c), so a kernel that hangs
 * with its interrupts off is still reset and rolled back.  Windows uses
 * such a timer through the ACPI WDAT table where firmware describes one;
 * that table is not read yet (docs/design/recovery.md).
 */

#pragma once

#include "../include/types.h"

/* Find the watchdog and start it to reset the PC about @seconds after the
 * last TcoReload (rounded to its 1.2 s steps).  False when there is no
 * such chipset, or its reset is switched off and can't be switched on. */
bool TcoStart(UINT32 seconds);

/* The watchdog is running */
bool TcoRunning(void);

/* Start the count again (nothing when it isn't running) */
void TcoReload(void);

/* Halt it */
void TcoStop(void);
