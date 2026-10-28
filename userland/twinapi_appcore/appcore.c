/*
 * twinapi.appcore.dll — the UWP app model's core: CoreApplication
 * (Windows.ApplicationModel.Core), its one view, and that view's CoreWindow
 * and CoreDispatcher (Windows.UI.Core; Windows keeps those two in
 * Windows.UI.dll).  This is the layer Windows.UI.Xaml's Application.Start
 * and every DirectX UWP app stand on: the app hands CoreApplication.Run an
 * IFrameworkViewSource, and Run makes the view, gives it its window, and
 * calls its Initialize, SetWindow, Load, Run and Uninitialize in turn,
 * firing the view's Activated event (a launch) before Run.
 *
 * Programs reach the classes through RoGetActivationFactory: the registry's
 * ActivatableClassId keys (kernel/um/um_registry.c) name this DLL, and
 * DllGetActivationFactory answers for
 *   Windows.ApplicationModel.Core.CoreApplication  (ICoreApplication,
 *       ICoreApplication2, ICoreApplicationExit, ICoreImmersiveApplication)
 *   Windows.UI.Core.CoreWindow                     (ICoreWindowStatic)
 *   Windows.UI.Core.CoreCursor                     (ICoreCursorFactory)
 *
 * A CoreWindow is a top-level user32 window of class
 * "Windows.UI.Core.CoreWindow"; ICoreWindowInterop gives its HWND, which is
 * how DXGI's CreateSwapChainForCoreWindow finds where to present.  Its
 * dispatcher pumps the thread's messages (ProcessEvents) and turns them
 * into the window's events: Activated, CharacterReceived, Closed, KeyDown,
 * KeyUp, SizeChanged and VisibilityChanged, and from the mouse's messages
 * the pointer events (PointerEntered, PointerMoved, PointerPressed,
 * PointerReleased, PointerWheelChanged, PointerExited and
 * PointerCaptureLost), whose PointerEventArgs carry a
 * Windows.UI.Input.PointerPoint (pointer 1, a mouse: its position in the
 * window, timestamp, contact, and in its PointerPointProperties the
 * buttons held, the button that changed and the wheel's turn).  A second
 * button pressed while one is held is a PointerMoved, as on Windows.
 * RunAsync posts work to the window's thread.  The input-enabled, touch
 * hit testing and automation events accept handlers and are not raised
 * yet; there are no intermediate points (GetIntermediatePoints) and no
 * touch or pen pointers; sizes and positions are in device-independent
 * pixels (the window's DPI over 96).
 *
 * There is one view, the main one, as in an app that never calls
 * CreateNewView, with its Dispatcher and title bar (ICoreApplicationView2
 * and 3; WinUI 2's NavigationView follows the title bar).  A desktop thread that hosts XAML gets a hidden CoreWindow
 * from PrivateCreateCoreWindow, as XAML islands make on Windows.  The
 * application, view, window and dispatcher objects are static and agile,
 * so their reference counts are kept only for show; event arguments and
 * IAsyncActions are counted for real.
 *
 * Its other statics: DesignMode (never in a designer), PowerManager,
 * AnalyticsInfo (a desktop, Windows.Desktop, at the system's version),
 * MemoryManager (the process's commit against the system's), and
 * Windows.System.Threading's ThreadPool and ThreadPoolTimer (the process's
 * thread pool, its work items as IAsyncActions).
 */
#include <windows.h>
#include <roapi.h>

/* ntdll's RtlGetVersion, with its RTL_OSVERSIONINFOW */
typedef struct { ULONG size, major, minor, build, platform; WCHAR csd[128]; } VersionInfo;
__declspec(dllimport) LONG WINAPI RtlGetVersion(VersionInfo *info);

#define APPCORE_API __declspec(dllexport)
typedef BYTE boolean_t;
int _fltused = 0x9875;          /* sizes in DIPs are floats (the compiler references it) */

#define E_ILLEGAL_METHOD_CALL_ ((HRESULT)0x8000000EL)
#define RPC_E_WRONG_THREAD_    ((HRESULT)0x8001010EL)
#define WM_NOVA_DISPATCH       (WM_APP + 0x3A1)       /* RunAsync's work, posted to the window */

