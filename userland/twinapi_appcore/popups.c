/*
 * popups.c — Windows.UI.Popups' PopupMenu and UICommand: the context menu
 * a program shows at a point and the commands on it.  XAML's text boxes
 * show theirs with it (Cut, Copy, Paste, Undo, Redo and Select All:
 * TextContextMenu::Show in the XAML core) when they have no ContextFlyout,
 * and UWP apps use it for menus of their own.
 *
 * A PopupMenu holds its commands (Commands, an IVector<IUICommand>) and,
 * given its window (IInitializeWithWindow, as a desktop program or XAML
 * gives it; else, or when that window is hidden, the thread's focus
 * window, then its active one), ShowAsync shows them as a
 * user32 popup menu at the point (device-independent pixels in the
 * window's client area, as on Windows).  The menu opens once the caller's
 * thread is back in its message loop (a thread timer), so ShowAsync
 * returns at once, Started, as Windows' does; when it closes, the command
 * chosen has its Invoked handler called, and the operation completes with
 * that command, or with none when the menu was dismissed (Completed, then
 * GetResults).  ShowAsyncWithRect and ShowAsyncWithRectAndPlacement show
 * it beside the rectangle (below it; above for Placement Above).  A
 * PopupMenu shows at most six commands, as Windows', and fails with
 * E_INVALIDARG for more.  A UICommand keeps its Label, Invoked handler
 * and Id (UICommandFactory's Create* make them with any of the three).
 * Separators (UICommandSeparator) are not here yet.
 *
 * twinapi.appcore.dll's DllGetActivationFactory hands the two class names
 * to popups_factory; the registry's ActivatableClassId keys
 * (kernel/um/um_registry.c) name that DLL.
 */
#include <windows.h>
#include <roapi.h>

