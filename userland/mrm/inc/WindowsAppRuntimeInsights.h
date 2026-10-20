/* NovaOS: the Windows App SDK's telemetry header (WindowsAppRuntimeInsights.h,
 * over WIL's TraceLogging), which MRT Core's MrmTraceLogging.h declares its
 * events with.  NovaOS sends no telemetry: each event is a function that
 * does nothing. */
#pragma once
#include <windows.h>

namespace wil
{
class TraceLoggingProvider
{
};
}

#define IMPLEMENT_TRACELOGGING_CLASS(cls, name, guid) \
public:                                                \
    static cls *Instance() { static cls one; return &one; }

#define DEFINE_COMPLIANT_MEASURES_EVENT(name, pdt) \
public:                                            \
    static void name() {}
#define DEFINE_COMPLIANT_TELEMETRY_EVENT_PARAM3(name, pdt, t1, n1, t2, n2, t3, n3) \
public:                                                                            \
    static void name(t1, t2, t3) {}
#define DEFINE_COMPLIANT_TELEMETRY_EVENT_PARAM4(name, pdt, t1, n1, t2, n2, t3, n3, t4, n4) \
public:                                                                                    \
    static void name(t1, t2, t3, t4) {}
#define DEFINE_COMPLIANT_MEASURES_EVENT_PARAM3 DEFINE_COMPLIANT_TELEMETRY_EVENT_PARAM3
#define DEFINE_COMPLIANT_MEASURES_EVENT_PARAM4 DEFINE_COMPLIANT_TELEMETRY_EVENT_PARAM4
