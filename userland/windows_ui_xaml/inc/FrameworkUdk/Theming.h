/* NovaOS: the Windows App SDK's FrameworkUdk theming header (shipped
 * closed): the host backdrop a desktop window's system backdrop (Mica,
 * acrylic) samples, which XAML's desktop window turns on for its top-level
 * window (dxaml/lib DesktopWindowImpl.cpp); popups (core Popup.cpp)
 * include it too. */
#pragma once
#include <windows.h>

HRESULT Theming_TryEnableHostBackdropBrush(HWND hwnd);
