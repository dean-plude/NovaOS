/* winui2test.exe — WinUI 2's own controls (Microsoft.UI.Xaml.dll 2.x, built
 * from Microsoft's winui2/main sources by userland/microsoft_ui_xaml) on the
 * system XAML, under its Windows.UI.Xaml names (roadmap step 32.5, second
 * part; docs/design/windows-ui-xaml.md).  The framework starts as
 * wuxamltest starts it, serving the Windows.UI.Xaml classes WinUI 2's
 * controls compose over; in OnLaunched the test is a WinUI 2 app:
 *   - Microsoft.UI.Xaml.dll loaded, its DllGetActivationFactory serving
 *     WinUI 2's XAML type table (XamlControlsXamlMetaDataProvider, Windows'
 *     IXamlMetadataProvider), which the app's Application hands the
 *     framework as a WinUI 2 app's generated one does (the framework's
 *     metadata, XamlLibMetadataProvider, asks it for WinUI 2's types);
 *   - the type Microsoft.UI.Xaml.Controls.InfoBadge, its base type, and an
 *     InfoBadge it activates: its class name, and the framework's Windows
 *     IControl, IFrameworkElement and IDependencyObject behind the same
 *     object (WinUI 2 composes its controls over Windows.UI.Xaml.Controls.
 *     Control);
 *   - InfoBadge.Value (a dependency property WinUI 2 registered on the
 *     framework) read, set and read back through the type's member;
 *   - a DropDownButton (composed over Windows.UI.Xaml.Controls.Button): its
 *     Windows IButton and its content set and read back;
 *   - WinUI 2's XAML resources (step 32.5's third part): the merged
 *     19h1 theme resources and default styles installed where ms-appx:///
 *     Microsoft.UI.Xaml/Themes/ finds them.
 *
 *   winui2test        the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <new>
#include <corep.h>
#include <CStaticLock.h>
#include <SystemThemingInterop.h>
#include <DXamlInstanceStorage.h>
#include <WindowsXamlManager.g.h>
#include <roapi.h>
#include <FrameworkApplication_Partial.h>
#include <microsoft.ui.dispatching.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.markup.h>

namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxm = ABI::Windows::UI::Xaml::Markup;

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define CHECK_HR(what, expr) do { HRESULT h_ = (expr); if (SUCCEEDED(h_)) pass++; else { fail++; \
    printf("FAIL: %s: 0x%08lx (line %d)\n", what, static_cast<unsigned long>(h_), __LINE__); fflush(stdout); } } while (0)

extern "C" HRESULT nova_xaml_windows_factory(HSTRING cls, IActivationFactory **out);

static bool is(HSTRING h, const wchar_t *s)
{
    return wcscmp(h ? WindowsGetStringRawBuffer(h, nullptr) : L"", s) == 0;
}

template <class T> static HRESULT as(IUnknown *p, T **out)
{
    *out = nullptr;
    return p ? p->QueryInterface(__uuidof(T), reinterpret_cast<void **>(out)) : E_POINTER;
}

static IUnknown *identity(IUnknown *p)
{
    IUnknown *u = nullptr;
    if (p && SUCCEEDED(p->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&u))))
        u->Release();
    return u;
}

static HRESULT box_int(INT32 v, IInspectable **out)
{
    ctl::ComPtr<wf::IPropertyValueStatics> pv;
    IFC_RETURN(wf::GetActivationFactory(wrl_wrappers::HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(), pv.ReleaseAndGetAddressOf()));
    return pv->CreateInt32(v, out);
}

static bool unboxed_int(IInspectable *v, INT32 want)
{
    ctl::ComPtr<wf::IPropertyValue> pv;
    INT32 got = 0;
    return v && SUCCEEDED(v->QueryInterface(IID_PPV_ARGS(&pv))) && SUCCEEDED(pv->GetInt32(&got)) && got == want;
}

static bool unboxed_is(IInspectable *v, const wchar_t *s)
{
    ctl::ComPtr<wf::IPropertyValue> pv;
    HSTRING h = nullptr;
    bool r = v && SUCCEEDED(v->QueryInterface(IID_PPV_ARGS(&pv))) && SUCCEEDED(pv->GetString(&h)) && is(h, s);
    WindowsDeleteString(h);
    return r;
}

/* Microsoft.UI.Xaml.dll's factory of class @cls, as C++/WinRT's
 * activation finds a class no registration names (the DLL named after its
 * namespace, then the namespace's parent's) */
