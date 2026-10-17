/* NovaOS: the Windows App SDK's microsoft.ui.composition.interop.h, as far
 * as XAML uses it.  XAML's composition tree host
 * (components/comptree/DCompTreeHost.cpp) asks its Compositor for
 * ICompositorSwapChainInterop to make the graphics device its drawing
 * surfaces live on (on its Direct3D 11 device) and composition surfaces
 * for swap chains (SwapChainPanel), by object or by handle.  The methods
 * are those, in that order, of Windows.UI.Composition's ICompositorInterop
 * (NovaOS's dcomp implements it); the IID is NovaOS's. */
#pragma once
#include <microsoft.ui.composition.h>

namespace ABI { namespace Microsoft { namespace UI { namespace Composition {

struct __declspec(uuid("a0b03594-c413-5f5d-bc70-ce9382eb5e83")) __declspec(novtable) ICompositorSwapChainInterop : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE CreateCompositionSurfaceForHandle(HANDLE swapChain, ICompositionSurface **result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCompositionSurfaceForSwapChain(IUnknown *swapChain, ICompositionSurface **result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateGraphicsDevice(IUnknown *renderingDevice, ICompositionGraphicsDevice **result) = 0;
};

}}}}
