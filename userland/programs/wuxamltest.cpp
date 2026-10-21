/* wuxamltest.exe — the XAML framework under its Windows.UI.Xaml names
 * (roadmap step 32.5, WinUI 2 on the system XAML, first part;
 * docs/design/windows-ui-xaml.md): what WinUI 2 and a UWP app are compiled
 * against.  The framework is Microsoft's WinUI 3 source, whose types are
 * the lifted Microsoft.UI.Xaml ones; its Windows.UI.Xaml faces
 * (tools/winrt/winfaces.py, userland/windows_ui_xaml/src/windows_faces.cpp)
 * give each framework object Windows' interfaces (IUIElement ...
 * IUIElement10, IControl, IContentControl) over its own, and its classes
 * their Windows names.  The framework starts as xamlapptest starts it
 * (Application.Start on the desktop path); in OnLaunched the test is a
 * Windows.UI.Xaml client, as WinUI 2's controls are:
 *   - Windows.UI.Xaml.Controls.Button activated by its Windows name, its
 *     content, width and class name through Windows' interfaces, one
 *     object behind them all;
 *   - a Windows-boxed Visibility (Windows' IReference<Visibility>, as
 *     C++/WinRT boxes it) set through UIElement.VisibilityProperty;
 *   - DependencyProperty.Register with Windows type names, and its value;
 *   - XamlReader.Load through Windows' statics, the page's children through
 *     Windows' IVector<UIElement>;
 *   - Control.IsTabStopProperty (a base class's statics: WinUI 3 moved it
 *     to UIElement);
 *   - Button.Click added and removed with a Windows RoutedEventHandler;
 *   - a control of the app's own composed over Windows.UI.Xaml.Controls.
 *     Control (as WinUI 2 derives its controls), whose Windows
 *     MeasureOverride the framework's layout calls.
 *
 *   wuxamltest        the self-test runs it with no arguments
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
#include <windows.ui.xaml.controls.primitives.h>
#include <windows.ui.xaml.markup.h>

namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define CHECK_HR(what, expr) do { HRESULT h_ = (expr); if (SUCCEEDED(h_)) pass++; else { fail++; \
    printf("FAIL: %s: 0x%08lx (line %d)\n", what, static_cast<unsigned long>(h_), __LINE__); fflush(stdout); } } while (0)

#define XMLNS L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
#define XMLNS_X L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'"

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

template <class T> static HRESULT statics(const wchar_t *cls, T **out)
{
    return RoGetActivationFactory(wrl_wrappers::HStringReference(cls).Get(), __uuidof(T), reinterpret_cast<void **>(out));
}

static IUnknown *identity(IUnknown *p)
{
    IUnknown *u = nullptr;
    if (p && SUCCEEDED(p->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&u))))
        u->Release();
    return u;
}

static HRESULT box_string(const wchar_t *s, IInspectable **out)
{
    ctl::ComPtr<wf::IPropertyValueStatics> pv;
    IFC_RETURN(wf::GetActivationFactory(wrl_wrappers::HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(), pv.ReleaseAndGetAddressOf()));
    return pv->CreateString(wrl_wrappers::HStringReference(s).Get(), out);
}

static bool unboxed_is(IInspectable *v, const wchar_t *s)
{
    ctl::ComPtr<wf::IPropertyValue> pv;
    HSTRING h = nullptr;
    bool r = v && SUCCEEDED(v->QueryInterface(IID_PPV_ARGS(&pv))) && SUCCEEDED(pv->GetString(&h)) && is(h, s);
    WindowsDeleteString(h);
    return r;
}

/* a Visibility boxed as C++/WinRT boxes an enum: Windows' IReference<Visibility>
 * (its IID, pinterface({61c17706-...};enum(Windows.UI.Xaml.Visibility;i4)))
 * with IPropertyValue saying OtherType */
