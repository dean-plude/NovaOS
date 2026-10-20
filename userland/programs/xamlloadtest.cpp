/* xamlloadtest.exe — Microsoft's XAML resource dictionaries and object
 * writer built for NovaOS (roadmap step 32.3, parts 7 to 10;
 * docs/design/windows-ui-xaml.md): CResourceDictionary from
 * dxaml/xcp/core/core/elements (Resources.cpp: keyed resources and their
 * lookup) and the parser's object writer runtime from dxaml/xcp/core/Parser
 * (Parser.Runtime: XamlNativeRuntime creating the core's objects and adding
 * them to collections and dictionaries), so XAML text is loaded into a live
 * object tree: a ResourceDictionary of strings, numbers, booleans and
 * colours, read by XmlLite, parsed into the node stream and written into
 * objects; and (part 8) a dictionary's merged dictionaries and theme
 * dictionaries, set through field-backed properties whose offsets come
 * from the core's property runtime data
 * (userland/windows_ui_xaml/src/property_data.cpp), searched by lookup;
 * and (part 9) all of it on the XAML core's own core services
 * (CCoreServices, core/dll/xcpcore.cpp), which load the XAML as
 * XamlReader.Load has them load it (CCoreServices::ParseXaml); and
 * (part 10) the core made as the framework makes it, by
 * CCoreServices::Create on the platform layer's services; and (part 11)
 * built with the framework above the core (dxaml/lib, what becomes
 * Windows.UI.Xaml.dll): its per-thread core, DXamlCore, and the public
 * XamlReader's activation factory, which on a thread without XAML refuses
 * to load, as Windows' does; and (part 12) the framework's error helper
 * (ErrorHelper: a null markup string is an invalid argument) and the
 * control layer DXamlCore starts on: the XAML dispatcher, CXcpDispatcher,
 * made on the thread's DispatcherQueue (CoreMessaging.dll) and ticking from
 * its timer, and the Jupiter control, CJupiterControl, which makes the
 * dispatcher and the browser host (CXcpBrowserHost, with the thread's core
 * services) and stops where the rendering, not built yet, begins; and
 * (the 32.3 push) what DXamlCore::InitializeInstance makes after the
 * control: the XAML dispatcher (DispatcherImpl, which knows its thread and
 * refuses work without a core) and the default control styles' cache
 * (DefaultStyles); the Window and the framework's peer base class,
 * DirectUI::DependencyObject, build and link here, but a Window is made
 * only on a thread whose DXamlCore is initialized.
 *
 * Until the core's generated type table builds, the table here activates
 * the handful of types the cases use (below).
 *
 *   xamlloadtest       the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <new>
#include <corep.h>
#include <CDependencyObject.h>
#include <Resources.h>
#include <CString.h>
#include <Double.h>
#include <primitives.h>
#include <CColor.h>
#include <TypeTableStructsPrivate.h>
#include <CStaticLock.h>
#include <ParserAPI.h>
#include <SystemThemingInterop.h>
#include <XamlReader.g.h>
#include <JupiterControl.h>
#include <XcpWindow.h>
#include <microsoft.ui.dispatching.h>
#include <Dispatcher.h>
#include <Callback.h>
#include <DefaultStyles.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define CHECK_HR(what, expr) do { HRESULT h_ = (expr); if (SUCCEEDED(h_)) pass++; else { fail++; \
    printf("FAIL: %s: 0x%08lx (line %d)\n", what, static_cast<unsigned long>(h_), __LINE__); fflush(stdout); } } while (0)

/* the types the object writer may create here, by the core's own
 * constructors (the XAML core's generated table, core/metadata
 * DynamicMetadata.g.cpp, has every type's) */
static HRESULT create_dictionary(CDependencyObject **out, CREATEPARAMETERS *create)
{
    return CResourceDictionary::Create(out, create);
}

#define ACT(type, fn) [static_cast<int>(KnownTypeIndex::type)] = { fn, nullptr }
extern const MetaDataTypeActivation c_aTypeActivations[KnownTypeCount] = {
    ACT(ResourceDictionary, &create_dictionary),
    ACT(ResourceDictionaryCollection, &CResourceDictionaryCollection::Create),
    ACT(String, &CString::Create),
    ACT(Double, &CDouble::Create),
    ACT(Int32, &CInt32::Create),
    ACT(Boolean, &CBoolean::Create),
    ACT(Color, &CColor::Create),
};

