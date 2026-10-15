/* coreapptest.exe — the UWP app model's core, as a DirectX UWP app (or
 * Windows.UI.Xaml's Application.Start) drives it: CoreApplication from its
 * activation factory (RoGetActivationFactory finds twinapi.appcore.dll in
 * the registry's ActivatableClassId keys), Run with an IFrameworkViewSource
 * whose view is initialized, given its CoreWindow, loaded, activated by a
 * launch (with the command line's arguments) and run; the window shown by
 * Activate, its HWND (ICoreWindowInterop), its events from the window's
 * messages (KeyDown, CharacterReceived, KeyUp, SizeChanged,
 * VisibilityChanged, Closed), its dispatcher pumping them, RunAsync from
 * another thread completing its IAsyncAction, and CoreApplication.Exit
 * ending the view; in 64 bits a Direct3D 11 swap chain made for the
 * CoreWindow (DXGI's CreateSwapChainForCoreWindow) presenting into it.
 *
 *   coreapptest ARGS...   the self-test runs it as "coreapptest hello world"
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <roapi.h>
#include <novadx.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define VT(o, i, T) ((T)((void **)*(void **)(o))[i])
typedef ULONG (STDMETHODCALLTYPE *Rel_t)(void *);
typedef HRESULT (STDMETHODCALLTYPE *QI_t)(void *, const GUID *, void **);
typedef HRESULT (STDMETHODCALLTYPE *Get_t)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *Call_t)(void *);
#define RELEASE(o) do { if (o) VT(o, 2, Rel_t)(o); (o) = NULL; } while (0)

static const GUID IID_IUnknown_T = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_T = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_ICoreApplication_T = { 0x0AACF7A4, 0x5E1D, 0x49DF, { 0x80, 0x34, 0xFB, 0x6A, 0x68, 0xBC, 0x5E, 0xD1 } };
static const GUID IID_ICoreApplicationExit_T = { 0xCF86461D, 0x261E, 0x4B72, { 0x9A, 0xCD, 0x44, 0xED, 0x2A, 0xCE, 0x6A, 0x29 } };
static const GUID IID_ICoreImmersiveApplication_T = { 0x1ADA0E3E, 0xE4A2, 0x4123, { 0xB4, 0x51, 0xDC, 0x96, 0xBF, 0x80, 0x04, 0x19 } };
static const GUID IID_IFrameworkView_T = { 0xFAAB5CD0, 0x8924, 0x45AC, { 0xAD, 0x0F, 0xA0, 0x8F, 0xAE, 0x5D, 0x03, 0x24 } };
static const GUID IID_IFrameworkViewSource_T = { 0xCD770614, 0x65C4, 0x426C, { 0x94, 0x94, 0x34, 0xFC, 0x43, 0x55, 0x48, 0x62 } };
static const GUID IID_ICoreWindowStatic_T = { 0x4D239005, 0x3C2A, 0x41B1, { 0x90, 0x22, 0x53, 0x6B, 0xB9, 0xCF, 0x93, 0xB1 } };
static const GUID IID_ICoreWindowInterop_T = { 0x45D64A29, 0xA63E, 0x4CB6, { 0xB4, 0x98, 0x57, 0x81, 0xD2, 0x98, 0xCB, 0x4F } };
static const GUID IID_ICoreWindowEventArgs_T = { 0x272B1EF3, 0xC633, 0x4DA5, { 0xA2, 0x6C, 0xC6, 0xD0, 0xF5, 0x6B, 0x29, 0xDA } };
static const GUID IID_ILaunchActivatedEventArgs_T = { 0xFBC93E26, 0xA14A, 0x4B4F, { 0x82, 0xB0, 0x33, 0xBE, 0xD9, 0x20, 0xAF, 0x52 } };
static const GUID IID_IAsyncInfo_T = { 0x00000036, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };

/* ICoreWindow's slots (IInspectable's six first) */
enum { W_BOUNDS = 7, W_DISPATCHER = 9, W_VISIBLE = 17, W_ACTIVATE = 18, W_ADD_CHAR = 28, W_ADD_CLOSED = 30,
       W_ADD_KEYDOWN = 34, W_ADD_KEYUP = 36, W_ADD_SIZE = 54, W_ADD_VISIBILITY = 56 };
