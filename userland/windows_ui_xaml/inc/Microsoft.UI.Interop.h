/* NovaOS: the Windows App SDK's Microsoft.UI.Interop.h (shipped with the
 * SDK, not with XAML): a Win32 window's WindowId and back.  On Windows a
 * WindowId is the window handle's value, which the SDK's windowing keeps
 * and the functions translate; NovaOS's lifted windowing (src/windowing.cpp)
 * does the same. */
#pragma once
#include <windows.h>
#include <microsoft.ui.h>

namespace ABI { namespace Microsoft { namespace UI {
HRESULT GetWindowIdFromWindow(HWND hwnd, ABI::Microsoft::UI::WindowId *windowId);
HRESULT GetWindowFromWindowId(ABI::Microsoft::UI::WindowId windowId, HWND *hwnd);
}}}
