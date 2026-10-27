/* xamldlltest.exe — the system Windows.UI.Xaml.dll (roadmap step 32.6,
 * 0.2.2 "Pulsar" item 1; docs/design/windows-ui-xaml.md): a XAML page run
 * as a Windows app runs one, with nothing of the framework in the program
 * and no classes registered by it.  Every class comes from
 * RoGetActivationFactory by its Windows.UI.Xaml name, which the registry's
 * ActivatableClassId keys lead to Windows.UI.Xaml.dll:
 *   - Windows.UI.Xaml.Application's statics, from Windows.UI.Xaml.dll,
 *     loaded by the activation (the program does not link it);
 *   - Application.Start with a Windows ApplicationInitializationCallback,
 *     in which the app makes its Application (Windows' IApplicationFactory)
 *     and Application.Current is it;
 *   - on the thread's DispatcherQueue, once the framework runs: a page
 *     loaded by Windows.UI.Xaml.Markup.XamlReader (a Grid holding a Border
 *     and a TextBlock), a Windows.UI.Xaml.Window showing it (Activate);
 *   - once the page is laid out: the window's bounds, the Border's and the
 *     TextBlock's laid-out sizes, through Windows' IFrameworkElement; then
 *     the window closes and Application.Exit returns from Application.Start.
 *
 *   xamldlltest        the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.foundation.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.markup.h>
#include <microsoft.ui.dispatching.h>

namespace wf = ABI::Windows::Foundation;
namespace wux = ABI::Windows::UI::Xaml;
namespace msd = ABI::Microsoft::UI::Dispatching;

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define CHECK_HR(what, expr) do { HRESULT h_ = (expr); if (SUCCEEDED(h_)) pass++; else { fail++; \
    printf("FAIL: %s: 0x%08lx (line %d)\n", what, static_cast<unsigned long>(h_), __LINE__); fflush(stdout); } } while (0)

#define XMLNS L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
#define XMLNS_X L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'"

template <class T> static void release(T *&p) { if (p) p->Release(); p = nullptr; }

template <class T> static T *qi(IUnknown *o)
{
    void *p = nullptr;
    return o && o->QueryInterface(__uuidof(T), &p) == S_OK ? static_cast<T *>(p) : nullptr;
}

template <class T> static HRESULT factory(const wchar_t *cls, T **out)
{
    HSTRING s = nullptr;
    WindowsCreateString((const WCHAR *)cls, (UINT32)wcslen(cls), &s);
    HRESULT hr = RoGetActivationFactory(s, __uuidof(T), reinterpret_cast<void **>(out));
    WindowsDeleteString(s);
    return hr;
}

/* (a plain C++ program has no C++ library: the delegates' new and delete) */
void *operator new(size_t n) { return HeapAlloc(GetProcessHeap(), 0, n); }
void operator delete(void *p) noexcept { HeapFree(GetProcessHeap(), 0, p); }
void operator delete(void *p, size_t) noexcept { HeapFree(GetProcessHeap(), 0, p); }

static const GUID IID_IAgileObject_ = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};

/* a delegate of the app's: interface I, its Invoke calling fn */
template <class I, class... A> struct Handler : I
{
    LONG refs = 1;
    HRESULT (*fn)(A...);
    explicit Handler(HRESULT (*f)(A...)) : fn(f) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(I) || iid == IID_IAgileObject_) {
            *out = static_cast<I *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&refs); if (!r) delete this; return (ULONG)r; }
    HRESULT STDMETHODCALLTYPE Invoke(A... a) override { return fn(a...); }
};

/* the framework's own (lifted) IWindowFactory: a desktop app's Window,
 * where a UWP app is given Window.Current */
struct __declspec(uuid("f0441536-afef-5222-918f-324a9b2dec75")) ILiftedWindowFactory : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable *outer, IInspectable **inner, IInspectable **window) = 0;
};

static wux::IApplication *s_app;
static wux::IWindow *s_window;
static wux::IFrameworkElement *s_grid, *s_border, *s_text;
static msd::IDispatcherQueueTimer *s_timer;
static bool s_callback, s_shown, s_checked, s_exit_called;
static int s_ticks;

static wux::IFrameworkElement *find(const wchar_t *name)
{
    HSTRING h = nullptr;
    IInspectable *o = nullptr;
    WindowsCreateString((const WCHAR *)name, (UINT32)wcslen(name), &h);
    if (s_grid) s_grid->FindName(h, &o);
    WindowsDeleteString(h);
    wux::IFrameworkElement *fe = qi<wux::IFrameworkElement>(o);
    release(o);
    return fe;
}

