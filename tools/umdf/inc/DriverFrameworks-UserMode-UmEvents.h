/*
 * DriverFrameworks-UserMode-UmEvents.h — the framework's ETW events (NovaOS
 * stand-in for the header the event manifest generates, README.md):
 * compiled out, as NovaOS records no framework events; the performance
 * provider's handle stays 0, so the performance-analysis paths, which
 * check the verifier flag first, never run
 */
#pragma once
#include <evntprov.h>

#define EventRegisterMicrosoft_Windows_DriverFrameworks_UserMode()               ((ULONG)ERROR_SUCCESS)
#define EventUnregisterMicrosoft_Windows_DriverFrameworks_UserMode()             ((ULONG)ERROR_SUCCESS)
#define EventRegisterMicrosoft_Windows_DriverFrameworks_UserMode_Performance()   ((ULONG)ERROR_SUCCESS)
#define EventUnregisterMicrosoft_Windows_DriverFrameworks_UserMode_Performance() ((ULONG)ERROR_SUCCESS)
#define Microsoft_Windows_DriverFrameworks_UserMode_PerformanceHandle ((REGHANDLE)0)

#define EventWriteEVENT_UMDF_DRIVER_DDI_TABLE_MISMATCH(...)          ((ULONG)ERROR_SUCCESS)
#define EventWriteEVENT_UMDF_FX_DDI_READ_FROM_HARDWARE_START(...)    ((ULONG)ERROR_SUCCESS)
#define EventWriteEVENT_UMDF_FX_DDI_READ_FROM_HARDWARE_END(...)      ((ULONG)ERROR_SUCCESS)
#define EventWriteEVENT_UMDF_FX_DDI_WRITE_TO_HARDWARE_START(...)     ((ULONG)ERROR_SUCCESS)
#define EventWriteEVENT_UMDF_FX_DDI_WRITE_TO_HARDWARE_END(...)       ((ULONG)ERROR_SUCCESS)
#define EventWriteEVENT_UMDF_FX_INTERRUPT_NOTIFICATION_RECEIVED(...) ((ULONG)ERROR_SUCCESS)
#define EventWriteEVENT_UMDF_FX_READ_FROM_HARDWARE_START(...)        ((ULONG)ERROR_SUCCESS)
#define EventWriteEVENT_UMDF_FX_READ_FROM_HARDWARE_END(...)          ((ULONG)ERROR_SUCCESS)
#define EventWriteEVENT_UMDF_FX_WRITE_TO_HARDWARE_START(...)         ((ULONG)ERROR_SUCCESS)
#define EventWriteEVENT_UMDF_FX_WRITE_TO_HARDWARE_END(...)           ((ULONG)ERROR_SUCCESS)

/* The performance provider's event descriptors (Id, Version, Channel,
 * Level, Opcode, Task, Keyword) */
#define NOVA_UMDF_PERF_EVENT(n, id) static const EVENT_DESCRIPTOR n = { id, 0, 0, 4, 0, 0, 0 };
NOVA_UMDF_PERF_EVENT(FX_REQUEST_START, 1)
NOVA_UMDF_PERF_EVENT(FX_REQUEST_COMPLETE, 2)
NOVA_UMDF_PERF_EVENT(FX_POWER_D0_ENTRY_START, 3)
NOVA_UMDF_PERF_EVENT(FX_POWER_D0_ENTRY_STOP, 4)
NOVA_UMDF_PERF_EVENT(FX_POWER_D0_EXIT_START, 5)
NOVA_UMDF_PERF_EVENT(FX_POWER_D0_EXIT_STOP, 6)
NOVA_UMDF_PERF_EVENT(FX_POWER_HW_PREPARE_START, 7)
NOVA_UMDF_PERF_EVENT(FX_POWER_HW_PREPARE_STOP, 8)
NOVA_UMDF_PERF_EVENT(FX_POWER_HW_RELEASE_START, 9)
NOVA_UMDF_PERF_EVENT(FX_POWER_HW_RELEASE_STOP, 10)
NOVA_UMDF_PERF_EVENT(FX_EVTIOSTOP_START, 11)
NOVA_UMDF_PERF_EVENT(FX_EVTIOSTOP_STOP, 12)
