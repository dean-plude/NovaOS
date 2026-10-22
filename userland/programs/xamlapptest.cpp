/* xamlapptest.exe — Microsoft's XAML framework started as a desktop app
 * starts it (roadmap step 32.3, the end of it; docs/design/windows-ui-xaml.md):
 * Application.Start (FrameworkApplicationFactory::StartImpl, built from
 * dxaml/lib) on the program's main thread takes the desktop path
 * (FrameworkApplication::StartDesktop): a DispatcherQueue on the thread,
 * the framework's core for islands, the app's initialization callback, in
 * which the app makes its Application (a class of its own composed over
 * the framework's, as a C++/WinRT or C# app's App is), then the thread's
 * WindowsXamlManager, which starts the application on the thread and raises
 * its OnLaunched.  OnLaunched loads a page with XamlReader.Load (a Grid
 * holding a TextBlock) on the app's thread and queues work on the thread's
 * DispatcherQueue; the work runs from the desktop message loop and calls
 * Application.Exit, which ends the loop and returns from Application.Start.
 * There, too, the framework's theme resources and control styles
 * (Microsoft.UI.Xaml.Resources.19H1.dll, from generic.xaml: step 32.4) are
 * found as the framework finds them (DXamlCore::GetResourceBytes), and the
 * core's resource manager, over the platform's resource provider, is made.
 * A Window (DesktopWindowImpl, on the Windows App SDK's content islands)
 * is step 32.4.
 *
 *   xamlapptest        the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <new>
#include <corep.h>
#include <CStaticLock.h>
#include <SystemThemingInterop.h>
#include <DXamlInstanceStorage.h>
#include <XamlReader.g.h>
#include <WindowsXamlManager.g.h>
#include <Grid.g.h>
#include <TextBlock.g.h>
#include <DXamlCore.h>
#include <ParserAPI.h>
#include <roapi.h>
#include <FrameworkApplication_Partial.h>
#include <microsoft.ui.dispatching.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define CHECK_HR(what, expr) do { HRESULT h_ = (expr); if (SUCCEEDED(h_)) pass++; else { fail++; \
    printf("FAIL: %s: 0x%08lx (line %d)\n", what, static_cast<unsigned long>(h_), __LINE__); fflush(stdout); } } while (0)

#define XMLNS L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
#define XMLNS_X L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'"

static bool is(HSTRING h, const wchar_t *s)
{
    return wcscmp(h ? WindowsGetStringRawBuffer(h, nullptr) : L"", s) == 0;
}

/* what the app saw, checked after Application.Start returns */
static bool s_callback, s_launched, s_queued_ran, s_exit_called;
static DWORD s_main_thread;

/* the app's Application: its own object composed over the framework's (the
 * framework's is the inner object; this, the outer, answers
 * IApplicationOverrides itself and every other interface through the
 * inner), as a projection makes an app's App class */
class App : public xaml::IApplicationOverrides
{
public:
    IInspectable *m_inner = nullptr;   /* (the framework's, which this owns) */
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
        return WindowsCreateString(L"XamlAppTest.App", 15, name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }

    HRESULT STDMETHODCALLTYPE OnLaunched(xaml::ILaunchActivatedEventArgs *args) override;
};
static App *s_app;

/* OnLaunched: as an app's: a window, its content, activated; then work on
 * the thread's DispatcherQueue, which the message loop runs, ends the app */
