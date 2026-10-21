/* NovaOS: the Windows App SDK's lifted windowing's private header
 * (windowing.h, shipped closed with the SDK), which XAML's input services,
 * desktop window, window chrome, island root and popups include: a Win32
 * window's WindowId and back, as the framework asks for them.  On Windows a
 * WindowId is the window handle's value; NovaOS's stand-ins are in
 * src/windowing.cpp. */
#pragma once
#include <windows.h>
#include <microsoft.ui.h>

HRESULT Windowing_GetWindowIdFromWindow(HWND hwnd, ABI::Microsoft::UI::WindowId *windowId);
HRESULT Windowing_GetWindowFromWindowId(ABI::Microsoft::UI::WindowId windowId, HWND *hwnd);
