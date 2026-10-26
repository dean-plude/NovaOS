// NovaOS: the parts of the Windows App SDK's FrameworkUdk (shipped closed)
// the framework's start-up calls, written for NovaOS:
//   FinalUnhandledErrorDetected: the error helper's (dxaml/lib
//     ErrorHelper.cpp) registration for the last chance to see an error no
//     handler took.  NovaOS's CoreApplication raises no
//     UnhandledErrorDetected yet (ole32's RoReportUnhandledError says so by
//     failing, and the error helper then handles the error itself), so the
//     handler is kept and not called.
//   Mui_GetFontFallbackLanguageList: a language's font fallback list (the
//     MUI fallback languages), which the text core asks for each language
//     it resolves (core/text/common/TextFormatting.cpp).  NovaOS has no MUI
//     fallback data, so there is none, and the text core uses the language
//     itself, as it does on Windows when the lookup fails.
//   Mui_PropagateApplicationLanguages: hands the app's preferred languages
//     on as the process's MUI languages, which the resource provider asks
//     for when the app's languages change (CommonResourceProvider.cpp).
//     NovaOS loads no MUI resources by language, so there is nothing to
//     hand on, and it succeeds.
//   VisualDiagnosticsPort: the message port diagnostics tools connect to a
//     XAML thread through.  NovaOS has no such tools, so there is nothing to
//     register with; the framework starts as it does on Windows when no
//     tool is listening.
//   WindowPositionChanged_RegisterListener and _UnregisterListener: a
//     window on a CoreWindow (dxaml/lib JupiterWindow.cpp, a UWP app's
//     window) registers to hear when the CoreWindow moves, so its popups
//     and text input follow.  The listeners are kept, not counted: the
//     window unregisters only in its destructor, so a counted reference
//     would keep it (and its subclass of the CoreWindow's window) alive
//     after the XAML core is gone, into the CoreWindow's WM_NCDESTROY.
//     They are not called yet: NovaOS's CoreWindow (twinapi.appcore)
//     reports no moves to FrameworkUdk.
//   BackButtonIntegration_RegisterListener, _UnregisterListener and
//     _InjectBackButtonPress: a control that takes the system's back button
//     while it is open (SplitView's pane: NavigationView's, an app bar, a
//     popup) registers while it is open and unregisters on closing and in
//     its destructor.  The listeners are kept, not counted, as the window
//     position ones are; a press goes to the newest first, until one
//     handles it.  NovaOS has no system back button yet, so nothing
//     injects a press but an app that calls it.
#include <windows.h>
#include <FrameworkUdk/FinalUnhandledErrorDetected.h>
#include <FrameworkUdk/VisualDiagnosticsPort.h>
#include <MuiUdk.h>
#include <FrameworkUdk/CoreWindowIntegration.h>
#include <FrameworkUdk/BackButtonIntegration.h>

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

HRESULT Mui_GetFontFallbackLanguageList(PCWSTR, size_t, PWSTR, size_t *cchWritten)
{
    if (cchWritten) *cchWritten = 0;
    return E_NOTIMPL;
}

HRESULT Mui_PropagateApplicationLanguages()
{
    return S_OK;
}

namespace {
using PositionListener = ABI::Microsoft::Internal::FrameworkUdk::ICoreWindowPositionChangedListener;
PositionListener *g_position_listeners[16];
}

HRESULT WindowPositionChanged_RegisterListener(PositionListener *listener)
{
    if (!listener) return E_INVALIDARG;
    HRESULT hr = E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&g_lock);
    for (auto &l : g_position_listeners) {
        if (!l) {
            l = listener;
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return hr;
}

HRESULT WindowPositionChanged_UnregisterListener(PositionListener *listener)
{
    AcquireSRWLockExclusive(&g_lock);
    for (auto &l : g_position_listeners) {
        if (l && l == listener) {
            l = nullptr;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return S_OK;
}

namespace {
using BackListener = ABI::Microsoft::Internal::FrameworkUdk::IBackButtonPressedListener;
BackListener *g_back_listeners[16];                     // (in registration order)
}

HRESULT BackButtonIntegration_RegisterListener(BackListener *listener)
{
    if (!listener) return E_INVALIDARG;
    HRESULT hr = E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&g_lock);
    for (auto &l : g_back_listeners) {
        if (l == listener) {                            // (open again: already registered)
            hr = S_OK;
            break;
        }
    }
    if (hr != S_OK) {
        for (auto &l : g_back_listeners) {
            if (!l) {
                l = listener;
                hr = S_OK;
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return hr;
}

HRESULT BackButtonIntegration_UnregisterListener(BackListener *listener)
{
    AcquireSRWLockExclusive(&g_lock);
    for (auto &l : g_back_listeners) {
        if (l && l == listener) l = nullptr;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return S_OK;
}

HRESULT BackButtonIntegration_InjectBackButtonPress(BOOLEAN *handled)
{
    if (!handled) return E_INVALIDARG;
    *handled = FALSE;
    for (int i = 15; i >= 0 && !*handled; i--) {
        AcquireSRWLockShared(&g_lock);
        BackListener *l = g_back_listeners[i];
        if (l) l->AddRef();
        ReleaseSRWLockShared(&g_lock);
        if (!l) continue;
        HRESULT hr = l->OnBackButtonPressed(handled);
        l->Release();
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}