static const GUID IID_IReference_WindowsVisibility = {0xc19bf229, 0x815a, 0x5cbd, {0xa5, 0x06, 0xe3, 0x1f, 0x9b, 0x67, 0xe3, 0x17}};
struct VisibilityBox : IInspectable
{
    LONG refs = 1;
    INT32 value;
    struct PV : IInspectable {
        VisibilityBox *box;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override { return box->QueryInterface(iid, out); }
        ULONG STDMETHODCALLTYPE AddRef() override { return box->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() override { return box->Release(); }
        HRESULT STDMETHODCALLTYPE GetIids(ULONG *n, IID **iids) override { *n = 0; *iids = nullptr; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override { *name = nullptr; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }
        virtual HRESULT STDMETHODCALLTYPE get_Type(INT32 *t) { *t = 20; return S_OK; }      /* OtherType */
    } pv;
    explicit VisibilityBox(INT32 v) : value(v) { pv.box = this; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == IID_IReference_WindowsVisibility)
            *out = static_cast<IInspectable *>(this);
        else if (iid == __uuidof(wf::IPropertyValue))
            *out = &pv;
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&refs); if (!r) delete this; return (ULONG)r; }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *n, IID **iids) override { *n = 0; *iids = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override
    {
        return WindowsCreateString(L"Windows.Foundation.IReference`1<Windows.UI.Xaml.Visibility>", 59, name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE get_Value(INT32 *v) { *v = value; return S_OK; }   /* IReference<T>::get_Value */
};

/* a Windows RoutedEventHandler, as an app's */
struct ClickHandler : wux::IRoutedEventHandler
{
    LONG refs = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(wux::IRoutedEventHandler) || iid == __uuidof(IAgileObject)) {
            *out = static_cast<wux::IRoutedEventHandler *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&refs); if (!r) delete this; return (ULONG)r; }
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable *, wux::IRoutedEventArgs *) override { return S_OK; }
};

/* the app's own control composed over Windows.UI.Xaml.Controls.Control, as
 * WinUI 2 (C++/WinRT) composes its controls: the outer answers its
 * overrides itself, every other interface through the framework's inner
 * object */
