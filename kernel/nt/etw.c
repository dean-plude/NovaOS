/*
 * etw.c — Event Tracing for Windows, for drivers that log through it
 *
 * Drivers register ETW providers and WPP software tracing and write
 * events; on NovaOS no trace session consumes them, so registration
 * succeeds with a handle, events are dropped, and "is anyone listening?"
 * is always no.  This is what a Windows driver sees when ETW is enabled
 * but nothing is tracing it, so drivers that trace run unchanged; a real
 * trace consumer is a later step.  DbgPrint (ntoskrnl.c) is the log that
 * does reach the NovaOS console.
 */
#include "nt.h"
#include "../lib/string.h"

typedef UINT64 NT_REGHANDLE;

/* EtwRegister(LPCGUID, PETWENABLECALLBACK, PVOID, PREGHANDLE) */
NT_API NTSTATUS nt_EtwRegister(const void *guid, void *callback, void *ctx, NT_REGHANDLE *out)
{
    (void)guid; (void)callback; (void)ctx;
    if (out) *out = 1;                                /* a non-zero handle: registered, nobody tracing */
    return NT_STATUS_SUCCESS;
}

NT_API NTSTATUS nt_EtwUnregister(NT_REGHANDLE h) { (void)h; return NT_STATUS_SUCCESS; }

/* EtwWrite / EtwWriteTransfer / EtwWriteEx: dropped (no consumer) */
NT_API NTSTATUS nt_EtwWrite(NT_REGHANDLE h, const void *desc, const void *act, UINT32 count, void *data)
{
    (void)h; (void)desc; (void)act; (void)count; (void)data;
    return NT_STATUS_SUCCESS;
}

NT_API NTSTATUS nt_EtwWriteTransfer(NT_REGHANDLE h, const void *desc, const void *act, const void *rel,
                                    UINT32 count, void *data)
{
    (void)h; (void)desc; (void)act; (void)rel; (void)count; (void)data;
    return NT_STATUS_SUCCESS;
}

/* EtwEventEnabled / EtwProviderEnabled: nothing is tracing */
NT_API UINT8 nt_EtwEventEnabled(NT_REGHANDLE h, const void *desc) { (void)h; (void)desc; return 0; }
NT_API UINT8 nt_EtwProviderEnabled(NT_REGHANDLE h, UINT8 level, UINT64 keyword)
{
    (void)h; (void)level; (void)keyword;
    return 0;
}

/* WPP software tracing (WmiTraceMessage): dropped */
NT_API NTSTATUS nt_WmiTraceMessage(UINT64 logger, UINT32 flags, void *guid, UINT16 num, ...)
{
    (void)logger; (void)flags; (void)guid; (void)num;
    return NT_STATUS_SUCCESS;
}

/* WmiTraceMessageVa: the va_list form, dropped the same way */
NT_API NTSTATUS nt_WmiTraceMessageVa(UINT64 logger, UINT32 flags, void *guid, UINT16 num, __builtin_ms_va_list ap)
{
    (void)logger; (void)flags; (void)guid; (void)num; (void)ap;
    return NT_STATUS_SUCCESS;
}

/* WmiQueryTraceInformation: no trace session to describe (KMDF then
 * uses its own no-op performance-trace routines) */
NT_API NTSTATUS nt_WmiQueryTraceInformation(int cls, void *info, UINT32 len, UINT32 *ret_len, void *buffer)
{
    (void)cls; (void)info; (void)len; (void)buffer;
    if (ret_len) *ret_len = 0;
    return NT_STATUS_NOT_SUPPORTED;
}

/* EtwActivityIdControl: no thread carries an activity ID (GET and
 * GET_SET give the zero GUID); CREATE gives a new random one */
NT_API NTSTATUS nt_EtwActivityIdControl(UINT32 code, UINT8 *guid)
{
    enum { GET_ID = 1, SET_ID, CREATE_ID, GET_SET_ID, CREATE_SET_ID };
    if (!guid) return NT_STATUS_INVALID_PARAMETER;
    switch (code) {
    case GET_ID: case GET_SET_ID: memset(guid, 0, 16); return NT_STATUS_SUCCESS;
    case SET_ID: return NT_STATUS_SUCCESS;
    case CREATE_ID: case CREATE_SET_ID: return NtExUuid(guid);
    default: return NT_STATUS_INVALID_PARAMETER;
    }
}