static std::string narrow(const xstring_ptr_view &s)
{
    std::string out;
    for (XUINT32 i = 0; i < s.GetCount(); i++) out += (char)s.GetBuffer()[i];
    return out;
}

/* the core: the XAML core's own core services (CCoreServices,
 * core/dll/xcpcore.cpp, built since part 9), made as the framework makes
 * them (DXamlCore::InitializeInstance): the platform layer's services
 * (CWindowsServices, plat/win/browserdesktop/xcpwin.cpp, built since
 * part 10: its queues, work items and security token), handed to
 * ObtainCoreServices, which has CCoreServices::Create set up the rest (the
 * theming over the system's, the theme cache, the media queue, the text
 * core and render target bitmaps); the system theme is fixed at Dark
 * through the theming's own test hook, so the cases do not depend on the
 * machine's.  The objects the core makes name it, and the parser reads it
 * through its IParserCoreServices; XamlReader.Load loads XAML as load()
 * does, through CCoreServices::ParseXaml. */
static CCoreServices *make_core()
{
    IPlatformServices *pal = nullptr;
    CCoreServices *c = nullptr;
    SystemThemingInterop::OverrideSystemTheme(Theming::Theme::Dark);
    CHECK_HR("ObtainPlatformServices", ObtainPlatformServices(&pal));
    CHECK_HR("ObtainCoreServices (CCoreServices::Create)", pal ? ObtainCoreServices(pal, &c) : E_POINTER);
    CHECK("the core services", c != nullptr);
    return c;
}

static CDependencyObject *load(const wchar_t *xaml, HRESULT *hr_out = nullptr);

/* the core's own theme resources, which the framework loads on first
 * lookup (FrameworkCallbacks_LoadThemeResources, from its built-in theme
 * dictionaries) and a dictionary's lookup falls back on; until the
 * framework builds, a small dictionary of this test's, loaded through the
 * core and given it as the framework gives it its own */
static const wchar_t c_themeResources[] =
    L"<ResourceDictionary xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
    L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>"
    L"<x:String x:Key='FromTheme'>theme</x:String></ResourceDictionary>";

static CCoreServices *core()
{
    static CCoreServices *c;
    if (!c) {
        c = make_core();
        if (!c) return nullptr;
        CDependencyObject *theme = load(c_themeResources);
        c->SetThemeResources(do_pointer_cast<CResourceDictionary>(theme));
        ReleaseInterface(theme);
    }
    return c;
}

/* XAML text @xaml loaded into objects by the core (CCoreServices::ParseXaml:
 * read by XmlLite, parsed into the node stream, written into objects): the
 * root (referenced), or null with the failing HRESULT in *@hr */
static CDependencyObject *load(const wchar_t *xaml, HRESULT *hr_out)
{
    Parser::XamlBuffer buffer;
    buffer.m_count = (uint32_t)(wcslen(xaml) * sizeof(wchar_t));
    buffer.m_buffer = reinterpret_cast<const BYTE *>(xaml);
    CDependencyObject *root = nullptr;
    HRESULT hr = !core() ? E_FAIL : core()->ParseXaml(buffer, /* bForceUtf16 */ true, /* bCreatePermanentNamescope */ false,
                                   /* bRequiresDefaultNamespace */ false, &root);
    if (SUCCEEDED(hr) && !root) hr = E_POINTER;
    if (hr_out) *hr_out = hr;
    return root;
}

static std::string string_of(CDependencyObject *o)
{
    CString *s = do_pointer_cast<CString>(o);
    return s ? narrow(s->m_strString) : "(not a string)";
}

static xstring_ptr str(const wchar_t *s)
{
    xstring_ptr out;
    xstring_ptr::CloneBuffer(s, (XUINT32)wcslen(s), &out);
    return out;
}

static CDependencyObject *lookup(CResourceDictionary *d, const wchar_t *key)
{
    CDependencyObject *o = nullptr;
    if (FAILED(d->GetKeyNoRef(str(key), &o))) return nullptr;
    return o;
}

