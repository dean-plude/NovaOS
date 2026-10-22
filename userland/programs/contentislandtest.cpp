/* contentislandtest.exe — the Windows App SDK's lifted content islands,
 * input and windowing classes as NovaOS's Microsoft.UI.Input.dll serves them
 * (roadmap step 32.4, its first part; docs/design/windows-ui-xaml.md): what
 * a XAML Window sits on, driven here through the generated ABI headers as
 * the XAML framework drives it.  A top-level window gets a
 * DesktopChildSiteBridge (its child window) on a Compositor, a
 * ContentIsland on a visual is connected to it, and the bridge's size
 * becomes the island's (StateChanged); the island's input sources raise
 * pointer, key and focus events from the bridge window's messages; AppWindow
 * reports the top-level window's moves; closing the bridge destroys its
 * window.
 *
 *   contentislandtest       the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.foundation.h>
#include <windows.graphics.h>
#include <microsoft.ui.h>
#include <microsoft.ui.composition.h>
#include <microsoft.ui.content.h>
#include <microsoft.ui.input.h>
#include <microsoft.ui.windowing.h>

namespace mui = ABI::Microsoft::UI;
namespace mcomp = ABI::Microsoft::UI::Composition;
namespace ixp = ABI::Microsoft::UI::Content;
namespace ixi = ABI::Microsoft::UI::Input;
namespace ixw = ABI::Microsoft::UI::Windowing;
namespace wf = ABI::Windows::Foundation;
namespace wfn = ABI::Windows::Foundation::Numerics;

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)

template <class T> static void release(T *&p) { if (p) p->Release(); p = nullptr; }

template <class T> static T *qi(IUnknown *o)
{
    void *p = nullptr;
    return o && o->QueryInterface(__uuidof(T), &p) == S_OK ? (T *)p : nullptr;
}

template <class T> static T *factory(const wchar_t *cls)
{
    HSTRING s = nullptr;
    WindowsCreateString((const WCHAR *)cls, (UINT32)wcslen(cls), &s);
    T *f = nullptr;
    HRESULT hr = RoGetActivationFactory(s, __uuidof(T), (void **)&f);
    WindowsDeleteString(s);
    return SUCCEEDED(hr) ? f : nullptr;
}

/* an event handler counting its calls, keeping its last arguments */
template <class H, class S, class A> struct Handler : H
{
    LONG refs = 1;
    int calls = 0;
    A last = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(H)) {
            *out = static_cast<H *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { return (ULONG)InterlockedDecrement(&refs); }  /* (on the stack) */
    HRESULT STDMETHODCALLTYPE Invoke(S, A args) override
    {
        calls++;
        if (last) last->Release();
        last = args;
        if (last) last->AddRef();
        return S_OK;
    }
    ~Handler() { if (last) last->Release(); }
};

typedef Handler<wf::ITypedEventHandler<ixp::ContentIsland *, ixp::ContentIslandStateChangedEventArgs *>,
                ixp::IContentIsland *, ixp::IContentIslandStateChangedEventArgs *> StateHandler;
typedef Handler<wf::ITypedEventHandler<ixi::InputPointerSource *, ixi::PointerEventArgs *>,
                ixi::IInputPointerSource *, ixi::IPointerEventArgs *> PointerHandler;
typedef Handler<wf::ITypedEventHandler<ixi::InputKeyboardSource *, ixi::KeyEventArgs *>,
                ixi::IInputKeyboardSource *, ixi::IKeyEventArgs *> KeyHandler;
typedef Handler<wf::ITypedEventHandler<ixi::InputFocusController *, ixi::FocusChangedEventArgs *>,
                ixi::IInputFocusController *, ixi::IFocusChangedEventArgs *> FocusHandler;
typedef Handler<wf::ITypedEventHandler<ixw::AppWindow *, ixw::AppWindowChangedEventArgs *>,
                ixw::IAppWindow *, ixw::IAppWindowChangedEventArgs *> ChangedHandler;

