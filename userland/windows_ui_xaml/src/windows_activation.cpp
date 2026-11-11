// NovaOS: the XAML framework's classes by their Windows names
// (Windows.UI.Xaml.Controls.Button for the framework's
// Microsoft.UI.Xaml.Controls.Button), from the framework's own table of
// activation factories (Factories.g.cpp, ctl::__module), for the
// Windows.UI.Xaml faces (src/windows_faces.cpp): the factory a Windows
// name gives answers Windows' statics and factory interfaces through them.
#include "comModule.h"
#include <roapi.h>

// (and the phone library's classes the framework builds, the navigation
// transitions, from their table: src/phone/phone_factories.cpp)
extern "C" IActivationFactory *nova_phone_factory(const wchar_t *cls);

extern "C" IActivationFactory *nova_lifted_factory(const wchar_t *cls)
{
    HSTRING h;
    if (FAILED(WindowsCreateString(cls, (UINT32)wcslen(cls), &h)))
        return nullptr;
    IActivationFactory *f = ctl::__module.GetActivationFactory(h);
    WindowsDeleteString(h);
    return f ? f : nova_phone_factory(cls);
}

// the framework's factory of the Windows.UI.Xaml class @cls
extern "C" HRESULT nova_xaml_windows_factory(HSTRING cls, IActivationFactory **out)
{
    static const wchar_t windows[] = L"Windows.UI.Xaml.", lifted[] = L"Microsoft.UI.Xaml.";
    const UINT32 wn = ARRAYSIZE(windows) - 1, ln = ARRAYSIZE(lifted) - 1;
    UINT32 n;
    const wchar_t *s = WindowsGetStringRawBuffer(cls, &n);
    *out = nullptr;
    wchar_t buf[256];
    if (n <= wn || wcsncmp(s, windows, wn) || n - wn + ln >= ARRAYSIZE(buf))
        return CLASS_E_CLASSNOTAVAILABLE;
    memcpy(buf, lifted, ln * sizeof(wchar_t));
    memcpy(buf + ln, s + wn, (n - wn) * sizeof(wchar_t));
    buf[n - wn + ln] = 0;
    *out = nova_lifted_factory(buf);
    return *out ? S_OK : CLASS_E_CLASSNOTAVAILABLE;
}

// the classes the framework itself activates by their lifted names, which
// the system Windows.UI.Xaml.dll (userland/windows_ui_xaml_dll) and a
// self-test serving the framework registers for the process: Application.
// Start's WindowsXamlManager, a window's XamlSourceFocusNavigationRequest,
// the UWP start-up's FrameworkViewSource and FrameworkView (Windows App
// SDK's Microsoft.UI.Xaml.dll would serve them), and the phone library's
// navigation transitions, a Frame's NavigationThemeTransition and the
// transition infos
// (src/phone/phone_factories.cpp, Microsoft.UI.Xaml.Phone.dll's in the
// Windows App SDK) with the framework classes they make by name: their
// base classes, storyboards, key frames, easing functions, projections
// and dependency properties.  Only these names: WinUI 2's
// Microsoft.UI.Xaml.dll, which a UWP app may carry, serves classes under
// Microsoft.UI.Xaml names of its own.
extern "C" const wchar_t *nova_phone_class(unsigned int i);

static const wchar_t *const s_lifted[] = {
    L"Microsoft.UI.Xaml.Hosting.WindowsXamlManager", L"Microsoft.UI.Xaml.Hosting.XamlSourceFocusNavigationRequest",
    L"Microsoft.UI.Xaml.FrameworkViewSource", L"Microsoft.UI.Xaml.FrameworkView",
    L"Microsoft.UI.Xaml.DependencyProperty", L"Microsoft.UI.Xaml.PropertyMetadata",
    L"Microsoft.UI.Xaml.Media.VisualTreeHelper", L"Microsoft.UI.Xaml.Media.PlaneProjection",
    L"Microsoft.UI.Xaml.Media.Animation.Transition", L"Microsoft.UI.Xaml.Media.Animation.NavigationTransitionInfo",
    L"Microsoft.UI.Xaml.Media.Animation.Storyboard", L"Microsoft.UI.Xaml.Media.Animation.DoubleAnimationUsingKeyFrames",
    L"Microsoft.UI.Xaml.Media.Animation.ObjectAnimationUsingKeyFrames",
    L"Microsoft.UI.Xaml.Media.Animation.DiscreteDoubleKeyFrame", L"Microsoft.UI.Xaml.Media.Animation.DiscreteObjectKeyFrame",
    L"Microsoft.UI.Xaml.Media.Animation.EasingDoubleKeyFrame", L"Microsoft.UI.Xaml.Media.Animation.LinearDoubleKeyFrame",
    L"Microsoft.UI.Xaml.Media.Animation.SplineDoubleKeyFrame", L"Microsoft.UI.Xaml.Media.Animation.KeySpline",
    L"Microsoft.UI.Xaml.Media.Animation.PointAnimation", L"Microsoft.UI.Xaml.Media.Animation.CircleEase",
    L"Microsoft.UI.Xaml.Media.Animation.ExponentialEase", L"Microsoft.UI.Xaml.Media.Animation.PowerEase"};

// registers them all with @maker (which answers with nova_lifted_factory),
// for the rest of the process
extern "C" HRESULT nova_register_lifted_classes(PFNGETACTIVATIONFACTORY maker)
{
    const UINT32 most = ARRAYSIZE(s_lifted) + 16;
    HSTRING names[most];
    PFNGETACTIVATIONFACTORY makers[most];
    UINT32 n = 0;
    for (; n < ARRAYSIZE(s_lifted); n++)
        WindowsCreateString(s_lifted[n], (UINT32)wcslen(s_lifted[n]), &names[n]);
    for (unsigned int i = 0; n < most && nova_phone_class(i); i++, n++)
        WindowsCreateString(nova_phone_class(i), (UINT32)wcslen(nova_phone_class(i)), &names[n]);
    for (UINT32 i = 0; i < n; i++)
        makers[i] = maker;
    RO_REGISTRATION_COOKIE cookie = nullptr;
    HRESULT hr = RoRegisterActivationFactories(names, makers, n, &cookie);
    for (UINT32 i = 0; i < n; i++)
        WindowsDeleteString(names[i]);
    return hr;
}