static CDependencyObject *make_string(const wchar_t *text)
{
    CDependencyObject *o = nullptr;
    CREATEPARAMETERS cp(core(), str(text));
    return SUCCEEDED(CString::Create(&o, &cp)) ? o : nullptr;
}

/* CResourceDictionary on its own: keyed values added, found, refused
 * twice, removed */
static void test_dictionary()
{
    CDependencyObject *o = nullptr;
    CREATEPARAMETERS cp(core());
    CHECK("CResourceDictionary::Create", SUCCEEDED(CResourceDictionary::Create(&o, &cp)) && o);
    CResourceDictionary *d = do_pointer_cast<CResourceDictionary>(o);
    CHECK("is a CResourceDictionary", d != nullptr && o->GetTypeIndex() == KnownTypeIndex::ResourceDictionary);
    if (!d) return;
    CValue v;
    CDependencyObject *a = make_string(L"apple"), *b = make_string(L"banana");
    v.WrapObjectNoRef(a);
    CHECK_HR("Add a", d->Add(str(L"a"), &v, nullptr, false));
    v.WrapObjectNoRef(b);
    CHECK_HR("Add b", d->Add(str(L"b"), &v, nullptr, false));
    CHECK("two items", d->GetCount() == 2);
    CHECK("HasKey a", d->HasKey(str(L"a"), false));
    CHECK("lookup a", string_of(lookup(d, L"a")) == "apple");
    CHECK("lookup b", string_of(lookup(d, L"b")) == "banana");
    CHECK("missing key", lookup(d, L"c") == nullptr);
    CHECK("the core's theme resources", string_of(lookup(d, L"FromTheme")) == "theme");
    v.WrapObjectNoRef(a);
    CHECK("a duplicate key is refused", FAILED(d->Add(str(L"a"), &v, nullptr, false)));
    CHECK("Remove b", SUCCEEDED(d->Remove(str(L"b"))) && d->GetCount() == 1 && lookup(d, L"b") == nullptr);
    ReleaseInterface(a);
    ReleaseInterface(b);
    d->Release();
}

#define XMLNS L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
#define XMLNS_X L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'"

