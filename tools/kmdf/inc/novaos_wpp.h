/*
 * novaos_wpp.h — what the WDK's WPP preprocessor (tracewpp) would have
 * written into each .tmh file the KMDF source includes
 *
 * KMDF traces through WPP: DoTraceLevelMessage(globals, level, flags,
 * format, ...) with WPP's format extensions (%!STATUS!, %!FUNC!, ...),
 * compiled by tracewpp into ETW calls.  NovaOS has no tracewpp; every
 * .tmh is this header, which sends the messages to NovaOSWdfTrace
 * (tools/kmdf/novaos_wpp.c): errors go to the kernel log, the rest are
 * dropped (Windows records them in the in-flight recorder, which
 * NovaOS does not keep yet).
 */
#pragma once

#ifndef TRACE_LEVEL_NONE
#define TRACE_LEVEL_NONE        0
#define TRACE_LEVEL_CRITICAL    1
#define TRACE_LEVEL_FATAL       1
#define TRACE_LEVEL_ERROR       2
#define TRACE_LEVEL_WARNING     3
#define TRACE_LEVEL_INFORMATION 4
#define TRACE_LEVEL_VERBOSE     5
#endif

/* The trace flags, in fxtrace.h's WPP_DEFINE_BIT order */
#define TRACINGFULL              0x00000001
#define TRACINGERROR             0x00000002
#define TRACINGDBGPRINT          0x00000004
#define TRACINGFRAMEWORKS        0x00000008
#define TRACINGAPI               0x00000010
#define TRACINGAPIERROR          0x00000020
#define TRACINGRESOURCES         0x00000040
#define TRACINGLOCKING           0x00000080
#define TRACINGCONTEXT           0x00000100
#define TRACINGPOOL              0x00000200
#define TRACINGHANDLE            0x00000400
#define TRACINGPNP               0x00000800
#define TRACINGIO                0x00001000
#define TRACINGIOTARGET          0x00002000
#define TRACINGDMA               0x00004000
#define TRACINGREQUEST           0x00008000
#define TRACINGDRIVER            0x00010000
#define TRACINGDEVICE            0x00020000
#define TRACINGUSEROBJECT        0x00040000
#define TRACINGOBJECT            0x00080000
#define TRACINGPNPPOWERSTATES    0x00100000
#define TRACINGIFRCAPTURE        0x00200000

#ifdef __cplusplus
extern "C"
#endif
void __cdecl NovaOSWdfTrace(unsigned long Level, unsigned long Flags, const char *Format, ...);

#define DoTraceLevelMessage(Globals, Level, Flags, ...) NovaOSWdfTrace((Level), (Flags), __VA_ARGS__)
#define DoTraceMessage(Flags, ...)                     NovaOSWdfTrace(TRACE_LEVEL_INFORMATION, (Flags), __VA_ARGS__)
#define WPP_INIT_TRACING(DriverObject, RegistryPath)
#define WPP_CLEANUP(DriverObject)
