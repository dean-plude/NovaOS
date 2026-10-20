// NovaOS: the parts of the Windows App SDK's FrameworkUdk (shipped closed)
// the framework's start-up calls, written for NovaOS:
//   FinalUnhandledErrorDetected: the error helper's (dxaml/lib
//     ErrorHelper.cpp) registration for the last chance to see an error no
//     handler took.  NovaOS's CoreApplication raises no
//     UnhandledErrorDetected yet (ole32's RoReportUnhandledError says so by
//     failing, and the error helper then handles the error itself), so the
//     handler is kept and not called.
//   VisualDiagnosticsPort: the message port diagnostics tools connect to a
//     XAML thread through.  NovaOS has no such tools, so there is nothing to
//     register with; the framework starts as it does on Windows when no
//     tool is listening.
#include <windows.h>
#include <FrameworkUdk/FinalUnhandledErrorDetected.h>
#include <FrameworkUdk/VisualDiagnosticsPort.h>

namespace {
SRWLOCK g_lock = SRWLOCK_INIT;
IUnknown *g_final_handler;
INT64 g_token;
}

HRESULT FinalUnhandledErrorDetected_RegisterHandler(
    ABI::Windows::Foundation::IEventHandler<ABI::Windows::ApplicationModel::Core::UnhandledErrorDetectedEventArgs *> *handler,
    EventRegistrationToken *token)
{
    if (!handler || !token) return E_INVALIDARG;
    AcquireSRWLockExclusive(&g_lock);
    if (g_final_handler) {                              // (one registration, the framework's)
        ReleaseSRWLockExclusive(&g_lock);
        return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    }
    handler->AddRef();
    g_final_handler = handler;
    token->value = ++g_token;
    ReleaseSRWLockExclusive(&g_lock);
    return S_OK;
}

HRESULT FinalUnhandledErrorDetected_UnregisterHandler(EventRegistrationToken *token)
{
    if (!token) return E_INVALIDARG;
    IUnknown *h = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    if (g_final_handler && token->value == g_token) {
        h = g_final_handler;
        g_final_handler = nullptr;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (h) h->Release();
    token->value = 0;
    return S_OK;
}

HRESULT VisualDiagnosticsPort_Register(PCWSTR endpointName, VisualDiagnosticsPort_InitCallback callback, void *userContext)
{
    (void)endpointName; (void)callback; (void)userContext;
    return S_OK;
}

void VisualDiagnosticsPort_Unregister()
{
}
