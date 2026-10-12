/* novaos_kmdf.h — what the WDK build environment defined for every KMDF file */
#pragma once
#define FX_CORE_KERNEL_MODE 0
#define FX_CORE_USER_MODE   1
#define FX_CORE_MODE        FX_CORE_KERNEL_MODE
#define _AMD64_ 1
#define AMD64 1
#define _WIN64 1
#define NT_PROCESSOR_GROUPS 1     /* (the x64 WDK's Windows 7+ layouts: interrupt groups) */
#define NTDDI_VERSION 0x0A000000
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define __WDF_MAJOR_VERSION 1
#define __WDF_MINOR_VERSION 33
#define __WDF_BUILD_NUMBER 0
#define __WDF_MAJOR_VERSION_STRING 01
#define __WDF_MINOR_VERSION_STRING 033

/* mingw-w64's wdm.h defines these itself; keep its intrin-impl.h from
 * defining them a second time */
#define __INTRINSIC_DEFINED_InterlockedBitTestAndSet
#define __INTRINSIC_DEFINED_InterlockedBitTestAndReset

/* The WDK's C compiler mode: try/except/finally/leave are SEH keywords in C */
#ifndef __cplusplus
#define try     __try
#define except  __except
#define finally __finally
#define leave   __leave
#endif
#include "novaos_sal.h"

/* The framework includes the public KMDF headers for their types; their
 * inline client wrappers (which the framework never calls) then need no
 * availability checks against a client's bind */
#define WDF_EVERYTHING_ALWAYS_AVAILABLE 1

/* KMDF is built with WPP tracing: each file includes its .tmh, which here
 * is tools/kmdf/inc/novaos_wpp.h */
#define EVENT_TRACING 1

/* The WDK build's switches for a production (free) framework: no break
 * into the debugger on a client's version mismatch */
#define WDF_PRODUCTION_RELEASE 1

/* (fxrequest.cpp defines this; fxrequestapi.cpp names it too) */
#define WDF_REQUEST_REUSE_MUST_COMPLETE 2