/* XAML loaded into a ResourceDictionary through the object writer */
static void test_load()
{
    HRESULT hr = S_OK;
    CDependencyObject *root = load(L"<x:String " XMLNS_X L">Hello</x:String>", &hr);
    CHECK_HR("load an x:String", root ? S_OK : hr);
    CHECK("the x:String", string_of(root) == "Hello");
    if (root) root->Release();
    root = load(L"<ResourceDictionary " XMLNS XMLNS_X L"/>", &hr);
    CHECK_HR("load an empty ResourceDictionary", root ? S_OK : hr);
    if (root) root->Release();
    root = load(
        L"<ResourceDictionary " XMLNS XMLNS_X L">"
        L"<x:String x:Key='Greeting'>Hello, Kepler</x:String>"
        L"<x:Double x:Key='Size'>14.5</x:Double>"
        L"<x:Int32 x:Key='Count'>42</x:Int32>"
        L"<x:Boolean x:Key='On'>True</x:Boolean>"
        L"<Color x:Key='Accent'>#FF0078D7</Color>"
        L"</ResourceDictionary>", &hr);
    CHECK("load a ResourceDictionary", root != nullptr);
    if (!root) {
        printf("  hr %08lx\n", (unsigned long)hr);
        return;
    }
    CResourceDictionary *d = do_pointer_cast<CResourceDictionary>(root);
    CHECK("the root is a CResourceDictionary", d != nullptr);
    if (d) {
        CHECK("five resources", d->GetCount() == 5);
        CHECK("x:String", string_of(lookup(d, L"Greeting")) == "Hello, Kepler");
        CDouble *size = do_pointer_cast<CDouble>(lookup(d, L"Size"));
        CHECK("x:Double", size && size->m_eValue == 14.5f);
        CInt32 *count = do_pointer_cast<CInt32>(lookup(d, L"Count"));
        CHECK("x:Int32", count && count->m_iValue == 42);
        /* (a boolean is the core's CEnumerated of the Boolean type) */
        CEnumerated *on = do_pointer_cast<CEnumerated>(lookup(d, L"On"));
        CHECK("x:Boolean", on && on->GetEnumTypeIndex() == KnownTypeIndex::Boolean && on->m_nValue == 1);
        CColor *accent = do_pointer_cast<CColor>(lookup(d, L"Accent"));
        CHECK("Color", accent && accent->m_rgb == 0xFF0078D7);
    }
    root->Release();

    /* merged dictionaries and theme dictionaries */
    root = load(
        L"<ResourceDictionary " XMLNS XMLNS_X L">"
        L"<ResourceDictionary.MergedDictionaries>"
        L"<ResourceDictionary><x:String x:Key='Shared'>first</x:String><x:String x:Key='Deep'>deep</x:String></ResourceDictionary>"
        L"<ResourceDictionary><x:String x:Key='Shared'>second</x:String></ResourceDictionary>"
        L"</ResourceDictionary.MergedDictionaries>"
        L"<ResourceDictionary.ThemeDictionaries>"
        L"<ResourceDictionary x:Key='Light'><x:String x:Key='Themed'>light</x:String></ResourceDictionary>"
        L"<ResourceDictionary x:Key='Dark'><x:String x:Key='Themed'>dark</x:String></ResourceDictionary>"
        L"</ResourceDictionary.ThemeDictionaries>"
        L"<x:String x:Key='Own'>own</x:String>"
        L"</ResourceDictionary>", &hr);
    CHECK("load merged and theme dictionaries", root != nullptr);
    if (!root) {
        printf("  hr %08lx\n", (unsigned long)hr);
        return;
    }
    d = do_pointer_cast<CResourceDictionary>(root);
    if (d) {
        CHECK("own resource", string_of(lookup(d, L"Own")) == "own");
        CHECK("two merged dictionaries", d->GetMergedDictionaries() && d->GetMergedDictionaries()->GetCount() == 2);
        CHECK("the last merged dictionary wins", string_of(lookup(d, L"Shared")) == "second");
        CHECK("found in the first merged dictionary", string_of(lookup(d, L"Deep")) == "deep");
        CResourceDictionary *themes = d->GetThemeDictionaries();
        CHECK("theme dictionaries", themes && themes->IsThemeDictionaries() && themes->GetCount() == 2);
        CResourceDictionary *dark = themes ? do_pointer_cast<CResourceDictionary>(lookup(themes, L"Dark")) : nullptr;
        CHECK("the Dark theme's dictionary", dark && string_of(lookup(dark, L"Themed")) == "dark");
    }
    root->Release();

    /* malformed XAML fails to load, it does not crash */
    root = load(L"<ResourceDictionary " XMLNS XMLNS_X L"><x:String x:Key='a'>unclosed</x:String>", &hr);
    CHECK("unclosed XAML is an error", root == nullptr && FAILED(hr));
    if (root) root->Release();
}

/* the framework (dxaml/lib) on a thread it has not started on: no
 * DXamlCore, and XamlReader.Load, through its activation factory (the
 * public IXamlReaderStatics), refuses with RPC_E_WRONG_THREAD before it
 * parses anything, as Windows.UI.Xaml does on a thread without XAML (the
 * core's own loading, above, does not need the framework) */
static void test_framework()
{
    CHECK("no framework core on this thread", DirectUI::DXamlCore::GetCurrent() == nullptr);
    CHECK("the framework is not initialized here", !DirectUI::DXamlServices::IsDXamlCoreInitialized());
    ctl::ComPtr<DirectUI::XamlReaderFactory> reader;
    CHECK_HR("XamlReader's activation factory", ctl::ComObject<DirectUI::XamlReaderFactory>::CreateInstance(
        reader.ReleaseAndGetAddressOf()));
    if (!reader) return;
    ctl::ComPtr<xaml_markup::IXamlReaderStatics> statics;
    CHECK_HR("IXamlReaderStatics", reader.As(&statics));
    if (!statics) return;
    IInspectable *obj = reinterpret_cast<IInspectable *>(1);
    HRESULT hr = statics->Load(wrl_wrappers::HStringReference(L"<x:String " XMLNS_X L">Hello</x:String>").Get(), &obj);
    CHECK("XamlReader.Load without XAML on the thread: RPC_E_WRONG_THREAD", hr == RPC_E_WRONG_THREAD && obj == nullptr);
}

