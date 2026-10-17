/* NovaOS: the Windows App SDK's FrameworkUdk/FrameworkInputView.h
 * (FrameworkUdk, shipped closed).  XAML learns where the touch keyboard
 * and other input panes cover its window through a listener on the
 * thread's CoreInputView (core/input/FrameworkInputViewHandler.cpp):
 * FrameworkInputView_RegisterListener and _UnregisterListener add and
 * remove it, and it is called with the occlusions and whether an earlier
 * listener handled them.  Declared here as XAML uses it; NovaOS defines the
 * functions when the input code builds. */
#pragma once
#include <windows.ui.viewmanagement.core.h>

namespace ABI { namespace Microsoft { namespace Internal { namespace FrameworkUdk {

struct __declspec(novtable) IFrameworkInputViewOcculusionsChangedListener
{
    virtual HRESULT STDMETHODCALLTYPE OnFrameworkInputViewOcclusionsChanged(
        boolean handled,
        ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::UI::ViewManagement::Core::CoreInputViewOcclusion *> *occlusions) = 0;
};

}}}}

HRESULT FrameworkInputView_RegisterListener(ABI::Windows::UI::ViewManagement::Core::ICoreInputView *coreInputView,
                                            ABI::Microsoft::Internal::FrameworkUdk::IFrameworkInputViewOcculusionsChangedListener *listener);
HRESULT FrameworkInputView_UnregisterListener(ABI::Windows::UI::ViewManagement::Core::ICoreInputView *coreInputView,
                                              ABI::Microsoft::Internal::FrameworkUdk::IFrameworkInputViewOcculusionsChangedListener *listener);
