// NovaOS: the Windows App SDK's DirectManipulationHelper.h (FrameworkUdk,
// shipped closed), the helper XAML's DirectManipulation service and
// composition nodes reach the lifted DirectManipulation through
// (plat/win/browserdesktop/DirectManipulationService.h holds one).  So far
// it declares what the headers NovaOS builds name: the helper's functions
// are defined when the DirectManipulation service builds (until then a
// call stands in as an unbuilt function: userland/windows_ui_xaml/build.py).
#pragma once
#include <wrl/client.h>
#include <Microsoft.DirectManipulation.h>

class DirectManipulationHelper
{
public:
    // the lifted DirectManipulation manager's class factory, from its DLL
    static HRESULT GetDirectManipulationManagerFactory(_In_ HMODULE dmanip, _Outptr_ IClassFactory **factory);
};

// the helper a DirectManipulation service owns, made once it has its
// manager and compositor
class DirectManipulationHelperHolder
{
public:
    void Initialize(_In_ IDirectManipulationCompositor *compositor, _In_ IDirectManipulationManager *manager);
    DirectManipulationHelper *operator->() const { return m_helper; }
    DirectManipulationHelper *Get() const { return m_helper; }

private:
    DirectManipulationHelper *m_helper = nullptr;
};
