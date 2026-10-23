/* NovaOS: CompositionAnimationHelper (inc/CompHelper/CompositionAnimationHelper.h),
 * which XAML's animations start their composition animations through.
 * NovaOS's Windows.UI.Composition (userland/dcomp/composition.c) starts and
 * stops an animation on an object's property by name and keeps no
 * animation controllers, so the controller here remembers the object, the
 * property and the animation: Start starts it (again after a Stop; a
 * resume after Pause is a no-op, as the animation was never paused), Stop
 * stops it, and Pause and Seek leave it running (the composition side has
 * no clock to pause or move; XAML's own timeline keeps the right value). */
#include <windows.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <new>
#include <CompHelper/CompositionAnimationHelper.h>

namespace WUComp = ABI::Microsoft::UI::Composition;

namespace {

class Controller final : public ICompositionAnimationController
{
public:
    Controller(WUComp::ICompositionObject *object, const wchar_t *property, WUComp::ICompositionAnimation *animation)
        : m_object(object), m_animation(animation)
    {
        m_property.Set(property);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == __uuidof(IUnknown)) {
            *ppv = static_cast<IUnknown *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG n = InterlockedDecrement(&m_refs);
        if (!n)
            delete this;
        return n;
    }

    HRESULT Start()
    {
        if (m_running)
            return S_OK;
        HRESULT hr = m_object->StartAnimation(m_property.Get(), m_animation.Get());
        if (SUCCEEDED(hr))
            m_running = true;
        return hr;
    }
    HRESULT Stop()
    {
        if (!m_running)
            return S_OK;
        m_running = false;
        return m_object->StopAnimation(m_property.Get());
    }

private:
    LONG m_refs = 1;
    bool m_running = false;
    Microsoft::WRL::ComPtr<WUComp::ICompositionObject> m_object;
    Microsoft::WRL::Wrappers::HString m_property;
    Microsoft::WRL::ComPtr<WUComp::ICompositionAnimation> m_animation;
};

}

HRESULT CompositionAnimationHelper::ConnectAnimation(WUComp::ICompositionObject *animatingObject,
                                                     const wchar_t *propertyName,
                                                     WUComp::ICompositionAnimation *animation,
                                                     ICompositionAnimationController **controller)
{
    if (!animatingObject || !propertyName || !animation || !controller)
        return E_INVALIDARG;
    *controller = new (std::nothrow) Controller(animatingObject, propertyName, animation);
    return *controller ? S_OK : E_OUTOFMEMORY;
}

HRESULT CompositionAnimationHelper::Start(ICompositionAnimationController *controller)
{
    return controller ? static_cast<Controller *>(controller)->Start() : S_OK;
}

HRESULT CompositionAnimationHelper::Pause(ICompositionAnimationController *)
{
    return S_OK;
}

HRESULT CompositionAnimationHelper::Stop(ICompositionAnimationController *controller)
{
    return controller ? static_cast<Controller *>(controller)->Stop() : S_OK;
}

HRESULT CompositionAnimationHelper::Seek(ICompositionAnimationController *, UINT64)
{
    return S_OK;
}
