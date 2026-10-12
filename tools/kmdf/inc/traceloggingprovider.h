/*
 * traceloggingprovider.h — TraceLogging, compiled out
 *
 * The framework sends usage telemetry to Microsoft through TraceLogging
 * providers.  NovaOS has no ETW consumer and sends nothing anywhere, so the
 * provider never registers and every event compiles to nothing.
 */
#pragma once

typedef const struct _TlgProvider_t *TraceLoggingHProvider;

#define TRACELOGGING_DECLARE_PROVIDER(handle) extern const TraceLoggingHProvider handle
#define TRACELOGGING_DEFINE_PROVIDER(handle, name, guid, ...) const TraceLoggingHProvider handle = 0
#define TraceLoggingOptionMicrosoftTelemetry()
#define TraceLoggingRegister(handle) ((NTSTATUS)0)
#define TraceLoggingUnregister(handle) ((void)0)
#define TraceLoggingProviderEnabled(handle, level, keyword) FALSE
#define TraceLoggingWrite(handle, ...) ((void)0)
