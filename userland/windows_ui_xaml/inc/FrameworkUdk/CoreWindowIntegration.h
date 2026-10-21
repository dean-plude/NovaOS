/* NovaOS: the Windows App SDK's FrameworkUdk/CoreWindowIntegration.h
 * (FrameworkUdk, shipped closed): a window hosting XAML on a CoreWindow
 * (dxaml/lib JupiterWindow.cpp) registers here to hear when the CoreWindow
 * moves.  NovaOS's desktop path has no CoreWindow, so the functions are
 * not built yet (a call stands in as an unbuilt function:
 * userland/windows_ui_xaml/build.py). */
#pragma once
#include <FrameworkUdk/CoreWindowIntegrationInterface.h>

HRESULT WindowPositionChanged_RegisterListener(
    _In_ ABI::Microsoft::Internal::FrameworkUdk::ICoreWindowPositionChangedListener *listener);
HRESULT WindowPositionChanged_UnregisterListener(
    _In_ ABI::Microsoft::Internal::FrameworkUdk::ICoreWindowPositionChangedListener *listener);
