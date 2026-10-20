/* NovaOS: the Windows App SDK's FrameworkUdk/BackButtonIntegration.h
 * (FrameworkUdk, shipped closed).  A control that handles the system's back
 * button (an app bar, a popup, a dialog: their generated .g.h) implements
 * this listener and registers it with BackButtonIntegration_RegisterListener
 * while it is open.  The interface is private to XAML and FrameworkUdk, so
 * its IID here is NovaOS's own. */
#pragma once
#include <unknwn.h>

namespace ABI { namespace Microsoft { namespace Internal { namespace FrameworkUdk {

struct __declspec(uuid("6e6f7661-0032-000b-0001-000000000001")) __declspec(novtable)
IBackButtonPressedListener : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE OnBackButtonPressed(BOOLEAN *handled) = 0;
};

}}}}

HRESULT BackButtonIntegration_RegisterListener(ABI::Microsoft::Internal::FrameworkUdk::IBackButtonPressedListener *listener);
HRESULT BackButtonIntegration_UnregisterListener(ABI::Microsoft::Internal::FrameworkUdk::IBackButtonPressedListener *listener);
HRESULT BackButtonIntegration_InjectBackButtonPress(BOOLEAN *handled);
