/*
 * procgrp.h — the WDK's processor-group library (procgrp.lib)
 *
 * The library lets a driver built for Windows 7's processor-group routines
 * run on Vista by looking them up at run time.  NovaOS's kernel exports
 * the routines themselves, so there is nothing to initialize.
 */
#pragma once
FORCEINLINE VOID WdmlibProcgrpInitialize(VOID) { }
