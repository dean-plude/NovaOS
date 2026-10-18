/*
 * wdat.h — the firmware's hardware watchdog, described by the ACPI WDAT
 * table (Microsoft's "Hardware Watchdog Timers Design Specification")
 *
 * Firmware that has a watchdog for the OS describes it in WDAT as a list
 * of register operations per action (start, stop, reload, set the count,
 * reboot on expiry): the OS runs them without knowing the chipset.
 * Windows drives its watchdog this way (WdatWdt), and so does NovaOS
 * before trying a chipset it knows (tco.h).  Register operations reach
 * memory, I/O ports and, NovaOS's own addition, PCI configuration space
 * of bus 0 (the Generic Address Structure's space 2).
 */

#pragma once

#include "../include/types.h"

/* Read the WDAT table (after AcpiInitialize) and start its watchdog to
 * reset the PC about @seconds after the last WdatReload (rounded to its
 * counts).  False when there is no table, the firmware says the watchdog
 * is switched off, or an action it needs is missing. */
bool WdatStart(UINT32 seconds);

/* The watchdog is running */
bool WdatRunning(void);

/* Start the count again (nothing when it isn't running) */
void WdatReload(void);

/* Halt it */
void WdatStop(void);

/* One line describing the table's watchdog ("" without one), e.g.
 * "ACPI WDAT: counts of 1200 ms, 4 to 1023, reboots" */
void WdatDescribe(char *out, int cap);
