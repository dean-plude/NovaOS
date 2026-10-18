/*
 * cstate.h — processor C-states from ACPI (_OSC/_PDC, then _CST)
 *
 * Tells each processor object what NovaOS can do and hands the C-states
 * its _CST describes to the idle loop (ke/idle.c); see cstate.c.
 */

#pragma once

/* The acpi thread only, after the namespace is initialized */
void CStateProbe(void);
