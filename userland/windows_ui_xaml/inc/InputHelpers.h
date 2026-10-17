/* NovaOS: the Windows App SDK's InputHelpers.h (FrameworkUdk, shipped
 * closed).  Two helpers XAML's island and window code call
 * (core/core/elements/XamlIslandRoot.cpp, dxaml/lib/JupiterWindow.cpp):
 *   PointerPointHelper::CreateSimulatedNonClientPointerPoint makes the
 *     PointerPoint of a mouse press, move or release in the island's
 *     non-client area (a point in XAML's coordinates, whether the button is
 *     down now and was before, the island's window);
 *   SendKeyboardMessage hands a keyboard MSG XAML did not take as an
 *     accelerator back to the island's keyboard source, which raises its
 *     key events; *handled says whether they were handled.
 * Declared here as those callers use them; NovaOS defines them with its
 * lifted input (Microsoft.UI.Input) when the island code builds. */
#pragma once
#include <microsoft.ui.input.h>

namespace InputHelpers
{
    class PointerPointHelper
    {
    public:
        HRESULT CreateSimulatedNonClientPointerPoint(ABI::Windows::Foundation::Point point, bool isInContact,
                                                     bool wasPreviouslyInContact, ABI::Microsoft::UI::WindowId windowId,
                                                     ABI::Microsoft::UI::Input::IPointerPoint **pointerPoint);

    private:
        /* the pointer the simulated points belong to, and the last one */
        UINT32 m_pointerId = 1;
        UINT64 m_frameId = 0;
    };

    HRESULT SendKeyboardMessage(ABI::Microsoft::UI::Input::IInputKeyboardSource2 *keyboardSource, const MSG *msg, bool *handled);
}
