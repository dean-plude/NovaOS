/*
 * MxWnf.h — Windows Notification Facility subscriptions, not available
 *
 * The framework subscribes to the power manager's
 * WNF_PO_DRIPS_DEVICE_CONSTRAINTS_REGISTERED state to join Sleep Study,
 * Modern Standby's battery diagnostics.  NovaOS has no WNF: subscribing
 * fails with STATUS_NOT_SUPPORTED, and the framework then leaves Sleep
 * Study off for the device, its documented behaviour on such a failure.
 */
#pragma once

/* (the name's value: the framework only passes it back to MxWnf) */
#define WNF_PO_DRIPS_DEVICE_CONSTRAINTS_REGISTERED { { 0xa3bc1075, 0x41c6033f } }

typedef struct _MxWnfSubscriptionContext *PMxWnfSubscriptionContext;

typedef NTSTATUS (MX_WNF_CALLBACK)(_In_ PMxWnfSubscriptionContext SubscriptionContext, _In_ PVOID CallbackContext);
typedef MX_WNF_CALLBACK *PMX_WNF_CALLBACK;

class MxWnf {
public:
    static __inline NTSTATUS MxSubscribeWnfStateChange(_Out_ PMxWnfSubscriptionContext *SubscriptionContext,
                                                       _In_ PCWNF_STATE_NAME StateName,
                                                       _In_ PMX_WNF_CALLBACK Callback,
                                                       _In_opt_ PVOID CallbackContext,
                                                       _In_ PVOID Tag)
    {
        UNREFERENCED_PARAMETER(StateName);
        UNREFERENCED_PARAMETER(Callback);
        UNREFERENCED_PARAMETER(CallbackContext);
        UNREFERENCED_PARAMETER(Tag);
        if (SubscriptionContext != NULL) {
            *SubscriptionContext = NULL;
        }
        return STATUS_NOT_SUPPORTED;
    }

    static __inline NTSTATUS MxQueryWnfStateData(_In_ PMxWnfSubscriptionContext SubscriptionContext,
                                                 _Out_writes_bytes_(*BufferSize) PVOID Buffer,
                                                 _Inout_ PULONG BufferSize)
    {
        UNREFERENCED_PARAMETER(SubscriptionContext);
        UNREFERENCED_PARAMETER(Buffer);
        UNREFERENCED_PARAMETER(BufferSize);
        return STATUS_NOT_SUPPORTED;
    }

    static __inline VOID MxUnsubscribeWnfStateChange(_Inout_ PMxWnfSubscriptionContext *SubscriptionContext)
    {
        if (SubscriptionContext != NULL) {
            *SubscriptionContext = NULL;
        }
    }
};