static const GUID IID_IUnknown_p       = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_p   = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_p   = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IActivationFactory_p = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IAsyncInfo_p     = { 0x00000036, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInitializeWithWindow_p = { 0x3E68D4BD, 0x7135, 0x4D10, { 0x80, 0x18, 0x9F, 0xB6, 0xD9, 0xF3, 0x3F, 0xA1 } };
/* Windows.UI.Popups (Windows.winmd), and the generic instances over
 * IUICommand (their pinterface IIDs, tools/winrt/abigen.py's) */
static const GUID IID_IPopupMenu_      = { 0x4E9BC6DC, 0x880D, 0x47FC, { 0xA0, 0xA1, 0x72, 0xB6, 0x39, 0xE6, 0x25, 0x59 } };
static const GUID IID_IUICommand_      = { 0x4FF93A75, 0x4145, 0x47FF, { 0xAC, 0x7F, 0xDF, 0xF1, 0xC1, 0xFA, 0x5B, 0x0F } };
static const GUID IID_IUICommandFactory_ = { 0xA21A8189, 0x26B0, 0x4676, { 0xAE, 0x94, 0x54, 0x04, 0x1B, 0xC1, 0x25, 0xE8 } };
static const GUID IID_IVector_UICommand = { 0x105139A1, 0xDCB8, 0x5F65, { 0x97, 0xEF, 0xCB, 0x1B, 0xF0, 0xB7, 0x5F, 0x9D } };
static const GUID IID_IAsyncOp_UICommand = { 0xB8770535, 0x6A4B, 0x52B1, { 0xB5, 0x78, 0xF3, 0xCD, 0xC5, 0x00, 0x7A, 0x1F } };

#define E_ILLEGAL_METHOD_CALL_ ((HRESULT)0x8000000EL)
#define E_BOUNDS_              ((HRESULT)0x8000000BL)
#ifndef TPM_VERTICAL
#define TPM_VERTICAL 0x0040
#endif
#define MAX_COMMANDS 6                              /* (Windows' limit for a PopupMenu) */
enum { Started = 0, Completed = 1 };

typedef struct { float x, y; } PointF;
typedef struct { float x, y, w, h; } RectF;
typedef HRESULT (STDMETHODCALLTYPE *QiFn)(void *, REFIID, void **);
typedef ULONG (STDMETHODCALLTYPE *RefFn)(void *);
#define SLOT(o, i) ((*(void *const *const *)(o))[i])
static void obj_addref(void *o) { if (o) ((RefFn)SLOT(o, 1))(o); }
static void obj_release(void *o) { if (o) ((RefFn)SLOT(o, 2))(o); }

static void *zalloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void zfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
static BOOL is_base(REFIID iid)
{
    return IsEqualGUID(iid, &IID_IUnknown_p) || IsEqualGUID(iid, &IID_IInspectable_p) || IsEqualGUID(iid, &IID_IAgileObject_p);
}
static HRESULT STDMETHODCALLTYPE no_iids(void *o, ULONG *n, GUID **iids) { (void)o; if (!n || !iids) return E_POINTER; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE base_trust(void *o, int *t) { (void)o; if (!t) return E_POINTER; *t = 0; return S_OK; }
static HRESULT named(const WCHAR *s, HSTRING *out)
{
    UINT32 n = 0;
    while (s[n]) n++;
    return WindowsCreateString(s, n, out);
}

/* ---- UICommand ------------------------------------------------------------ */
typedef struct {
    const void *const *vt;              /* IUICommand */
    LONG refs;
    HSTRING label;
    void *invoked;                      /* UICommandInvokedHandler */
    void *id;                           /* IInspectable */
} Command;

static HRESULT STDMETHODCALLTYPE cmd_qi(Command *c, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!is_base(iid) && !IsEqualGUID(iid, &IID_IUICommand_)) { *out = 0; return E_NOINTERFACE; }
    *out = c;
    InterlockedIncrement(&c->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE cmd_addref(Command *c) { return (ULONG)InterlockedIncrement(&c->refs); }
static ULONG STDMETHODCALLTYPE cmd_release(Command *c)
{
    LONG r = InterlockedDecrement(&c->refs);
    if (!r) {
        WindowsDeleteString(c->label);
        obj_release(c->invoked);
        obj_release(c->id);
        zfree(c);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE cmd_name(Command *c, HSTRING *n) { (void)c; return named(L"Windows.UI.Popups.UICommand", n); }
static HRESULT STDMETHODCALLTYPE cmd_get_label(Command *c, HSTRING *v) { if (!v) return E_POINTER; return WindowsDuplicateString(c->label, v); }
static HRESULT STDMETHODCALLTYPE cmd_put_label(Command *c, HSTRING v)
{
    HSTRING copy = 0;
    HRESULT hr = WindowsDuplicateString(v, &copy);
    if (FAILED(hr)) return hr;
    WindowsDeleteString(c->label);
    c->label = copy;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cmd_get_invoked(Command *c, void **v) { if (!v) return E_POINTER; *v = c->invoked; obj_addref(*v); return S_OK; }
static HRESULT STDMETHODCALLTYPE cmd_put_invoked(Command *c, void *v) { obj_addref(v); obj_release(c->invoked); c->invoked = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE cmd_get_id(Command *c, void **v) { if (!v) return E_POINTER; *v = c->id; obj_addref(*v); return S_OK; }
static HRESULT STDMETHODCALLTYPE cmd_put_id(Command *c, void *v) { obj_addref(v); obj_release(c->id); c->id = v; return S_OK; }
static const void *const command_vt[] = { (void *)cmd_qi, (void *)cmd_addref, (void *)cmd_release, (void *)no_iids, (void *)cmd_name,
                                          (void *)base_trust, (void *)cmd_get_label, (void *)cmd_put_label,
                                          (void *)cmd_get_invoked, (void *)cmd_put_invoked, (void *)cmd_get_id, (void *)cmd_put_id };

static HRESULT new_command(HSTRING label, void *invoked, void *id, Command **out)
{
    Command *c = zalloc(sizeof *c);
    if (!c) return E_OUTOFMEMORY;
    c->vt = command_vt;
    c->refs = 1;
    if (label && FAILED(WindowsDuplicateString(label, &c->label))) { zfree(c); return E_OUTOFMEMORY; }
    obj_addref(invoked);
    c->invoked = invoked;
    obj_addref(id);
    c->id = id;
    *out = c;
    return S_OK;
}

/* its factory: IActivationFactory and IUICommandFactory */
typedef struct { const void *const *act; const void *const *fac; } CommandFactory;
#define CF_OF_FAC(p) ((CommandFactory *)((char *)(p) - __builtin_offsetof(CommandFactory, fac)))
static HRESULT STDMETHODCALLTYPE cf_qi(CommandFactory *f, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_p)) *out = &f->act;
    else if (IsEqualGUID(iid, &IID_IUICommandFactory_)) *out = &f->fac;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static ULONG STDMETHODCALLTYPE static_ref(void *o) { (void)o; return 2; }
static HRESULT STDMETHODCALLTYPE cf_name(void *f, HSTRING *n) { (void)f; return named(L"Windows.UI.Popups.UICommand", n); }
static HRESULT STDMETHODCALLTYPE cf_activate(CommandFactory *f, void **out)
{
    (void)f;
    if (!out) return E_POINTER;
    *out = 0;
    return new_command(0, 0, 0, (Command **)out);
}
static HRESULT STDMETHODCALLTYPE cff_qi(void *p, REFIID iid, void **out) { return cf_qi(CF_OF_FAC(p), iid, out); }
static HRESULT STDMETHODCALLTYPE cff_create(void *p, HSTRING label, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    *out = 0;
    return new_command(label, 0, 0, (Command **)out);
}
static HRESULT STDMETHODCALLTYPE cff_with_handler(void *p, HSTRING label, void *h, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    *out = 0;
    return new_command(label, h, 0, (Command **)out);
}
static HRESULT STDMETHODCALLTYPE cff_with_handler_id(void *p, HSTRING label, void *h, void *id, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    *out = 0;
    return new_command(label, h, id, (Command **)out);
}
static const void *const cf_act_vt[] = { (void *)cf_qi, (void *)static_ref, (void *)static_ref, (void *)no_iids, (void *)cf_name,
                                         (void *)base_trust, (void *)cf_activate };
static const void *const cf_fac_vt[] = { (void *)cff_qi, (void *)static_ref, (void *)static_ref, (void *)no_iids, (void *)cf_name,
                                         (void *)base_trust, (void *)cff_create, (void *)cff_with_handler,
                                         (void *)cff_with_handler_id };
static CommandFactory g_command_factory = { cf_act_vt, cf_fac_vt };

/* ---- the commands: IVector<IUICommand> ------------------------------------ */
typedef struct {
    const void *const *vt;
    LONG refs;
    void *items[MAX_COMMANDS + 1];      /* IUICommand, held (one over the limit, for ShowAsync to refuse) */
    UINT32 n;
} Commands;

static HRESULT STDMETHODCALLTYPE vec_qi(Commands *v, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!is_base(iid) && !IsEqualGUID(iid, &IID_IVector_UICommand)) { *out = 0; return E_NOINTERFACE; }
    *out = v;
    InterlockedIncrement(&v->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE vec_addref(Commands *v) { return (ULONG)InterlockedIncrement(&v->refs); }
static ULONG STDMETHODCALLTYPE vec_release(Commands *v)
{
    LONG r = InterlockedDecrement(&v->refs);
    if (!r) {
        for (UINT32 i = 0; i < v->n; i++) obj_release(v->items[i]);
        zfree(v);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE vec_name(Commands *v, HSTRING *n)
{
    (void)v;
    return named(L"Windows.Foundation.Collections.IVector`1<Windows.UI.Popups.IUICommand>", n);
}
static HRESULT STDMETHODCALLTYPE vec_get_at(Commands *v, UINT32 i, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (i >= v->n) return E_BOUNDS_;
    *out = v->items[i];
    obj_addref(*out);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vec_size(Commands *v, UINT32 *out) { if (!out) return E_POINTER; *out = v->n; return S_OK; }
static HRESULT STDMETHODCALLTYPE vec_view(Commands *v, void **out) { (void)v; if (out) *out = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE vec_index_of(Commands *v, void *item, UINT32 *at, BYTE *found)
{
    if (!at || !found) return E_POINTER;
    *at = 0;
    *found = 0;
    for (UINT32 i = 0; i < v->n; i++)
        if (v->items[i] == item) { *at = i; *found = 1; break; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vec_set_at(Commands *v, UINT32 i, void *item)
{
    if (i >= v->n) return E_BOUNDS_;
    obj_addref(item);
    obj_release(v->items[i]);
    v->items[i] = item;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vec_insert_at(Commands *v, UINT32 i, void *item)
{
    if (i > v->n) return E_BOUNDS_;
    if (v->n > MAX_COMMANDS) return E_INVALIDARG;
    for (UINT32 k = v->n; k > i; k--) v->items[k] = v->items[k - 1];
    obj_addref(item);
    v->items[i] = item;
    v->n++;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vec_remove_at(Commands *v, UINT32 i)
{
    if (i >= v->n) return E_BOUNDS_;
    obj_release(v->items[i]);
    for (UINT32 k = i; k + 1 < v->n; k++) v->items[k] = v->items[k + 1];
    v->n--;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vec_append(Commands *v, void *item) { return vec_insert_at(v, v->n, item); }
static HRESULT STDMETHODCALLTYPE vec_remove_end(Commands *v) { return v->n ? vec_remove_at(v, v->n - 1) : E_BOUNDS_; }
static HRESULT STDMETHODCALLTYPE vec_clear(Commands *v)
{
    for (UINT32 i = 0; i < v->n; i++) obj_release(v->items[i]);
    v->n = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vec_get_many(Commands *v, UINT32 start, UINT32 cap, void **items, UINT32 *got)
{
    if (!got || (cap && !items)) return E_POINTER;
    *got = 0;
    if (start > v->n) return E_BOUNDS_;
    for (UINT32 i = start; i < v->n && *got < cap; i++) {
        items[*got] = v->items[i];
        obj_addref(items[(*got)++]);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vec_replace_all(Commands *v, UINT32 n, void **items)
{
    if (n > MAX_COMMANDS + 1) return E_INVALIDARG;
    if (n && !items) return E_POINTER;
    vec_clear(v);
    for (UINT32 i = 0; i < n; i++) { obj_addref(items[i]); v->items[v->n++] = items[i]; }
    return S_OK;
}
static const void *const commands_vt[] = { (void *)vec_qi, (void *)vec_addref, (void *)vec_release, (void *)no_iids, (void *)vec_name,
                                           (void *)base_trust, (void *)vec_get_at, (void *)vec_size, (void *)vec_view,
                                           (void *)vec_index_of, (void *)vec_set_at, (void *)vec_insert_at,
                                           (void *)vec_remove_at, (void *)vec_append, (void *)vec_remove_end,
                                           (void *)vec_clear, (void *)vec_get_many, (void *)vec_replace_all };

/* ---- ShowAsync's operation: IAsyncOperation<IUICommand> ------------------- */
typedef struct Op Op;
struct Op {
    const void *const *op;              /* IAsyncOperation<IUICommand> */
    const void *const *info;            /* IAsyncInfo */
    LONG refs;
    SRWLOCK lock;
    int status;
    void *completed;                    /* AsyncOperationCompletedHandler<IUICommand> */
    void *result;                       /* the command chosen, or none */
    UINT32 id;
    /* the menu to show */
    HWND owner;
    POINT at;                           /* (screen pixels) */
    RECT exclude;                       /* (screen pixels; empty: none) */
    UINT flags;
    void *items[MAX_COMMANDS];
    UINT32 n;
    UINT_PTR timer;
    Op *next;                           /* (the thread's menus waiting to open) */
};
#define OP_OF_INFO(p) ((Op *)((char *)(p) - __builtin_offsetof(Op, info)))
static HRESULT STDMETHODCALLTYPE op_qi(Op *o, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IAsyncOp_UICommand)) *out = &o->op;
    else if (IsEqualGUID(iid, &IID_IAsyncInfo_p)) *out = &o->info;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&o->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE op_addref(Op *o) { return (ULONG)InterlockedIncrement(&o->refs); }
static ULONG STDMETHODCALLTYPE op_release(Op *o)
{
    LONG r = InterlockedDecrement(&o->refs);
    if (!r) {
        obj_release(o->completed);
        obj_release(o->result);
        for (UINT32 i = 0; i < o->n; i++) obj_release(o->items[i]);
        zfree(o);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE op_name(Op *o, HSTRING *n)
{
    (void)o;
    return named(L"Windows.Foundation.IAsyncOperation`1<Windows.UI.Popups.IUICommand>", n);
}
/* (a handler set after completion is called at once, as Windows does) */
static HRESULT STDMETHODCALLTYPE op_put_completed(Op *o, void *h)
{
    if (!h) return E_INVALIDARG;
    AcquireSRWLockExclusive(&o->lock);
    if (o->completed) { ReleaseSRWLockExclusive(&o->lock); return E_ILLEGAL_METHOD_CALL_; }
    obj_addref(h);
    o->completed = h;
    int done = o->status != Started;
    ReleaseSRWLockExclusive(&o->lock);
    if (done) ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, int))SLOT(h, 3))(h, &o->op, o->status);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE op_get_completed(Op *o, void **h)
{
    if (!h) return E_POINTER;
    AcquireSRWLockShared(&o->lock);
    *h = o->completed;
    obj_addref(*h);
    ReleaseSRWLockShared(&o->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE op_results(Op *o, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (o->status != Completed) return E_ILLEGAL_METHOD_CALL_;
    *out = o->result;
    obj_addref(*out);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE oi_qi(void *p, REFIID iid, void **out) { return op_qi(OP_OF_INFO(p), iid, out); }
static ULONG STDMETHODCALLTYPE oi_addref(void *p) { return op_addref(OP_OF_INFO(p)); }
static ULONG STDMETHODCALLTYPE oi_release(void *p) { return op_release(OP_OF_INFO(p)); }
static HRESULT STDMETHODCALLTYPE oi_name(void *p, HSTRING *n) { return op_name(OP_OF_INFO(p), n); }
static HRESULT STDMETHODCALLTYPE oi_id(void *p, UINT32 *v) { if (!v) return E_POINTER; *v = OP_OF_INFO(p)->id; return S_OK; }
static HRESULT STDMETHODCALLTYPE oi_status(void *p, int *v) { if (!v) return E_POINTER; *v = OP_OF_INFO(p)->status; return S_OK; }
static HRESULT STDMETHODCALLTYPE oi_error(void *p, HRESULT *v) { (void)p; if (!v) return E_POINTER; *v = S_OK; return S_OK; }
/* (an open menu closes as dismissed; one not yet open is not shown) */
static HRESULT STDMETHODCALLTYPE oi_cancel(void *p) { (void)p; EndMenu(); return S_OK; }
static HRESULT STDMETHODCALLTYPE oi_close(void *p) { (void)p; return S_OK; }
static const void *const op_vt[] = { (void *)op_qi, (void *)op_addref, (void *)op_release, (void *)no_iids, (void *)op_name,
                                     (void *)base_trust, (void *)op_put_completed, (void *)op_get_completed, (void *)op_results };
static const void *const op_info_vt[] = { (void *)oi_qi, (void *)oi_addref, (void *)oi_release, (void *)no_iids, (void *)oi_name,
                                          (void *)base_trust, (void *)oi_id, (void *)oi_status, (void *)oi_error,
                                          (void *)oi_cancel, (void *)oi_close };

/* the menus waiting for their thread's message loop (each its timer) */
static SRWLOCK g_wait_lock = SRWLOCK_INIT;
static Op *g_waiting;
static LONG g_next_id;

/* the menu itself: shown, then the command chosen is invoked and the
 * operation completes */
static void show(Op *o)
{
    HMENU menu = CreatePopupMenu();
    UINT cmd = 0;
    if (menu) {
        for (UINT32 i = 0; i < o->n; i++) {
            HSTRING label = 0;
            ((HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))SLOT(o->items[i], 6))(o->items[i], &label);
            const WCHAR *text = WindowsGetStringRawBuffer(label, 0);
            AppendMenuW(menu, MF_STRING, i + 1, text && *text ? text : L" ");
            WindowsDeleteString(label);
        }
        TPMPARAMS tp = { sizeof tp, o->exclude };
        BOOL excl = o->exclude.right > o->exclude.left || o->exclude.bottom > o->exclude.top;
        cmd = (UINT)TrackPopupMenuEx(menu, o->flags | TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, o->at.x, o->at.y,
                                     o->owner, excl ? &tp : 0);
        DestroyMenu(menu);
    }
    void *chosen = cmd >= 1 && cmd <= o->n ? o->items[cmd - 1] : 0;
    if (chosen) {
        void *h = 0;
        ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))SLOT(chosen, 8))(chosen, &h);   /* get_Invoked */
        if (h) {
            ((HRESULT (STDMETHODCALLTYPE *)(void *, void *))SLOT(h, 3))(h, chosen);        /* UICommandInvokedHandler::Invoke */
            obj_release(h);
        }
    }
    AcquireSRWLockExclusive(&o->lock);
    obj_addref(chosen);
    o->result = chosen;
    o->status = Completed;
    void *done = o->completed;
    obj_addref(done);
    ReleaseSRWLockExclusive(&o->lock);
    if (done) {
        ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, int))SLOT(done, 3))(done, &o->op, Completed);
        obj_release(done);
    }
}

static void CALLBACK open_menu(HWND h, UINT msg, UINT_PTR timer, DWORD time)
{
    (void)h; (void)msg; (void)time;
    KillTimer(0, timer);
    Op *o = 0;
    AcquireSRWLockExclusive(&g_wait_lock);
    for (Op **p = &g_waiting; *p; p = &(*p)->next)
        if ((*p)->timer == timer) { o = *p; *p = o->next; break; }
    ReleaseSRWLockExclusive(&g_wait_lock);
    if (!o) return;
    show(o);
    op_release(o);                      /* (the waiting list's reference) */
}

/* ---- PopupMenu ------------------------------------------------------------ */
typedef struct {
    const void *const *vt;              /* IPopupMenu */
    const void *const *init;            /* IInitializeWithWindow */
    LONG refs;
    Commands *commands;
    HWND window;
} Menu;
#define MENU_OF_INIT(p) ((Menu *)((char *)(p) - __builtin_offsetof(Menu, init)))

static HRESULT STDMETHODCALLTYPE menu_qi(Menu *m, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IPopupMenu_)) *out = &m->vt;
    else if (IsEqualGUID(iid, &IID_IInitializeWithWindow_p)) *out = &m->init;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&m->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE menu_addref(Menu *m) { return (ULONG)InterlockedIncrement(&m->refs); }
static ULONG STDMETHODCALLTYPE menu_release(Menu *m)
{
    LONG r = InterlockedDecrement(&m->refs);
    if (!r) {
        vec_release(m->commands);
        zfree(m);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE menu_name(Menu *m, HSTRING *n) { (void)m; return named(L"Windows.UI.Popups.PopupMenu", n); }
static HRESULT STDMETHODCALLTYPE menu_commands(Menu *m, void **out)
{
    if (!out) return E_POINTER;
    vec_addref(m->commands);
    *out = m->commands;
    return S_OK;
}

/* the menu's window and its scale: device-independent pixels to the
 * screen's */
/* (no window, or one not shown, as the hidden CoreWindow XAML gives on a
 * desktop thread that hosts XAML islands, gives way to the thread's focus window,
 * the island's, whose client area the content fills; then its active one) */
static HWND menu_window(Menu *m)
{
    if (m->window && IsWindow(m->window) && IsWindowVisible(m->window)) return m->window;
    HWND w = GetFocus();
    return w ? w : GetActiveWindow();
}
static POINT to_screen(HWND w, float x, float y)
{
    float scale = w ? (float)GetDpiForWindow(w) / 96.0f : 1.0f;
    if (scale <= 0) scale = 1.0f;
    POINT p = { (LONG)(x * scale + 0.5f), (LONG)(y * scale + 0.5f) };
    if (w) ClientToScreen(w, &p);
    return p;
}

static HRESULT start(Menu *m, POINT at, const RECT *exclude, UINT flags, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (m->commands->n > MAX_COMMANDS) return E_INVALIDARG;
    Op *o = zalloc(sizeof *o);
    if (!o) return E_OUTOFMEMORY;
    o->op = op_vt;
    o->info = op_info_vt;
    o->refs = 2;                        /* (the caller's and the waiting list's) */
    InitializeSRWLock(&o->lock);
    o->id = (UINT32)InterlockedIncrement(&g_next_id);
    o->owner = menu_window(m);
    o->at = at;
    if (exclude) o->exclude = *exclude;
    o->flags = flags;
    for (UINT32 i = 0; i < m->commands->n; i++) { o->items[i] = m->commands->items[i]; obj_addref(o->items[i]); }
    o->n = m->commands->n;
    AcquireSRWLockExclusive(&g_wait_lock);
    o->timer = SetTimer(0, 0, 0, open_menu);
    if (o->timer) { o->next = g_waiting; g_waiting = o; }
    ReleaseSRWLockExclusive(&g_wait_lock);
    if (!o->timer) { o->refs = 1; op_release(o); return HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY); }
    *out = &o->op;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE menu_show(Menu *m, PointF at, void **out)
{
    HWND w = menu_window(m);
    return start(m, to_screen(w, at.x, at.y), 0, TPM_LEFTALIGN | TPM_TOPALIGN, out);
}
/* beside @r: below it (Placement Default 0, Below 2, Left 3, Right 4 drop
 * there too), above it for Above (1); the menu keeps off the rectangle */
static HRESULT STDMETHODCALLTYPE menu_show_rect_placement(Menu *m, RectF r, int placement, void **out)
{
    HWND w = menu_window(m);
    POINT a = to_screen(w, r.x, r.y), b = to_screen(w, r.x + r.w, r.y + r.h);
    RECT ex = { a.x, a.y, b.x, b.y };
    POINT at = { a.x, placement == 1 ? a.y : b.y };
    return start(m, at, &ex, TPM_LEFTALIGN | (placement == 1 ? TPM_BOTTOMALIGN : TPM_TOPALIGN) | TPM_VERTICAL, out);
}
static HRESULT STDMETHODCALLTYPE menu_show_rect(Menu *m, RectF r, void **out) { return menu_show_rect_placement(m, r, 0, out); }
static const void *const menu_vt[] = { (void *)menu_qi, (void *)menu_addref, (void *)menu_release, (void *)no_iids, (void *)menu_name,
                                       (void *)base_trust, (void *)menu_commands, (void *)menu_show, (void *)menu_show_rect,
                                       (void *)menu_show_rect_placement };

/* IInitializeWithWindow (an IUnknown interface) */
static HRESULT STDMETHODCALLTYPE mi_qi(void *p, REFIID iid, void **out) { return menu_qi(MENU_OF_INIT(p), iid, out); }
static ULONG STDMETHODCALLTYPE mi_addref(void *p) { return menu_addref(MENU_OF_INIT(p)); }
static ULONG STDMETHODCALLTYPE mi_release(void *p) { return menu_release(MENU_OF_INIT(p)); }
/* (taken as given: XAML hands over its host's window, which a desktop
 * thread's islands may not have; showing falls back as menu_window says) */
static HRESULT STDMETHODCALLTYPE mi_initialize(void *p, HWND w)
{
    MENU_OF_INIT(p)->window = w;
    return S_OK;
}
static const void *const menu_init_vt[] = { (void *)mi_qi, (void *)mi_addref, (void *)mi_release, (void *)mi_initialize };

static HRESULT STDMETHODCALLTYPE mf_qi(void *f, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!is_base(iid) && !IsEqualGUID(iid, &IID_IActivationFactory_p)) { *out = 0; return E_NOINTERFACE; }
    *out = f;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mf_name(void *f, HSTRING *n) { (void)f; return named(L"Windows.UI.Popups.PopupMenu", n); }
static HRESULT STDMETHODCALLTYPE mf_activate(void *f, void **out)
{
    (void)f;
    if (!out) return E_POINTER;
    *out = 0;
    Menu *m = zalloc(sizeof *m);
    Commands *c = zalloc(sizeof *c);
    if (!m || !c) { zfree(m); zfree(c); return E_OUTOFMEMORY; }
    c->vt = commands_vt;
    c->refs = 1;
    m->vt = menu_vt;
    m->init = menu_init_vt;
    m->refs = 1;
    m->commands = c;
    *out = &m->vt;
    return S_OK;
}
static const void *const mf_vt[] = { (void *)mf_qi, (void *)static_ref, (void *)static_ref, (void *)no_iids, (void *)mf_name,
                                     (void *)base_trust, (void *)mf_activate };
static struct { const void *const *vt; } g_menu_factory = { mf_vt };

static BOOL name_is(HSTRING s, const WCHAR *want)
{
    UINT32 n, i = 0;
    const WCHAR *c = WindowsGetStringRawBuffer(s, &n);
    for (; i < n && want[i]; i++) if (c[i] != want[i]) return FALSE;
    return i == n && !want[i];
}

HRESULT popups_factory(HSTRING cls, void **factory)
{
    if (!factory) return E_POINTER;
    if (name_is(cls, L"Windows.UI.Popups.PopupMenu")) { *factory = &g_menu_factory; return S_OK; }
    if (name_is(cls, L"Windows.UI.Popups.UICommand")) { *factory = &g_command_factory.act; return S_OK; }
    *factory = 0;
    return CLASS_E_CLASSNOTAVAILABLE;
}
