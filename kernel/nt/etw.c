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

/* -----------------------------------------------------------------------
 * wmilib.sys: the WMI library a WDM driver hands its IRP_MJ_SYSTEM_CONTROL
 * requests to.  Nothing on NovaOS sends WMI requests yet (no WMI service;
 * IoWMIRegistrationControl registers nothing), so a request that does
 * reach a driver is not one of its registered blocks: WmiSystemControl
 * says to pass it down its stack, as Windows does for another provider's.
 * ----------------------------------------------------------------------- */
enum { IrpProcessed, IrpNotCompleted, IrpNotWmi, IrpForward };

NT_API NTSTATUS nt_WmiSystemControl(void *wmi_context, NT_DEVICE_OBJECT *dev, NT_IRP *irp, int *disposition)
{
    (void)wmi_context; (void)dev;
    if (disposition) *disposition = IrpForward;
    return irp ? irp->IoStatus.Status : NT_STATUS_SUCCESS;
}

NT_API void nt_IofCompleteRequest(NT_IRP *irp, char boost);

NT_API NTSTATUS nt_WmiCompleteRequest(NT_DEVICE_OBJECT *dev, NT_IRP *irp, NTSTATUS status, UINT32 used, char boost)
{
    (void)dev;
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = used;
    nt_IofCompleteRequest(irp, boost);
    return status;
}