HRESULT App::OnLaunched(xaml::ILaunchActivatedEventArgs *args)
{
    s_launched = true;
    CHECK("OnLaunched on the thread Application.Start was called on", GetCurrentThreadId() == s_main_thread);
    HSTRING arguments = nullptr;
    CHECK("its arguments: the (empty) command line's", args && SUCCEEDED(args->get_Arguments(&arguments)) && is(arguments, L""));
    WindowsDeleteString(arguments);

    /* Application.Current is the app's own object */
    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationStatics> appStatics;
    ctl::ComPtr<xaml::IApplication> current;
    CHECK("Application.Current", appFactory && SUCCEEDED(appFactory.As(&appStatics)) &&
          SUCCEEDED(appStatics->get_Current(&current)) && current);
    ctl::ComPtr<IUnknown> currentUnknown;
    if (current) current.As(&currentUnknown);
    CHECK("is the app's", currentUnknown.Get() == static_cast<IUnknown *>(static_cast<xaml::IApplicationOverrides *>(this)));

    /* the framework runs on the thread: a page parsed there, a Grid holding
     * a TextBlock, handed back as their framework objects */
    ctl::ComPtr<DirectUI::XamlReaderFactory> reader;
    ctl::ComObject<DirectUI::XamlReaderFactory>::CreateInstance(reader.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml_markup::IXamlReaderStatics> readerStatics;
    if (reader) reader.As(&readerStatics);
    ctl::ComPtr<IInspectable> page;
    CHECK_HR("XamlReader.Load of a page", readerStatics ? readerStatics->Load(wrl_wrappers::HStringReference(
        L"<Grid " XMLNS XMLNS_X L"><TextBlock x:Name='title' Text='Hello, Starlight'/></Grid>").Get(),
        page.ReleaseAndGetAddressOf()) : E_FAIL);
    ctl::ComPtr<xaml_controls::IPanel> panel;
    ctl::ComPtr<wfc::IVector<xaml::UIElement *>> children;
    UINT32 n = 0;
    CHECK("a Grid with one child", page && SUCCEEDED(page.As(&panel)) && SUCCEEDED(panel->get_Children(&children)) &&
          SUCCEEDED(children->get_Size(&n)) && n == 1);
    ctl::ComPtr<xaml::IUIElement> first;
    ctl::ComPtr<xaml_controls::ITextBlock> tb;
    HSTRING text = nullptr;
    CHECK("a TextBlock with its text", children && SUCCEEDED(children->GetAt(0, &first)) && SUCCEEDED(first.As(&tb)) &&
          SUCCEEDED(tb->get_Text(&text)) && is(text, L"Hello, Starlight"));
    WindowsDeleteString(text);

    /* the theme resources and the control styles, as the framework finds
     * them: the resource DLL's XAML (text: NovaOS builds it without the
     * XAML compiler's XBF) */
    DirectUI::DXamlCore *core = DirectUI::DXamlCore::GetCurrent();
    CHECK("the thread's framework core", core != nullptr);
    Parser::XamlBuffer themes, styles;
    if (core) {
        CHECK_HR("the theme resources", core->GetResourceBytes(L"ThemeResources.xbf", &themes));
        CHECK_HR("the control styles", core->GetResourceBytes(L"Styles.xbf", &styles));
    }
    auto has = [](const Parser::XamlBuffer &b, const char *what) {
        size_t n = strlen(what);
        for (size_t i = 0; b.m_buffer && i + n <= b.m_count; i++)
            if (!memcmp(b.m_buffer + i, what, n)) return true;
        return false;
    };
    CHECK("are a resource dictionary's XAML, as text", themes.m_bufferType == Parser::XamlBufferType::Text &&
          themes.m_count > 100000 && !memcmp(themes.m_buffer, "<ResourceDictionary", 19));
    CHECK("with the Light, Dark (Default) and HighContrast theme dictionaries", has(themes, "x:Key=\"Light\"") &&
          has(themes, "x:Key=\"Default\"") && has(themes, "x:Key=\"HighContrast\""));
    CHECK("the styles hold Button's", styles.m_bufferType == Parser::XamlBufferType::Text &&
          has(styles, "<Style TargetType=\"Button\""));
    CHECK("but none of the controls libraries' types (Pivot, TextCommandBarFlyout)", !has(styles, "\"Pivot\"") &&
          !has(themes, "<TextCommandBarFlyout"));
    xref_ptr<IPALResourceManager> resources;
    CHECK_HR("the core's resource manager", core ? core->GetHandle()->GetResourceManager(resources.ReleaseAndGetAddressOf()) : E_FAIL);
    CHECK("is made", resources != nullptr);

    /* then work the message loop runs, which ends the app */
    ctl::ComPtr<msy::IDispatcherQueueStatics> queueStatics;
    ctl::ComPtr<msy::IDispatcherQueue> queue;
    CHECK("the thread's DispatcherQueue", SUCCEEDED(wf::GetActivationFactory(
        wrl_wrappers::HStringReference(RuntimeClass_Microsoft_UI_Dispatching_DispatcherQueue).Get(), queueStatics.ReleaseAndGetAddressOf())) &&
        SUCCEEDED(queueStatics->GetForCurrentThread(&queue)) && queue);
    if (!queue) return S_OK;
    boolean queued = false;
    CHECK_HR("work queued on it", queue->TryEnqueue(wrl::Callback<msy::IDispatcherQueueHandler>([]() -> HRESULT {
        s_queued_ran = true;
        CHECK("the queued work runs on the app's thread", GetCurrentThreadId() == s_main_thread);
        ctl::ComPtr<xaml::IApplication> app;
        CHECK("and finds the app", s_app && SUCCEEDED(s_app->QueryInterface(__uuidof(xaml::IApplication),
                                                                        reinterpret_cast<void **>(app.GetAddressOf()))));
        CHECK_HR("Application.Exit", app ? app->Exit() : E_FAIL);
        s_exit_called = true;
        return S_OK;
    }).Get(), &queued));
    CHECK("queued", queued);
    return S_OK;
}

/* Application.Start's callback: the app makes its Application */
static HRESULT on_start(xaml::IApplicationInitializationCallbackParams *params)
{
    s_callback = true;
    CHECK("the initialization callback, given its parameters", params != nullptr);
    CHECK("on the thread Application.Start was called on", GetCurrentThreadId() == s_main_thread);
    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationFactory> factory;
    if (appFactory) appFactory.As(&factory);
    s_app = new App();
    ctl::ComPtr<xaml::IApplication> instance;
    CHECK_HR("the app's Application, composed over the framework's", factory ? factory->CreateInstance(
        static_cast<xaml::IApplicationOverrides *>(s_app), &s_app->m_inner,
        instance.ReleaseAndGetAddressOf()) : E_FAIL);
    return S_OK;
}

/* (a framework that never returns from Application.Start fails the test) */
static DWORD WINAPI watchdog(void *)
{
    Sleep(90000);
    printf("FAIL: Application.Start did not return\nxamlapptest: %d passed, %d failed (%s)\n", pass, fail + 1,
           sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(1);
}

/* The framework's own classes Application.Start activates by name (the
 * thread's WindowsXamlManager), served by this program as Windows.UI.Xaml.dll
 * serves them to a packaged app: registered for the process
 * (RoRegisterActivationFactories), as an executable serves its classes */
static HRESULT WINAPI make_factory(HSTRING cls, IActivationFactory **out)
{
    *out = nullptr;
    if (!is(cls, RuntimeClass_Microsoft_UI_Xaml_Hosting_WindowsXamlManager)) return REGDB_E_CLASSNOTREG;
    ctl::ComPtr<DirectUI::WindowsXamlManagerFactory> f;
    IFC_RETURN(ctl::ComObject<DirectUI::WindowsXamlManagerFactory>::CreateInstance(f.ReleaseAndGetAddressOf()));
    return f.CopyTo(out);
}

int main()
{
    DirectUI::StaticLockGlobalInit();
    DXamlInstanceStorage::Initialize();     /* (as the framework's DllMain does: its per-thread core's slot, */
    DirectUI::FrameworkApplication::GlobalInit();   /* the application's lock, */
    IPlatformServices *pal = nullptr;                   /* and the platform layer's services) */
    CHECK_HR("ObtainPlatformServices", ObtainPlatformServices(&pal));
    SystemThemingInterop::OverrideSystemTheme(Theming::Theme::Dark);
    s_main_thread = GetCurrentThreadId();
    wrl_wrappers::HStringReference manager(RuntimeClass_Microsoft_UI_Xaml_Hosting_WindowsXamlManager);
    HSTRING classes[] = { manager.Get() };
    PFNGETACTIVATIONFACTORY makers[] = { make_factory };
    RO_REGISTRATION_COOKIE cookie = nullptr;
    CHECK_HR("RoRegisterActivationFactories (the framework's classes)", RoRegisterActivationFactories(classes, makers, 1, &cookie));
    CloseHandle(CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr));

    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationStatics> statics;
    CHECK("Application's statics", appFactory && SUCCEEDED(appFactory.As(&statics)));
    if (!statics) return 1;
    ctl::ComPtr<xaml::IApplication> none;
    CHECK("no Application.Current before Application.Start", FAILED(statics->get_Current(&none)) || !none);

    CHECK_HR("Application.Start", statics->Start(wrl::Callback<xaml::IApplicationInitializationCallback>(on_start).Get()));
    CHECK("the initialization callback ran", s_callback);
    CHECK("OnLaunched ran", s_launched);
    CHECK("the message loop ran the queued work", s_queued_ran);
    CHECK("Application.Exit returned from Application.Start", s_exit_called);

    RoRevokeActivationFactories(cookie);
    printf("xamlapptest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(fail != 0);
}