static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static void run(void)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = (const WCHAR *)L"ContentIslandTestWindow";
    RegisterClassW(&wc);
    HWND top = CreateWindowExW(0, wc.lpszClassName, (const WCHAR *)L"Content island test", WS_OVERLAPPEDWINDOW,
                               40, 40, 480, 360, nullptr, nullptr, wc.hInstance, nullptr);
    CHECK("a top-level window", top != nullptr);
    if (!top) return;
    ShowWindow(top, SW_SHOW);
    pump();

    /* the compositor and a visual, the island's root */
    HSTRING cls = nullptr;
    WindowsCreateString((const WCHAR *)L"Microsoft.UI.Composition.Compositor", 35, &cls);
    IInspectable *insp = nullptr;
    RoActivateInstance(cls, (void **)&insp);
    WindowsDeleteString(cls);
    mcomp::ICompositor *comp = qi<mcomp::ICompositor>(insp);
    release(insp);
    CHECK("a Compositor", comp != nullptr);
    if (!comp) return;
    mcomp::IContainerVisual *container = nullptr;
    CHECK("its visual", SUCCEEDED(comp->CreateContainerVisual(&container)) && container);
    mcomp::IVisual *root = qi<mcomp::IVisual>(container);

    /* the bridge: a child window of the top-level one */
    auto *bridgeStatics = factory<ixp::IDesktopChildSiteBridgeStatics>(L"Microsoft.UI.Content.DesktopChildSiteBridge");
    CHECK("DesktopChildSiteBridge's statics", bridgeStatics != nullptr);
    ixp::IDesktopChildSiteBridge *bridge = nullptr;
    mui::WindowId parent = { (UINT64)(ULONG_PTR)top };
    CHECK("DesktopChildSiteBridge.Create", bridgeStatics && SUCCEEDED(bridgeStatics->Create(comp, parent, &bridge)) && bridge);
    ixp::IDesktopSiteBridge *site = qi<ixp::IDesktopSiteBridge>(bridge);
    CHECK("is a DesktopSiteBridge", site != nullptr);
    mui::WindowId wid = {};
    HWND child = site && SUCCEEDED(site->get_WindowId(&wid)) ? (HWND)(ULONG_PTR)wid.Value : nullptr;
    CHECK("with its child window", child && IsWindow(child) && GetParent(child) == top);

    /* the island, on the visual, connected to the bridge */
    auto *islandStatics = factory<ixp::IContentIslandStatics>(L"Microsoft.UI.Content.ContentIsland");
    CHECK("ContentIsland's statics", islandStatics != nullptr);
    ixp::IContentIsland *island = nullptr;
    CHECK("ContentIsland.Create", islandStatics && root && SUCCEEDED(islandStatics->Create(root, &island)) && island);
    if (!island || !site) return;
    StateHandler state;
    EventRegistrationToken stateToken = {};
    CHECK("StateChanged", SUCCEEDED(island->add_StateChanged(&state, &stateToken)));
    CHECK("DesktopSiteBridge.Connect", SUCCEEDED(site->Connect(island)));
    boolean connected = false;
    CHECK("the island is connected", SUCCEEDED(island->get_IsConnected(&connected)) && connected);
    ixp::IContentIsland *found = nullptr;
    CHECK("ContentIsland.GetByVisual", SUCCEEDED(islandStatics->GetByVisual(root, &found)) && found == island);
    release(found);

    /* the bridge's size is the island's */
    ABI::Windows::Graphics::RectInt32 rect = { 0, 0, 400, 300 };
    CHECK("MoveAndResize", SUCCEEDED(site->MoveAndResize(rect)));
    boolean changed = false;
    CHECK("StateChanged raised for it", state.calls > 0 && state.last &&
          SUCCEEDED(state.last->get_DidActualSizeChange(&changed)) && changed);
    CHECK("Show", SUCCEEDED(site->Show()));
    pump();
    RECT rc = {};
    GetClientRect(child, &rc);
    CHECK("the child window's size", rc.right == 400 && rc.bottom == 300);
    wfn::Vector2 size = {};
    CHECK("the island's ActualSize", SUCCEEDED(island->get_ActualSize(&size)) && size.X == 400 && size.Y == 300);
    printf("contentislandtest: island %gx%g, %d state changes\n", size.X, size.Y, state.calls);

    /* the island's input: the bridge window's messages as its events */
    auto *pointerStatics = factory<ixi::IInputPointerSourceStatics>(L"Microsoft.UI.Input.InputPointerSource");
    ixi::IInputPointerSource *pointer = nullptr;
    CHECK("InputPointerSource.GetForIsland", pointerStatics && SUCCEEDED(pointerStatics->GetForIsland(island, &pointer)) && pointer);
    PointerHandler moved, pressed;
    EventRegistrationToken t1 = {}, t2 = {};
    if (pointer) {
        pointer->add_PointerMoved(&moved, &t1);
        pointer->add_PointerPressed(&pressed, &t2);
    }
    SendMessageW(child, WM_MOUSEMOVE, 0, MAKELPARAM(30, 40));
    SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 40));
    SendMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(30, 40));
    CHECK("PointerMoved", moved.calls == 1);
    ixi::IPointerPoint *point = nullptr;
    wf::Point at = {};
    CHECK("at the pointer's position", moved.last && SUCCEEDED(moved.last->get_CurrentPoint(&point)) && point &&
          SUCCEEDED(point->get_Position(&at)) && at.X == 30 && at.Y == 40);
    release(point);
    CHECK("PointerPressed", pressed.calls == 1);

    auto *keyStatics = factory<ixi::IInputKeyboardSourceStatics2>(L"Microsoft.UI.Input.InputKeyboardSource");
    ixi::IInputKeyboardSource *keys = nullptr;
    CHECK("InputKeyboardSource.GetForIsland", keyStatics && SUCCEEDED(keyStatics->GetForIsland(island, &keys)) && keys);
    ixi::IInputKeyboardSource2 *keys2 = qi<ixi::IInputKeyboardSource2>(keys);
    KeyHandler down;
    EventRegistrationToken t3 = {};
    if (keys2) keys2->add_KeyDown(&down, &t3);
    SendMessageW(child, WM_KEYDOWN, 'A', 1);
    ABI::Windows::System::VirtualKey vk = {};
    CHECK("KeyDown", down.calls == 1 && down.last && SUCCEEDED(down.last->get_VirtualKey(&vk)) && (int)vk == 'A');

    auto *focusStatics = factory<ixi::IInputFocusControllerStatics>(L"Microsoft.UI.Input.InputFocusController");
    ixi::IInputFocusController *focus = nullptr;
    CHECK("InputFocusController.GetForIsland", focusStatics && SUCCEEDED(focusStatics->GetForIsland(island, &focus)) && focus);
    FocusHandler got;
    EventRegistrationToken t4 = {};
    if (focus) focus->add_GotFocus(&got, &t4);
    SetFocus(top);
    boolean took = false;
    CHECK("TrySetFocus", focus && SUCCEEDED(focus->TrySetFocus(&took)) && took && GetFocus() == child);
    boolean has = false;
    CHECK("HasFocus", focus && SUCCEEDED(focus->get_HasFocus(&has)) && has);
    CHECK("GotFocus", got.calls >= 1);

    /* AppWindow: the top-level window's moves */
    auto *appStatics = factory<ixw::IAppWindowStatics>(L"Microsoft.UI.Windowing.AppWindow");
    ixw::IAppWindow *app = nullptr;
    CHECK("AppWindow.GetFromWindowId", appStatics && SUCCEEDED(appStatics->GetFromWindowId(parent, &app)) && app);
    ChangedHandler appChanged;
    EventRegistrationToken t5 = {};
    if (app) app->add_Changed(&appChanged, &t5);
    ABI::Windows::Graphics::PointInt32 to = { 100, 80 };
    CHECK("AppWindow.Move", app && SUCCEEDED(app->Move(to)));
    pump();
    ABI::Windows::Graphics::PointInt32 pos = {};
    CHECK("AppWindow.Position", app && SUCCEEDED(app->get_Position(&pos)) && pos.X == 100 && pos.Y == 80);
    boolean moved_ = false;
    CHECK("Changed raised for the move", appChanged.calls > 0 && appChanged.last &&
          SUCCEEDED(appChanged.last->get_DidPositionChange(&moved_)) && moved_);
    if (app) app->remove_Changed(t5);

    /* closing: the island, then the bridge with its window */
    if (pointer) { pointer->remove_PointerMoved(t1); pointer->remove_PointerPressed(t2); }
    if (keys2) keys2->remove_KeyDown(t3);
    if (focus) focus->remove_GotFocus(t4);
    island->remove_StateChanged(stateToken);
    wf::IClosable *closeIsland = qi<wf::IClosable>(island), *closeBridge = qi<wf::IClosable>(bridge);
    CHECK("ContentIsland.Close", closeIsland && SUCCEEDED(closeIsland->Close()));
    CHECK("DesktopChildSiteBridge.Close", closeBridge && SUCCEEDED(closeBridge->Close()));
    CHECK("destroys its window", !IsWindow(child));
    release(closeIsland); release(closeBridge);
    release(app); release(appStatics); release(focus); release(focusStatics); release(keys2); release(keys);
    release(keyStatics); release(pointer); release(pointerStatics); release(island); release(islandStatics);
    release(site); release(bridge); release(bridgeStatics); release(root); release(container); release(comp);
    DestroyWindow(top);
}

int main(void)
{
    RoInitialize(RO_INIT_SINGLETHREADED);
    run();
    printf("contentislandtest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    return fail ? 1 : 0;
}
