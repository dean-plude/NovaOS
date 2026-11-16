// NovaOS: the activation factories of the phone library's classes NovaOS
// builds, by name (src/phone/precomp.h).  Their sources list each class
// with WRL's ActivatableClass and ActivatableClassWithFactory, which put
// it in the DLL's WRL module (Microsoft.UI.Xaml.Phone.dll's
// DllGetActivationFactory answers from it); NovaOS's WRL keeps no such
// registry (inc/wrl/module.h), so the table is here, and the framework's
// own lookup by name (src/windows_activation.cpp, nova_lifted_factory)
// asks it after the framework's table.  Each factory is made once and
// kept while the process runs, as the module's are.
#include "precomp.h"

using namespace ABI::Microsoft::UI::Xaml::Media::Animation;

namespace {

// ActivatableClass(T): WRL's SimpleActivationFactory, which makes a T
// (here initialized: the class composes its base in RuntimeClassInitialize)
template <class T> class SimpleFactory : public wrl::AgileActivationFactory<>
{
public:
    IFACEMETHOD(ActivateInstance)(_Outptr_ IInspectable **instance) override
    {
        *instance = nullptr;
        wrl::ComPtr<T> object;
        HRESULT hr = wrl::MakeAndInitialize<T>(&object);
        return SUCCEEDED(hr) ? object.CopyTo(instance) : hr;
    }
};

template <class F> HRESULT make(IActivationFactory **out)
{
    wrl::ComPtr<F> factory;
    HRESULT hr = wrl::MakeAndInitialize<F>(&factory);
    return SUCCEEDED(hr) ? factory.CopyTo(out) : hr;
}

template <class T> HRESULT make_simple(IActivationFactory **out)
{
    wrl::ComPtr<SimpleFactory<T>> factory = wrl::Make<SimpleFactory<T>>();
    return factory ? factory.CopyTo(out) : E_OUTOFMEMORY;
}

struct Entry
{
    const wchar_t *name;
    HRESULT (*make)(IActivationFactory **);
};

const Entry s_classes[] = {
    {RuntimeClass_Microsoft_UI_Xaml_Media_Animation_NavigationThemeTransition, make<NavigationThemeTransitionFactory>},
    {RuntimeClass_Microsoft_UI_Xaml_Media_Animation_CommonNavigationTransitionInfo,
     make<CommonNavigationTransitionInfoFactory>},
    {RuntimeClass_Microsoft_UI_Xaml_Media_Animation_SlideNavigationTransitionInfo,
     make<SlideNavigationTransitionInfoFactory>},
    {RuntimeClass_Microsoft_UI_Xaml_Media_Animation_ContinuumNavigationTransitionInfo,
     make<ContinuumNavigationTransitionInfoFactory>},
    {RuntimeClass_Microsoft_UI_Xaml_Media_Animation_DrillInNavigationTransitionInfo,
     make_simple<DrillInNavigationTransitionInfo>},
    {RuntimeClass_Microsoft_UI_Xaml_Media_Animation_SuppressNavigationTransitionInfo,
     make_simple<SuppressNavigationTransitionInfo>},
    {RuntimeClass_Microsoft_UI_Xaml_Media_Animation_EntranceNavigationTransitionInfo,
     make<EntranceNavigationTransitionInfoFactory>},
};

IActivationFactory *s_made[ARRAYSIZE(s_classes)];

} // namespace

// the classes' names, for the DLL's registration of them
extern "C" const wchar_t *nova_phone_class(unsigned int i)
{
    return i < ARRAYSIZE(s_classes) ? s_classes[i].name : nullptr;
}

// the factory of @cls (a reference the caller releases), or null
extern "C" IActivationFactory *nova_phone_factory(const wchar_t *cls)
{
    for (unsigned int i = 0; i < ARRAYSIZE(s_classes); i++) {
        if (wcscmp(cls, s_classes[i].name))
            continue;
        IActivationFactory *f = s_made[i];
        if (!f) {
            IActivationFactory *made = nullptr;
            if (FAILED(s_classes[i].make(&made)))
                return nullptr;
            f = static_cast<IActivationFactory *>(
                InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&s_made[i]), made, nullptr));
            if (f)
                made->Release();        // (another thread's stands)
            else
                f = made;
        }
        f->AddRef();
        return f;
    }
    return nullptr;
}
