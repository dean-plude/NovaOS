/*
 * DriverFrameworks-KernelMode-KmEvents.h — the framework's ETW performance
 * events, compiled out
 *
 * Microsoft generates this header from an event manifest with mc.exe.  The
 * events go to ETW, which NovaOS does not have: registering does nothing and
 * every event write is empty.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
__declspec(selectany) REGHANDLE Microsoft_Windows_DriverFrameworks_KernelMode_PerformanceHandle = (REGHANDLE)0;
#ifdef __cplusplus
}
#endif

#define EventRegisterMicrosoft_Windows_DriverFrameworks_KernelMode_Performance()   ((ULONG)0)
#define EventUnregisterMicrosoft_Windows_DriverFrameworks_KernelMode_Performance() ((ULONG)0)

#define EventWriteFX_REQUEST_START(...)          ((ULONG)0)
#define EventWriteFX_REQUEST_COMPLETE(...)       ((ULONG)0)
#define EventWriteFX_POWER_D0_ENTRY_START(...)   ((ULONG)0)
#define EventWriteFX_POWER_D0_ENTRY_STOP(...)    ((ULONG)0)
#define EventWriteFX_POWER_D0_EXIT_START(...)    ((ULONG)0)
#define EventWriteFX_POWER_D0_EXIT_STOP(...)     ((ULONG)0)
#define EventWriteFX_POWER_HW_PREPARE_START(...) ((ULONG)0)
#define EventWriteFX_POWER_HW_PREPARE_STOP(...)  ((ULONG)0)
#define EventWriteFX_POWER_HW_RELEASE_START(...) ((ULONG)0)
#define EventWriteFX_POWER_HW_RELEASE_STOP(...)  ((ULONG)0)
#define EventWriteFX_EVTIOSTOP_START(...)        ((ULONG)0)
#define EventWriteFX_EVTIOSTOP_STOP(...)         ((ULONG)0)
