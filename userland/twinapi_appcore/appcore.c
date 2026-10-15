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
 *       ICoreApplicationExit, ICoreImmersiveApplication)
 *   Windows.UI.Core.CoreWindow                     (ICoreWindowStatic)
 *
 * A CoreWindow is a top-level user32 window of class
 * "Windows.UI.Core.CoreWindow"; ICoreWindowInterop gives its HWND, which is
 * how DXGI's CreateSwapChainForCoreWindow finds where to present.  Its
 * dispatcher pumps the thread's messages (ProcessEvents) and turns them
 * into the window's events: Activated, CharacterReceived, Closed, KeyDown,
 * KeyUp, SizeChanged and VisibilityChanged.  RunAsync posts work to the
 * window's thread.  The pointer events, input-enabled, touch hit testing
 * and automation events accept handlers and are not raised yet; sizes are
 * in device-independent pixels (the window's DPI over 96).
 *
 * There is one view, the main one, as in an app that never calls
 * CreateNewView.  The application, view, window and dispatcher objects are
 * static and agile, so their reference counts are kept only for show;
 * event arguments and IAsyncActions are counted for real.
 */
#include <windows.h>
#include <roapi.h>

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
static const GUID IID_IAsyncAction_          = { 0x5A648006, 0x843A, 0x4DA9, { 0x86, 0x5B, 0x9D, 0x26, 0xE5, 0xDF, 0xAD, 0x7B } };
static const GUID IID_ICoreApplication_      = { 0x0AACF7A4, 0x5E1D, 0x49DF, { 0x80, 0x34, 0xFB, 0x6A, 0x68, 0xBC, 0x5E, 0xD1 } };
static const GUID IID_ICoreApplicationExit_  = { 0xCF86461D, 0x261E, 0x4B72, { 0x9A, 0xCD, 0x44, 0xED, 0x2A, 0xCE, 0x6A, 0x29 } };
static const GUID IID_ICoreImmersiveApplication_ = { 0x1ADA0E3E, 0xE4A2, 0x4123, { 0xB4, 0x51, 0xDC, 0x96, 0xBF, 0x80, 0x04, 0x19 } };
static const GUID IID_ICoreApplicationView_  = { 0x638BB2DB, 0x451D, 0x4661, { 0xB0, 0x99, 0x41, 0x4F, 0x34, 0xFF, 0xB9, 0xF1 } };
static const GUID IID_ICoreWindow_           = { 0x79B9D5F2, 0x879E, 0x4B89, { 0xB7, 0x98, 0x79, 0xE4, 0x75, 0x98, 0x03, 0x0C } };
static const GUID IID_ICoreWindowStatic_     = { 0x4D239005, 0x3C2A, 0x41B1, { 0x90, 0x22, 0x53, 0x6B, 0xB9, 0xCF, 0x93, 0xB1 } };
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
enum { ARGS_PLAIN, ARGS_KEY, ARGS_CHAR, ARGS_SIZE, ARGS_VISIBLE, ARGS_ACTIVATED };
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
static ULONG STDMETHODCALLTYPE args_release(Args *a)
{
    LONG r = InterlockedDecrement(&a->refs);
    if (!r) HeapFree(GetProcessHeap(), 0, a);
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE args_name(Args *a, HSTRING *n)
{
    static const WCHAR *const names[] = {
        L"Windows.UI.Core.CoreWindowEventArgs", L"Windows.UI.Core.KeyEventArgs",
        L"Windows.UI.Core.CharacterReceivedEventArgs", L"Windows.UI.Core.WindowSizeChangedEventArgs",
        L"Windows.UI.Core.VisibilityChangedEventArgs", L"Windows.UI.Core.WindowActivatedEventArgs",
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

#define ARGS_FNS (void *)args_qi, (void *)args_addref, (void *)args_release, (void *)no_iids, (void *)args_name, (void *)base_trust
static const void *const args_base_vt[] = { ARGS_FNS, (void *)args_get_handled, (void *)args_put_handled };
static const void *const args_key_vt[] = { ARGS_FNS, (void *)key_vk, (void *)key_status };
static const void *const args_char_vt[] = { ARGS_FNS, (void *)char_code, (void *)key_status };
static const void *const args_size_vt[] = { ARGS_FNS, (void *)size_size };
static const void *const args_vis_vt[] = { ARGS_FNS, (void *)vis_visible };
static const void *const args_act_vt[] = { ARGS_FNS, (void *)act_state };

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
    static const void *const *const specs[] = { args_base_vt, args_key_vt, args_char_vt, args_size_vt, args_vis_vt, args_act_vt };
    a->spec = (const ArgsVtbl *)specs[kind];
    a->base = (const ArgsVtbl *)args_base2_vt;
    a->refs = 1;
    a->kind = kind;
    return a;
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
static HRESULT STDMETHODCALLTYPE act_results(Action *a) { return a->status == 1 ? S_OK : E_ILLEGAL_METHOD_CALL_; }
static HRESULT STDMETHODCALLTYPE ai_qi(void *p, REFIID iid, void **out) { return act_qi(ACTION_OF_INFO(p), iid, out); }
static ULONG STDMETHODCALLTYPE ai_addref(void *p) { return act_addref(ACTION_OF_INFO(p)); }
static ULONG STDMETHODCALLTYPE ai_release(void *p) { return act_release(ACTION_OF_INFO(p)); }
static HRESULT STDMETHODCALLTYPE ai_name(void *p, HSTRING *n) { return act_name(ACTION_OF_INFO(p), n); }
static HRESULT STDMETHODCALLTYPE ai_id(void *p, UINT32 *v) { if (!v) return E_POINTER; *v = ACTION_OF_INFO(p)->id; return S_OK; }
static HRESULT STDMETHODCALLTYPE ai_status(void *p, int *v) { if (!v) return E_POINTER; *v = ACTION_OF_INFO(p)->status; return S_OK; }
static HRESULT STDMETHODCALLTYPE ai_error(void *p, HRESULT *v) { (void)p; if (!v) return E_POINTER; *v = S_OK; return S_OK; }
static HRESULT STDMETHODCALLTYPE ai_cancel(void *p) { (void)p; return S_OK; }      /* (the work runs anyway once posted) */
static HRESULT STDMETHODCALLTYPE ai_close(void *p) { (void)p; return S_OK; }
static const void *const action_vt[] = { (void *)act_qi, (void *)act_addref, (void *)act_release, (void *)no_iids, (void *)act_name,
                                         (void *)base_trust, (void *)act_put_completed, (void *)act_get_completed, (void *)act_results };
static const void *const action_info_vt[] = { (void *)ai_qi, (void *)ai_addref, (void *)ai_release, (void *)no_iids, (void *)ai_name,
                                              (void *)base_trust, (void *)ai_id, (void *)ai_status, (void *)ai_error, (void *)ai_cancel, (void *)ai_close };

/* Runs the work on the window's thread, then completes the action */
static void action_run(Action *a)
{
    void *work = a->work;
    a->work = 0;
    if (work) {
        ((Fn0)SLOT(work, 3))(work);                     /* DispatchedHandler::Invoke() */
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
    HWND hwnd;
    DWORD tid;
    Event ev[EV_COUNT];
    boolean_t input_enabled, message_handled, cursor_hidden, closed, visible;
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
static HRESULT STDMETHODCALLTYPE win_activate(Win *w)
{
    if (!w->hwnd) return E_ILLEGAL_METHOD_CALL_;
    if (!IsWindowVisible(w->hwnd)) ShowWindow(w->hwnd, SW_SHOW);
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

static Win g_win = { (const void *const *)win_vt, (const void *const *)win_interop_vt };

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
static HRESULT STDMETHODCALLTYPE disp_run_async(Dispatcher *d, int priority, void *work, void **out)
{
    (void)d; (void)priority;
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
    if (!PostMessageW(g_win.hwnd, WM_NOVA_DISPATCH, 0, (LPARAM)a)) {
        HeapFree(GetProcessHeap(), 0, a);
        obj_release(work);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    *out = &a->op;
    return S_OK;
}
/* RunIdleAsync's handler takes IdleDispatchedHandlerArgs, which are not
 * made yet */
static HRESULT STDMETHODCALLTYPE disp_run_idle(Dispatcher *d, void *work, void **out)
{
    (void)d; (void)work;
    if (out) *out = 0;
    return E_NOTIMPL;
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
    g_win.input_enabled = 1;
    g_win.tid = GetCurrentThreadId();
    g_win.hwnd = CreateWindowExW(0, cls, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                 0, 0, wc.hInstance, 0);
    return g_win.hwnd ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

/* ---- the view (CoreApplicationView) and the launch's arguments -------- */
typedef struct { const void *const *vt; Event activated; } View;
static View g_view;
static HRESULT STDMETHODCALLTYPE view_qi(View *v, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_ICoreApplicationView_)) { *out = &v->vt; return S_OK; }
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
static View g_view = { (const void *const *)view_vt };

/* LaunchActivatedEventArgs: Kind Launch (0), PreviousExecutionState
 * NotRunning (0), no splash screen; Arguments are the command line after
 * the program's name, TileId "App" */
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
static HRESULT STDMETHODCALLTYPE ll_tile(void *p, HSTRING *v) { (void)p; return make_hstring(L"App", v); }
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
    Event suspending, resuming, exiting;
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

static const void *const app_factory_vt[] = { INSP_FNS(app_qi, app_name), (void *)app_activate };
static const void *const app_vt[] = { INSP_FNS(app_qi, app_name), (void *)app_id, (void *)app_add_suspending, (void *)app_remove_suspending,
                                      (void *)app_add_resuming, (void *)app_remove_resuming, (void *)app_properties,
                                      (void *)app_current_view, (void *)app_run, (void *)app_run_factories };
static const void *const app_exit_vt[] = { INSP_FNS(app_qi, app_name), (void *)exit_exit, (void *)exit_add, (void *)exit_remove };
static const void *const app_immersive_vt[] = { INSP_FNS(app_qi, app_name), (void *)imm_views, (void *)imm_new_view, (void *)imm_main_view };
static App g_app = { (const void *const *)app_factory_vt, (const void *const *)app_vt,
                     (const void *const *)app_exit_vt, (const void *const *)app_immersive_vt };

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
    *factory = 0;
    return CLASS_E_CLASSNOTAVAILABLE;
}
APPCORE_API HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
