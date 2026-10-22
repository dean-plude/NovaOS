// NovaOS: the XAML framework's classes by their Windows names
// (Windows.UI.Xaml.Controls.Button for the framework's
// Microsoft.UI.Xaml.Controls.Button), from the framework's own table of
// activation factories (Factories.g.cpp, ctl::__module), for the
// Windows.UI.Xaml faces (src/windows_faces.cpp): the factory a Windows
// name gives answers Windows' statics and factory interfaces through them.
#include "comModule.h"

extern "C" IActivationFactory *nova_lifted_factory(const wchar_t *cls)
{
    HSTRING h;
    if (FAILED(WindowsCreateString(cls, (UINT32)wcslen(cls), &h)))
        return nullptr;
    IActivationFactory *f = ctl::__module.GetActivationFactory(h);
    WindowsDeleteString(h);
    return f;
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
