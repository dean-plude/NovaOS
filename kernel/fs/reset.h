/*
 * reset.h — Reset this PC, keeping your files
 * (roadmap step 33.3, docs/design/recovery.md)
 *
 * As Windows' "Reset this PC: Keep my files": the apps and drivers
 * installed and every change to the settings go, the accounts and their
 * files stay.  Settings > System > Recovery (or `systemreset /keep` in the
 * Terminal) asks for it and restarts; the next start does it before any
 * program runs:
 *
 *   - drive C: loses everything but the profiles: C:\Users, and the
 *     first account's folders at the top of C: (Desktop, Documents, ...,
 *     AppData: its profile is C:\ itself, um_accounts.c); NovaOS's own
 *     files come from the OS image at every start anyway, and starter
 *     files come back as they were;
 *   - the registry goes back to its defaults except the accounts (the
 *     SAM), their profile list, the last account signed in and Setup's
 *     first-boot answers (so the Welcome screens don't run again);
 *
 * then it restarts once more, into a NovaOS as installed, with the same
 * accounts and files.  A report goes to C:\Windows\Logs\SystemReset\
 * ResetReport.txt.  NovaOS itself (the kernel on the EFI System
 * Partition) is the version installed; reinstalling it, or erasing
 * everything, is Setup's (docs/design/installer.md).
 */
#pragma once

#include "../include/types.h"

/* Ask for a reset at the next start; false when drive C: is not kept on
 * a disk (live from the installation disc or stick).  The caller restarts. */
bool ResetRequest(void);

/* UmInit, after the registry is loaded: reset, if it was asked for */
void ResetBoot(void);

/* Interrupts are on: restart after a reset (a thread) */
void ResetBootDone(void);
