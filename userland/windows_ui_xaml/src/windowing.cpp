// NovaOS: the parts of the Windows App SDK's lifted windowing and content
// (shipped closed with the SDK, not with XAML) that the framework's desktop
// start-up and windows call, written for NovaOS:
//   ContentPreTranslateMessage (Microsoft.UI.Dispatching.Interop.h): what
//     Application.Start's message loop (dxaml/lib
//     FrameworkApplication_Partial.cpp, RunDesktopWindowMessageLoop) asks
//     before TranslateMessage: whether a content input site takes the
//     message itself (an accelerator, a key for an island).  NovaOS's
//     islands take their input as window messages, so none does, and every
//     message is translated and dispatched as a Win32 loop does.
#include <windows.h>
#include <Microsoft.UI.Dispatching.Interop.h>

extern "C" BOOL WINAPI ContentPreTranslateMessage(const MSG *msg)
{
    (void)msg;
    return FALSE;
}

// Theming_TryEnableHostBackdropBrush (FrameworkUdk/Theming.h): marks a
//   top-level window as where the host backdrop brush (a system backdrop's
//   sample of what is behind the window) is taken.  NovaOS's compositor
//   draws no host backdrop, so there is nothing to mark; the brush falls back
//   to its solid colour, as on Windows when transparency effects are off.
#include <FrameworkUdk/Theming.h>

HRESULT Theming_TryEnableHostBackdropBrush(HWND hwnd)
{
    (void)hwnd;
    return S_OK;
}

// Windowing_GetWindowIdFromWindow, Windowing_GetWindowFromWindowId
//   (windowing.h) and the SDK's public wrappers of them,
//   ABI::Microsoft::UI::GetWindowIdFromWindow and GetWindowFromWindowId
//   (Microsoft.UI.Interop.h): a window's WindowId is its handle's value, as
//   on Windows; an id that names no window is refused.
#include <windowing.h>
#include <Microsoft.UI.Interop.h>

HRESULT Windowing_GetWindowIdFromWindow(HWND hwnd, ABI::Microsoft::UI::WindowId *windowId)
{
    if (!windowId) return E_POINTER;
    windowId->Value = 0;
    if (hwnd && !IsWindow(hwnd)) return E_INVALIDARG;
    windowId->Value = static_cast<UINT64>(reinterpret_cast<UINT_PTR>(hwnd));
    return S_OK;
}

HRESULT Windowing_GetWindowFromWindowId(ABI::Microsoft::UI::WindowId windowId, HWND *hwnd)
{
    if (!hwnd) return E_POINTER;
    HWND h = reinterpret_cast<HWND>(static_cast<UINT_PTR>(windowId.Value));
    *hwnd = nullptr;
    if (h && !IsWindow(h)) return E_INVALIDARG;
    *hwnd = h;
    return S_OK;
}

namespace ABI { namespace Microsoft { namespace UI {
HRESULT GetWindowIdFromWindow(HWND hwnd, WindowId *windowId) { return Windowing_GetWindowIdFromWindow(hwnd, windowId); }
HRESULT GetWindowFromWindowId(WindowId windowId, HWND *hwnd) { return Windowing_GetWindowFromWindowId(windowId, hwnd); }
}}}