static int s_measured;
struct MyControl : wux::IFrameworkElementOverrides
{
    LONG refs = 1;
    IInspectable *inner = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == __uuidof(wux::IFrameworkElementOverrides)) {
            *out = static_cast<wux::IFrameworkElementOverrides *>(this);
            AddRef();
            return S_OK;
        }
        if (!inner) { *out = nullptr; return E_NOINTERFACE; }
        return inner->QueryInterface(iid, out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&refs);
        if (!r) { if (inner) inner->Release(); delete this; }
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *n, IID **iids) override { *n = 0; *iids = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override
    {
        return WindowsCreateString(L"WuxamlTest.MyControl", 20, name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }
    HRESULT STDMETHODCALLTYPE MeasureOverride(wf::Size available, wf::Size *result) override
    {
        s_measured++;
        CHECK("MeasureOverride given the available size", available.Width == 300 && available.Height == 200);
        result->Width = 42;
        result->Height = 24;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ArrangeOverride(wf::Size final, wf::Size *result) override { *result = final; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnApplyTemplate() override { return S_OK; }
};

/* the checks: the framework as a Windows.UI.Xaml client sees it */
static void windows_checks()
{
    /* a Button by its Windows name, through Windows' interfaces */
    ctl::ComPtr<IInspectable> button;
    CHECK_HR("RoActivateInstance(Windows.UI.Xaml.Controls.Button)", RoActivateInstance(
        wrl_wrappers::HStringReference(L"Windows.UI.Xaml.Controls.Button").Get(), button.ReleaseAndGetAddressOf()));
    ctl::ComPtr<wuxc::IButton> ib;
    ctl::ComPtr<wuxc::IContentControl> content;
    ctl::ComPtr<wux::IFrameworkElement> fe;
    ctl::ComPtr<wux::IUIElement> ue;
    ctl::ComPtr<wux::IUIElement5> ue5;
    CHECK("it answers Windows' IButton, IContentControl, IFrameworkElement, IUIElement and IUIElement5",
          SUCCEEDED(as(button.Get(), ib.GetAddressOf())) && SUCCEEDED(as(button.Get(), content.GetAddressOf())) &&
          SUCCEEDED(as(button.Get(), fe.GetAddressOf())) && SUCCEEDED(as(button.Get(), ue.GetAddressOf())) &&
          SUCCEEDED(as(button.Get(), ue5.GetAddressOf())));
    CHECK("one object behind them (one IUnknown)", content && fe && ue && identity(content.Get()) == identity(button.Get()) &&
          identity(fe.Get()) == identity(button.Get()) && identity(ue5.Get()) == identity(button.Get()));
    HSTRING name = nullptr;
    CHECK("its class name, Windows.UI.Xaml.Controls.Button", ib && SUCCEEDED(ib->GetRuntimeClassName(&name)) &&
          is(name, L"Windows.UI.Xaml.Controls.Button"));
    WindowsDeleteString(name);
    if (!content || !fe || !ue)
        return;
    ctl::ComPtr<IInspectable> label, got;
    CHECK_HR("ContentControl.Content set", SUCCEEDED(box_string(L"Press me", label.GetAddressOf())) ?
             content->put_Content(label.Get()) : E_FAIL);
    CHECK("and read back", SUCCEEDED(content->get_Content(got.GetAddressOf())) && unboxed_is(got.Get(), L"Press me"));
    DOUBLE width = 0;
    CHECK("FrameworkElement.Width set and read back", SUCCEEDED(fe->put_Width(120)) && SUCCEEDED(fe->get_Width(&width)) &&
          width == 120);
    wux::Visibility vis = wux::Visibility_Collapsed;
    CHECK("UIElement.Visibility: Visible", SUCCEEDED(ue->get_Visibility(&vis)) && vis == wux::Visibility_Visible);
    wux::ElementTheme theme = wux::ElementTheme_Dark;
    ctl::ComPtr<wux::IFrameworkElement2> fe2;
    CHECK("FrameworkElement.RequestedTheme (IFrameworkElement2): Default", SUCCEEDED(as(button.Get(), fe2.GetAddressOf())) &&
          SUCCEEDED(fe2->get_RequestedTheme(&theme)) && theme == wux::ElementTheme_Default);

    /* a Windows-boxed Visibility through UIElement.VisibilityProperty, on a
     * TextBlock (a Button's visual states, VisualStateManager, are 32.4's) */
    ctl::ComPtr<wux::IUIElementStatics> ues;
    ctl::ComPtr<wux::IDependencyProperty> visibilityProperty;
    CHECK_HR("UIElement's Windows statics, VisibilityProperty", SUCCEEDED(statics(L"Windows.UI.Xaml.UIElement", ues.GetAddressOf())) ?
             ues->get_VisibilityProperty(visibilityProperty.GetAddressOf()) : E_FAIL);
    ctl::ComPtr<IInspectable> textBlock;
    ctl::ComPtr<wux::IDependencyObject> dobj, textObj;
    ctl::ComPtr<wux::IUIElement> textElement;
    as(button.Get(), dobj.GetAddressOf());
    VisibilityBox *box = new VisibilityBox(wux::Visibility_Collapsed);
    CHECK_HR("DependencyObject.SetValue with a Windows-boxed Visibility", visibilityProperty &&
             SUCCEEDED(RoActivateInstance(wrl_wrappers::HStringReference(L"Windows.UI.Xaml.Controls.TextBlock").Get(),
                                          textBlock.GetAddressOf())) &&
             SUCCEEDED(as(textBlock.Get(), textObj.GetAddressOf())) ? textObj->SetValue(visibilityProperty.Get(), box) : E_FAIL);
    box->Release();
    CHECK("UIElement.Visibility: Collapsed", SUCCEEDED(as(textBlock.Get(), textElement.GetAddressOf())) &&
          SUCCEEDED(textElement->get_Visibility(&vis)) && vis == wux::Visibility_Collapsed);

    /* DependencyProperty.Register with Windows type names */
    ctl::ComPtr<wux::IDependencyPropertyStatics> dps;
    ctl::ComPtr<wux::IDependencyProperty> answer;
    wrl_wrappers::HStringReference int32Name(L"Int32"), buttonName(L"Windows.UI.Xaml.Controls.Button");
    wux::Interop::TypeName valueType = {int32Name.Get(), wux::Interop::TypeKind_Primitive};
    wux::Interop::TypeName ownerType = {buttonName.Get(), wux::Interop::TypeKind_Metadata};
    CHECK_HR("DependencyProperty.Register (Int32, owner Windows.UI.Xaml.Controls.Button)",
             SUCCEEDED(statics(L"Windows.UI.Xaml.DependencyProperty", dps.GetAddressOf())) ?
             dps->Register(wrl_wrappers::HStringReference(L"Answer").Get(), valueType, ownerType, nullptr,
                           answer.GetAddressOf()) : E_FAIL);
    ctl::ComPtr<wf::IPropertyValueStatics> pvs;
    ctl::ComPtr<IInspectable> fortyTwo, read;
    ctl::ComPtr<wf::IPropertyValue> readValue;
    INT32 v = 0;
    CHECK("its value set and read back", answer && dobj &&
          SUCCEEDED(wf::GetActivationFactory(wrl_wrappers::HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(), pvs.ReleaseAndGetAddressOf())) &&
          SUCCEEDED(pvs->CreateInt32(42, fortyTwo.GetAddressOf())) && SUCCEEDED(dobj->SetValue(answer.Get(), fortyTwo.Get())) &&
          SUCCEEDED(dobj->GetValue(answer.Get(), read.GetAddressOf())) && read && SUCCEEDED(read.As(&readValue)) &&
          SUCCEEDED(readValue->GetInt32(&v)) && v == 42);

    /* XamlReader.Load through Windows' statics */
    ctl::ComPtr<wux::Markup::IXamlReaderStatics> reader;
    ctl::ComPtr<IInspectable> page;
    CHECK_HR("Windows.UI.Xaml.Markup.XamlReader.Load", SUCCEEDED(statics(L"Windows.UI.Xaml.Markup.XamlReader", reader.GetAddressOf())) ?
             reader->Load(wrl_wrappers::HStringReference(L"<Grid " XMLNS XMLNS_X L"><TextBlock Text='Hello, Windows'/>"
                                                         L"<Button Content='OK'/></Grid>").Get(), page.GetAddressOf()) : E_FAIL);
    ctl::ComPtr<wuxc::IPanel> panel;
    ctl::ComPtr<wf::Collections::IVector<wux::UIElement *>> children;
    UINT32 n = 0;
    CHECK("a Grid (Windows' IPanel) with two children (Windows' IVector<UIElement>)", page &&
          SUCCEEDED(as(page.Get(), panel.GetAddressOf())) && SUCCEEDED(panel->get_Children(children.GetAddressOf())) &&
          SUCCEEDED(children->get_Size(&n)) && n == 2);
    ctl::ComPtr<wux::IUIElement> first, second;
    ctl::ComPtr<wuxc::ITextBlock> tb;
    ctl::ComPtr<wuxc::IContentControl> ok;
    ctl::ComPtr<IInspectable> okContent;
    HSTRING text = nullptr;
    CHECK("the first a TextBlock (Windows' ITextBlock) with its text", children && SUCCEEDED(children->GetAt(0, first.GetAddressOf())) &&
          SUCCEEDED(as(first.Get(), tb.GetAddressOf())) && SUCCEEDED(tb->get_Text(&text)) && is(text, L"Hello, Windows"));
    WindowsDeleteString(text);
    CHECK("the second a Button with its content", children && SUCCEEDED(children->GetAt(1, second.GetAddressOf())) &&
          SUCCEEDED(as(second.Get(), ok.GetAddressOf())) && SUCCEEDED(ok->get_Content(okContent.GetAddressOf())) &&
          unboxed_is(okContent.Get(), L"OK"));
    UINT32 at = 9;
    boolean found = false;
    CHECK("IndexOf finds it", children && second && SUCCEEDED(children->IndexOf(second.Get(), &at, &found)) && found && at == 1);

    /* a base class's statics: Control.IsTabStopProperty (UIElement's in WinUI 3) */
    ctl::ComPtr<wuxc::IControlStatics> cs;
    ctl::ComPtr<wux::IDependencyProperty> tabStop;
    CHECK("Control.IsTabStopProperty", SUCCEEDED(statics(L"Windows.UI.Xaml.Controls.Control", cs.GetAddressOf())) &&
          SUCCEEDED(cs->get_IsTabStopProperty(tabStop.GetAddressOf())) && tabStop);

    /* Button.Click with a Windows RoutedEventHandler */
    ctl::ComPtr<wuxc::Primitives::IButtonBase> bb;
    EventRegistrationToken token = {};
    ClickHandler *handler = new ClickHandler();
    CHECK_HR("ButtonBase.Click added", SUCCEEDED(as(button.Get(), bb.GetAddressOf())) ? bb->add_Click(handler, &token) : E_FAIL);
    CHECK_HR("and removed", bb ? bb->remove_Click(token) : E_FAIL);
    handler->Release();

    /* an app's control composed over Windows.UI.Xaml.Controls.Control */
    ctl::ComPtr<wuxc::IControlFactory> cf;
    MyControl *mine = new MyControl();
    ctl::ComPtr<wuxc::IControl> instance;
    CHECK_HR("Control's Windows factory composes the app's control", SUCCEEDED(statics(L"Windows.UI.Xaml.Controls.Control", cf.GetAddressOf())) ?
             cf->CreateInstance(static_cast<wux::IFrameworkElementOverrides *>(mine), &mine->inner, instance.GetAddressOf()) : E_FAIL);
    instance.Reset();
    ctl::ComPtr<wux::IUIElement> mineElement;
    wf::Size desired = {};
    CHECK("its Windows IUIElement through the framework's inner object",
          SUCCEEDED(mine->QueryInterface(__uuidof(wux::IUIElement), reinterpret_cast<void **>(mineElement.GetAddressOf()))));
    CHECK_HR("UIElement.Measure", mineElement ? mineElement->Measure(wf::Size{300, 200}) : E_FAIL);
    CHECK("calls the app's Windows MeasureOverride", s_measured == 1);
    CHECK("whose size is its DesiredSize", mineElement && SUCCEEDED(mineElement->get_DesiredSize(&desired)) &&
          desired.Width == 42 && desired.Height == 24);
    mineElement.Reset();
    mine->Release();

    /* a Windows method the framework has no counterpart for says so */
    ctl::ComPtr<wux::IUIElement9> ue9;
    CHECK("UIElement.StopAnimation (Windows.UI.Composition): E_NOTIMPL",
          SUCCEEDED(as(button.Get(), ue9.GetAddressOf())) && ue9->StopAnimation(nullptr) == E_NOTIMPL);
}

/* the framework started as xamlapptest starts it */
static bool s_launched, s_exit_called;
static DWORD s_main_thread;

class App : public xaml::IApplicationOverrides
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
        return WindowsCreateString(L"WuxamlTest.App", 14, name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }

    HRESULT STDMETHODCALLTYPE OnLaunched(xaml::ILaunchActivatedEventArgs *) override
    {
        s_launched = true;
        windows_checks();
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
    printf("FAIL: Application.Start did not return\nwuxamltest: %d passed, %d failed (%s)\n", pass, fail + 1,
           sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(1);
}

/* the classes this program serves, as Windows.UI.Xaml.dll serves them: the
 * framework's WindowsXamlManager (which Application.Start activates by its
 * name) and the Windows.UI.Xaml classes the checks use, by their Windows
 * names (nova_xaml_windows_factory) */
static const wchar_t *const s_windows_classes[] = {
    L"Windows.UI.Xaml.Controls.Button", L"Windows.UI.Xaml.Controls.TextBlock", L"Windows.UI.Xaml.UIElement",
    L"Windows.UI.Xaml.DependencyProperty",
    L"Windows.UI.Xaml.Markup.XamlReader", L"Windows.UI.Xaml.Controls.Control"};

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
    s_main_thread = GetCurrentThreadId();
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

    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationStatics> appStatics;
    CHECK("Application's statics", appFactory && SUCCEEDED(appFactory.As(&appStatics)));
    if (!appStatics) return 1;
    CHECK_HR("Application.Start", appStatics->Start(wrl::Callback<xaml::IApplicationInitializationCallback>(on_start).Get()));
    CHECK("OnLaunched ran", s_launched);
    CHECK("Application.Exit returned from Application.Start", s_exit_called);

    RoRevokeActivationFactories(cookie);
    printf("wuxamltest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(fail != 0);
}