static const GUID IID_IUnknown_              = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_          = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_          = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IActivationFactory_    = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IAsyncInfo_            = { 0x00000036, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IIdleDispatchedHandlerArgs_ = { 0x98BB6A24, 0xDC1C, 0x43CB, { 0xB4, 0xED, 0xD1, 0xC0, 0xEB, 0x23, 0x91, 0xF3 } };
static const GUID IID_IAsyncAction_          = { 0x5A648006, 0x843A, 0x4DA9, { 0x86, 0x5B, 0x9D, 0x26, 0xE5, 0xDF, 0xAD, 0x7B } };
static const GUID IID_ICoreApplication_      = { 0x0AACF7A4, 0x5E1D, 0x49DF, { 0x80, 0x34, 0xFB, 0x6A, 0x68, 0xBC, 0x5E, 0xD1 } };
static const GUID IID_ICoreApplication2_     = { 0x998681FB, 0x1AB6, 0x4B7F, { 0xBE, 0x4A, 0x9A, 0x06, 0x45, 0x22, 0x4C, 0x04 } };
static const GUID IID_ICoreCursor_           = { 0x96893ACF, 0x111D, 0x442C, { 0x8A, 0x77, 0xB8, 0x79, 0x92, 0xF8, 0xE2, 0xD6 } };
static const GUID IID_ICoreCursorFactory_    = { 0xF6359621, 0xA79D, 0x4ED3, { 0x8C, 0x32, 0xA9, 0xEF, 0x9D, 0x6B, 0x76, 0xA4 } };
static const GUID IID_ICoreApplicationExit_  = { 0xCF86461D, 0x261E, 0x4B72, { 0x9A, 0xCD, 0x44, 0xED, 0x2A, 0xCE, 0x6A, 0x29 } };
static const GUID IID_ICoreImmersiveApplication_ = { 0x1ADA0E3E, 0xE4A2, 0x4123, { 0xB4, 0x51, 0xDC, 0x96, 0xBF, 0x80, 0x04, 0x19 } };
static const GUID IID_ICoreApplicationView_  = { 0x638BB2DB, 0x451D, 0x4661, { 0xB0, 0x99, 0x41, 0x4F, 0x34, 0xFF, 0xB9, 0xF1 } };
static const GUID IID_ICoreApplicationView2_ = { 0x68EB7ADF, 0x917F, 0x48EB, { 0x9A, 0xEB, 0x7D, 0xE5, 0x3E, 0x08, 0x6A, 0xB1 } };
static const GUID IID_ICoreApplicationView3_ = { 0x07EBE1B3, 0xA4CF, 0x4550, { 0xAB, 0x70, 0xB0, 0x7E, 0x85, 0x33, 0x0B, 0xC8 } };
static const GUID IID_ICoreApplicationViewTitleBar_ = { 0x006D35E3, 0xE1F1, 0x431B, { 0x95, 0x08, 0x29, 0xB9, 0x69, 0x26, 0xAC, 0x53 } };
static const GUID IID_ICoreWindow_           = { 0x79B9D5F2, 0x879E, 0x4B89, { 0xB7, 0x98, 0x79, 0xE4, 0x75, 0x98, 0x03, 0x0C } };
static const GUID IID_ICoreWindowStatic_     = { 0x4D239005, 0x3C2A, 0x41B1, { 0x90, 0x22, 0x53, 0x6B, 0xB9, 0xCF, 0x93, 0xB1 } };
static const GUID IID_ICoreWindow5_          = { 0x4B4AE1E1, 0x2E6D, 0x4EAA, { 0xBD, 0xA1, 0x1C, 0x5C, 0xC1, 0xBE, 0xE1, 0x41 } };
static const GUID IID_IWeakReferenceSource_  = { 0x00000038, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_IWeakReference_        = { 0x00000037, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_IDispatcherQueueStatics_ = { 0xA96D83D7, 0x9371, 0x4517, { 0x92, 0x45, 0xD0, 0x82, 0x4A, 0xC1, 0x2C, 0x74 } };
static const GUID IID_ICoreWindowInterop_    = { 0x45D64A29, 0xA63E, 0x4CB6, { 0xB4, 0x98, 0x57, 0x81, 0xD2, 0x98, 0xCB, 0x4F } };
static const GUID IID_ICoreDispatcher_       = { 0x60DB2FA8, 0xB705, 0x4FDE, { 0xA7, 0xD6, 0xEB, 0xBB, 0x18, 0x91, 0xD3, 0x9E } };
static const GUID IID_ICoreAcceleratorKeys_  = { 0x9FFDF7F5, 0xB8C9, 0x4EF0, { 0xB7, 0xD2, 0x1D, 0xE6, 0x26, 0x56, 0x1F, 0xC8 } };
static const GUID IID_ICoreWindowEventArgs_  = { 0x272B1EF3, 0xC633, 0x4DA5, { 0xA2, 0x6C, 0xC6, 0xD0, 0xF5, 0x6B, 0x29, 0xDA } };
static const GUID IID_IKeyEventArgs_         = { 0x5FF5E930, 0x2544, 0x4A17, { 0xBD, 0x78, 0x1F, 0x2F, 0xDE, 0xBB, 0x10, 0x6B } };
static const GUID IID_ICharacterReceivedEventArgs_ = { 0xC584659F, 0x99B2, 0x4BCC, { 0xBD, 0x33, 0x04, 0xE6, 0x3F, 0x42, 0x90, 0x2E } };
static const GUID IID_IWindowSizeChangedEventArgs_ = { 0x5A200EC7, 0x0426, 0x47DC, { 0xB8, 0x6C, 0x6F, 0x47, 0x59, 0x15, 0xE4, 0x51 } };
static const GUID IID_IVisibilityChangedEventArgs_ = { 0xBF9918EA, 0xD801, 0x4564, { 0xA4, 0x95, 0xB1, 0xE8, 0x4F, 0x8A, 0xD0, 0x85 } };
static const GUID IID_IWindowActivatedEventArgs_   = { 0x179D65E7, 0x4658, 0x4CB6, { 0xAA, 0x13, 0x41, 0xD0, 0x94, 0xEA, 0x25, 0x5E } };
static const GUID IID_IActivatedEventArgs_         = { 0xCF651713, 0xCD08, 0x4FD8, { 0xB6, 0x97, 0xA2, 0x81, 0xB6, 0x54, 0x4E, 0x2E } };
static const GUID IID_ILaunchActivatedEventArgs_   = { 0xFBC93E26, 0xA14A, 0x4B4F, { 0x82, 0xB0, 0x33, 0xBE, 0xD9, 0x20, 0xAF, 0x52 } };
static const GUID IID_IPointerEventArgs_           = { 0x920D9CB1, 0xA5FC, 0x4A21, { 0x8C, 0x09, 0x49, 0xDF, 0xE6, 0xFF, 0xE2, 0x5F } };
static const GUID IID_IPointerPoint_               = { 0xE995317D, 0x7296, 0x42D9, { 0x82, 0x33, 0xC5, 0xBE, 0x73, 0xB7, 0x4A, 0x4A } };
static const GUID IID_IPointerPointProperties_     = { 0xC79D8A4B, 0xC163, 0x4EE7, { 0x80, 0x3F, 0x67, 0xCE, 0x79, 0xF9, 0x97, 0x2D } };
static const GUID IID_IPointerDevice_              = { 0x93C9BAFC, 0xEBCB, 0x467E, { 0x82, 0xC6, 0x27, 0x6F, 0xEA, 0xE3, 0x6B, 0x5A } };

typedef struct { float X, Y; } PointF;
typedef struct { float Width, Height; } SizeF;
typedef struct { float X, Y, Width, Height; } RectF;
typedef struct { UINT32 RepeatCount, ScanCode; boolean_t IsExtendedKey, IsMenuKeyDown, WasKeyDown, IsKeyReleased; } KeyStatus;

/* Calls into the app's objects by vtable slot (IInspectable's six first) */
typedef HRESULT (STDMETHODCALLTYPE *Fn0)(void *);
typedef HRESULT (STDMETHODCALLTYPE *FnP)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *FnPP)(void *, void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *FnPI)(void *, void *, int);
#define SLOT(o, i) (((void **)*(void **)(o))[i])
static ULONG obj_addref(void *o) { return ((ULONG (STDMETHODCALLTYPE *)(void *))SLOT(o, 1))(o); }
static ULONG obj_release(void *o) { return ((ULONG (STDMETHODCALLTYPE *)(void *))SLOT(o, 2))(o); }

static HRESULT make_hstring(const WCHAR *s, HSTRING *out)
{
    if (!out) return E_POINTER;
    UINT32 n = 0;
    while (s[n]) n++;
    return WindowsCreateString(s, n, out);
}

/* IInspectable's own three, shared by every object here */
static HRESULT STDMETHODCALLTYPE no_iids(void *o, ULONG *n, GUID **iids) { (void)o; if (!n || !iids) return E_POINTER; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE base_trust(void *o, int *t) { (void)o; if (!t) return E_POINTER; *t = BaseTrust; return S_OK; }
static ULONG STDMETHODCALLTYPE static_ref(void *o) { (void)o; return 2; }
static BOOL is_base(REFIID iid)
{
    return IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_);
}
#define INSPECTABLE(T) \
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(T *, REFIID, void **); \
    ULONG (STDMETHODCALLTYPE *AddRef)(T *); \
    ULONG (STDMETHODCALLTYPE *Release)(T *); \
    HRESULT (STDMETHODCALLTYPE *GetIids)(T *, ULONG *, GUID **); \
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(T *, HSTRING *); \
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(T *, int *)
#define INSP_FNS(qi, name) (void *)(qi), (void *)static_ref, (void *)static_ref, (void *)no_iids, (void *)(name), (void *)base_trust

/* ---- event handler lists ----------------------------------------------
 * A handler (TypedEventHandler<S, A> or EventHandler<A>) is a delegate:
 * Invoke(sender, args) is the slot after IUnknown's three. */
typedef struct { INT64 token; void *handler; } Sub;
typedef struct { Sub *subs; UINT32 n, cap; } Event;
static SRWLOCK g_ev_lock = SRWLOCK_INIT;
static INT64 g_next_token;

static HRESULT ev_add(Event *e, void *h, EventRegistrationToken *tok)
{
    if (!h || !tok) return E_INVALIDARG;
    AcquireSRWLockExclusive(&g_ev_lock);
    if (e->n == e->cap) {
        UINT32 cap = e->cap ? e->cap * 2 : 4;
        Sub *s = e->subs ? HeapReAlloc(GetProcessHeap(), 0, e->subs, cap * sizeof *s) : HeapAlloc(GetProcessHeap(), 0, cap * sizeof *s);
        if (!s) { ReleaseSRWLockExclusive(&g_ev_lock); return E_OUTOFMEMORY; }
        e->subs = s;
        e->cap = cap;
    }
    obj_addref(h);
    tok->value = ++g_next_token;
    e->subs[e->n].token = tok->value;
    e->subs[e->n++].handler = h;
    ReleaseSRWLockExclusive(&g_ev_lock);
    return S_OK;
}
static HRESULT ev_remove(Event *e, EventRegistrationToken tok)
{
    void *h = 0;
    AcquireSRWLockExclusive(&g_ev_lock);
    for (UINT32 i = 0; i < e->n; i++)
        if (e->subs[i].token == tok.value) {
            h = e->subs[i].handler;
            e->subs[i] = e->subs[--e->n];
            break;
        }
    ReleaseSRWLockExclusive(&g_ev_lock);
    if (h) obj_release(h);
    return S_OK;                        /* (an unknown token is not an error on Windows) */
}
/* Calls each handler with (sender, args); handlers may add or remove
 * handlers meanwhile, so it calls a copy of the list */
static void ev_fire(Event *e, void *sender, void *args)
{
    void *local[16], **hs = local;
    AcquireSRWLockShared(&g_ev_lock);
    UINT32 n = e->n;
    if (n > 16) hs = HeapAlloc(GetProcessHeap(), 0, n * sizeof *hs);
    if (!hs) { ReleaseSRWLockShared(&g_ev_lock); return; }
    for (UINT32 i = 0; i < n; i++) { hs[i] = e->subs[i].handler; obj_addref(hs[i]); }
    ReleaseSRWLockShared(&g_ev_lock);
    for (UINT32 i = 0; i < n; i++) {
        ((FnPP)SLOT(hs[i], 3))(hs[i], sender, args);
        obj_release(hs[i]);
    }
    if (hs != local) HeapFree(GetProcessHeap(), 0, hs);
}

/* ---- event arguments --------------------------------------------------
 * One object for every CoreWindow event: its default interface (spec)
 * depends on the event, and ICoreWindowEventArgs (Handled) is beside it. */
enum { ARGS_PLAIN, ARGS_KEY, ARGS_CHAR, ARGS_SIZE, ARGS_VISIBLE, ARGS_ACTIVATED, ARGS_POINTER };
typedef struct Pt Pt;
typedef struct Args Args;
typedef struct { INSPECTABLE(Args); void *m[2]; } ArgsVtbl;
struct Args {
    const ArgsVtbl *spec;               /* IKeyEventArgs, IWindowSizeChangedEventArgs, ... */
    const ArgsVtbl *base;               /* ICoreWindowEventArgs */
    LONG refs;
    int kind;
    boolean_t handled;
    UINT32 key;                         /* virtual key or character */
    KeyStatus status;
    SizeF size;
    boolean_t visible;
    int state;                          /* CoreWindowActivationState */
    Pt *point;                          /* (counted) */
    int modifiers;                      /* VirtualKeyModifiers */
};
#define ARGS_OF_BASE(p) ((Args *)((char *)(p) - __builtin_offsetof(Args, base)))

static const GUID *args_iid(int kind)
{
    switch (kind) {
    case ARGS_KEY: return &IID_IKeyEventArgs_;
    case ARGS_CHAR: return &IID_ICharacterReceivedEventArgs_;
    case ARGS_SIZE: return &IID_IWindowSizeChangedEventArgs_;
    case ARGS_VISIBLE: return &IID_IVisibilityChangedEventArgs_;
    case ARGS_ACTIVATED: return &IID_IWindowActivatedEventArgs_;
    case ARGS_POINTER: return &IID_IPointerEventArgs_;
    }
    return &IID_ICoreWindowEventArgs_;
}
static HRESULT STDMETHODCALLTYPE args_qi(Args *a, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, args_iid(a->kind))) *out = &a->spec;
    else if (IsEqualGUID(iid, &IID_ICoreWindowEventArgs_)) *out = &a->base;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&a->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE args_addref(Args *a) { return (ULONG)InterlockedIncrement(&a->refs); }
static ULONG pt_release(Pt *p);
static ULONG STDMETHODCALLTYPE args_release(Args *a)
{
    LONG r = InterlockedDecrement(&a->refs);
    if (!r) {
        if (a->point) pt_release(a->point);
        HeapFree(GetProcessHeap(), 0, a);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE args_name(Args *a, HSTRING *n)
{
    static const WCHAR *const names[] = {
        L"Windows.UI.Core.CoreWindowEventArgs", L"Windows.UI.Core.KeyEventArgs",
        L"Windows.UI.Core.CharacterReceivedEventArgs", L"Windows.UI.Core.WindowSizeChangedEventArgs",
        L"Windows.UI.Core.VisibilityChangedEventArgs", L"Windows.UI.Core.WindowActivatedEventArgs",
        L"Windows.UI.Core.PointerEventArgs",
    };
    return make_hstring(names[a->kind], n);
}
static HRESULT STDMETHODCALLTYPE args_get_handled(Args *a, boolean_t *v) { if (!v) return E_POINTER; *v = a->handled; return S_OK; }
static HRESULT STDMETHODCALLTYPE args_put_handled(Args *a, boolean_t v) { a->handled = v ? 1 : 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE key_vk(Args *a, int *v) { if (!v) return E_POINTER; *v = (int)a->key; return S_OK; }
static HRESULT STDMETHODCALLTYPE char_code(Args *a, UINT32 *v) { if (!v) return E_POINTER; *v = a->key; return S_OK; }
static HRESULT STDMETHODCALLTYPE key_status(Args *a, KeyStatus *v) { if (!v) return E_POINTER; *v = a->status; return S_OK; }
static HRESULT STDMETHODCALLTYPE size_size(Args *a, SizeF *v) { if (!v) return E_POINTER; *v = a->size; return S_OK; }
static HRESULT STDMETHODCALLTYPE vis_visible(Args *a, boolean_t *v) { if (!v) return E_POINTER; *v = a->visible; return S_OK; }
static HRESULT STDMETHODCALLTYPE act_state(Args *a, int *v) { if (!v) return E_POINTER; *v = a->state; return S_OK; }
static HRESULT STDMETHODCALLTYPE ptr_point(Args *a, void **v);
static HRESULT STDMETHODCALLTYPE ptr_modifiers(Args *a, int *v) { if (!v) return E_POINTER; *v = a->modifiers; return S_OK; }
static HRESULT STDMETHODCALLTYPE ptr_intermediate(Args *a, void **v) { (void)a; if (v) *v = 0; return E_NOTIMPL; }

#define ARGS_FNS (void *)args_qi, (void *)args_addref, (void *)args_release, (void *)no_iids, (void *)args_name, (void *)base_trust
static const void *const args_base_vt[] = { ARGS_FNS, (void *)args_get_handled, (void *)args_put_handled };
static const void *const args_key_vt[] = { ARGS_FNS, (void *)key_vk, (void *)key_status };
static const void *const args_char_vt[] = { ARGS_FNS, (void *)char_code, (void *)key_status };
static const void *const args_size_vt[] = { ARGS_FNS, (void *)size_size };
static const void *const args_vis_vt[] = { ARGS_FNS, (void *)vis_visible };
static const void *const args_act_vt[] = { ARGS_FNS, (void *)act_state };
static const void *const args_ptr_vt[] = { ARGS_FNS, (void *)ptr_point, (void *)ptr_modifiers, (void *)ptr_intermediate };

/* The base interface's QueryInterface and counts are reached through the
 * Args the pointer sits in: a thunk table for the second vtable */
static HRESULT STDMETHODCALLTYPE argsb_qi(void *p, REFIID iid, void **out) { return args_qi(ARGS_OF_BASE(p), iid, out); }
static ULONG STDMETHODCALLTYPE argsb_addref(void *p) { return args_addref(ARGS_OF_BASE(p)); }
static ULONG STDMETHODCALLTYPE argsb_release(void *p) { return args_release(ARGS_OF_BASE(p)); }
static HRESULT STDMETHODCALLTYPE argsb_name(void *p, HSTRING *n) { return args_name(ARGS_OF_BASE(p), n); }
static HRESULT STDMETHODCALLTYPE argsb_get_handled(void *p, boolean_t *v) { return args_get_handled(ARGS_OF_BASE(p), v); }
static HRESULT STDMETHODCALLTYPE argsb_put_handled(void *p, boolean_t v) { return args_put_handled(ARGS_OF_BASE(p), v); }
static const void *const args_base2_vt[] = { (void *)argsb_qi, (void *)argsb_addref, (void *)argsb_release, (void *)no_iids,
                                             (void *)argsb_name, (void *)base_trust, (void *)argsb_get_handled, (void *)argsb_put_handled };

static Args *args_new(int kind)
{
    Args *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a);
    if (!a) return 0;
    static const void *const *const specs[] = { args_base_vt, args_key_vt, args_char_vt, args_size_vt, args_vis_vt, args_act_vt,
                                                args_ptr_vt };
    a->spec = (const ArgsVtbl *)specs[kind];
    a->base = (const ArgsVtbl *)args_base2_vt;
    a->refs = 1;
    a->kind = kind;
    return a;
}

/* ---- PointerPoint (Windows.UI.Input) ----------------------------------
 * One allocation for a PointerPoint and its PointerPointProperties (each
 * answers QueryInterface for its own interfaces; one count), and a static
 * PointerDevice: the mouse */
struct Pt {
    const void *const *vt;              /* IPointerPoint */
    const void *const *props;           /* IPointerPointProperties */
    LONG refs;
    PointF pos;
    UINT32 frame;
    UINT64 time;                        /* microseconds */
    boolean_t contact, left, right, middle, x1, x2, hwheel;
    INT32 wheel;
    int update;                         /* PointerUpdateKind */
};
#define PT_OF_PROPS(p) ((Pt *)((char *)(p) - __builtin_offsetof(Pt, props)))
static HRESULT STDMETHODCALLTYPE pt_qi(Pt *p, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!is_base(iid) && !IsEqualGUID(iid, &IID_IPointerPoint_)) { *out = 0; return E_NOINTERFACE; }
    *out = &p->vt;
    InterlockedIncrement(&p->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE pt_addref(Pt *p) { return (ULONG)InterlockedIncrement(&p->refs); }
static ULONG pt_release(Pt *p)
{
    LONG r = InterlockedDecrement(&p->refs);
    if (!r) HeapFree(GetProcessHeap(), 0, p);
    return (ULONG)r;
}
static ULONG STDMETHODCALLTYPE pt_release_m(Pt *p) { return pt_release(p); }
static HRESULT STDMETHODCALLTYPE pt_name(Pt *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.UI.Input.PointerPoint", n); }
static const void *const *const g_mouse_device;
static HRESULT STDMETHODCALLTYPE pt_device(Pt *p, void **v) { (void)p; if (!v) return E_POINTER; *v = (void *)&g_mouse_device; return S_OK; }
static HRESULT STDMETHODCALLTYPE pt_pos(Pt *p, PointF *v) { if (!v) return E_POINTER; *v = p->pos; return S_OK; }
static HRESULT STDMETHODCALLTYPE pt_id(Pt *p, UINT32 *v) { (void)p; if (!v) return E_POINTER; *v = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE pt_frame(Pt *p, UINT32 *v) { if (!v) return E_POINTER; *v = p->frame; return S_OK; }
static HRESULT STDMETHODCALLTYPE pt_time(Pt *p, UINT64 *v) { if (!v) return E_POINTER; *v = p->time; return S_OK; }
static HRESULT STDMETHODCALLTYPE pt_contact(Pt *p, boolean_t *v) { if (!v) return E_POINTER; *v = p->contact; return S_OK; }
static HRESULT STDMETHODCALLTYPE pt_props(Pt *p, void **v)
{
    if (!v) return E_POINTER;
    InterlockedIncrement(&p->refs);
    *v = &p->props;
    return S_OK;
}
static const void *const pt_vt[] = { (void *)pt_qi, (void *)pt_addref, (void *)pt_release_m, (void *)no_iids, (void *)pt_name,
                                     (void *)base_trust, (void *)pt_device, (void *)pt_pos, (void *)pt_pos /* RawPosition */,
                                     (void *)pt_id, (void *)pt_frame, (void *)pt_time, (void *)pt_contact, (void *)pt_props };
/* IPointerPointProperties, in the order of its declaration */
#define PP(p) PT_OF_PROPS(p)
static HRESULT STDMETHODCALLTYPE pp_qi(void *p, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!is_base(iid) && !IsEqualGUID(iid, &IID_IPointerPointProperties_)) { *out = 0; return E_NOINTERFACE; }
    *out = p;
    InterlockedIncrement(&PP(p)->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE pp_addref(void *p) { return pt_addref(PP(p)); }
static ULONG STDMETHODCALLTYPE pp_release(void *p) { return pt_release(PP(p)); }
static HRESULT STDMETHODCALLTYPE pp_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.UI.Input.PointerPointProperties", n); }
static HRESULT STDMETHODCALLTYPE pp_pressure(void *p, float *v) { if (!v) return E_POINTER; *v = PP(p)->contact ? 0.5f : 0.0f; return S_OK; }
static HRESULT STDMETHODCALLTYPE pp_false(void *p, boolean_t *v) { (void)p; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE pp_true(void *p, boolean_t *v) { (void)p; if (!v) return E_POINTER; *v = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE pp_zero(void *p, float *v) { (void)p; if (!v) return E_POINTER; *v = 0.0f; return S_OK; }
static HRESULT STDMETHODCALLTYPE pp_rect(void *p, RectF *v)
{
    if (!v) return E_POINTER;
    v->X = PP(p)->pos.X; v->Y = PP(p)->pos.Y; v->Width = v->Height = 0;
    return S_OK;
}
#define PP_BOOL(name, field) static HRESULT STDMETHODCALLTYPE pp_##name(void *p, boolean_t *v) { if (!v) return E_POINTER; *v = PP(p)->field; return S_OK; }
PP_BOOL(left, left) PP_BOOL(right, right) PP_BOOL(middle, middle) PP_BOOL(x1, x1) PP_BOOL(x2, x2) PP_BOOL(hwheel, hwheel)
static HRESULT STDMETHODCALLTYPE pp_wheel(void *p, INT32 *v) { if (!v) return E_POINTER; *v = PP(p)->wheel; return S_OK; }
static HRESULT STDMETHODCALLTYPE pp_update(void *p, int *v) { if (!v) return E_POINTER; *v = PP(p)->update; return S_OK; }
static HRESULT STDMETHODCALLTYPE pp_has_usage(void *p, UINT32 page, UINT32 id, boolean_t *v) { (void)p; (void)page; (void)id; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE pp_usage(void *p, UINT32 page, UINT32 id, INT32 *v) { (void)p; (void)page; (void)id; if (!v) return E_POINTER; *v = 0; return S_OK; }
static const void *const pp_vt[] = {
    (void *)pp_qi, (void *)pp_addref, (void *)pp_release, (void *)no_iids, (void *)pp_name, (void *)base_trust,
    (void *)pp_pressure, (void *)pp_false /* IsInverted */, (void *)pp_false /* IsEraser */, (void *)pp_zero /* Orientation */,
    (void *)pp_zero /* XTilt */, (void *)pp_zero /* YTilt */, (void *)pp_zero /* Twist */, (void *)pp_rect, (void *)pp_rect,
    (void *)pp_true /* TouchConfidence */, (void *)pp_left, (void *)pp_right, (void *)pp_middle, (void *)pp_wheel,
    (void *)pp_hwheel, (void *)pp_true /* IsPrimary */, (void *)pp_true /* IsInRange */, (void *)pp_false /* IsCanceled */,
    (void *)pp_false /* IsBarrelButtonPressed */, (void *)pp_x1, (void *)pp_x2, (void *)pp_update, (void *)pp_has_usage,
    (void *)pp_usage,
};
_Static_assert(sizeof pp_vt / sizeof pp_vt[0] == 6 + 24, "IPointerPointProperties has 24 methods");

/* IPointerDevice: the mouse (PointerDeviceType Mouse 2, one contact, the
 * whole screen) */
static HRESULT STDMETHODCALLTYPE dev_qi(void *p, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!is_base(iid) && !IsEqualGUID(iid, &IID_IPointerDevice_)) { *out = 0; return E_NOINTERFACE; }
    *out = p;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dev_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.Devices.Input.PointerDevice", n); }
static HRESULT STDMETHODCALLTYPE dev_type(void *p, int *v) { (void)p; if (!v) return E_POINTER; *v = 2; return S_OK; }
static HRESULT STDMETHODCALLTYPE dev_contacts(void *p, UINT32 *v) { (void)p; if (!v) return E_POINTER; *v = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE dev_screen(void *p, RectF *v)
{
    (void)p;
    if (!v) return E_POINTER;
    v->X = v->Y = 0;
    v->Width = (float)GetSystemMetrics(SM_CXSCREEN); v->Height = (float)GetSystemMetrics(SM_CYSCREEN);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dev_usages(void *p, void **v) { (void)p; if (v) *v = 0; return E_NOTIMPL; }
static const void *const dev_vt[] = { (void *)dev_qi, (void *)static_ref, (void *)static_ref, (void *)no_iids, (void *)dev_name,
                                      (void *)base_trust, (void *)dev_type, (void *)pp_true /* IsIntegrated */, (void *)dev_contacts,
                                      (void *)dev_screen, (void *)dev_screen, (void *)dev_usages };
static const void *const *const g_mouse_device = dev_vt;

static HRESULT STDMETHODCALLTYPE ptr_point(Args *a, void **v)
{
    if (!v) return E_POINTER;
    *v = a->point ? (void *)&a->point->vt : 0;
    if (a->point) pt_addref(a->point);
    return S_OK;
}

/* ---- IAsyncAction (RunAsync) ------------------------------------------ */
typedef struct Action Action;
struct Action {
    const void *const *op;              /* IAsyncAction */
    const void *const *info;            /* IAsyncInfo */
    LONG refs;
    SRWLOCK lock;
    int status;                         /* AsyncStatus: Started 0, Completed 1 */
    void *completed;                    /* AsyncActionCompletedHandler */
    void *work;                         /* DispatchedHandler, until it has run */
    UINT32 id;
    int idle;                           /* RunIdleAsync's: work is an IdleDispatchedHandler */
    int pool;                           /* ThreadPool.RunAsync's: work is a WorkItemHandler */
    int priority;                       /* (its thread's priority meanwhile) */
    volatile LONG cancel;               /* Cancel called (a pool item not yet started is skipped) */
    HRESULT error;                      /* the work's failure, with status Error 3 */
};
#define ACTION_OF_INFO(p) ((Action *)((char *)(p) - __builtin_offsetof(Action, info)))
static HRESULT STDMETHODCALLTYPE act_qi(Action *a, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IAsyncAction_)) *out = &a->op;
    else if (IsEqualGUID(iid, &IID_IAsyncInfo_)) *out = &a->info;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&a->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE act_addref(Action *a) { return (ULONG)InterlockedIncrement(&a->refs); }
static ULONG STDMETHODCALLTYPE act_release(Action *a)
{
    LONG r = InterlockedDecrement(&a->refs);
    if (!r) {
        if (a->completed) obj_release(a->completed);
        if (a->work) obj_release(a->work);
        HeapFree(GetProcessHeap(), 0, a);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE act_name(Action *a, HSTRING *n) { (void)a; return make_hstring(L"Windows.Foundation.IAsyncAction", n); }
/* A handler set after completion is called at once, as Windows does */
static HRESULT STDMETHODCALLTYPE act_put_completed(Action *a, void *h)
{
    if (!h) return E_INVALIDARG;
    AcquireSRWLockExclusive(&a->lock);
    if (a->completed) { ReleaseSRWLockExclusive(&a->lock); return E_ILLEGAL_METHOD_CALL_; }
    obj_addref(h);
    a->completed = h;
    int done = a->status != 0;
    ReleaseSRWLockExclusive(&a->lock);
    if (done) ((FnPI)SLOT(h, 3))(h, &a->op, a->status);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE act_get_completed(Action *a, void **h)
{
    if (!h) return E_POINTER;
    AcquireSRWLockShared(&a->lock);
    *h = a->completed;
    if (*h) obj_addref(*h);
    ReleaseSRWLockShared(&a->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE act_results(Action *a) { return a->status == 1 ? S_OK : a->status == 3 ? a->error : E_ILLEGAL_METHOD_CALL_; }
static HRESULT STDMETHODCALLTYPE ai_qi(void *p, REFIID iid, void **out) { return act_qi(ACTION_OF_INFO(p), iid, out); }
static ULONG STDMETHODCALLTYPE ai_addref(void *p) { return act_addref(ACTION_OF_INFO(p)); }
static ULONG STDMETHODCALLTYPE ai_release(void *p) { return act_release(ACTION_OF_INFO(p)); }
static HRESULT STDMETHODCALLTYPE ai_name(void *p, HSTRING *n) { return act_name(ACTION_OF_INFO(p), n); }
static HRESULT STDMETHODCALLTYPE ai_id(void *p, UINT32 *v) { if (!v) return E_POINTER; *v = ACTION_OF_INFO(p)->id; return S_OK; }
/* (a pool item's work polls it: Canceled 2 once Cancel was called) */
static HRESULT STDMETHODCALLTYPE ai_status(void *p, int *v)
{
    if (!v) return E_POINTER;
    Action *a = ACTION_OF_INFO(p);
    *v = a->pool && a->cancel && a->status == 0 ? 2 : a->status;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ai_error(void *p, HRESULT *v) { if (!v) return E_POINTER; *v = ACTION_OF_INFO(p)->error; return S_OK; }
/* (a dispatcher's work runs anyway once posted; a pool item's is skipped
 * if it has not started, and a running one can watch Status) */
static HRESULT STDMETHODCALLTYPE ai_cancel(void *p) { InterlockedExchange(&ACTION_OF_INFO(p)->cancel, 1); return S_OK; }
static HRESULT STDMETHODCALLTYPE ai_close(void *p) { (void)p; return S_OK; }
static const void *const action_vt[] = { (void *)act_qi, (void *)act_addref, (void *)act_release, (void *)no_iids, (void *)act_name,
                                         (void *)base_trust, (void *)act_put_completed, (void *)act_get_completed, (void *)act_results };
static const void *const action_info_vt[] = { (void *)ai_qi, (void *)ai_addref, (void *)ai_release, (void *)no_iids, (void *)ai_name,
                                              (void *)base_trust, (void *)ai_id, (void *)ai_status, (void *)ai_error, (void *)ai_cancel, (void *)ai_close };

/* IdleDispatchedHandlerArgs: the work runs when the window's queue holds
 * nothing else (posted behind what is queued), so the dispatcher is idle */
static HRESULT STDMETHODCALLTYPE idle_qi(void *p, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IIdleDispatchedHandlerArgs_)) { *out = p; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE idle_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.UI.Core.IdleDispatchedHandlerArgs", n); }
static HRESULT STDMETHODCALLTYPE idle_is_idle(void *p, boolean_t *v) { (void)p; if (!v) return E_POINTER; *v = TRUE; return S_OK; }
static const void *const idle_args_vt[] = { INSP_FNS(idle_qi, idle_name), (void *)idle_is_idle };
static const void *const *g_idle_args = idle_args_vt;

/* Runs the work on the window's thread, then completes the action */
static void action_run(Action *a)
{
    void *work = a->work;
    a->work = 0;
    if (work) {
        if (a->idle)                                    /* IdleDispatchedHandler::Invoke(args) */
            ((HRESULT (STDMETHODCALLTYPE *)(void *, void *))SLOT(work, 3))(work, &g_idle_args);
        else
            ((Fn0)SLOT(work, 3))(work);                 /* DispatchedHandler::Invoke() */
        obj_release(work);
    }
    AcquireSRWLockExclusive(&a->lock);
    a->status = 1;
    void *h = a->completed;
    if (h) obj_addref(h);
    ReleaseSRWLockExclusive(&a->lock);
    if (h) { ((FnPI)SLOT(h, 3))(h, &a->op, 1); obj_release(h); }
    act_release(a);                                     /* (the posted message's reference) */
}

/* ---- the CoreWindow and its dispatcher ------------------------------- */
enum { EV_ACTIVATED, EV_AUTOMATION, EV_CHAR, EV_CLOSED, EV_INPUT_ENABLED, EV_KEYDOWN, EV_KEYUP,
       EV_PTR_CAPTURE_LOST, EV_PTR_ENTERED, EV_PTR_EXITED, EV_PTR_MOVED, EV_PTR_PRESSED, EV_PTR_RELEASED,
       EV_TOUCH_HIT, EV_PTR_WHEEL, EV_SIZE, EV_VISIBILITY, EV_COUNT };
typedef struct Win {
    const void *const *vt;              /* ICoreWindow */
    const void *const *interop;         /* ICoreWindowInterop */
    const void *const *win5;            /* ICoreWindow5 */
    const void *const *wrs;             /* IWeakReferenceSource */
    const void *const *weak;            /* IWeakReference */
    HWND hwnd;
    DWORD tid;
    Event ev[EV_COUNT];
    boolean_t input_enabled, message_handled, cursor_hidden, closed, visible, pointer_inside;
    WPARAM buttons;                     /* the mouse buttons held (MK_*) */
    UINT32 frames;
    int flow;
    UINT32 next_action;
} Win;
typedef struct { const void *const *vt; const void *const *keys; Event accel; } Dispatcher;
static Win g_win;
static Dispatcher g_disp;
#define WIN_OF_INTEROP(p) ((Win *)((char *)(p) - __builtin_offsetof(Win, interop)))

static float win_scale(void)
{
    UINT dpi = g_win.hwnd ? GetDpiForWindow(g_win.hwnd) : 96;
    return dpi ? dpi / 96.0f : 1.0f;
}

static HRESULT STDMETHODCALLTYPE win_qi(Win *w, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_ICoreWindow_)) *out = &w->vt;
    else if (IsEqualGUID(iid, &IID_ICoreWindowInterop_)) *out = &w->interop;
    else if (IsEqualGUID(iid, &IID_ICoreWindow5_)) *out = &w->win5;
    else if (IsEqualGUID(iid, &IID_IWeakReferenceSource_)) *out = &w->wrs;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE win_name(Win *w, HSTRING *n) { (void)w; return make_hstring(L"Windows.UI.Core.CoreWindow", n); }
static HRESULT STDMETHODCALLTYPE win_automation(Win *w, void **v) { (void)w; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE win_bounds(Win *w, RectF *r)
{
    if (!r) return E_POINTER;
    RECT c = { 0, 0, 0, 0 };
    POINT o = { 0, 0 };
    if (w->hwnd) { GetClientRect(w->hwnd, &c); ClientToScreen(w->hwnd, &o); }
    float s = win_scale();
    r->X = o.x / s; r->Y = o.y / s;
    r->Width = (c.right - c.left) / s; r->Height = (c.bottom - c.top) / s;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE win_custom_props(Win *w, void **v) { (void)w; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE win_dispatcher(Win *w, void **v) { (void)w; if (!v) return E_POINTER; *v = &g_disp.vt; return S_OK; }
static HRESULT STDMETHODCALLTYPE win_get_flow(Win *w, int *v) { if (!v) return E_POINTER; *v = w->flow; return S_OK; }
static HRESULT STDMETHODCALLTYPE win_put_flow(Win *w, int v) { w->flow = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE win_get_input(Win *w, boolean_t *v) { if (!v) return E_POINTER; *v = w->input_enabled; return S_OK; }
static HRESULT STDMETHODCALLTYPE win_put_input(Win *w, boolean_t v) { w->input_enabled = v ? 1 : 0; return S_OK; }
/* PointerCursor: null hides the pointer over the window (games do that);
 * any CoreCursor shows the arrow */
static HRESULT STDMETHODCALLTYPE win_get_cursor(Win *w, void **v) { (void)w; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE win_put_cursor(Win *w, void *c)
{
    w->cursor_hidden = !c;
    SetCursor(c ? LoadCursorW(0, (LPCWSTR)IDC_ARROW) : 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE win_ptr_pos(Win *w, PointF *p)
{
    (void)w;
    if (!p) return E_POINTER;
    POINT c = { 0, 0 };
    GetCursorPos(&c);
    float s = win_scale();
    p->X = c.x / s; p->Y = c.y / s;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE win_visible(Win *w, boolean_t *v)
{
    if (!v) return E_POINTER;
    *v = w->hwnd && IsWindowVisible(w->hwnd) && !IsIconic(w->hwnd);
    return S_OK;
}
static void raise_visible(BOOL v);
static HRESULT STDMETHODCALLTYPE win_activate(Win *w)
{
    if (!w->hwnd) return E_ILLEGAL_METHOD_CALL_;
    if (!IsWindowVisible(w->hwnd)) ShowWindow(w->hwnd, SW_SHOW);
    raise_visible(IsWindowVisible(w->hwnd) && !IsIconic(w->hwnd));    /* (whether or not WM_SHOWWINDOW came) */
    SetForegroundWindow(w->hwnd);
    UpdateWindow(w->hwnd);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE win_close(Win *w)
{
    if (w->hwnd && !w->closed) SendMessageW(w->hwnd, WM_CLOSE, 0, 0);
    return S_OK;
}
static int key_states(SHORT s) { return (s & 0x8000 ? 1 : 0) | (s & 1 ? 2 : 0); }     /* CoreVirtualKeyStates: Down 1, Locked 2 */
static HRESULT STDMETHODCALLTYPE win_async_key(Win *w, int vk, int *st) { (void)w; if (!st) return E_POINTER; *st = key_states(GetAsyncKeyState(vk)); return S_OK; }
static HRESULT STDMETHODCALLTYPE win_key(Win *w, int vk, int *st) { (void)w; if (!st) return E_POINTER; *st = key_states(GetKeyState(vk)); return S_OK; }
static HRESULT STDMETHODCALLTYPE win_release_capture(Win *w) { (void)w; ReleaseCapture(); return S_OK; }
static HRESULT STDMETHODCALLTYPE win_set_capture(Win *w) { if (w->hwnd) SetCapture(w->hwnd); return S_OK; }

#define EVENT_PAIR(n) \
    static HRESULT STDMETHODCALLTYPE win_add_##n(Win *w, void *h, EventRegistrationToken *t) { return ev_add(&w->ev[n], h, t); } \
    static HRESULT STDMETHODCALLTYPE win_remove_##n(Win *w, EventRegistrationToken t) { return ev_remove(&w->ev[n], t); }
EVENT_PAIR(EV_ACTIVATED) EVENT_PAIR(EV_AUTOMATION) EVENT_PAIR(EV_CHAR) EVENT_PAIR(EV_CLOSED) EVENT_PAIR(EV_INPUT_ENABLED)
EVENT_PAIR(EV_KEYDOWN) EVENT_PAIR(EV_KEYUP) EVENT_PAIR(EV_PTR_CAPTURE_LOST) EVENT_PAIR(EV_PTR_ENTERED) EVENT_PAIR(EV_PTR_EXITED)
EVENT_PAIR(EV_PTR_MOVED) EVENT_PAIR(EV_PTR_PRESSED) EVENT_PAIR(EV_PTR_RELEASED) EVENT_PAIR(EV_TOUCH_HIT) EVENT_PAIR(EV_PTR_WHEEL)
EVENT_PAIR(EV_SIZE) EVENT_PAIR(EV_VISIBILITY)
#define EVENT_FNS(n) (void *)win_add_##n, (void *)win_remove_##n

/* ICoreWindow, in the order of its declaration */
static const void *const win_vt[] = {
    INSP_FNS(win_qi, win_name),
    (void *)win_automation, (void *)win_bounds, (void *)win_custom_props, (void *)win_dispatcher,
    (void *)win_get_flow, (void *)win_put_flow, (void *)win_get_input, (void *)win_put_input,
    (void *)win_get_cursor, (void *)win_put_cursor, (void *)win_ptr_pos, (void *)win_visible,
    (void *)win_activate, (void *)win_close, (void *)win_async_key, (void *)win_key,
    (void *)win_release_capture, (void *)win_set_capture,
    EVENT_FNS(EV_ACTIVATED), EVENT_FNS(EV_AUTOMATION), EVENT_FNS(EV_CHAR), EVENT_FNS(EV_CLOSED),
    EVENT_FNS(EV_INPUT_ENABLED), EVENT_FNS(EV_KEYDOWN), EVENT_FNS(EV_KEYUP), EVENT_FNS(EV_PTR_CAPTURE_LOST),
    EVENT_FNS(EV_PTR_ENTERED), EVENT_FNS(EV_PTR_EXITED), EVENT_FNS(EV_PTR_MOVED), EVENT_FNS(EV_PTR_PRESSED),
    EVENT_FNS(EV_PTR_RELEASED), EVENT_FNS(EV_TOUCH_HIT), EVENT_FNS(EV_PTR_WHEEL), EVENT_FNS(EV_SIZE),
    EVENT_FNS(EV_VISIBILITY),
};
_Static_assert(sizeof win_vt / sizeof win_vt[0] == 6 + 18 + 2 * EV_COUNT, "ICoreWindow has 58 slots");

/* ICoreWindowInterop (IUnknown): the HWND, and whether the app handled
 * the message it is being shown */
static HRESULT STDMETHODCALLTYPE wi_qi(void *p, REFIID iid, void **out) { return win_qi(WIN_OF_INTEROP(p), iid, out); }
static HRESULT STDMETHODCALLTYPE wi_hwnd(void *p, HWND *h) { if (!h) return E_POINTER; *h = WIN_OF_INTEROP(p)->hwnd; return S_OK; }
static HRESULT STDMETHODCALLTYPE wi_handled(void *p, boolean_t v) { WIN_OF_INTEROP(p)->message_handled = v ? 1 : 0; return S_OK; }
static const void *const win_interop_vt[] = { (void *)wi_qi, (void *)static_ref, (void *)static_ref, (void *)wi_hwnd, (void *)wi_handled };

/* ICoreWindow5: the thread's DispatcherQueue (Windows.System's, from
 * CoreMessaging) and how the window is activated (XAML's focus observer
 * asks) */
#define WIN_OF_WIN5(p) ((Win *)((char *)(p) - __builtin_offsetof(Win, win5)))
static HRESULT STDMETHODCALLTYPE w5_qi(void *p, REFIID iid, void **out) { return win_qi(WIN_OF_WIN5(p), iid, out); }
static HRESULT STDMETHODCALLTYPE w5_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.UI.Core.CoreWindow", n); }
static HRESULT STDMETHODCALLTYPE w5_queue(void *p, void **v)
{
    (void)p;
    if (!v) return E_POINTER;
    *v = 0;
    HSTRING name = 0;
    void *statics = 0;
    HRESULT hr = make_hstring(L"Windows.System.DispatcherQueue", &name);
    if (SUCCEEDED(hr))
        hr = RoGetActivationFactory(name, &IID_IDispatcherQueueStatics_, &statics);
    WindowsDeleteString(name);
    if (FAILED(hr))
        return hr;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))SLOT(statics, 6))(statics, v);   /* GetForCurrentThread */
    obj_release(statics);
    return hr;
}
/* CoreWindowActivationMode: None until the window exists, then
 * ActivatedInForeground while it is the foreground window, else Deactivated */
static HRESULT STDMETHODCALLTYPE w5_mode(void *p, int *v)
{
    Win *w = WIN_OF_WIN5(p);
    if (!v) return E_POINTER;
    *v = !w->hwnd ? 0 : GetForegroundWindow() == w->hwnd ? 3 : 1;
    return S_OK;
}
static const void *const win5_vt[] = { INSP_FNS(w5_qi, w5_name), (void *)w5_queue, (void *)w5_mode };

/* IWeakReferenceSource and its IWeakReference (C++/WinRT's event revokers
 * hold the window weakly): the window resolves while it exists */
#define WIN_OF_WRS(p) ((Win *)((char *)(p) - __builtin_offsetof(Win, wrs)))
#define WIN_OF_WEAK(p) ((Win *)((char *)(p) - __builtin_offsetof(Win, weak)))
static HRESULT STDMETHODCALLTYPE wrs_qi(void *p, REFIID iid, void **out) { return win_qi(WIN_OF_WRS(p), iid, out); }
static HRESULT STDMETHODCALLTYPE wrs_get(void *p, void **v) { if (!v) return E_POINTER; *v = &WIN_OF_WRS(p)->weak; return S_OK; }
static HRESULT STDMETHODCALLTYPE weak_qi(void *p, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IWeakReference_)) { *out = p; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE weak_resolve(void *p, REFIID iid, void **v)
{
    Win *w = WIN_OF_WEAK(p);
    if (!v) return E_POINTER;
    *v = 0;
    return w->hwnd ? win_qi(w, iid, v) : S_OK;
}
static const void *const win_wrs_vt[] = { (void *)wrs_qi, (void *)static_ref, (void *)static_ref, (void *)wrs_get };
static const void *const win_weak_vt[] = { (void *)weak_qi, (void *)static_ref, (void *)static_ref, (void *)weak_resolve };

static Win g_win = { (const void *const *)win_vt, (const void *const *)win_interop_vt, (const void *const *)win5_vt,
                     (const void *const *)win_wrs_vt, (const void *const *)win_weak_vt };

/* ICoreDispatcher */
static HRESULT STDMETHODCALLTYPE disp_qi(Dispatcher *d, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_ICoreDispatcher_)) *out = &d->vt;
    else if (IsEqualGUID(iid, &IID_ICoreAcceleratorKeys_)) *out = &d->keys;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE disp_name(Dispatcher *d, HSTRING *n) { (void)d; return make_hstring(L"Windows.UI.Core.CoreDispatcher", n); }
static HRESULT STDMETHODCALLTYPE disp_access(Dispatcher *d, boolean_t *v)
{
    (void)d;
    if (!v) return E_POINTER;
    *v = g_win.hwnd && GetCurrentThreadId() == g_win.tid;
    return S_OK;
}
static void dispatch(MSG *m)
{
    TranslateMessage(m);
    DispatchMessageW(m);
}
/* CoreProcessEventsOption: ProcessOneAndAllPending 0 (wait for one, then
 * the rest), ProcessOneIfPresent 1, ProcessUntilQuit 2 (until the window
 * closes), ProcessAllIfPresent 3 */
static HRESULT STDMETHODCALLTYPE disp_process(Dispatcher *d, int option)
{
    (void)d;
    if (!g_win.hwnd || GetCurrentThreadId() != g_win.tid) return RPC_E_WRONG_THREAD_;
    if (option < 0 || option > 3) return E_INVALIDARG;
    MSG m;
    if (option == 2) {
        while (!g_win.closed) {
            BOOL r = GetMessageW(&m, 0, 0, 0);
            if (r <= 0) { if (!r) PostQuitMessage((int)m.wParam); break; }
            dispatch(&m);
        }
        return S_OK;
    }
    if (option == 0 && !g_win.closed) {
        BOOL r = GetMessageW(&m, 0, 0, 0);
        if (r <= 0) { if (!r) PostQuitMessage((int)m.wParam); return S_OK; }
        dispatch(&m);
    }
    while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); break; }
        dispatch(&m);
        if (option == 1) break;
    }
    return S_OK;
}
/* Posts @work to the window as an IAsyncAction (@idle: an
 * IdleDispatchedHandler's) */
static HRESULT post_action(void *work, int idle, void **out)
{
    if (!work || !out) return E_INVALIDARG;
    *out = 0;
    if (!g_win.hwnd) return E_ILLEGAL_METHOD_CALL_;
    Action *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a);
    if (!a) return E_OUTOFMEMORY;
    a->op = action_vt;
    a->info = action_info_vt;
    a->refs = 2;                        /* the caller's and the posted message's */
    InitializeSRWLock(&a->lock);
    a->id = (UINT32)InterlockedIncrement((LONG *)&g_win.next_action);
    obj_addref(work);
    a->work = work;
    a->idle = idle;
    if (!PostMessageW(g_win.hwnd, WM_NOVA_DISPATCH, 0, (LPARAM)a)) {
        HeapFree(GetProcessHeap(), 0, a);
        obj_release(work);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    *out = &a->op;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE disp_run_async(Dispatcher *d, int priority, void *work, void **out)
{
    (void)d; (void)priority;
    return post_action(work, 0, out);
}
/* RunIdleAsync: the work posted as RunAsync's, its handler given
 * IdleDispatchedHandlerArgs */
static HRESULT STDMETHODCALLTYPE disp_run_idle(Dispatcher *d, void *work, void **out)
{
    (void)d;
    return post_action(work, 1, out);
}
static const void *const disp_vt[] = { INSP_FNS(disp_qi, disp_name), (void *)disp_access, (void *)disp_process,
                                       (void *)disp_run_async, (void *)disp_run_idle };
/* ICoreAcceleratorKeys: AcceleratorKeyActivated accepts handlers */
#define DISP_OF_KEYS(p) ((Dispatcher *)((char *)(p) - __builtin_offsetof(Dispatcher, keys)))
static HRESULT STDMETHODCALLTYPE keys_qi(void *p, REFIID iid, void **out) { return disp_qi(DISP_OF_KEYS(p), iid, out); }
static HRESULT STDMETHODCALLTYPE keys_name(void *p, HSTRING *n) { return disp_name(DISP_OF_KEYS(p), n); }
static HRESULT STDMETHODCALLTYPE keys_add(void *p, void *h, EventRegistrationToken *t) { return ev_add(&DISP_OF_KEYS(p)->accel, h, t); }
static HRESULT STDMETHODCALLTYPE keys_remove(void *p, EventRegistrationToken t) { return ev_remove(&DISP_OF_KEYS(p)->accel, t); }
static const void *const keys_vt[] = { INSP_FNS(keys_qi, keys_name), (void *)keys_add, (void *)keys_remove };
static Dispatcher g_disp = { (const void *const *)disp_vt, (const void *const *)keys_vt };

/* Raises a window event with a fresh argument object; TRUE when a handler
 * set Handled */
static BOOL raise_event(int ev, Args *a)
{
    if (!a) return FALSE;
    ev_fire(&g_win.ev[ev], &g_win.vt, &a->spec);
    BOOL handled = a->handled;
    args_release(a);
    return handled;
}
static KeyStatus key_status_of(LPARAM lp)
{
    KeyStatus s;
    s.RepeatCount = (UINT32)(lp & 0xFFFF);
    s.ScanCode = (UINT32)((lp >> 16) & 0xFF);
    s.IsExtendedKey = (lp >> 24) & 1;
    s.IsMenuKeyDown = (lp >> 29) & 1;
    s.WasKeyDown = (lp >> 30) & 1;
    s.IsKeyReleased = (lp >> 31) & 1;
    return s;
}
static void raise_visible(BOOL v)
{
    if (g_win.visible == (v ? 1 : 0)) return;
    g_win.visible = v ? 1 : 0;
    Args *a = args_new(ARGS_VISIBLE);
    if (a) a->visible = g_win.visible;
    raise_event(EV_VISIBILITY, a);
}

/* A pointer event from a mouse message; TRUE when a handler set Handled */
#define MK_ALL (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2)
static BOOL raise_one_pointer(int ev, int update, POINT at, WPARAM held, INT32 wheel, BOOL hwheel)
{
    Args *a = args_new(ARGS_POINTER);
    Pt *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *p);
    if (!a || !p) { if (a) args_release(a); HeapFree(GetProcessHeap(), 0, p); return FALSE; }
    p->vt = pt_vt;
    p->props = pp_vt;
    p->refs = 1;
    float s = win_scale();
    p->pos.X = at.x / s; p->pos.Y = at.y / s;
    p->frame = ++g_win.frames;
    p->time = (UINT64)GetMessageTime() * 1000;
    p->left = !!(held & MK_LBUTTON); p->right = !!(held & MK_RBUTTON); p->middle = !!(held & MK_MBUTTON);
    p->x1 = !!(held & MK_XBUTTON1); p->x2 = !!(held & MK_XBUTTON2);
    p->contact = (held & MK_ALL) != 0;
    p->wheel = wheel; p->hwheel = hwheel ? 1 : 0;
    p->update = update;
    a->point = p;
    a->modifiers = (GetKeyState(VK_CONTROL) < 0 ? 1 : 0) | (GetKeyState(VK_MENU) < 0 ? 2 : 0) | (GetKeyState(VK_SHIFT) < 0 ? 4 : 0) |
                   (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0 ? 8 : 0);
    return raise_event(ev, a);
}
static BOOL raise_pointer(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    POINT at = { (short)LOWORD(lp), (short)HIWORD(lp) };
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) ScreenToClient(h, &at);     /* (a wheel's place is the screen's) */
    if (msg == WM_MOUSELEAVE || msg == WM_CAPTURECHANGED) {
        POINT c;
        GetCursorPos(&c);
        ScreenToClient(h, &c);
        at = c;
    }
    WPARAM held = msg == WM_MOUSELEAVE || msg == WM_CAPTURECHANGED ? g_win.buttons : (LOWORD(wp) & MK_ALL);
    if (msg == WM_MOUSELEAVE) {
        g_win.pointer_inside = 0;
        return raise_one_pointer(EV_PTR_EXITED, 0, at, held, 0, FALSE);
    }
    if (msg == WM_CAPTURECHANGED) return (HWND)lp != h && raise_one_pointer(EV_PTR_CAPTURE_LOST, 0, at, held, 0, FALSE);
    if (!g_win.pointer_inside) {                                                     /* (entered: watch for leaving) */
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
        g_win.pointer_inside = 1;
        TrackMouseEvent(&t);
        raise_one_pointer(EV_PTR_ENTERED, 0, at, held, 0, FALSE);
    }
    WPARAM before = g_win.buttons;
    g_win.buttons = held;
    int ev = EV_PTR_MOVED, update = 0;                                               /* PointerUpdateKind Other */
    switch (msg) {
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        return raise_one_pointer(EV_PTR_WHEEL, 0, at, held, (short)HIWORD(wp), msg == WM_MOUSEHWHEEL);
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: update = 1; break;
    case WM_LBUTTONUP: update = 2; break;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: update = 3; break;
    case WM_RBUTTONUP: update = 4; break;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: update = 5; break;
    case WM_MBUTTONUP: update = 6; break;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: update = HIWORD(wp) == XBUTTON2 ? 9 : 7; break;
    case WM_XBUTTONUP: update = HIWORD(wp) == XBUTTON2 ? 10 : 8; break;
    }
    if (update && (update & 1) && !(before & MK_ALL)) ev = EV_PTR_PRESSED;         /* (the first button down) */
    if (update && !(update & 1) && !(held & MK_ALL)) ev = EV_PTR_RELEASED;         /* (the last one up) */
    return raise_one_pointer(ev, update, at, held, 0, FALSE);
}

static LRESULT CALLBACK win_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Args *a;
    switch (msg) {
    case WM_NOVA_DISPATCH:
        action_run((Action *)lp);
        return 0;
    case WM_ACTIVATE:
        if ((a = args_new(ARGS_ACTIVATED)))
            a->state = LOWORD(wp) == WA_INACTIVE ? 1 : LOWORD(wp) == WA_CLICKACTIVE ? 2 : 0;
        raise_event(EV_ACTIVATED, a);
        break;
    case WM_SHOWWINDOW:
        raise_visible(wp != 0);
        break;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) { raise_visible(FALSE); return 0; }
        raise_visible(IsWindowVisible(h));
        if ((a = args_new(ARGS_SIZE))) {
            float s = win_scale();
            a->size.Width = LOWORD(lp) / s;
            a->size.Height = HIWORD(lp) / s;
        }
        raise_event(EV_SIZE, a);
        return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
        if ((a = args_new(ARGS_KEY))) { a->key = (UINT32)wp; a->status = key_status_of(lp); }
        g_win.message_handled = 0;
        if (raise_event(msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN ? EV_KEYDOWN : EV_KEYUP, a) || g_win.message_handled) return 0;
        break;
    case WM_CHAR: case WM_SYSCHAR:
        if ((a = args_new(ARGS_CHAR))) { a->key = (UINT32)wp; a->status = key_status_of(lp); }
        if (raise_event(EV_CHAR, a)) return 0;
        break;
    case WM_MOUSEMOVE: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_MOUSELEAVE: case WM_CAPTURECHANGED:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        if (raise_pointer(h, msg, wp, lp)) return msg >= WM_XBUTTONDOWN && msg <= WM_XBUTTONDBLCLK ? TRUE : 0;
        break;
    case WM_SETCURSOR:
        if (g_win.cursor_hidden && LOWORD(lp) == HTCLIENT) { SetCursor(0); return TRUE; }
        break;
    case WM_ERASEBKGND:
        return 1;                       /* (the app draws the whole window) */
    case WM_CLOSE:
        /* Closed, then the window goes away from the screen; the HWND
         * itself stays until CoreApplication.Run returns, since the app
         * may still be presenting to it */
        if (!g_win.closed) {
            g_win.closed = 1;
            raise_event(EV_CLOSED, args_new(ARGS_PLAIN));
            ShowWindow(h, SW_HIDE);
            raise_visible(FALSE);
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HRESULT create_window(void)
{
    static const WCHAR cls[] = L"Windows.UI.Core.CoreWindow";
    WNDCLASSEXW wc;
    __builtin_memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = win_proc;
    wc.hInstance = GetModuleHandleW(L"twinapi.appcore.dll");
    wc.hCursor = LoadCursorW(0, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = cls;
    HICON icon = LoadIconW(GetModuleHandleW(0), MAKEINTRESOURCEW(1));
    wc.hIcon = icon;
    RegisterClassExW(&wc);
    /* the title: the program's file name without .exe (the package's
     * display name, once packages are installed) */
    WCHAR path[MAX_PATH], *title = path;
    DWORD n = GetModuleFileNameW(0, path, MAX_PATH);
    if (n >= MAX_PATH) n = MAX_PATH - 1;
    path[n] = 0;
    for (WCHAR *p = path; *p; p++) if (*p == '\\' || *p == '/') title = p + 1;
    for (WCHAR *p = title; *p; p++) if (p[0] == '.' && (p[1] | 0x20) == 'e' && (p[2] | 0x20) == 'x' && (p[3] | 0x20) == 'e' && !p[4]) *p = 0;
    g_win.closed = 0;
    g_win.visible = 0;
    g_win.pointer_inside = 0;
    g_win.buttons = 0;
    g_win.input_enabled = 1;
    g_win.tid = GetCurrentThreadId();
    g_win.hwnd = CreateWindowExW(0, cls, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                 0, 0, wc.hInstance, 0);
    return g_win.hwnd ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

/* PrivateCreateCoreWindow (Windows: Windows.UI.dll's ordinal 1500): a
 * hidden CoreWindow for the calling thread, the one XAML islands make on a
 * desktop thread, so CoreWindow.GetForCurrentThread answers there too
 * (WinUI 2's AnimatedVisualPlayer and SwipeControl ask it).  It is never
 * shown; the thread's message loop pumps it.  One CoreWindow per process */
APPCORE_API HRESULT WINAPI PrivateCreateCoreWindow(int type, LPCWSTR title, int x, int y, UINT w, UINT h,
                                                   DWORD attrs, HWND owner, REFIID iid, void **out)
{
    (void)type; (void)title; (void)x; (void)y; (void)w; (void)h; (void)attrs; (void)owner;
    if (!out) return E_POINTER;
    *out = 0;
    if (g_win.hwnd) return GetCurrentThreadId() == g_win.tid ? win_qi(&g_win, iid, out) : E_ILLEGAL_METHOD_CALL_;
    HRESULT hr = create_window();
    return SUCCEEDED(hr) ? win_qi(&g_win, iid, out) : hr;
}

/* ---- the view (CoreApplicationView) and the launch's arguments --------
 * The one view: ICoreApplicationView, 2 (its Dispatcher, the CoreWindow's)
 * and 3 (not a component; its title bar: the system's, visible, the view
 * not extended into it) */
typedef struct { const void *const *vt, *const *vt2, *const *vt3; Event activated, hosted_closing; } View;
static View g_view;
typedef struct { const void *const *vt, *const *wrs, *const *weak; boolean_t extend; Event metrics, visible; } TitleBar;
static TitleBar g_title;
static HRESULT STDMETHODCALLTYPE view_qi(View *v, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_ICoreApplicationView_)) { *out = &v->vt; return S_OK; }
    if (IsEqualGUID(iid, &IID_ICoreApplicationView2_)) { *out = &v->vt2; return S_OK; }
    if (IsEqualGUID(iid, &IID_ICoreApplicationView3_)) { *out = &v->vt3; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE view_name(View *v, HSTRING *n) { (void)v; return make_hstring(L"Windows.ApplicationModel.Core.CoreApplicationView", n); }
static HRESULT STDMETHODCALLTYPE view_window(View *v, void **w) { (void)v; if (!w) return E_POINTER; *w = g_win.hwnd ? (void *)&g_win.vt : 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE view_add_activated(View *v, void *h, EventRegistrationToken *t) { return ev_add(&v->activated, h, t); }
static HRESULT STDMETHODCALLTYPE view_remove_activated(View *v, EventRegistrationToken t) { return ev_remove(&v->activated, t); }
static HRESULT STDMETHODCALLTYPE view_is_main(View *v, boolean_t *b) { (void)v; if (!b) return E_POINTER; *b = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE view_is_hosted(View *v, boolean_t *b) { (void)v; if (!b) return E_POINTER; *b = 0; return S_OK; }
static const void *const view_vt[] = { INSP_FNS(view_qi, view_name), (void *)view_window, (void *)view_add_activated,
                                       (void *)view_remove_activated, (void *)view_is_main, (void *)view_is_hosted };
static HRESULT STDMETHODCALLTYPE view2_qi(void *p, REFIID iid, void **out) { (void)p; return view_qi(&g_view, iid, out); }
static HRESULT STDMETHODCALLTYPE view2_name(void *p, HSTRING *n) { (void)p; return view_name(&g_view, n); }
static HRESULT STDMETHODCALLTYPE view2_dispatcher(void *p, void **d) { (void)p; if (!d) return E_POINTER; *d = &g_disp.vt; return S_OK; }
static const void *const view2_vt[] = { INSP_FNS(view2_qi, view2_name), (void *)view2_dispatcher };
static HRESULT STDMETHODCALLTYPE view3_component(void *p, boolean_t *b) { (void)p; if (!b) return E_POINTER; *b = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE view3_title(void *p, void **t) { (void)p; if (!t) return E_POINTER; *t = &g_title.vt; return S_OK; }
static HRESULT STDMETHODCALLTYPE view3_add_closing(void *p, void *h, EventRegistrationToken *t) { (void)p; return ev_add(&g_view.hosted_closing, h, t); }
static HRESULT STDMETHODCALLTYPE view3_remove_closing(void *p, EventRegistrationToken t) { (void)p; return ev_remove(&g_view.hosted_closing, t); }
static const void *const view3_vt[] = { INSP_FNS(view2_qi, view2_name), (void *)view3_component, (void *)view3_title,
                                        (void *)view3_add_closing, (void *)view3_remove_closing };
static View g_view = { (const void *const *)view_vt, (const void *const *)view2_vt, (const void *const *)view3_vt };

/* CoreApplicationViewTitleBar: the system's title bar over the view (its
 * window's caption, 32 pixels, no system overlay insets); extending the
 * view into it is remembered, nothing else changes, so neither of its
 * events is raised */
static HRESULT STDMETHODCALLTYPE title_qi(TitleBar *t, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_ICoreApplicationViewTitleBar_)) { *out = &t->vt; return S_OK; }
    if (IsEqualGUID(iid, &IID_IWeakReferenceSource_)) { *out = &t->wrs; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE title_name(TitleBar *t, HSTRING *n) { (void)t; return make_hstring(L"Windows.ApplicationModel.Core.CoreApplicationViewTitleBar", n); }
static HRESULT STDMETHODCALLTYPE title_put_extend(TitleBar *t, boolean_t b) { t->extend = b != 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE title_get_extend(TitleBar *t, boolean_t *b) { if (!b) return E_POINTER; *b = t->extend; return S_OK; }
static HRESULT STDMETHODCALLTYPE title_inset(TitleBar *t, double *v) { (void)t; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE title_height(TitleBar *t, double *v) { (void)t; if (!v) return E_POINTER; *v = 32; return S_OK; }
static HRESULT STDMETHODCALLTYPE title_add_metrics(TitleBar *t, void *h, EventRegistrationToken *k) { return ev_add(&t->metrics, h, k); }
static HRESULT STDMETHODCALLTYPE title_remove_metrics(TitleBar *t, EventRegistrationToken k) { return ev_remove(&t->metrics, k); }
static HRESULT STDMETHODCALLTYPE title_visible(TitleBar *t, boolean_t *b) { (void)t; if (!b) return E_POINTER; *b = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE title_add_visible(TitleBar *t, void *h, EventRegistrationToken *k) { return ev_add(&t->visible, h, k); }
static HRESULT STDMETHODCALLTYPE title_remove_visible(TitleBar *t, EventRegistrationToken k) { return ev_remove(&t->visible, k); }
static const void *const title_vt[] = { INSP_FNS(title_qi, title_name), (void *)title_put_extend, (void *)title_get_extend,
                                        (void *)title_inset, (void *)title_inset, (void *)title_height,
                                        (void *)title_add_metrics, (void *)title_remove_metrics, (void *)title_visible,
                                        (void *)title_add_visible, (void *)title_remove_visible };
/* (and weakly held, as C++/WinRT's event revokers hold it: it always resolves) */
static HRESULT STDMETHODCALLTYPE title_wrs_qi(void *p, REFIID iid, void **out) { (void)p; return title_qi(&g_title, iid, out); }
static HRESULT STDMETHODCALLTYPE title_wrs_get(void *p, void **v) { (void)p; if (!v) return E_POINTER; *v = &g_title.weak; return S_OK; }
static HRESULT STDMETHODCALLTYPE title_weak_qi(void *p, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IWeakReference_)) { *out = p; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE title_resolve(void *p, REFIID iid, void **v) { (void)p; return title_qi(&g_title, iid, v); }
static const void *const title_wrs_vt[] = { (void *)title_wrs_qi, (void *)static_ref, (void *)static_ref, (void *)title_wrs_get };
static const void *const title_weak_vt[] = { (void *)title_weak_qi, (void *)static_ref, (void *)static_ref, (void *)title_resolve };
static TitleBar g_title = { (const void *const *)title_vt, (const void *const *)title_wrs_vt, (const void *const *)title_weak_vt };

/* LaunchActivatedEventArgs: Kind Launch (0), PreviousExecutionState
 * NotRunning (0), no splash screen; Arguments are the command line after
 * the program's name (ApplicationActivationManager, activation.c, puts the
 * activation's arguments there), TileId the packaged application's id
 * ("App" for a program outside a package) */
typedef struct { const void *const *act; const void *const *launch; LONG refs; HSTRING args; } Launch;
#define LAUNCH_OF_L(p) ((Launch *)((char *)(p) - __builtin_offsetof(Launch, launch)))
static HRESULT STDMETHODCALLTYPE la_qi(Launch *l, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivatedEventArgs_)) *out = &l->act;
    else if (IsEqualGUID(iid, &IID_ILaunchActivatedEventArgs_)) *out = &l->launch;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&l->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE la_addref(Launch *l) { return (ULONG)InterlockedIncrement(&l->refs); }
static ULONG STDMETHODCALLTYPE la_release(Launch *l)
{
    LONG r = InterlockedDecrement(&l->refs);
    if (!r) { WindowsDeleteString(l->args); HeapFree(GetProcessHeap(), 0, l); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE la_name(Launch *l, HSTRING *n) { (void)l; return make_hstring(L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs", n); }
static HRESULT STDMETHODCALLTYPE la_kind(Launch *l, int *v) { (void)l; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE la_prev(Launch *l, int *v) { (void)l; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE la_splash(Launch *l, void **v) { (void)l; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE ll_qi(void *p, REFIID iid, void **out) { return la_qi(LAUNCH_OF_L(p), iid, out); }
static ULONG STDMETHODCALLTYPE ll_addref(void *p) { return la_addref(LAUNCH_OF_L(p)); }
static ULONG STDMETHODCALLTYPE ll_release(void *p) { return la_release(LAUNCH_OF_L(p)); }
static HRESULT STDMETHODCALLTYPE ll_name(void *p, HSTRING *n) { return la_name(LAUNCH_OF_L(p), n); }
static HRESULT STDMETHODCALLTYPE ll_arguments(void *p, HSTRING *v) { if (!v) return E_POINTER; return WindowsDuplicateString(LAUNCH_OF_L(p)->args, v); }
LONG WINAPI GetCurrentApplicationUserModelId(UINT32 *len, PWSTR id);
static HRESULT STDMETHODCALLTYPE ll_tile(void *p, HSTRING *v)
{
    (void)p;
    WCHAR id[200];
    UINT32 n = 200;
    const WCHAR *app = L"App";
    if (!GetCurrentApplicationUserModelId(&n, id))
        for (const WCHAR *s = id; *s; s++) if (*s == '!') app = s + 1;
    return make_hstring(app, v);
}
static const void *const launch_act_vt[] = { (void *)la_qi, (void *)la_addref, (void *)la_release, (void *)no_iids, (void *)la_name,
                                             (void *)base_trust, (void *)la_kind, (void *)la_prev, (void *)la_splash };
static const void *const launch_vt[] = { (void *)ll_qi, (void *)ll_addref, (void *)ll_release, (void *)no_iids, (void *)ll_name,
                                         (void *)base_trust, (void *)ll_arguments, (void *)ll_tile };

static Launch *launch_new(void)
{
    Launch *l = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *l);
    if (!l) return 0;
    l->act = launch_act_vt;
    l->launch = launch_vt;
    l->refs = 1;
    /* the command line after the program's (possibly quoted) name */
    const WCHAR *c = GetCommandLineW();
    if (*c == '"') { c++; while (*c && *c != '"') c++; if (*c) c++; }
    else while (*c && *c != ' ' && *c != '\t') c++;
    while (*c == ' ' || *c == '\t') c++;
    make_hstring(c, &l->args);
    return l;
}

/* ---- CoreApplication (its activation factory carries the statics) ----- */
typedef struct {
    const void *const *factory;         /* IActivationFactory */
    const void *const *app;             /* ICoreApplication */
    const void *const *exit;            /* ICoreApplicationExit */
    const void *const *immersive;       /* ICoreImmersiveApplication */
    const void *const *app2;            /* ICoreApplication2 */
    Event suspending, resuming, exiting, background, leaving, entered;
    LONG running;
} App;
static App g_app;
static HRESULT STDMETHODCALLTYPE app_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &g_app.factory;
    else if (IsEqualGUID(iid, &IID_ICoreApplication_)) *out = &g_app.app;
    else if (IsEqualGUID(iid, &IID_ICoreApplicationExit_)) *out = &g_app.exit;
    else if (IsEqualGUID(iid, &IID_ICoreImmersiveApplication_)) *out = &g_app.immersive;
    else if (IsEqualGUID(iid, &IID_ICoreApplication2_)) *out = &g_app.app2;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE app_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.ApplicationModel.Core.CoreApplication", n); }
static HRESULT STDMETHODCALLTYPE app_activate(void *p, void **out) { (void)p; if (out) *out = 0; return E_NOTIMPL; }   /* (static class) */
static HRESULT STDMETHODCALLTYPE app_id(void *p, HSTRING *v) { (void)p; return make_hstring(L"App", v); }
static HRESULT STDMETHODCALLTYPE app_add_suspending(void *p, void *h, EventRegistrationToken *t) { (void)p; return ev_add(&g_app.suspending, h, t); }
static HRESULT STDMETHODCALLTYPE app_remove_suspending(void *p, EventRegistrationToken t) { (void)p; return ev_remove(&g_app.suspending, t); }
static HRESULT STDMETHODCALLTYPE app_add_resuming(void *p, void *h, EventRegistrationToken *t) { (void)p; return ev_add(&g_app.resuming, h, t); }
static HRESULT STDMETHODCALLTYPE app_remove_resuming(void *p, EventRegistrationToken t) { (void)p; return ev_remove(&g_app.resuming, t); }
static HRESULT STDMETHODCALLTYPE app_properties(void *p, void **v) { (void)p; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE app_current_view(void *p, void **v)
{
    (void)p;
    if (!v) return E_POINTER;
    if (!g_win.hwnd || GetCurrentThreadId() != g_win.tid) { *v = 0; return E_FAIL; }   /* not on the view's thread */
    *v = &g_view.vt;
    return S_OK;
}

/* Run: the app's view, start to finish */
static HRESULT STDMETHODCALLTYPE app_run(void *p, void *source)
{
    (void)p;
    if (!source) return E_INVALIDARG;
    if (InterlockedCompareExchange(&g_app.running, 1, 0)) return E_ILLEGAL_METHOD_CALL_;
    CoInitializeEx(0, COINIT_APARTMENTTHREADED);        /* (the view's thread is an STA; RPC_E_CHANGED_MODE is fine) */
    void *view = 0;
    HRESULT hr = ((FnP)SLOT(source, 6))(source, &view);                  /* IFrameworkViewSource::CreateView */
    if (SUCCEEDED(hr) && !view) hr = E_POINTER;
    if (SUCCEEDED(hr)) hr = create_window();
    if (SUCCEEDED(hr)) {
        OutputDebugStringA("twinapi.appcore: CoreApplication.Run\n");
        hr = ((FnP)SLOT(view, 6))(view, &g_view.vt);                     /* Initialize(applicationView) */
        if (SUCCEEDED(hr)) hr = ((FnP)SLOT(view, 7))(view, &g_win.vt);   /* SetWindow(window) */
        if (SUCCEEDED(hr)) hr = ((FnP)SLOT(view, 8))(view, 0);           /* Load(entryPoint: none) */
        if (SUCCEEDED(hr)) {
            Launch *l = launch_new();
            if (l) { ev_fire(&g_view.activated, &g_view.vt, &l->act); la_release(l); }
            hr = ((Fn0)SLOT(view, 9))(view);                             /* Run() */
        }
        ((Fn0)SLOT(view, 10))(view);                                     /* Uninitialize() */
        ev_fire(&g_app.exiting, 0, 0);
        /* the posted work nobody will run still holds its references */
        MSG m;
        while (PeekMessageW(&m, g_win.hwnd, WM_NOVA_DISPATCH, WM_NOVA_DISPATCH, PM_REMOVE)) action_run((Action *)m.lParam);
        DestroyWindow(g_win.hwnd);
        g_win.hwnd = 0;
    }
    if (view) obj_release(view);
    InterlockedExchange(&g_app.running, 0);
    return hr;
}
static HRESULT STDMETHODCALLTYPE app_run_factories(void *p, void *f) { (void)p; (void)f; return E_NOTIMPL; }

/* ICoreApplicationExit: Exit closes the main window, which ends the view */
static HRESULT STDMETHODCALLTYPE exit_exit(void *p)
{
    (void)p;
    if (g_win.hwnd) PostMessageW(g_win.hwnd, WM_CLOSE, 0, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE exit_add(void *p, void *h, EventRegistrationToken *t) { (void)p; return ev_add(&g_app.exiting, h, t); }
static HRESULT STDMETHODCALLTYPE exit_remove(void *p, EventRegistrationToken t) { (void)p; return ev_remove(&g_app.exiting, t); }

/* ICoreImmersiveApplication: the main view (no others) */
static HRESULT STDMETHODCALLTYPE imm_views(void *p, void **v) { (void)p; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE imm_new_view(void *p, HSTRING type, HSTRING entry, void **v) { (void)p; (void)type; (void)entry; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE imm_main_view(void *p, void **v) { (void)p; if (!v) return E_POINTER; *v = &g_view.vt; return S_OK; }

/* ICoreApplication2: the background events (none is raised: no background
 * tasks, and the app stays in the foreground) and prelaunch (ignored) */
static HRESULT STDMETHODCALLTYPE app2_add_background(void *p, void *h, EventRegistrationToken *t) { (void)p; return ev_add(&g_app.background, h, t); }
static HRESULT STDMETHODCALLTYPE app2_remove_background(void *p, EventRegistrationToken t) { (void)p; return ev_remove(&g_app.background, t); }
static HRESULT STDMETHODCALLTYPE app2_add_leaving(void *p, void *h, EventRegistrationToken *t) { (void)p; return ev_add(&g_app.leaving, h, t); }
static HRESULT STDMETHODCALLTYPE app2_remove_leaving(void *p, EventRegistrationToken t) { (void)p; return ev_remove(&g_app.leaving, t); }
static HRESULT STDMETHODCALLTYPE app2_add_entered(void *p, void *h, EventRegistrationToken *t) { (void)p; return ev_add(&g_app.entered, h, t); }
static HRESULT STDMETHODCALLTYPE app2_remove_entered(void *p, EventRegistrationToken t) { (void)p; return ev_remove(&g_app.entered, t); }
static HRESULT STDMETHODCALLTYPE app2_prelaunch(void *p, boolean_t v) { (void)p; (void)v; return S_OK; }

static const void *const app_factory_vt[] = { INSP_FNS(app_qi, app_name), (void *)app_activate };
static const void *const app_vt[] = { INSP_FNS(app_qi, app_name), (void *)app_id, (void *)app_add_suspending, (void *)app_remove_suspending,
                                      (void *)app_add_resuming, (void *)app_remove_resuming, (void *)app_properties,
                                      (void *)app_current_view, (void *)app_run, (void *)app_run_factories };
static const void *const app_exit_vt[] = { INSP_FNS(app_qi, app_name), (void *)exit_exit, (void *)exit_add, (void *)exit_remove };
static const void *const app_immersive_vt[] = { INSP_FNS(app_qi, app_name), (void *)imm_views, (void *)imm_new_view, (void *)imm_main_view };
static const void *const app2_vt[] = { INSP_FNS(app_qi, app_name), (void *)app2_add_background, (void *)app2_remove_background,
                                       (void *)app2_add_leaving, (void *)app2_remove_leaving, (void *)app2_add_entered,
                                       (void *)app2_remove_entered, (void *)app2_prelaunch };
static App g_app = { (const void *const *)app_factory_vt, (const void *const *)app_vt,
                     (const void *const *)app_exit_vt, (const void *const *)app_immersive_vt, (const void *const *)app2_vt };

/* ---- CoreCursor: a cursor type and id (a CoreWindow shows the arrow for any) */
typedef struct { const void *const *vt; LONG refs; INT32 type; UINT32 id; } Cursor;
static HRESULT STDMETHODCALLTYPE cur_qi(Cursor *c, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!is_base(iid) && !IsEqualGUID(iid, &IID_ICoreCursor_)) { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&c->refs);
    *out = c;
    return S_OK;
}
static ULONG STDMETHODCALLTYPE cur_ref(Cursor *c) { return InterlockedIncrement(&c->refs); }
static ULONG STDMETHODCALLTYPE cur_release(Cursor *c)
{
    LONG n = InterlockedDecrement(&c->refs);
    if (!n) HeapFree(GetProcessHeap(), 0, c);
    return n;
}
static HRESULT STDMETHODCALLTYPE cur_name(Cursor *c, HSTRING *n) { (void)c; return make_hstring(L"Windows.UI.Core.CoreCursor", n); }
static HRESULT STDMETHODCALLTYPE cur_id(Cursor *c, UINT32 *v) { if (!v) return E_POINTER; *v = c->id; return S_OK; }
static HRESULT STDMETHODCALLTYPE cur_type(Cursor *c, INT32 *v) { if (!v) return E_POINTER; *v = c->type; return S_OK; }
static const void *const cur_vt[] = { (void *)cur_qi, (void *)cur_ref, (void *)cur_release, (void *)no_iids, (void *)cur_name,
                                      (void *)base_trust, (void *)cur_id, (void *)cur_type };

typedef struct { const void *const *factory; const void *const *cursors; } CursorStatics;
static CursorStatics g_cursor_statics;
static HRESULT STDMETHODCALLTYPE cs_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &g_cursor_statics.factory;
    else if (IsEqualGUID(iid, &IID_ICoreCursorFactory_)) *out = &g_cursor_statics.cursors;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cs_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.UI.Core.CoreCursor", n); }
static HRESULT STDMETHODCALLTYPE cs_activate(void *p, void **out) { (void)p; if (out) *out = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE cs_create(void *p, INT32 type, UINT32 id, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    Cursor *c = HeapAlloc(GetProcessHeap(), 0, sizeof *c);
    if (!c) { *out = 0; return E_OUTOFMEMORY; }
    c->vt = cur_vt; c->refs = 1; c->type = type; c->id = id;
    *out = c;
    return S_OK;
}
static const void *const cs_factory_vt[] = { INSP_FNS(cs_qi, cs_name), (void *)cs_activate };
static const void *const cs_cursors_vt[] = { INSP_FNS(cs_qi, cs_name), (void *)cs_create };
static CursorStatics g_cursor_statics = { (const void *const *)cs_factory_vt, (const void *const *)cs_cursors_vt };

/* ---- CoreWindow's statics: GetForCurrentThread ------------------------ */
typedef struct { const void *const *factory; const void *const *statics; } WinStatics;
static WinStatics g_win_statics;
static HRESULT STDMETHODCALLTYPE ws_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &g_win_statics.factory;
    else if (IsEqualGUID(iid, &IID_ICoreWindowStatic_)) *out = &g_win_statics.statics;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ws_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.UI.Core.CoreWindow", n); }
static HRESULT STDMETHODCALLTYPE ws_activate(void *p, void **out) { (void)p; if (out) *out = 0; return E_NOTIMPL; }
/* a thread with no CoreWindow gets null, as on Windows */
static HRESULT STDMETHODCALLTYPE ws_for_thread(void *p, void **w)
{
    (void)p;
    if (!w) return E_POINTER;
    *w = g_win.hwnd && GetCurrentThreadId() == g_win.tid ? (void *)&g_win.vt : 0;
    return S_OK;
}
static const void *const ws_factory_vt[] = { INSP_FNS(ws_qi, ws_name), (void *)ws_activate };
static const void *const ws_statics_vt[] = { INSP_FNS(ws_qi, ws_name), (void *)ws_for_thread };
static WinStatics g_win_statics = { (const void *const *)ws_factory_vt, (const void *const *)ws_statics_vt };

/* ---- DesignMode: whether the code runs in a XAML designer (never here;
 * Windows serves it from Windows.ApplicationModel.dll) ------------------ */
static const GUID IID_IDesignModeStatics_  = { 0x2C3893CC, 0xF81A, 0x4E7A, { 0xB8, 0x57, 0x76, 0xA8, 0x08, 0x87, 0xE1, 0x85 } };
static const GUID IID_IDesignModeStatics2_ = { 0x80CF8137, 0xB064, 0x4858, { 0xBE, 0xC8, 0x3E, 0xBA, 0x22, 0x35, 0x75, 0x35 } };
typedef struct { const void *const *factory; const void *const *statics; const void *const *statics2; } DesignMode;
static DesignMode g_design;
static HRESULT STDMETHODCALLTYPE dm_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &g_design.factory;
    else if (IsEqualGUID(iid, &IID_IDesignModeStatics_)) *out = &g_design.statics;
    else if (IsEqualGUID(iid, &IID_IDesignModeStatics2_)) *out = &g_design.statics2;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dm_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.ApplicationModel.DesignMode", n); }
static HRESULT STDMETHODCALLTYPE dm_activate(void *p, void **o) { (void)p; if (o) *o = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE dm_enabled(void *p, boolean_t *v) { (void)p; if (!v) return E_POINTER; *v = 0; return S_OK; }
static const void *const dm_factory_vt[] = { INSP_FNS(dm_qi, dm_name), (void *)dm_activate };
static const void *const dm_statics_vt[] = { INSP_FNS(dm_qi, dm_name), (void *)dm_enabled };
static DesignMode g_design = { (const void *const *)dm_factory_vt, (const void *const *)dm_statics_vt,
                               (const void *const *)dm_statics_vt };

/* ---- PowerManager: the battery and power supply, from
 * GetSystemPowerStatus (Windows serves it from Windows.System.Power; WinUI
 * 2's acrylic asks EnergySaverStatus).  The Changed events take handlers
 * and are not raised yet ------------------------------------------------ */
static const GUID IID_IPowerManagerStatics_ = { 0x1394825D, 0x62CE, 0x4364, { 0x98, 0xD5, 0xAA, 0x28, 0xC7, 0xFB, 0xD1, 0x5B } };
enum { PM_SAVER, PM_BATTERY, PM_SUPPLY, PM_PERCENT, PM_DISCHARGE, PM_COUNT };
typedef struct { const void *const *factory; const void *const *statics; Event ev[PM_COUNT]; } PowerManager;
static PowerManager g_power;
static HRESULT STDMETHODCALLTYPE pm_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &g_power.factory;
    else if (IsEqualGUID(iid, &IID_IPowerManagerStatics_)) *out = &g_power.statics;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pm_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.System.Power.PowerManager", n); }
static HRESULT STDMETHODCALLTYPE pm_activate(void *p, void **o) { (void)p; if (o) *o = 0; return E_NOTIMPL; }
static SYSTEM_POWER_STATUS pm_status(void)
{
    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s)) { s.ACLineStatus = 1; s.BatteryFlag = 128; s.BatteryLifePercent = 255;
                                     s.SystemStatusFlag = 0; s.BatteryLifeTime = (DWORD)-1; }
    return s;
}
/* EnergySaverStatus: Disabled 0 (no battery), Off 1, On 2 */
static HRESULT STDMETHODCALLTYPE pm_saver(void *p, int *v)
{
    (void)p;
    if (!v) return E_POINTER;
    SYSTEM_POWER_STATUS s = pm_status();
    *v = (s.SystemStatusFlag & 1) ? 2 : (s.BatteryFlag & 128) ? 0 : 1;
    return S_OK;
}
/* BatteryStatus: NotPresent 0, Discharging 1, Idle 2, Charging 3 */
static HRESULT STDMETHODCALLTYPE pm_battery(void *p, int *v)
{
    (void)p;
    if (!v) return E_POINTER;
    SYSTEM_POWER_STATUS s = pm_status();
    *v = (s.BatteryFlag & 128) ? 0 : (s.BatteryFlag & 8) ? 3 : s.ACLineStatus == 1 ? 2 : 1;
    return S_OK;
}
/* PowerSupplyStatus: NotPresent 0, Inadequate 1, Adequate 2 */
static HRESULT STDMETHODCALLTYPE pm_supply(void *p, int *v)
{
    (void)p;
    if (!v) return E_POINTER;
    *v = pm_status().ACLineStatus == 1 ? 2 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pm_percent(void *p, INT32 *v)
{
    (void)p;
    if (!v) return E_POINTER;
    SYSTEM_POWER_STATUS s = pm_status();
    *v = s.BatteryLifePercent > 100 ? 100 : s.BatteryLifePercent;
    return S_OK;
}
/* RemainingDischargeTime: a TimeSpan (100 ns units); unknown is the largest */
static HRESULT STDMETHODCALLTYPE pm_discharge(void *p, INT64 *v)
{
    (void)p;
    if (!v) return E_POINTER;
    SYSTEM_POWER_STATUS s = pm_status();
    *v = s.BatteryLifeTime == (DWORD)-1 ? 0x7FFFFFFFFFFFFFFFLL : (INT64)s.BatteryLifeTime * 10000000;
    return S_OK;
}
#define PM_EVENT_FNS(i) (void *)pm_add_##i, (void *)pm_remove_##i
#define PM_EVENT(i) \
    static HRESULT STDMETHODCALLTYPE pm_add_##i(void *p, void *h, EventRegistrationToken *t) { (void)p; return ev_add(&g_power.ev[i], h, t); } \
    static HRESULT STDMETHODCALLTYPE pm_remove_##i(void *p, EventRegistrationToken t) { (void)p; return ev_remove(&g_power.ev[i], t); }
PM_EVENT(PM_SAVER) PM_EVENT(PM_BATTERY) PM_EVENT(PM_SUPPLY) PM_EVENT(PM_PERCENT) PM_EVENT(PM_DISCHARGE)
static const void *const pm_factory_vt[] = { INSP_FNS(pm_qi, pm_name), (void *)pm_activate };
static const void *const pm_statics_vt[] = {
    INSP_FNS(pm_qi, pm_name),
    (void *)pm_saver, PM_EVENT_FNS(PM_SAVER), (void *)pm_battery, PM_EVENT_FNS(PM_BATTERY),
    (void *)pm_supply, PM_EVENT_FNS(PM_SUPPLY), (void *)pm_percent, PM_EVENT_FNS(PM_PERCENT),
    (void *)pm_discharge, PM_EVENT_FNS(PM_DISCHARGE),
};
_Static_assert(sizeof pm_statics_vt / sizeof pm_statics_vt[0] == 6 + 15, "IPowerManagerStatics has 21 slots");
static PowerManager g_power = { (const void *const *)pm_factory_vt, (const void *const *)pm_statics_vt };

/* ---- AnalyticsInfo: the device family a program runs on, Windows.Desktop
 * with the system's version (Windows serves it from Windows.System.Profile;
 * WinUI 2 asks whether it is on an Xbox).  DeviceForm is Unknown, as on a
 * desktop PC that reports none ---------------------------------------- */
static const GUID IID_IAnalyticsInfoStatics_ = { 0x1D5EE066, 0x188D, 0x5BA9, { 0x43, 0x87, 0xAC, 0xAE, 0xB0, 0xE7, 0xE3, 0x05 } };
static const GUID IID_IAnalyticsVersionInfo_ = { 0x926130B8, 0x9955, 0x4C74, { 0xBD, 0xC1, 0x7C, 0xD0, 0xDE, 0xCF, 0x9B, 0x03 } };
typedef struct { const void *const *factory; const void *const *statics; const void *const *version; } AnalyticsInfo;
static AnalyticsInfo g_analytics;
static HRESULT STDMETHODCALLTYPE pi_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &g_analytics.factory;
    else if (IsEqualGUID(iid, &IID_IAnalyticsInfoStatics_)) *out = &g_analytics.statics;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pi_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.System.Profile.AnalyticsInfo", n); }
static HRESULT STDMETHODCALLTYPE pi_activate(void *p, void **o) { (void)p; if (o) *o = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE pi_version_info(void *p, void **o) { (void)p; if (!o) return E_POINTER; *o = &g_analytics.version; return S_OK; }
static HRESULT STDMETHODCALLTYPE pi_form(void *p, HSTRING *n) { (void)p; return make_hstring(L"Unknown", n); }
static HRESULT STDMETHODCALLTYPE avi_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IAnalyticsVersionInfo_)) { *out = &g_analytics.version; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE avi_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.System.Profile.AnalyticsVersionInfo", n); }
static HRESULT STDMETHODCALLTYPE avi_family(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.Desktop", n); }
/* DeviceFamilyVersion: the version as one decimal number, major, minor,
 * build and revision in 16 bits each */
static HRESULT STDMETHODCALLTYPE avi_family_version(void *p, HSTRING *n)
{
    (void)p;
    VersionInfo v = { sizeof v };
    if (RtlGetVersion(&v) != 0) { v.major = 10; v.minor = 0; v.build = 0; }
    unsigned long long x = ((unsigned long long)v.major << 48) | ((unsigned long long)v.minor << 32)
                         | ((unsigned long long)(v.build & 0xFFFF) << 16);
    WCHAR buf[24], *e = buf + 23;
    *e = 0;
    do { *--e = (WCHAR)(L'0' + x % 10); x /= 10; } while (x);
    return make_hstring(e, n);
}
static const void *const pi_factory_vt[] = { INSP_FNS(pi_qi, pi_name), (void *)pi_activate };
static const void *const pi_statics_vt[] = { INSP_FNS(pi_qi, pi_name), (void *)pi_version_info, (void *)pi_form };
static const void *const avi_vt[] = { INSP_FNS(avi_qi, avi_name), (void *)avi_family, (void *)avi_family_version };
static AnalyticsInfo g_analytics = { (const void *const *)pi_factory_vt, (const void *const *)pi_statics_vt,
                                     (const void *const *)avi_vt };

/* ---- MemoryManager: the app's memory use against its limit (Windows
 * serves it from Windows.System; XAML's core listens to its events to trim
 * its caches).  A desktop app's limit is the system's commit limit
 * (memory and page file), its usage the process's private commit, its
 * level Low under half the limit, Medium under 80%, High to the limit and
 * OverLimit past it.  The events take handlers and are not raised yet, and
 * TrySetAppMemoryUsageLimit refuses, as it does for a desktop app -------- */
static const GUID IID_IMemoryManagerStatics_  = { 0x5C6C279C, 0xD7CA, 0x4779, { 0x91, 0x88, 0x40, 0x57, 0x21, 0x9C, 0xE6, 0x4C } };
static const GUID IID_IMemoryManagerStatics2_ = { 0x6EEE351F, 0x6D62, 0x423F, { 0x94, 0x79, 0xB0, 0x1F, 0x9C, 0x9F, 0x76, 0x69 } };
static const GUID IID_IMemoryManagerStatics3_ = { 0x149B59CE, 0x92AD, 0x4E35, { 0x89, 0xEB, 0x50, 0xDF, 0xB4, 0xC0, 0xD9, 0x1C } };
static const GUID IID_IMemoryManagerStatics4_ = { 0xC5A94828, 0xE84E, 0x4886, { 0x8A, 0x0D, 0x44, 0xB3, 0x19, 0x0E, 0x3B, 0x72 } };
static const GUID IID_IAppMemoryReport_       = { 0x6D65339B, 0x4D6F, 0x45BC, { 0x9C, 0x5E, 0xE4, 0x9B, 0x3F, 0xF2, 0x75, 0x8D } };
static const GUID IID_IAppMemoryReport2_      = { 0x5F7F3738, 0x51B7, 0x42DC, { 0xB7, 0xED, 0x79, 0xBA, 0x46, 0xD2, 0x88, 0x57 } };
static const GUID IID_IProcessMemoryReport_   = { 0x087305A8, 0x9B70, 0x4782, { 0x87, 0x41, 0x3A, 0x98, 0x2B, 0x6C, 0xE5, 0xE4 } };

/* kernel32's K32GetProcessMemoryInfo, with PROCESS_MEMORY_COUNTERS_EX */
typedef struct { DWORD cb, faults; SIZE_T peak_ws, ws, peak_paged, paged, peak_nonpaged, nonpaged, pagefile, peak_pagefile, priv; } MemCounters;
__declspec(dllimport) BOOL WINAPI K32GetProcessMemoryInfo(HANDLE process, MemCounters *c, DWORD cb);
static MemCounters mm_counters(void)
{
    MemCounters c = { sizeof c };
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof c)) { MemCounters none = { sizeof c }; c = none; }
    return c;
}
static UINT64 mm_limit(void)
{
    MEMORYSTATUSEX m = { sizeof m };
    return GlobalMemoryStatusEx(&m) ? m.ullTotalPageFile : 0;
}

enum { MM_INCREASED, MM_DECREASED, MM_LIMIT_CHANGING, MM_COUNT };
typedef struct { const void *const *factory, *const *statics, *const *statics2, *const *statics3, *const *statics4; Event ev[MM_COUNT]; } MemoryManager;
static MemoryManager g_memory;
static HRESULT STDMETHODCALLTYPE mm_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &g_memory.factory;
    else if (IsEqualGUID(iid, &IID_IMemoryManagerStatics_)) *out = &g_memory.statics;
    else if (IsEqualGUID(iid, &IID_IMemoryManagerStatics2_)) *out = &g_memory.statics2;
    else if (IsEqualGUID(iid, &IID_IMemoryManagerStatics3_)) *out = &g_memory.statics3;
    else if (IsEqualGUID(iid, &IID_IMemoryManagerStatics4_)) *out = &g_memory.statics4;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mm_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.System.MemoryManager", n); }
static HRESULT STDMETHODCALLTYPE mm_activate(void *p, void **o) { (void)p; if (o) *o = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE mm_usage(void *p, UINT64 *v) { (void)p; if (!v) return E_POINTER; *v = mm_counters().priv; return S_OK; }
static HRESULT STDMETHODCALLTYPE mm_usage_limit(void *p, UINT64 *v) { (void)p; if (!v) return E_POINTER; *v = mm_limit(); return S_OK; }
/* AppMemoryUsageLevel: Low 0, Medium 1, High 2, OverLimit 3 */
static HRESULT STDMETHODCALLTYPE mm_level(void *p, int *v)
{
    (void)p;
    if (!v) return E_POINTER;
    UINT64 used = mm_counters().priv, limit = mm_limit();
    *v = !limit || used < limit / 2 ? 0 : used < limit / 10 * 8 ? 1 : used <= limit ? 2 : 3;
    return S_OK;
}
#define MM_EVENT_FNS(i) (void *)mm_add_##i, (void *)mm_remove_##i
#define MM_EVENT(i) \
    static HRESULT STDMETHODCALLTYPE mm_add_##i(void *p, void *h, EventRegistrationToken *t) { (void)p; return ev_add(&g_memory.ev[i], h, t); } \
    static HRESULT STDMETHODCALLTYPE mm_remove_##i(void *p, EventRegistrationToken t) { (void)p; return ev_remove(&g_memory.ev[i], t); }
MM_EVENT(MM_INCREASED) MM_EVENT(MM_DECREASED) MM_EVENT(MM_LIMIT_CHANGING)

/* AppMemoryReport and ProcessMemoryReport: the figures when asked */
typedef struct { const void *const *vt, *const *vt2; LONG refs; UINT64 priv, peak, total, limit; } AppReport;
#define APP_REPORT_OF2(p) ((AppReport *)((char *)(p) - __builtin_offsetof(AppReport, vt2)))
static HRESULT STDMETHODCALLTYPE ar_qi(AppReport *r, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IAppMemoryReport_)) *out = &r->vt;
    else if (IsEqualGUID(iid, &IID_IAppMemoryReport2_)) *out = &r->vt2;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&r->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE ar_addref(AppReport *r) { return (ULONG)InterlockedIncrement(&r->refs); }
static ULONG STDMETHODCALLTYPE ar_release(AppReport *r)
{
    LONG n = InterlockedDecrement(&r->refs);
    if (!n) HeapFree(GetProcessHeap(), 0, r);
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE ar_name(AppReport *r, HSTRING *n) { (void)r; return make_hstring(L"Windows.System.AppMemoryReport", n); }
static HRESULT STDMETHODCALLTYPE ar_priv(AppReport *r, UINT64 *v) { if (!v) return E_POINTER; *v = r->priv; return S_OK; }
static HRESULT STDMETHODCALLTYPE ar_peak(AppReport *r, UINT64 *v) { if (!v) return E_POINTER; *v = r->peak; return S_OK; }
static HRESULT STDMETHODCALLTYPE ar_total(AppReport *r, UINT64 *v) { if (!v) return E_POINTER; *v = r->total; return S_OK; }
static HRESULT STDMETHODCALLTYPE ar_limit(AppReport *r, UINT64 *v) { if (!v) return E_POINTER; *v = r->limit; return S_OK; }
static HRESULT STDMETHODCALLTYPE ar2_qi(void *p, REFIID iid, void **out) { return ar_qi(APP_REPORT_OF2(p), iid, out); }
static ULONG STDMETHODCALLTYPE ar2_addref(void *p) { return ar_addref(APP_REPORT_OF2(p)); }
static ULONG STDMETHODCALLTYPE ar2_release(void *p) { return ar_release(APP_REPORT_OF2(p)); }
static HRESULT STDMETHODCALLTYPE ar2_name(void *p, HSTRING *n) { return ar_name(APP_REPORT_OF2(p), n); }
static HRESULT STDMETHODCALLTYPE ar2_expected(void *p, UINT64 *v) { return ar_limit(APP_REPORT_OF2(p), v); }
static const void *const app_report_vt[] = { (void *)ar_qi, (void *)ar_addref, (void *)ar_release, (void *)no_iids, (void *)ar_name,
                                             (void *)base_trust, (void *)ar_priv, (void *)ar_peak, (void *)ar_total, (void *)ar_limit };
static const void *const app_report2_vt[] = { (void *)ar2_qi, (void *)ar2_addref, (void *)ar2_release, (void *)no_iids, (void *)ar2_name,
                                              (void *)base_trust, (void *)ar2_expected };

typedef struct { const void *const *vt; LONG refs; UINT64 priv_ws, total_ws; } ProcReport;
static HRESULT STDMETHODCALLTYPE pr_qi(ProcReport *r, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!is_base(iid) && !IsEqualGUID(iid, &IID_IProcessMemoryReport_)) { *out = 0; return E_NOINTERFACE; }
    *out = r;
    InterlockedIncrement(&r->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE pr_addref(ProcReport *r) { return (ULONG)InterlockedIncrement(&r->refs); }
static ULONG STDMETHODCALLTYPE pr_release(ProcReport *r)
{
    LONG n = InterlockedDecrement(&r->refs);
    if (!n) HeapFree(GetProcessHeap(), 0, r);
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE pr_name(ProcReport *r, HSTRING *n) { (void)r; return make_hstring(L"Windows.System.ProcessMemoryReport", n); }
static HRESULT STDMETHODCALLTYPE pr_priv(ProcReport *r, UINT64 *v) { if (!v) return E_POINTER; *v = r->priv_ws; return S_OK; }
static HRESULT STDMETHODCALLTYPE pr_total(ProcReport *r, UINT64 *v) { if (!v) return E_POINTER; *v = r->total_ws; return S_OK; }
static const void *const proc_report_vt[] = { (void *)pr_qi, (void *)pr_addref, (void *)pr_release, (void *)no_iids, (void *)pr_name,
                                              (void *)base_trust, (void *)pr_priv, (void *)pr_total };

static HRESULT STDMETHODCALLTYPE mm_app_report(void *p, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    AppReport *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *r);
    if (!(*out = r)) return E_OUTOFMEMORY;
    MemCounters c = mm_counters();
    r->vt = app_report_vt;
    r->vt2 = app_report2_vt;
    r->refs = 1;
    r->priv = r->total = c.priv;
    r->peak = c.peak_pagefile > c.priv ? c.peak_pagefile : c.priv;
    r->limit = mm_limit();
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mm_process_report(void *p, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    ProcReport *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *r);
    if (!(*out = r)) return E_OUTOFMEMORY;
    MemCounters c = mm_counters();
    r->vt = proc_report_vt;
    r->refs = 1;
    r->priv_ws = r->total_ws = c.ws;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mm_try_set_limit(void *p, UINT64 v, boolean_t *ok) { (void)p; (void)v; if (!ok) return E_POINTER; *ok = 0; return S_OK; }
static const void *const mm_factory_vt[] = { INSP_FNS(mm_qi, mm_name), (void *)mm_activate };
static const void *const mm_statics_vt[] = {
    INSP_FNS(mm_qi, mm_name), (void *)mm_usage, (void *)mm_usage_limit, (void *)mm_level,
    MM_EVENT_FNS(MM_INCREASED), MM_EVENT_FNS(MM_DECREASED), MM_EVENT_FNS(MM_LIMIT_CHANGING),
};
_Static_assert(sizeof mm_statics_vt / sizeof mm_statics_vt[0] == 6 + 9, "IMemoryManagerStatics has 15 slots");
static const void *const mm_statics2_vt[] = { INSP_FNS(mm_qi, mm_name), (void *)mm_app_report, (void *)mm_process_report };
static const void *const mm_statics3_vt[] = { INSP_FNS(mm_qi, mm_name), (void *)mm_try_set_limit };
static const void *const mm_statics4_vt[] = { INSP_FNS(mm_qi, mm_name), (void *)mm_usage_limit };   /* ExpectedAppMemoryUsageLimit */
static MemoryManager g_memory = { (const void *const *)mm_factory_vt, (const void *const *)mm_statics_vt,
                                  (const void *const *)mm_statics2_vt, (const void *const *)mm_statics3_vt,
                                  (const void *const *)mm_statics4_vt };

/* ---- Windows.System.Threading: ThreadPool and ThreadPoolTimer, the
 * Windows Runtime's face of the process's thread pool (Windows serves them
 * from threadpoolwinrt.dll; XAML's core and .NET Native's Task scheduler
 * reach their work through them).  RunAsync submits a WorkItemHandler as
 * a thread pool callback (TrySubmitThreadpoolCallback) behind an
 * IAsyncAction, which its handler is given: Cancel before it starts skips
 * it, after shows as Status Canceled; Low and High priority set the
 * thread's for the item; TimeSliced is the default's behaviour here.  A
 * ThreadPoolTimer is a thread pool timer (CreateThreadpoolTimer) calling
 * its TimerElapsedHandler after Delay, or every Period; it lives until it
 * has fired once (a one-shot) or Cancel, whatever the caller holds, and
 * then calls its TimerDestroyedHandler ---------------------------------- */
static const GUID IID_IThreadPoolStatics_      = { 0xB6BF67DD, 0x84BD, 0x44F8, { 0xAC, 0x1C, 0x93, 0xEB, 0xCB, 0x9D, 0xBA, 0x91 } };
static const GUID IID_IThreadPoolTimerStatics_ = { 0x1A8A9D02, 0xE482, 0x461B, { 0xB8, 0xC7, 0x8E, 0xFA, 0xD1, 0xCC, 0xE5, 0x90 } };
static const GUID IID_IThreadPoolTimer_        = { 0x594EBE78, 0x55EA, 0x4A88, { 0xA5, 0x0D, 0x34, 0x02, 0xAE, 0x1F, 0x9C, 0xF2 } };

/* kernel32's thread pool (as threadpoolapiset.h has it) */
typedef VOID (CALLBACK *TpSimpleFn)(PVOID instance, PVOID ctx);
typedef VOID (CALLBACK *TpTimerFn)(PVOID instance, PVOID ctx, PVOID timer);
__declspec(dllimport) BOOL WINAPI TrySubmitThreadpoolCallback(TpSimpleFn fn, PVOID ctx, PVOID env);
__declspec(dllimport) PVOID WINAPI CreateThreadpoolTimer(TpTimerFn fn, PVOID ctx, PVOID env);
__declspec(dllimport) VOID WINAPI SetThreadpoolTimer(PVOID timer, FILETIME *due, DWORD period, DWORD window);
__declspec(dllimport) VOID WINAPI WaitForThreadpoolTimerCallbacks(PVOID timer, BOOL cancel);
__declspec(dllimport) VOID WINAPI CloseThreadpoolTimer(PVOID timer);

typedef struct { const void *const *factory; const void *const *statics; } PoolStatics;
static PoolStatics g_pool;
static volatile LONG g_pool_ids;
static HRESULT STDMETHODCALLTYPE tp_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &g_pool.factory;
    else if (IsEqualGUID(iid, &IID_IThreadPoolStatics_)) *out = &g_pool.statics;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tp_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.System.Threading.ThreadPool", n); }
static HRESULT STDMETHODCALLTYPE tp_activate(void *p, void **o) { (void)p; if (o) *o = 0; return E_NOTIMPL; }

/* Runs the work item on a pool thread, then completes its action:
 * Completed 1, Canceled 2 or Error 3 */
static VOID CALLBACK tp_work(PVOID instance, PVOID ctx)
{
    (void)instance;
    Action *a = ctx;
    void *work = a->work;
    a->work = 0;
    int status = 2;
    if (!a->cancel) {
        HANDLE t = GetCurrentThread();
        int prio = GetThreadPriority(t);
        if (a->priority) SetThreadPriority(t, a->priority > 0 ? THREAD_PRIORITY_ABOVE_NORMAL : THREAD_PRIORITY_BELOW_NORMAL);
        HRESULT hr = ((FnP)SLOT(work, 3))(work, &a->op);     /* WorkItemHandler::Invoke(operation) */
        if (a->priority) SetThreadPriority(t, prio);
        status = FAILED(hr) ? 3 : a->cancel ? 2 : 1;
        a->error = FAILED(hr) ? hr : status == 2 ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK;
    } else
        a->error = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    obj_release(work);
    AcquireSRWLockExclusive(&a->lock);
    a->status = status;
    void *h = a->completed;
    if (h) obj_addref(h);
    ReleaseSRWLockExclusive(&a->lock);
    if (h) { ((FnPI)SLOT(h, 3))(h, &a->op, status); obj_release(h); }
    act_release(a);                                     /* (the pool's reference) */
}
static HRESULT STDMETHODCALLTYPE tp_run_options(void *p, void *work, int priority, int options, void **out)
{
    (void)p; (void)options;
    if (!work) return E_INVALIDARG;
    if (!out) return E_POINTER;
    *out = 0;
    Action *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a);
    if (!a) return E_OUTOFMEMORY;
    a->op = action_vt;
    a->info = action_info_vt;
    a->refs = 2;                                        /* the caller's and the pool's */
    a->lock = (SRWLOCK)SRWLOCK_INIT;
    a->pool = 1;
    a->priority = priority;
    a->id = (UINT32)InterlockedIncrement(&g_pool_ids);
    a->work = work;
    obj_addref(work);
    if (!TrySubmitThreadpoolCallback(tp_work, a, 0)) {
        obj_release(work);
        HeapFree(GetProcessHeap(), 0, a);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    *out = &a->op;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tp_run_priority(void *p, void *work, int priority, void **out) { return tp_run_options(p, work, priority, 0, out); }
static HRESULT STDMETHODCALLTYPE tp_run(void *p, void *work, void **out) { return tp_run_options(p, work, 0, 0, out); }
static const void *const tp_factory_vt[] = { INSP_FNS(tp_qi, tp_name), (void *)tp_activate };
static const void *const tp_statics_vt[] = { INSP_FNS(tp_qi, tp_name), (void *)tp_run, (void *)tp_run_priority, (void *)tp_run_options };
static PoolStatics g_pool = { (const void *const *)tp_factory_vt, (const void *const *)tp_statics_vt };

/* ThreadPoolTimer: Delay and Period are TimeSpans (100 ns units) */
typedef struct {
    const void *const *vt;              /* IThreadPoolTimer */
    LONG refs;
    PVOID tp;                           /* the thread pool timer */
    void *elapsed, *destroyed;          /* TimerElapsedHandler, TimerDestroyedHandler (or 0) */
    INT64 delay, period;
    volatile LONG done;                 /* fired (a one-shot) or cancelled */
} PoolTimer;
static HRESULT STDMETHODCALLTYPE tmr_qi(PoolTimer *t, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!is_base(iid) && !IsEqualGUID(iid, &IID_IThreadPoolTimer_)) { *out = 0; return E_NOINTERFACE; }
    *out = t;
    InterlockedIncrement(&t->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE tmr_addref(PoolTimer *t) { return (ULONG)InterlockedIncrement(&t->refs); }
static ULONG STDMETHODCALLTYPE tmr_release(PoolTimer *t)
{
    LONG n = InterlockedDecrement(&t->refs);
    if (!n) {
        if (t->tp) CloseThreadpoolTimer(t->tp);
        obj_release(t->elapsed);
        if (t->destroyed) obj_release(t->destroyed);
        HeapFree(GetProcessHeap(), 0, t);
    }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE tmr_name(PoolTimer *t, HSTRING *n) { (void)t; return make_hstring(L"Windows.System.Threading.ThreadPoolTimer", n); }
static HRESULT STDMETHODCALLTYPE tmr_period(PoolTimer *t, INT64 *v) { if (!v) return E_POINTER; *v = t->period; return S_OK; }
static HRESULT STDMETHODCALLTYPE tmr_delay(PoolTimer *t, INT64 *v) { if (!v) return E_POINTER; *v = t->delay; return S_OK; }
/* The timer's end, once: its destroyed handler, then the timer's own
 * reference goes */
static void tmr_finish(PoolTimer *t)
{
    if (InterlockedExchange(&t->done, 1)) return;
    if (t->destroyed) ((FnP)SLOT(t->destroyed, 3))(t->destroyed, t);   /* TimerDestroyedHandler::Invoke(timer) */
    tmr_release(t);
}
static HRESULT STDMETHODCALLTYPE tmr_cancel(PoolTimer *t)
{
    if (t->done) return S_OK;
    WaitForThreadpoolTimerCallbacks(t->tp, TRUE);        /* (not waiting for itself, from its own handler) */
    tmr_finish(t);
    return S_OK;
}
static const void *const timer_vt[] = { (void *)tmr_qi, (void *)tmr_addref, (void *)tmr_release, (void *)no_iids, (void *)tmr_name,
                                        (void *)base_trust, (void *)tmr_period, (void *)tmr_delay, (void *)tmr_cancel };
static VOID CALLBACK tmr_fire(PVOID instance, PVOID ctx, PVOID tp)
{
    (void)instance; (void)tp;
    PoolTimer *t = ctx;
    if (t->done) return;
    tmr_addref(t);
    ((FnP)SLOT(t->elapsed, 3))(t->elapsed, t);              /* TimerElapsedHandler::Invoke(timer) */
    if (!t->period) tmr_finish(t);
    tmr_release(t);
}

typedef struct { const void *const *factory; const void *const *statics; } TimerStatics;
static TimerStatics g_timers;
static HRESULT STDMETHODCALLTYPE tps_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &g_timers.factory;
    else if (IsEqualGUID(iid, &IID_IThreadPoolTimerStatics_)) *out = &g_timers.statics;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tps_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.System.Threading.ThreadPoolTimer", n); }
static HRESULT STDMETHODCALLTYPE tps_activate(void *p, void **o) { (void)p; if (o) *o = 0; return E_NOTIMPL; }
static HRESULT timer_make(void *elapsed, INT64 delay, INT64 period, void *destroyed, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (!elapsed || delay < 0 || period < 0) return E_INVALIDARG;
    PoolTimer *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *t);
    if (!t) return E_OUTOFMEMORY;
    t->vt = timer_vt;
    t->refs = 2;                                        /* the caller's and its own, until it ends */
    t->elapsed = elapsed;
    obj_addref(elapsed);
    if ((t->destroyed = destroyed)) obj_addref(destroyed);
    t->delay = delay;
    t->period = period;
    if (!(t->tp = CreateThreadpoolTimer(tmr_fire, t, 0))) {
        t->refs = 1;
        tmr_release(t);
        return E_OUTOFMEMORY;
    }
    /* a relative due time (negative 100 ns units); periods in whole
     * milliseconds, at least one */
    INT64 due = -(period ? period : delay);
    FILETIME ft = { (DWORD)due, (DWORD)((UINT64)due >> 32) };
    DWORD ms = period ? (DWORD)((period + 9999) / 10000) : 0;
    SetThreadpoolTimer(t->tp, &ft, ms, 0);
    *out = t;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tps_periodic(void *p, void *h, INT64 period, void **out) { (void)p; return period ? timer_make(h, 0, period, 0, out) : E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE tps_timer(void *p, void *h, INT64 delay, void **out) { (void)p; return timer_make(h, delay, 0, 0, out); }
static HRESULT STDMETHODCALLTYPE tps_periodic_destroyed(void *p, void *h, INT64 period, void *d, void **out) { (void)p; return period ? timer_make(h, 0, period, d, out) : E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE tps_timer_destroyed(void *p, void *h, INT64 delay, void *d, void **out) { (void)p; return timer_make(h, delay, 0, d, out); }
static const void *const tps_factory_vt[] = { INSP_FNS(tps_qi, tps_name), (void *)tps_activate };
static const void *const tps_statics_vt[] = { INSP_FNS(tps_qi, tps_name), (void *)tps_periodic, (void *)tps_timer,
                                              (void *)tps_periodic_destroyed, (void *)tps_timer_destroyed };
static TimerStatics g_timers = { (const void *const *)tps_factory_vt, (const void *const *)tps_statics_vt };

/* ---- the in-process server's exports ---------------------------------- */
static BOOL name_is(HSTRING s, const WCHAR *want)
{
    UINT32 n, i = 0;
    const WCHAR *c = WindowsGetStringRawBuffer(s, &n);
    for (; i < n && want[i]; i++) if (c[i] != want[i]) return FALSE;
    return i == n && !want[i];
}
APPCORE_API HRESULT WINAPI DllGetActivationFactory(HSTRING cls, void **factory)
{
    if (!factory) return E_POINTER;
    if (name_is(cls, L"Windows.ApplicationModel.Core.CoreApplication")) { *factory = &g_app.factory; return S_OK; }
    if (name_is(cls, L"Windows.UI.Core.CoreWindow")) { *factory = &g_win_statics.factory; return S_OK; }
    if (name_is(cls, L"Windows.UI.Core.CoreCursor")) { *factory = &g_cursor_statics.factory; return S_OK; }
    if (name_is(cls, L"Windows.ApplicationModel.DesignMode")) { *factory = &g_design.factory; return S_OK; }
    if (name_is(cls, L"Windows.System.Power.PowerManager")) { *factory = &g_power.factory; return S_OK; }
    if (name_is(cls, L"Windows.System.Profile.AnalyticsInfo")) { *factory = &g_analytics.factory; return S_OK; }
    if (name_is(cls, L"Windows.System.MemoryManager")) { *factory = &g_memory.factory; return S_OK; }
    if (name_is(cls, L"Windows.System.Threading.ThreadPool")) { *factory = &g_pool.factory; return S_OK; }
    if (name_is(cls, L"Windows.System.Threading.ThreadPoolTimer")) { *factory = &g_timers.factory; return S_OK; }
    *factory = 0;
    return CLASS_E_CLASSNOTAVAILABLE;
}
APPCORE_API HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
