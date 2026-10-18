/*
 * watchdog.h — the PC's hardware watchdog, whichever there is
 *
 * The firmware's, when its ACPI WDAT table describes one (wdat.h), as
 * Windows uses; else a chipset NovaOS knows itself (the ICH9 family's TCO
 * timer, tco.h).  An update's first start runs under it
 * (kernel/fs/update.c), so a kernel that hangs with interrupts off is
 * still reset and rolled back.
 */

#pragma once

#include "../include/types.h"

/* Start the watchdog to reset the PC about @seconds after the last
 * WatchdogReload (after AcpiInitialize).  False when the PC has none
 * NovaOS can use. */
bool WatchdogStart(UINT32 seconds);

/* Start the count again (nothing when it isn't running) */
void WatchdogReload(void);

/* Halt it */
void WatchdogStop(void);

/* Which watchdog NovaOS would use, in a line ("" when none):
 * "ACPI WDAT: ..." or "Intel ICH9 TCO timer" */
void WatchdogDescribe(char *out, int cap);
