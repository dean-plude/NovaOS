/* NovaOS: the Windows App SDK's TextInputProducerHelper.h (FrameworkUdk,
 * shipped closed).  When XAML runs on a CoreWindow with the TSF3 text
 * framework (core/input/InputServices.cpp SetCoreWindow), its key events
 * come through the window's text input producer; Init takes it from the
 * window, IsValid says one was found, and SetKeyDownHandled stops the
 * producer inserting the character of a key XAML handled
 * (components/ContentRoot/KeyboardInputProcessor.cpp).
 * EnableTemporaryUWPTestMode is for Microsoft's tests.  Declared here as
 * XAML uses it; NovaOS defines Init and SetKeyDownHandled with its
 * CoreWindow (twinapi.appcore) when the input code builds. */
#pragma once
#include <windows.ui.core.h>

class TextInputProducerHelper
{
public:
    void Init(ABI::Windows::UI::Core::ICoreWindow *coreWindow);
    bool IsValid() const { return m_producer != nullptr; }
    void SetKeyDownHandled();
    static void EnableTemporaryUWPTestMode(bool enable);

private:
    /* the CoreWindow's text input producer, when it has one */
    IUnknown *m_producer = nullptr;
};