typedef struct { float X, Y, Width, Height; } RectF;
typedef struct { float Width, Height; } SizeF;
typedef struct { UINT32 RepeatCount, ScanCode; BYTE IsExtendedKey, IsMenuKeyDown, WasKeyDown, IsKeyReleased; } KeyStatus;

static void *g_app, *g_exit, *g_win, *g_disp, *g_win_statics;
static HWND g_hwnd;
static DWORD g_ui_tid;
static char g_order[16];
static int g_keydown_vk = -1, g_keyup_released = -1, g_char = -1, g_scancode = -1, g_closed, g_visible_events, g_last_visible = -1;
static int g_launch_kind = -1, g_ran_on_ui = -1, g_completed = -1;
static SizeF g_size;
static WCHAR g_arguments[64];
static volatile LONG g_work_ran;

static void order(char c) { size_t n = strlen(g_order); if (n + 1 < sizeof g_order) { g_order[n] = c; g_order[n + 1] = 0; } }

/* ---- delegates: Invoke(sender, args) or Invoke() after IUnknown's three */
typedef struct Handler Handler;
struct Handler { const void *const *vt; void (*fn)(void *, void *); };
static HRESULT STDMETHODCALLTYPE h_qi(Handler *h, const GUID *iid, void **out) { (void)iid; *out = h; return S_OK; }
static ULONG STDMETHODCALLTYPE h_ref(Handler *h) { (void)h; return 2; }
static HRESULT STDMETHODCALLTYPE h_invoke2(Handler *h, void *sender, void *args) { h->fn(sender, args); return S_OK; }
static HRESULT STDMETHODCALLTYPE h_invoke0(Handler *h) { h->fn(0, 0); return S_OK; }
static HRESULT STDMETHODCALLTYPE h_invoke_status(Handler *h, void *action, int status) { h->fn(action, (void *)(INT_PTR)status); return S_OK; }
static const void *const handler2_vt[] = { (void *)h_qi, (void *)h_ref, (void *)h_ref, (void *)h_invoke2 };
static const void *const handler0_vt[] = { (void *)h_qi, (void *)h_ref, (void *)h_ref, (void *)h_invoke0 };
static const void *const handler_status_vt[] = { (void *)h_qi, (void *)h_ref, (void *)h_ref, (void *)h_invoke_status };

static void on_activated(void *view, void *args)
{
    (void)view;
    VT(args, 6, Get_t)(args, &g_launch_kind);                       /* IActivatedEventArgs::Kind */
    void *launch = 0;
    if (SUCCEEDED(VT(args, 0, QI_t)(args, &IID_ILaunchActivatedEventArgs_T, &launch)) && launch) {
        HSTRING s = 0;
        VT(launch, 6, Get_t)(launch, &s);                           /* Arguments */
        UINT32 n;
        const WCHAR *c = WindowsGetStringRawBuffer(s, &n);
        if (n < 64) { memcpy(g_arguments, c, n * sizeof(WCHAR)); g_arguments[n] = 0; }
        WindowsDeleteString(s);
        RELEASE(launch);
    }
    order('A');
    CHECK("CoreWindow::Activate", VT(g_win, W_ACTIVATE, Call_t)(g_win) == S_OK);
}
static void key_args(void *args, KeyStatus *st, int *vk)
{
    if (vk) VT(args, 6, Get_t)(args, vk);
    VT(args, 7, Get_t)(args, st);
}
static void on_keydown(void *w, void *args) { (void)w; KeyStatus st; key_args(args, &st, &g_keydown_vk); g_scancode = (int)st.ScanCode; }
static void on_keyup(void *w, void *args) { (void)w; KeyStatus st; key_args(args, &st, 0); g_keyup_released = st.IsKeyReleased; }
static void on_char(void *w, void *args)
{
    (void)w;
    UINT32 code = 0;
    VT(args, 6, Get_t)(args, &code);
    g_char = (int)code;
    void *base = 0;                                                 /* ICoreWindowEventArgs::put_Handled */
    if (SUCCEEDED(VT(args, 0, QI_t)(args, &IID_ICoreWindowEventArgs_T, &base)) && base) {
        VT(base, 7, HRESULT (STDMETHODCALLTYPE *)(void *, BYTE))(base, 1);
        RELEASE(base);
    }
}
static void on_size(void *w, void *args) { (void)w; VT(args, 6, Get_t)(args, &g_size); }
static void on_visibility(void *w, void *args) { (void)w; BYTE v = 0; VT(args, 6, Get_t)(args, &v); g_visible_events++; g_last_visible = v; }
static void on_closed(void *w, void *args) { (void)w; (void)args; g_closed++; }
static void on_work(void *a, void *b)
{
    (void)a; (void)b;
    g_ran_on_ui = GetCurrentThreadId() == g_ui_tid;
    InterlockedIncrement(&g_work_ran);
    VT(g_exit, 6, Call_t)(g_exit);                                  /* CoreApplication.Exit() */
}
static void on_completed(void *action, void *status) { (void)action; g_completed = (int)(INT_PTR)status; }
static Handler h_activated = { handler2_vt, on_activated }, h_keydown = { handler2_vt, on_keydown }, h_keyup = { handler2_vt, on_keyup },
               h_char = { handler2_vt, on_char }, h_size = { handler2_vt, on_size }, h_visibility = { handler2_vt, on_visibility },
               h_closed = { handler2_vt, on_closed }, h_work = { handler0_vt, on_work }, h_completed = { handler_status_vt, on_completed };

