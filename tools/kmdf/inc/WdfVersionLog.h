/*
 * WdfVersionLog.h — the framework's event-log message identifiers
 *
 * Microsoft generates this header with mc.exe from the message file that
 * names Wdf01000's System event-log entries.  NovaOS's I/O manager writes
 * an entry's numbers to the kernel log; these are the identifiers, in the
 * error severity, facility 0x20 (KMDF), that the framework logs.
 */
#pragma once
#define WDFVER_CLIENT_INVALID_DDI_COUNT       ((NTSTATUS)0xC0200001L)
#define WDFVER_MINOR_VERSION_NOT_SUPPORTED    ((NTSTATUS)0xC0200002L)
#define WDFVER_DRIVER_COMPANION_FAIL_TO_LOAD  ((NTSTATUS)0xC0200003L)