typedef HRESULT (WINAPI *GetFactory)(HSTRING, IActivationFactory **);
static GetFactory s_mux;
static HRESULT activate(const wchar_t *cls, IInspectable **out)
{
    *out = nullptr;
    ctl::ComPtr<IActivationFactory> f;
    IFC_RETURN(s_mux ? s_mux(wrl_wrappers::HStringReference(cls).Get(), f.GetAddressOf()) : E_FAIL);
    return f->ActivateInstance(out);
}

static wuxm::IXamlMetadataProvider *winui2_provider();

static void winui2_checks()
{
    if (!s_mux)
        return;

    /* WinUI 2's XAML type table, through which the XAML parser makes its
     * controls (composable classes: their factories make them through
     * their own CreateInstance, IActivationFactory's ActivateInstance is
     * not theirs) */
    wuxm::IXamlMetadataProvider *provider = winui2_provider();
    CHECK("XamlControlsXamlMetaDataProvider activated, its Windows IXamlMetadataProvider", provider != nullptr);
    if (!provider)
        return;
    ctl::ComPtr<wuxm::IXamlType> type, base;
    CHECK_HR("the type Microsoft.UI.Xaml.Controls.InfoBadge", provider->GetXamlTypeByFullName(
        wrl_wrappers::HStringReference(L"Microsoft.UI.Xaml.Controls.InfoBadge").Get(), type.GetAddressOf()));
    CHECK("found", type != nullptr);
    if (!type)
        return;
    HSTRING baseName = nullptr;
    CHECK("its base type, Windows.UI.Xaml.Controls.Control", SUCCEEDED(type->get_BaseType(base.GetAddressOf())) && base &&
          SUCCEEDED(base->get_FullName(&baseName)) && is(baseName, L"Windows.UI.Xaml.Controls.Control"));
    WindowsDeleteString(baseName);

    /* an InfoBadge, composed over the framework's Control */
    ctl::ComPtr<IInspectable> badge;
    CHECK_HR("the type activates an InfoBadge", type->ActivateInstance(badge.GetAddressOf()));
    if (!badge)
        return;
    HSTRING name = nullptr;
    CHECK("its class name, Microsoft.UI.Xaml.Controls.InfoBadge", SUCCEEDED(badge->GetRuntimeClassName(&name)) &&
          is(name, L"Microsoft.UI.Xaml.Controls.InfoBadge"));
    WindowsDeleteString(name);
    ctl::ComPtr<wuxc::IControl> control;
    ctl::ComPtr<wux::IFrameworkElement> fe;
    ctl::ComPtr<wux::IDependencyObject> dobj;
    CHECK("it answers Windows' IControl, IFrameworkElement and IDependencyObject (the framework's Control under it)",
          SUCCEEDED(as(badge.Get(), control.GetAddressOf())) && SUCCEEDED(as(badge.Get(), fe.GetAddressOf())) &&
          SUCCEEDED(as(badge.Get(), dobj.GetAddressOf())));
    CHECK("one object behind them (one IUnknown)", control && fe && identity(control.Get()) == identity(badge.Get()) &&
          identity(fe.Get()) == identity(badge.Get()));
    DOUBLE width = 0;
    CHECK("FrameworkElement.Width set and read back through it", fe && SUCCEEDED(fe->put_Width(16)) &&
          SUCCEEDED(fe->get_Width(&width)) && width == 16);

    /* its Value, a dependency property WinUI 2 registered on the framework */
    ctl::ComPtr<wuxm::IXamlMember> value;
    boolean isDP = false;
    CHECK("the type's Value member, a dependency property", SUCCEEDED(type->GetMember(wrl_wrappers::HStringReference(L"Value").Get(),
          value.GetAddressOf())) && value && SUCCEEDED(value->get_IsDependencyProperty(&isDP)) && isDP);
    if (!value)
        return;
    ctl::ComPtr<IInspectable> v, five;
    CHECK("Value: -1 (InfoBadge's default) read through the member", SUCCEEDED(value->GetValue(badge.Get(), v.GetAddressOf())) &&
          unboxed_int(v.Get(), -1));
    CHECK_HR("Value set to 5", SUCCEEDED(box_int(5, five.GetAddressOf())) ? value->SetValue(badge.Get(), five.Get()) : E_FAIL);
    CHECK("and read back", SUCCEEDED(value->GetValue(badge.Get(), v.ReleaseAndGetAddressOf())) && unboxed_int(v.Get(), 5));

    /* a DropDownButton, composed over the framework's Button */
    ctl::ComPtr<IInspectable> drop, label, got;
    ctl::ComPtr<wuxm::IXamlType> dropType;
    ctl::ComPtr<wuxc::IButton> button;
    ctl::ComPtr<wuxc::IContentControl> content;
    CHECK_HR("Microsoft.UI.Xaml.Controls.DropDownButton activated through its type", SUCCEEDED(provider->GetXamlTypeByFullName(
        wrl_wrappers::HStringReference(L"Microsoft.UI.Xaml.Controls.DropDownButton").Get(), dropType.GetAddressOf())) && dropType ?
        dropType->ActivateInstance(drop.GetAddressOf()) : E_FAIL);
    CHECK("it answers Windows' IButton and IContentControl", SUCCEEDED(as(drop.Get(), button.GetAddressOf())) &&
          SUCCEEDED(as(drop.Get(), content.GetAddressOf())));
    ctl::ComPtr<wf::IPropertyValueStatics> pv;
    CHECK("its content set and read back", content &&
          SUCCEEDED(wf::GetActivationFactory(wrl_wrappers::HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(), pv.ReleaseAndGetAddressOf())) &&
          SUCCEEDED(pv->CreateString(wrl_wrappers::HStringReference(L"More").Get(), label.GetAddressOf())) &&
          SUCCEEDED(content->put_Content(label.Get())) && SUCCEEDED(content->get_Content(got.GetAddressOf())) &&
          unboxed_is(got.Get(), L"More"));

    /* WinUI 2's XAML resources, merged as Microsoft's build merges them,
     * where an unpackaged program's ms-appx:///Microsoft.UI.Xaml/Themes/
     * finds them (the files XamlControlsResources and the controls'
     * DefaultStyleResourceUri name on NovaOS's Windows 10 contracts).
     * Loading them waits on the framework: their styles use data binding
     * ({Binding}, not built yet) and XAML's text formatting starts from
     * DirectWrite's Windows 10 font model (32.4). */
    static const wchar_t *const themes[] = {L"19h1_themeresources.xaml", L"19h1_compact_themeresources.xaml",
                                            L"19h1_generic.xaml", L"19h1_themeresources_v1.xaml",
                                            L"19h1_compact_themeresources_v1.xaml", L"19h1_generic_v1.xaml"};
    for (const wchar_t *t : themes) {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"C:\\Programs\\Microsoft.UI.Xaml\\Themes\\%ls", t);
        WIN32_FILE_ATTRIBUTE_DATA fa;
        char what[96];
        snprintf(what, sizeof what, "Microsoft.UI.Xaml\\Themes\\%ls installed", t);
        CHECK(what, GetFileAttributesExW(path, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 10000);
    }
}