/* A host site for a dispatcher of the test's own (CJupiterControl is the
 * framework's): no window, no browser host, no automation */
struct TestSite final : IXcpHostSite
{
    void IncrementReference() override {}
    void ReleaseReference() override {}
    void ReportError(IErrorService *) override {}
    IPALUri *GetBaseUri() override { return nullptr; }
    IPALUri *GetSecurityUri() override { return nullptr; }
    HRESULT SetAppUri(IPALUri *) override { return S_OK; }
    IXcpDispatcher *GetXcpDispatcher() override { return nullptr; }
    IXcpBrowserHost *GetBrowserHost() const override { return nullptr; }
    HRESULT CreateResourceManager(IPALResourceManager **) override { return E_NOTIMPL; }
    XINT32 IsInitialized() override { return TRUE; }
    HRESULT GetActualHeight(XUINT32 *h) override { *h = 0; return S_OK; }
    HRESULT GetActualWidth(XUINT32 *w) override { *w = 0; return S_OK; }
    XUINT32 GetIdentity() override { return 1; }
    HRESULT IsDependencyObjectValid(CDependencyObject *) override { return S_OK; }
    bool GetEnableFrameRateCounter() const override { return false; }
    HRESULT SetEnableFrameRateCounter(bool) override { return S_OK; }
    XHANDLE GetXcpControlWindow() override { return nullptr; }
    HRESULT UIAClientsAreListening(UIAXcp::APAutomationEvents) override { return S_FALSE; }
    HRESULT UIARaiseAutomationEvent(CAutomationPeer *, UIAXcp::APAutomationEvents) override { return S_OK; }
    HRESULT UIARaiseAutomationPropertyChangedEvent(CAutomationPeer *, UIAXcp::APAutomationProperties, const CValue &,
                                                   const CValue &) override { return S_OK; }
    HRESULT UIARaiseFocusChangedEventOnUIAWindow() override { return S_OK; }
    HRESULT UIARaiseTextEditTextChangedEvent(CAutomationPeer *, UIAXcp::AutomationTextEditChangeType, CValue *) override { return S_OK; }
    HRESULT UIARaiseNotificationEvent(CAutomationPeer *, UIAXcp::AutomationNotificationKind, UIAXcp::AutomationNotificationProcessing,
                                      xstring_ptr, xstring_ptr) override { return S_OK; }
    HRESULT GetUIAWindow(CDependencyObject *, XHANDLE, bool, CUIAWindow **w) override { *w = nullptr; return S_OK; }
    HRESULT GetAccessibleRootObjectReference(XHANDLE, XDWORD, XDWORD, XDWORD *, bool *handled) override { *handled = false; return S_OK; }
    HRESULT OnFirstFrameDrawn() override { return S_OK; }
    bool IsWindowDestroyed() override { return false; }
    void OnReentrancyDetected() override {}
    bool IsHdrOutput() const override { return false; }
};

static void pump(DWORD ms)
{
    DWORD start = GetTickCount();
    MSG msg;
    while (GetTickCount() - start < ms) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}

/* Part 12: the error helper and the control layer, on a thread of their own
 * (a DispatcherQueue is the thread's for good once made) */