/* once the page is laid out (or after ten seconds): the checks, then the
 * window closes and the app exits */
static HRESULT on_tick(msd::IDispatcherQueueTimer *, IInspectable *)
{
    double w = 0, h = 0;
    if (!(s_border && SUCCEEDED(s_border->get_ActualWidth(&w)) && w > 0) && ++s_ticks < 40)
        return S_OK;
    if (!s_checked) {
        s_checked = true;
        printf("xamldlltest: page laid out after %d ticks\n", s_ticks);
        wf::Rect bounds = {};
        CHECK_HR("Window.Bounds", s_window->get_Bounds(&bounds));
        CHECK("the window has a size", bounds.Width > 0 && bounds.Height > 0);
        double gw = 0, gh = 0;
        CHECK("the Grid fills the window", s_grid && SUCCEEDED(s_grid->get_ActualWidth(&gw)) &&
              SUCCEEDED(s_grid->get_ActualHeight(&gh)) && gw == bounds.Width && gh == bounds.Height);
        if (s_border) s_border->get_ActualHeight(&h);
        CHECK("the Border is laid out at its size", w == 200 && h == 100);
        double tw = 0, th = 0;
        CHECK("the TextBlock is laid out to its text", s_text && SUCCEEDED(s_text->get_ActualWidth(&tw)) &&
              SUCCEEDED(s_text->get_ActualHeight(&th)) && tw > 0 && th > 0);
        printf("xamldlltest: window %gx%g, grid %gx%g, border %gx%g, text %gx%g\n", bounds.Width, bounds.Height,
               gw, gh, w, h, tw, th);
        printf("xamldlltest: page on screen\n");
        fflush(stdout);
        return S_OK;
    }
    /* (the page stays on screen for three seconds: the self-test's screenshot) */
    if (++s_ticks < 52) return S_OK;
    s_timer->Stop();
    CHECK_HR("Window.Close", s_window->Close());
    CHECK_HR("Application.Exit", s_app ? s_app->Exit() : E_FAIL);
    s_exit_called = true;
    return S_OK;
}

/* on the DispatcherQueue, once the framework runs: the page in a window */
static HRESULT show_page()
{
    s_shown = true;
    wux::Markup::IXamlReaderStatics *reader = nullptr;
    IInspectable *page = nullptr;
    HSTRING text = nullptr;
    const wchar_t *xaml = L"<Grid " XMLNS XMLNS_X L">"
        L"<Border x:Name='panel' Width='200' Height='100' Margin='24,24,0,0' HorizontalAlignment='Left' VerticalAlignment='Top' Background='SteelBlue'/>"
        L"<TextBlock x:Name='label' Text='Windows.UI.Xaml.dll' FontSize='20' Margin='240,24,0,0' HorizontalAlignment='Left' VerticalAlignment='Top'/>"
        L"</Grid>";
    WindowsCreateString((const WCHAR *)xaml, (UINT32)wcslen(xaml), &text);
    CHECK_HR("Windows.UI.Xaml.Markup.XamlReader.Load of a page",
             SUCCEEDED(factory(L"Windows.UI.Xaml.Markup.XamlReader", &reader)) ? reader->Load(text, &page) : E_FAIL);
    WindowsDeleteString(text);
    release(reader);
    s_grid = qi<wux::IFrameworkElement>(page);
    s_border = find(L"panel");
    s_text = find(L"label");
    CHECK("its Grid (Windows' IFrameworkElement), Border and TextBlock", s_grid && s_border && s_text);

    /* a Windows.UI.Xaml.Window (activated by its name; else the
     * framework's factory, as a desktop app makes one) */
    IActivationFactory *wf_ = nullptr;
    IInspectable *window = nullptr;
    HRESULT hr = factory(L"Windows.UI.Xaml.Window", &wf_);
    CHECK_HR("Windows.UI.Xaml.Window's factory", hr);
    if (wf_ && FAILED(wf_->ActivateInstance(&window))) {
        ILiftedWindowFactory *lifted = qi<ILiftedWindowFactory>(wf_);
        IInspectable *inner = nullptr;
        if (lifted) lifted->CreateInstance(nullptr, &inner, &window);
        printf("xamldlltest: Window made by the framework's IWindowFactory\n");
        release(inner);
        release(lifted);
    }
    release(wf_);
    s_window = qi<wux::IWindow>(window);
    release(window);
    CHECK("a Window (Windows' IWindow)", s_window != nullptr);
    if (!s_window) return S_OK;
    wux::IUIElement *content = qi<wux::IUIElement>(s_grid);
    CHECK_HR("Window.Content", s_window->put_Content(content));
    release(content);
    CHECK_HR("Window.Activate", s_window->Activate());

    msd::IDispatcherQueueStatics *statics = nullptr;
    msd::IDispatcherQueue *queue = nullptr;
    if (SUCCEEDED(factory(L"Microsoft.UI.Dispatching.DispatcherQueue", &statics)))
        statics->GetForCurrentThread(&queue);
    release(statics);
    CHECK_HR("a timer on the thread's DispatcherQueue", queue ? queue->CreateTimer(&s_timer) : E_FAIL);
    release(queue);
    if (!s_timer) return S_OK;
    EventRegistrationToken token;
    s_timer->put_Interval(wf::TimeSpan{ 2500000 });     /* (250 ms) */
    auto *tick = new Handler<wf::ITypedEventHandler<msd::DispatcherQueueTimer *, IInspectable *>,
                             msd::IDispatcherQueueTimer *, IInspectable *>(on_tick);
    s_timer->add_Tick(tick, &token);
    tick->Release();
    CHECK_HR("started", s_timer->Start());
    return S_OK;
}

