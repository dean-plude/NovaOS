// Windows.UI.Xaml.dll's entry points: the system XAML (roadmap step 32.6,
// 0.2.2 "Pulsar"; docs/design/windows-ui-xaml.md).  NovaOS's XAML framework
// is Microsoft's WinUI 3 source; this DLL is it, as Windows' own
// Windows.UI.Xaml.dll is the system XAML a UWP app and WinUI 2 run on.
//
// The registry names this DLL for every Windows.UI.Xaml class
// (ActivatableClassId keys, kernel/um/um_registry.c), so
// RoGetActivationFactory(Windows.UI.Xaml.Application) loads it and calls
// DllGetActivationFactory, which answers with the framework's factory of
// the class under its Windows name (the framework's Windows.UI.Xaml faces,
// userland/windows_ui_xaml/src/windows_activation.cpp).  The first call
// starts the framework as Windows' DLL does when it loads: its static
// locks, its per-thread core's slot, the application's lock and the
// platform layer; and registers, for the process, the framework's own
// classes it activates by their lifted names (Application.Start's
// WindowsXamlManager, a window's XamlSourceFocusNavigationRequest, the UWP
// start-up's FrameworkViewSource and FrameworkView, a Frame's
// NavigationThemeTransition), which Windows App SDK's Microsoft.UI.Xaml.dll
// would serve, and the phone library's navigation transitions with the
// framework classes they make by name (the list is
// userland/windows_ui_xaml/src/windows_activation.cpp's).
//
// A program with the framework linked into it (the XAML self-tests) serves
// the classes itself: there, every class is left to it
// (userland/windows_ui_xaml/src/in_program.cpp).
#include <corep.h>
#include <CStaticLock.h>
#include <DXamlInstanceStorage.h>
#include <FrameworkApplication_Partial.h>
#include <roapi.h>

extern "C" HRESULT nova_xaml_windows_factory(HSTRING cls, IActivationFactory **out);
extern "C" IActivationFactory *nova_lifted_factory(const wchar_t *cls);

extern "C" HRESULT nova_register_lifted_classes(PFNGETACTIVATIONFACTORY maker);

static HRESULT WINAPI lifted_factory(HSTRING cls, IActivationFactory **out)
{
    *out = nova_lifted_factory(WindowsGetStringRawBuffer(cls, nullptr));
    return *out ? S_OK : CLASS_E_CLASSNOTAVAILABLE;
}

static INIT_ONCE s_started = INIT_ONCE_STATIC_INIT;
static HRESULT s_start_hr = E_FAIL;

static BOOL CALLBACK start(INIT_ONCE *, void *, void **)
{
    DirectUI::StaticLockGlobalInit();
    DXamlInstanceStorage::Initialize();
    DirectUI::FrameworkApplication::GlobalInit();
    IPlatformServices *pal = nullptr;
    s_start_hr = ObtainPlatformServices(&pal);
    if (FAILED(s_start_hr))
        return TRUE;
    s_start_hr = nova_register_lifted_classes(lifted_factory);      // (kept while the process runs, as the DLL is)
    return TRUE;
}

extern "C" HRESULT WINAPI DllGetActivationFactory(HSTRING cls, IActivationFactory **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (GetProcAddress(GetModuleHandleW(nullptr), "nova_xaml_in_program"))
        return REGDB_E_CLASSNOTREG;
    InitOnceExecuteOnce(&s_started, start, nullptr, nullptr);
    if (FAILED(s_start_hr))
        return s_start_hr;
    UINT32 n = 0;
    const wchar_t *s = WindowsGetStringRawBuffer(cls, &n);
    if (n > 18 && !wcsncmp(s, L"Microsoft.UI.Xaml.", 18)) {
        *out = nova_lifted_factory(s);
        return *out ? S_OK : CLASS_E_CLASSNOTAVAILABLE;
    }
    return nova_xaml_windows_factory(cls, out);
}

extern "C" HRESULT WINAPI DllCanUnloadNow()
{
    return S_FALSE;     // (the framework stays while the process runs)
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