static DWORD WINAPI control_thread(void *)
{
    ctl::ComPtr<msy::IDispatcherQueueControllerStatics> statics;
    CHECK_HR("DispatcherQueueController's statics (CoreMessaging.dll)", wf::GetActivationFactory(
        wrl_wrappers::HStringReference(RuntimeClass_Microsoft_UI_Dispatching_DispatcherQueueController).Get(), statics.ReleaseAndGetAddressOf()));
    if (!statics) return 0;
    ctl::ComPtr<msy::IDispatcherQueueController> controller;
    CHECK_HR("a DispatcherQueue on the thread", statics->CreateOnCurrentThread(&controller));
    if (!controller) return 0;
    ctl::ComPtr<msy::IDispatcherQueue> queue;
    controller->get_DispatcherQueue(&queue);

    /* the XAML dispatcher, on the thread's queue; QueueTick starts its timer */
    TestSite site;
    IXcpDispatcher *disp = nullptr;
    CHECK_HR("CXcpDispatcher::Create on the thread's DispatcherQueue", CXcpDispatcher::Create(&site, &disp));
    if (disp) {
        CHECK("the dispatcher's queue is the thread's", disp->GetDispatcherQueueNoRef() == queue.Get());
        CHECK("the dispatcher's host site", disp->GetHostSite() == &site);
        CHECK_HR("the dispatcher starts", disp->Start());
        CHECK_HR("QueueTick", disp->QueueTick());
        pump(100);                                      /* (the timer's tick runs the queued tick) */
        disp->Deinit();
        disp->Release();
    }

    /* the Jupiter control: its dispatcher and browser host, as far as the
     * render target (the rendering does not build yet: E_NOTIMPL) */
    CJupiterControl *control = nullptr;
    HRESULT hr = CJupiterControl::Create(&control);
    CHECK("CJupiterControl::Create reaches the render target (not built: E_NOTIMPL)", hr == E_NOTIMPL && control == nullptr);
    if (control) control->Release();

    ctl::ComPtr<msy::IDispatcherQueueController2> controller2;
    if (SUCCEEDED(controller.As(&controller2))) controller2->ShutdownQueue();
    return 0;
}

static void test_control()
{
    /* the error helper: XamlReader.Load(null) is an invalid argument */
    ctl::ComPtr<DirectUI::XamlReaderFactory> reader;
    ctl::ComObject<DirectUI::XamlReaderFactory>::CreateInstance(reader.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml_markup::IXamlReaderStatics> statics;
    if (reader) reader.As(&statics);
    IInspectable *obj = reinterpret_cast<IInspectable *>(1);
    HRESULT hr = statics ? statics->Load(nullptr, &obj) : E_FAIL;
    CHECK("XamlReader.Load(null): E_INVALIDARG", hr == E_INVALIDARG);
    HANDLE t = CreateThread(nullptr, 0, control_thread, nullptr, 0, nullptr);
    CHECK("the control layer's thread", t && WaitForSingleObject(t, 60000) == WAIT_OBJECT_0);
    if (t) CloseHandle(t);
}

/* The pieces DXamlCore::InitializeInstance makes after the Jupiter
 * control, each on its own (InitializeInstance itself waits on the
 * rendering, above; the Window, on an initialized DXamlCore) */
static DWORD WINAPI other_thread_check(void *p)
{
    return static_cast<DirectUI::DispatcherImpl *>(p)->OnDispatcherThread() ? 1 : 0;
}

static void test_startup()
{
    ctl::ComPtr<DirectUI::DispatcherImpl> disp;
    CHECK_HR("the XAML dispatcher, DispatcherImpl", ctl::ComObject<DirectUI::DispatcherImpl>::CreateInstance(
        disp.ReleaseAndGetAddressOf()));
    if (disp) {
        /* (DXamlCore connects it, on its own thread; unconnected it is no
         * thread's and takes no work) */
        CHECK("an unconnected dispatcher is not this thread's", !disp->OnDispatcherThread());
        HANDLE t = CreateThread(nullptr, 0, other_thread_check, disp.Get(), 0, nullptr);
        DWORD code = 2;
        if (t) { WaitForSingleObject(t, 10000); GetExitCodeThread(t, &code); CloseHandle(t); }
        CHECK("nor another thread's", code == 0);
        HRESULT hr = disp->RunAsync(ctl::ComPtr<DirectUI::ICallback>());
        CHECK("RunAsync without a core fails", FAILED(hr));
    }

    DirectUI::DefaultStyles *styles = new DirectUI::DefaultStyles();
    CHECK("the default styles' cache, DefaultStyles", styles != nullptr);
    CHECK("its style cache", styles->GetStyleCache() != nullptr);
    styles->GetStyleCache()->Clear();
    delete styles;
}

int main()
{
    DirectUI::StaticLockGlobalInit();
    test_dictionary();
    test_load();
    test_framework();
    test_control();
    test_startup();
    printf("xamlloadtest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    return fail != 0;
}
