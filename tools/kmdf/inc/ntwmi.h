/*
 * ntwmi.h — the kernel logger's WDF performance hooks
 *
 * The framework asks the kernel (WmiQueryTraceInformation with
 * WdfNotifyRoutinesClass) for routines that log DPCs, interrupts and work
 * items to the kernel logger.  NovaOS's kernel has no kernel logger and
 * fails the query, so the framework uses its own empty table; these are the
 * types and constants that table and its callers name.
 */
#pragma once

typedef VOID (FASTCALL *PWMI_WDF_NOTIFY_ROUTINE)(_In_ PVOID Data, _In_ ULONG DataSize, _In_ ULONG GroupMask,
                                                 _In_ USHORT HookId, _In_ UCHAR Version);

typedef struct _WMI_WDF_NOTIFY_ROUTINES {
    ULONG Size;
    PWMI_WDF_NOTIFY_ROUTINE DpcNotifyRoutine;
    PWMI_WDF_NOTIFY_ROUTINE InterruptNotifyRoutine;
    PWMI_WDF_NOTIFY_ROUTINE WorkItemNotifyRoutine;
} WMI_WDF_NOTIFY_ROUTINES, *PWMI_WDF_NOTIFY_ROUTINES;

/* group masks and hook ids of the kernel logger's WDF events */
#define PERF_WORKER_THREAD  0x20000040
#define PERF_WDF_DPC        0x40000040
#define PERF_WDF_INTERRUPT  0x40000080

#define PERFINFO_LOG_TYPE_WDF_INTERRUPT         0x0F26
#define PERFINFO_LOG_TYPE_WDF_DPC               0x0F27
#define PERFINFO_LOG_TYPE_WDF_WORK_ITEM         0x0F28
#define PERFINFO_LOG_TYPE_WDF_PASSIVE_INTERRUPT 0x0F29