/* WinUI 2's XAML type table, made once (on the UI thread, when the
 * framework or the test first asks) */
static wuxm::IXamlMetadataProvider *winui2_provider()
{
    static wuxm::IXamlMetadataProvider *provider;
    ctl::ComPtr<IInspectable> made;
    if (!provider && SUCCEEDED(activate(L"Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider", made.GetAddressOf())))
        as(made.Get(), &provider);
    return provider;
}

/* the framework started as wuxamltest starts it; the app's Application is
 * its IXamlMetadataProvider, as a WinUI 2 app's (its generated XamlTypeInfo
 * hands the framework WinUI 2's types) */
static bool s_launched, s_exit_called;

class App : public xaml::IApplicationOverrides, public wuxm::IXamlMetadataProvider
{
public:
    IInspectable *m_inner = nullptr;
    LONG m_refs = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == __uuidof(xaml::IApplicationOverrides)) {
            *out = static_cast<xaml::IApplicationOverrides *>(this);
            AddRef();
            return S_OK;
        }
        if (iid == __uuidof(wuxm::IXamlMetadataProvider)) {
            *out = static_cast<wuxm::IXamlMetadataProvider *>(this);
            AddRef();
            return S_OK;
        }
        if (!m_inner) { *out = nullptr; return E_NOINTERFACE; }
        return m_inner->QueryInterface(iid, out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_refs);
        if (!r) { if (m_inner) m_inner->Release(); delete this; }
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *n, IID **iids) override { *n = 0; *iids = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override
    {
        return WindowsCreateString(L"Winui2Test.App", 14, name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }

    HRESULT STDMETHODCALLTYPE GetXamlType(wux::Interop::TypeName type, wuxm::IXamlType **result) override
    {
        *result = nullptr;
        wuxm::IXamlMetadataProvider *p = winui2_provider();
        return p ? p->GetXamlType(type, result) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetXamlTypeByFullName(HSTRING name, wuxm::IXamlType **result) override
    {
        *result = nullptr;
        wuxm::IXamlMetadataProvider *p = winui2_provider();
        return p ? p->GetXamlTypeByFullName(name, result) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetXmlnsDefinitions(UINT32 *n, wuxm::XmlnsDefinition **defs) override
    {
        *n = 0;
        *defs = nullptr;
        wuxm::IXamlMetadataProvider *p = winui2_provider();
        return p ? p->GetXmlnsDefinitions(n, defs) : S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnLaunched(xaml::ILaunchActivatedEventArgs *) override
    {
        s_launched = true;
        winui2_checks();
        ctl::ComPtr<msy::IDispatcherQueueStatics> queueStatics;
        ctl::ComPtr<msy::IDispatcherQueue> queue;
        CHECK("the thread's DispatcherQueue", SUCCEEDED(wf::GetActivationFactory(
            wrl_wrappers::HStringReference(RuntimeClass_Microsoft_UI_Dispatching_DispatcherQueue).Get(), queueStatics.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(queueStatics->GetForCurrentThread(&queue)) && queue);
        if (!queue) return S_OK;
        boolean queued = false;
        CHECK_HR("work queued on it", queue->TryEnqueue(wrl::Callback<msy::IDispatcherQueueHandler>([this]() -> HRESULT {
            ctl::ComPtr<xaml::IApplication> app;
            CHECK_HR("Application.Exit", SUCCEEDED(QueryInterface(__uuidof(xaml::IApplication),
                     reinterpret_cast<void **>(app.GetAddressOf()))) ? app->Exit() : E_FAIL);
            s_exit_called = true;
            return S_OK;
        }).Get(), &queued));
        return S_OK;
    }
};
static App *s_app;

static HRESULT on_start(xaml::IApplicationInitializationCallbackParams *)
{
    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationFactory> factory;
    if (appFactory) appFactory.As(&factory);
    s_app = new App();
    ctl::ComPtr<xaml::IApplication> instance;
    CHECK_HR("the app's Application", factory ? factory->CreateInstance(
        static_cast<xaml::IApplicationOverrides *>(s_app), &s_app->m_inner, instance.ReleaseAndGetAddressOf()) : E_FAIL);
    return S_OK;
}

static DWORD WINAPI watchdog(void *)
{
    Sleep(90000);
    printf("FAIL: Application.Start did not return\nwinui2test: %d passed, %d failed (%s)\n", pass, fail + 1,
           sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(1);
}

/* the classes this program serves, as Windows.UI.Xaml.dll serves them: the
 * framework's WindowsXamlManager (which Application.Start activates by its
 * name) and, by their Windows names (nova_xaml_windows_factory), the
 * Windows.UI.Xaml classes WinUI 2's controls compose over or call */
static const wchar_t *const s_windows_classes[] = {
    L"Windows.UI.Xaml.DependencyObject", L"Windows.UI.Xaml.DependencyProperty", L"Windows.UI.Xaml.PropertyMetadata",
    L"Windows.UI.Xaml.UIElement", L"Windows.UI.Xaml.FrameworkElement", L"Windows.UI.Xaml.Application",
    L"Windows.UI.Xaml.Window", L"Windows.UI.Xaml.VisualStateManager", L"Windows.UI.Xaml.ResourceDictionary",
    L"Windows.UI.Xaml.Style", L"Windows.UI.Xaml.Setter", L"Windows.UI.Xaml.DataTemplate",
    L"Windows.UI.Xaml.Controls.Control", L"Windows.UI.Xaml.Controls.ContentControl", L"Windows.UI.Xaml.Controls.Button",
    L"Windows.UI.Xaml.Controls.Primitives.ButtonBase", L"Windows.UI.Xaml.Controls.Primitives.ToggleButton",
    L"Windows.UI.Xaml.Controls.Primitives.FlyoutBase", L"Windows.UI.Xaml.Controls.IconElement",
    L"Windows.UI.Xaml.Controls.FontIcon", L"Windows.UI.Xaml.Controls.SymbolIcon", L"Windows.UI.Xaml.Controls.BitmapIcon",
    L"Windows.UI.Xaml.Controls.PathIcon", L"Windows.UI.Xaml.Controls.TextBlock", L"Windows.UI.Xaml.Controls.Grid",
    L"Windows.UI.Xaml.Controls.Border", L"Windows.UI.Xaml.Controls.Image", L"Windows.UI.Xaml.Controls.ToolTipService",
    L"Windows.UI.Xaml.Media.VisualTreeHelper", L"Windows.UI.Xaml.Media.SolidColorBrush", L"Windows.UI.Xaml.Media.Brush",
    L"Windows.UI.Xaml.Media.XamlCompositionBrushBase", L"Windows.UI.Xaml.Media.XamlLight",
    L"Windows.UI.Xaml.Media.CompositionTarget", L"Windows.UI.Xaml.Data.Binding",
    L"Windows.UI.Xaml.Automation.AutomationProperties", L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer",
    L"Windows.UI.Xaml.Automation.Peers.ButtonAutomationPeer", L"Windows.UI.Xaml.Hosting.ElementCompositionPreview",
    L"Windows.UI.Xaml.Markup.XamlReader"};

static HRESULT WINAPI make_factory(HSTRING cls, IActivationFactory **out)
{
    *out = nullptr;
    if (is(cls, RuntimeClass_Microsoft_UI_Xaml_Hosting_WindowsXamlManager)) {
        ctl::ComPtr<DirectUI::WindowsXamlManagerFactory> f;
        IFC_RETURN(ctl::ComObject<DirectUI::WindowsXamlManagerFactory>::CreateInstance(f.ReleaseAndGetAddressOf()));
        return f.CopyTo(out);
    }
    return nova_xaml_windows_factory(cls, out);
}

int main()
{
    DirectUI::StaticLockGlobalInit();
    DXamlInstanceStorage::Initialize();
    DirectUI::FrameworkApplication::GlobalInit();
    IPlatformServices *pal = nullptr;
    CHECK_HR("ObtainPlatformServices", ObtainPlatformServices(&pal));
    SystemThemingInterop::OverrideSystemTheme(Theming::Theme::Dark);
    const UINT32 nclasses = 1 + ARRAYSIZE(s_windows_classes);
    HSTRING classes[nclasses];
    PFNGETACTIVATIONFACTORY makers[nclasses];
    WindowsCreateString(RuntimeClass_Microsoft_UI_Xaml_Hosting_WindowsXamlManager,
                        (UINT32)wcslen(RuntimeClass_Microsoft_UI_Xaml_Hosting_WindowsXamlManager), &classes[0]);
    for (UINT32 i = 0; i < nclasses; i++) {
        if (i) WindowsCreateString(s_windows_classes[i - 1], (UINT32)wcslen(s_windows_classes[i - 1]), &classes[i]);
        makers[i] = make_factory;
    }
    RO_REGISTRATION_COOKIE cookie = nullptr;
    CHECK_HR("RoRegisterActivationFactories (the framework's classes)", RoRegisterActivationFactories(classes, makers, nclasses, &cookie));
    CloseHandle(CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr));
    HMODULE mux = LoadLibraryW(L"Microsoft.UI.Xaml.dll");
    CHECK("Microsoft.UI.Xaml.dll (WinUI 2) loaded", mux != nullptr);
    s_mux = mux ? reinterpret_cast<GetFactory>(GetProcAddress(mux, "DllGetActivationFactory")) : nullptr;
    CHECK("its DllGetActivationFactory", s_mux != nullptr);

    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationStatics> appStatics;
    CHECK("Application's statics", appFactory && SUCCEEDED(appFactory.As(&appStatics)));
    if (!appStatics) return 1;
    CHECK_HR("Application.Start", appStatics->Start(wrl::Callback<xaml::IApplicationInitializationCallback>(on_start).Get()));
    CHECK("OnLaunched ran", s_launched);
    CHECK("Application.Exit returned from Application.Start", s_exit_called);

    RoRevokeActivationFactories(cookie);
    printf("winui2test: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(fail != 0);
}
