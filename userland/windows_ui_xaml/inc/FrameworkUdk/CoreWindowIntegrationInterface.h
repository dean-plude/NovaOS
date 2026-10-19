/* NovaOS: the Windows App SDK's FrameworkUdk/CoreWindowIntegrationInterface.h
 * (FrameworkUdk, shipped closed).  A window that hosts XAML on a CoreWindow
 * (dxaml/lib JupiterWindow.h) is told when the CoreWindow moves through
 * this listener, which it registers with FrameworkUdk's
 * WindowPositionChanged_RegisterListener (CoreWindowIntegration.h).  The
 * interface is private to XAML and FrameworkUdk, so its IID here is
 * NovaOS's own. */
#pragma once
#include <unknwn.h>

namespace ABI { namespace Microsoft { namespace Internal { namespace FrameworkUdk {

struct __declspec(uuid("6e6f7661-0032-0009-0001-000000000001")) __declspec(novtable)
ICoreWindowPositionChangedListener : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE OnCoreWindowPositionChanged() = 0;
};

}}}}