/* RunAsync from a thread that is not the window's */
static DWORD WINAPI post_work(void *arg)
{
    (void)arg;
    void *action = 0;
    HRESULT hr = VT(g_disp, 8, HRESULT (STDMETHODCALLTYPE *)(void *, int, void *, void **))(g_disp, 0, &h_work, &action);
    CHECK("RunAsync from another thread", hr == S_OK && action);
    if (action) {
        void *info = 0;
        CHECK("its IAsyncAction is an IAsyncInfo", SUCCEEDED(VT(action, 0, QI_t)(action, &IID_IAsyncInfo_T, &info)) && info);
        RELEASE(info);
        VT(action, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(action, &h_completed);   /* put_Completed */
        RELEASE(action);
    }
    return 0;
}

/* 64 bits: a swap chain made for the CoreWindow, presenting cyan into it */
static void swap_chain(void)
{
#ifdef _WIN64
    static const GUID IID_ICoreWindow_T = { 0x79B9D5F2, 0x879E, 0x4B89, { 0xB7, 0x98, 0x79, 0xE4, 0x75, 0x98, 0x03, 0x0C } };
    typedef HRESULT (WINAPI *CreateDevice_t)(void *, UINT, HMODULE, UINT, const UINT *, UINT, UINT, void **, UINT *, void **);
    typedef HRESULT (WINAPI *CreateFactory_t)(const GUID *, void **);
    HMODULE d3d = LoadLibraryW(L"d3d11.dll"), dxgi = LoadLibraryW(L"dxgi.dll");
    CreateDevice_t cd = d3d ? (CreateDevice_t)GetProcAddress(d3d, "D3D11CreateDevice") : 0;
    CreateFactory_t cf = dxgi ? (CreateFactory_t)GetProcAddress(dxgi, "CreateDXGIFactory1") : 0;
    void *dev = 0, *ctx = 0, *f = 0, *chain = 0, *buf = 0, *core = 0;
    if (cd) cd(NULL, 5 /* WARP */, NULL, 0, NULL, 0, 7 /* D3D11_SDK_VERSION */, &dev, NULL, &ctx);
    if (cf) cf(&NIID_IDXGIFactory2, &f);
    NDXGI_SWAP_CHAIN_DESC1 d1 = { 0, 0, NDXGI_FORMAT_B8G8R8A8_UNORM, FALSE, { 1, 0 }, 0x20, 2, 0, 3 /* FLIP_SEQUENTIAL */, 0, 0 };
    CHECK("CreateSwapChainForCoreWindow", dev && f && SUCCEEDED(VT(f, 16, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *,
                                          const void *, void *, void **))(f, dev, g_win, &d1, NULL, &chain)) && chain);
    if (chain) {
        VT(chain, 21, HRESULT (STDMETHODCALLTYPE *)(void *, const GUID *, void **))(chain, &IID_ICoreWindow_T, &core);
        CHECK("GetCoreWindow: the window", core == g_win);
        RELEASE(core);
        NDXGI_SWAP_CHAIN_DESC1 got;
        VT(chain, 18, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(chain, &got);
        RECT cr;
        GetClientRect(g_hwnd, &cr);
        CHECK("its size: the window's", got.Width == (UINT)cr.right && got.Height == (UINT)cr.bottom);
        VT(chain, 9, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, const GUID *, void **))(chain, 0, &NIID_ID3D11Texture2D, &buf);
        DWORD row[64];
        for (int i = 0; i < 64; i++) row[i] = 0xFF00FFFF;
        ND3D11_BOX box = { 10, 10, 0, 74, 11, 1 };
        typedef void (STDMETHODCALLTYPE *Update_t)(void *, void *, UINT, const ND3D11_BOX *, const void *, UINT, UINT);
        for (UINT y = 10; y < 40 && buf; y++) { box.top = y; box.bottom = y + 1; VT(ctx, 48, Update_t)(ctx, buf, 0, &box, row, 256, 0); }
        CHECK("Present", SUCCEEDED(VT(chain, 8, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, UINT))(chain, 0, 0)));
        HDC dc = GetDC(g_hwnd);
        COLORREF px = GetPixel(dc, 40, 20);
        ReleaseDC(g_hwnd, dc);
        CHECK("drawn in the CoreWindow", px == RGB(0, 255, 255));
    }
    RELEASE(buf); RELEASE(chain); RELEASE(f); RELEASE(ctx); RELEASE(dev);
