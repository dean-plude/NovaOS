/* NovaOS: the Windows SDK's TraceLogging (ETW) event API, as no-ops.  NovaOS
 * has no ETW consumers, so the XAML framework's events (and WIL's) are
 * compiled out: providers register nowhere and are never enabled. */
#pragma once
#include <windows.h>
#include <evntprov.h>
#include <winmeta.h>

typedef void(NTAPI *TLG_PENABLECALLBACK)(LPCGUID sourceId, ULONG isEnabled, UCHAR level, ULONGLONG matchAnyKeyword,
                                         ULONGLONG matchAllKeyword, PEVENT_FILTER_DESCRIPTOR filterData, PVOID callbackContext);
struct _tlgProvider_t      /* the SDK's layout; RegHandle stays 0 (never registered) */
{
    ULONG LevelPlus1;
    const UINT16 *ProviderMetadataPtr;
    ULONGLONG KeywordAny;
    ULONGLONG KeywordAll;
    REGHANDLE RegHandle;
    TLG_PENABLECALLBACK EnableCallback;
    PVOID CallbackContext;
};
typedef const _tlgProvider_t *TraceLoggingHProvider;

#define TRACELOGGING_DECLARE_PROVIDER(h) extern const TraceLoggingHProvider h
#define TRACELOGGING_DEFINE_PROVIDER(h, name, id, ...) \
    static _tlgProvider_t _tlgProv_##h = {0};          \
    extern const TraceLoggingHProvider h = &_tlgProv_##h
#define TRACELOGGING_DEFINE_PROVIDER_STORAGE(storage, name, id, ...) static _tlgProvider_t storage = {0}
#define _tlg_DefineProvider_annotation(...)

inline HRESULT TraceLoggingRegister(TraceLoggingHProvider) { return S_OK; }
inline HRESULT TraceLoggingRegisterEx(TraceLoggingHProvider, TLG_PENABLECALLBACK, PVOID) { return S_OK; }
inline void TraceLoggingUnregister(TraceLoggingHProvider) {}
inline bool TraceLoggingProviderEnabled(TraceLoggingHProvider, UCHAR, ULONGLONG) { return false; }
inline HRESULT TraceLoggingSetInformation(TraceLoggingHProvider, int, PVOID, ULONG) { return S_OK; }
inline BOOL TraceLoggingProviderId(TraceLoggingHProvider) { return FALSE; }

#define TraceLoggingWrite(...) do { } while (0)
#define TraceLoggingWriteActivity(...) do { } while (0)
#define TraceLoggingWriteEx(...) do { } while (0)
#define TraceLoggingWriteTagged(...) do { } while (0)

#define _tlg_FOREACH(macro, ...)
#define _tlgKeywordVal(...)
#define _tlgLevelVal(...)
#define _tlgActivityDecl(activity)
#define _tlgActivityRef(activity) (activity)

struct _TlgReflectorTag_Param0IsProviderType {};
struct _TlgReflectorTag_Param0IsHProvider {};
struct _TlgReflectorTag_Param1IsProviderType {};
struct _TlgReflectorTag_Param1IsHProvider {};