/* Application.Start's callback: the app makes its Application */
static HRESULT on_start(wux::IApplicationInitializationCallbackParams *params)
{
    s_callback = true;
    CHECK("the initialization callback, given its parameters", params != nullptr);
    wux::IApplicationFactory *af = nullptr;
    IInspectable *inner = nullptr;
    CHECK_HR("the app's Application (Windows' IApplicationFactory)",
             SUCCEEDED(factory(L"Windows.UI.Xaml.Application", &af)) ? af->CreateInstance(nullptr, &inner, &s_app) : E_FAIL);
    release(af);
    release(inner);
    wux::IApplicationStatics *statics = nullptr;
    wux::IApplication *current = nullptr;
    CHECK("is Application.Current", SUCCEEDED(factory(L"Windows.UI.Xaml.Application", &statics)) &&
          SUCCEEDED(statics->get_Current(&current)) && current && s_app);
    release(current);
    release(statics);

    msd::IDispatcherQueueStatics *qs = nullptr;
    msd::IDispatcherQueue *queue = nullptr;
    if (SUCCEEDED(factory(L"Microsoft.UI.Dispatching.DispatcherQueue", &qs)))
        qs->GetForCurrentThread(&queue);
    release(qs);
    ::boolean queued = false;
    auto *work = new Handler<msd::IDispatcherQueueHandler>(show_page);
    CHECK_HR("the page queued on the thread's DispatcherQueue", queue ? queue->TryEnqueue(work, &queued) : E_FAIL);
    work->Release();
    release(queue);
    return S_OK;
}

/* (a framework that never returns from Application.Start fails the test) */
static DWORD WINAPI watchdog(void *)
{
    Sleep(120000);
    printf("FAIL: Application.Start did not return\nxamldlltest: %d passed, %d failed (%s)\n", pass, fail + 1,
           sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(1);
}

int main()
{
    SetEnvironmentVariableA("NOVA_PRESENT", "gdi");     /* (as xamlpagetest: the frames in the window's pixels) */
    CHECK("Windows.UI.Xaml.dll not loaded before the first activation", !GetModuleHandleA("Windows.UI.Xaml.dll"));
    wux::IApplicationStatics *statics = nullptr;
    CHECK_HR("RoGetActivationFactory(Windows.UI.Xaml.Application)", factory(L"Windows.UI.Xaml.Application", &statics));
    CHECK("served by Windows.UI.Xaml.dll (the registry's ActivatableClassId key)", GetModuleHandleA("Windows.UI.Xaml.dll") != nullptr);
    if (!statics) {
        printf("xamldlltest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
        return 1;
    }
    wux::IApplication *none = nullptr;
    CHECK("no Application.Current before Application.Start", FAILED(statics->get_Current(&none)) || !none);
    release(none);
    CloseHandle(CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr));

    auto *callback = new Handler<wux::IApplicationInitializationCallback, wux::IApplicationInitializationCallbackParams *>(on_start);
    CHECK_HR("Application.Start", statics->Start(callback));
    callback->Release();
    CHECK("the initialization callback ran", s_callback);
    CHECK("the page was shown", s_shown);
    CHECK("the timer ran the checks", s_checked);
    CHECK("Application.Exit returned from Application.Start", s_exit_called);
    release(statics);
    printf("xamldlltest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(fail != 0);
}