#endif
}

/* ---- the app's view and its source (IInspectable objects) ------------ */
typedef struct { const void *const *vt; } Obj;
static HRESULT STDMETHODCALLTYPE o_iids(Obj *o, ULONG *n, GUID **g) { (void)o; *n = 0; *g = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE o_name(Obj *o, HSTRING *s) { (void)o; *s = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE o_trust(Obj *o, int *t) { (void)o; *t = 0; return S_OK; }
static ULONG STDMETHODCALLTYPE o_ref(Obj *o) { (void)o; return 2; }

static HRESULT STDMETHODCALLTYPE v_qi(Obj *o, const GUID *iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown_T) || IsEqualGUID(iid, &IID_IInspectable_T) || IsEqualGUID(iid, &IID_IFrameworkView_T)) { *out = o; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE v_initialize(Obj *o, void *app_view)
{
    (void)o;
    order('I');
    BYTE main = 0;
    VT(app_view, 9, Get_t)(app_view, &main);                        /* IsMain */
    CHECK("the view is the main view", main == 1);
    EventRegistrationToken t;
    CHECK("CoreApplicationView.Activated += ", VT(app_view, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void *, EventRegistrationToken *))
                                                (app_view, &h_activated, &t) == S_OK);
    void *cur = 0;
    CHECK("GetCurrentView: this view", VT(g_app, 12, Get_t)(g_app, &cur) == S_OK && cur == app_view);
    return S_OK;
}
typedef HRESULT (STDMETHODCALLTYPE *Add_t)(void *, void *, EventRegistrationToken *);
static HRESULT STDMETHODCALLTYPE v_set_window(Obj *o, void *win)
{
    (void)o;
    order('S');
    g_win = win;
    g_ui_tid = GetCurrentThreadId();
    EventRegistrationToken t;
    int ok = 1;
    ok &= VT(win, W_ADD_KEYDOWN, Add_t)(win, &h_keydown, &t) == S_OK;
    ok &= VT(win, W_ADD_KEYUP, Add_t)(win, &h_keyup, &t) == S_OK;
    ok &= VT(win, W_ADD_CHAR, Add_t)(win, &h_char, &t) == S_OK;
    ok &= VT(win, W_ADD_SIZE, Add_t)(win, &h_size, &t) == S_OK;
    ok &= VT(win, W_ADD_VISIBILITY, Add_t)(win, &h_visibility, &t) == S_OK;
    ok &= VT(win, W_ADD_CLOSED, Add_t)(win, &h_closed, &t) == S_OK;
    CHECK("CoreWindow events +=", ok);
    void *interop = 0;
    CHECK("ICoreWindowInterop", SUCCEEDED(VT(win, 0, QI_t)(win, &IID_ICoreWindowInterop_T, &interop)) && interop);
    if (interop) VT(interop, 3, Get_t)(interop, &g_hwnd);
    RELEASE(interop);
    WCHAR cls[64] = L"";
    GetClassNameW(g_hwnd, cls, 64);
    CHECK("its window: class Windows.UI.Core.CoreWindow", g_hwnd && !wcscmp(cls, L"Windows.UI.Core.CoreWindow"));
    CHECK("hidden until activated", !IsWindowVisible(g_hwnd));
    void *same = 0;
    VT(g_win_statics, 6, Get_t)(g_win_statics, &same);
    CHECK("CoreWindow.GetForCurrentThread: the window", same == win);
    RELEASE(same);
    VT(win, W_DISPATCHER, Get_t)(win, &g_disp);
    CHECK("its dispatcher", g_disp != 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE v_load(Obj *o, HSTRING entry) { (void)o; (void)entry; order('L'); return S_OK; }
static HRESULT STDMETHODCALLTYPE v_run(Obj *o)
{
    (void)o;
    order('R');
    BYTE b = 0;
    VT(g_win, W_VISIBLE, Get_t)(g_win, &b);
    CHECK("activated: visible", b == 1 && IsWindowVisible(g_hwnd));
    CHECK("VisibilityChanged (visible)", g_visible_events >= 1 && g_last_visible == 1);
    RectF r = { 0, 0, 0, 0 };
    VT(g_win, W_BOUNDS, Get_t)(g_win, &r);
    CHECK("Bounds", r.Width > 0 && r.Height > 0);
    VT(g_disp, 6, Get_t)(g_disp, &b);
    CHECK("HasThreadAccess on the window's thread", b == 1);

    PostMessageW(g_hwnd, WM_KEYDOWN, 'A', 0x001E0001);
    PostMessageW(g_hwnd, WM_CHAR, 'a', 0x001E0001);
    PostMessageW(g_hwnd, WM_KEYUP, 'A', 0xC01E0001);
    CHECK("ProcessEvents(ProcessAllIfPresent)", VT(g_disp, 7, HRESULT (STDMETHODCALLTYPE *)(void *, int))(g_disp, 3) == S_OK);
    CHECK("KeyDown: VirtualKey A, scan code 0x1E", g_keydown_vk == 'A' && g_scancode == 0x1E);
    CHECK("CharacterReceived: 'a'", g_char == 'a');
    CHECK("KeyUp: IsKeyReleased", g_keyup_released == 1);

    SetWindowPos(g_hwnd, 0, 0, 0, 520, 400, SWP_NOMOVE | SWP_NOZORDER);
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    float s = GetDpiForWindow(g_hwnd) / 96.0f;
    CHECK("SizeChanged: the client area in DIPs", g_size.Width > 0 && (int)(g_size.Width * s + 0.5f) == cr.right &&
                                                  (int)(g_size.Height * s + 0.5f) == cr.bottom);
    swap_chain();

    HANDLE t = CreateThread(0, 0, post_work, 0, 0, 0);
    WaitForSingleObject(t, 10000);
    CloseHandle(t);
    CHECK("the posted work waits for the dispatcher", g_work_ran == 0);
    CHECK("ProcessEvents(ProcessUntilQuit)", VT(g_disp, 7, HRESULT (STDMETHODCALLTYPE *)(void *, int))(g_disp, 2) == S_OK);
    CHECK("RunAsync's work ran on the window's thread", g_work_ran == 1 && g_ran_on_ui == 1);
    CHECK("its IAsyncAction completed", g_completed == 1);
    CHECK("Exit: Closed", g_closed == 1);
    CHECK("closed: hidden", !IsWindowVisible(g_hwnd));
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE v_uninitialize(Obj *o) { (void)o; order('U'); return S_OK; }
static const void *const view_vt[] = { (void *)v_qi, (void *)o_ref, (void *)o_ref, (void *)o_iids, (void *)o_name, (void *)o_trust,
                                       (void *)v_initialize, (void *)v_set_window, (void *)v_load, (void *)v_run, (void *)v_uninitialize };
static Obj g_view = { view_vt };

static HRESULT STDMETHODCALLTYPE s_qi(Obj *o, const GUID *iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown_T) || IsEqualGUID(iid, &IID_IInspectable_T) || IsEqualGUID(iid, &IID_IFrameworkViewSource_T)) { *out = o; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE s_create(Obj *o, void **view) { (void)o; order('C'); *view = &g_view; return S_OK; }
static const void *const source_vt[] = { (void *)s_qi, (void *)o_ref, (void *)o_ref, (void *)o_iids, (void *)o_name, (void *)o_trust, (void *)s_create };
static Obj g_source = { source_vt };

static DWORD WINAPI watchdog(void *arg)
{
    (void)arg;
    Sleep(90000);
    printf("FAIL: coreapptest hung (order %s)\n", g_order);
    fflush(stdout);
    ExitProcess(1);
}

static HRESULT factory(const WCHAR *cls, const GUID *iid, void **out)
{
    HSTRING s = 0;
    WindowsCreateString(cls, (UINT32)wcslen(cls), &s);
    HRESULT hr = RoGetActivationFactory(s, iid, out);
    WindowsDeleteString(s);
    return hr;
}

int main(void)
{
    CloseHandle(CreateThread(0, 0, watchdog, 0, 0, 0));
    CHECK("RoInitialize", SUCCEEDED(RoInitialize(RO_INIT_MULTITHREADED)));
    CHECK("CoreWindow's statics", factory(L"Windows.UI.Core.CoreWindow", &IID_ICoreWindowStatic_T, &g_win_statics) == S_OK && g_win_statics);
    void *none = (void *)1;
    if (g_win_statics) VT(g_win_statics, 6, Get_t)(g_win_statics, &none);
    CHECK("GetForCurrentThread before Run: null", none == 0);
    CHECK("CoreApplication (ICoreApplication)", factory(L"Windows.ApplicationModel.Core.CoreApplication", &IID_ICoreApplication_T, &g_app) == S_OK && g_app);
    void *junk = 0;
    CHECK("an unknown class: REGDB_E_CLASSNOTREG", factory(L"Windows.NovaOS.NoSuchClass", &IID_IInspectable_T, &junk) == REGDB_E_CLASSNOTREG);
    if (!g_app || !g_win_statics) { printf("coreapptest: %d passed, %d failed\n", pass, fail + 1); return 1; }
    CHECK("ICoreApplicationExit", SUCCEEDED(VT(g_app, 0, QI_t)(g_app, &IID_ICoreApplicationExit_T, &g_exit)) && g_exit);
    void *imm = 0, *main_view = 0;
    CHECK("ICoreImmersiveApplication", SUCCEEDED(VT(g_app, 0, QI_t)(g_app, &IID_ICoreImmersiveApplication_T, &imm)) && imm);
    if (imm) VT(imm, 8, Get_t)(imm, &main_view);                     /* MainView */
    CHECK("MainView", main_view != 0);
    RELEASE(main_view);
    RELEASE(imm);

    HRESULT hr = VT(g_app, 13, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(g_app, &g_source);   /* Run */
    CHECK("CoreApplication.Run", hr == S_OK);
    CHECK("the view's calls in order (CreateView, Initialize, SetWindow, Load, Activated, Run, Uninitialize)", !strcmp(g_order, "CISLARU"));
    CHECK("a launch", g_launch_kind == 0);
    CHECK("its Arguments: the command line's", !wcscmp(g_arguments, L"hello world"));
    void *after = (void *)1;
    VT(g_win_statics, 6, Get_t)(g_win_statics, &after);
    CHECK("GetForCurrentThread after Run: null", after == 0);
    CHECK("the window destroyed", !IsWindow(g_hwnd));
    RELEASE(g_exit);
    printf("coreapptest: %d passed, %d failed (%d-bit)\n", pass, fail, (int)sizeof(void *) * 8);
    return fail ? 1 : 0;
}
