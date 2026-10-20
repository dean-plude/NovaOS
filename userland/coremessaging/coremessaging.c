/*
 * CoreMessaging.dll — the DispatcherQueue: a thread's queue of work and
 * timers, run by that thread's message loop.  Windows keeps
 * Windows.System.DispatcherQueue here; the Windows App SDK's lifted copy,
 * Microsoft.UI.Dispatching.DispatcherQueue (whose interfaces are the same
 * as Windows.System's, under other IIDs, plus IDispatcherQueue3's event
 * loop), is what Microsoft's XAML framework source asks for: every XAML
 * thread needs one, and the control layer's dispatcher (CXcpDispatcher)
 * runs the core's messages from a DispatcherQueueTimer.  This DLL serves
 * both namespaces from one set of objects; each object answers the IIDs of
 * either.
 *
 * Programs reach the classes through RoGetActivationFactory (the registry's
 * ActivatableClassId keys, kernel/um/um_registry.c) or, for Windows.System,
 * CreateDispatcherQueueController (DispatcherQueueOptions):
 *   {Windows.System,Microsoft.UI.Dispatching}.DispatcherQueue
 *       (IDispatcherQueueStatics: GetForCurrentThread)
 *   {Windows.System,Microsoft.UI.Dispatching}.DispatcherQueueController
 *       (IDispatcherQueueControllerStatics: CreateOnDedicatedThread, and
 *       Microsoft.UI's CreateOnCurrentThread)
 *
 * A queue belongs to one thread (a TLS slot names it) and owns a
 * message-only window there: TryEnqueue posts it a message, and the
 * thread's message loop (any: GetMessage/DispatchMessage, a modal loop,
 * RunEventLoop) runs the work, high priority first, then normal, then low.
 * A DispatcherQueueTimer is a user32 timer on that window (an interval of
 * zero is the shortest user32 gives); Tick fires on the queue's thread.
 * ShutdownQueueAsync (or ShutdownQueue), on the queue's thread, raises
 * ShutdownStarting and then ShutdownCompleted, stops the timers, drops the
 * work not yet run and frees the thread for a new queue; a dedicated
 * thread then ends.  Deferrals taken in ShutdownStarting complete at once
 * (the shutdown does not wait for them).
 */
#include <windows.h>
#include <roapi.h>

#define CM_API __declspec(dllexport)
typedef BYTE boolean_t;

#define E_ILLEGAL_METHOD_CALL_   ((HRESULT)0x8000000EL)
#define RPC_E_WRONG_THREAD_      ((HRESULT)0x8001010EL)
#define WM_NOVA_DQ_WORK          (WM_APP + 0x3A2)       /* TryEnqueue's message */
#define WM_NOVA_DQ_SHUTDOWN      (WM_APP + 0x3A3)       /* a dedicated thread's shutdown, posted from elsewhere */
#define TIMER_MIN_MS             10                     /* user32's least interval (USER_TIMER_MINIMUM) */

static const GUID IID_IUnknown_     = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_ = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_ = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IActivationFactory_ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IAsyncInfo_   = { 0x00000036, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IAsyncAction_ = { 0x5A648006, 0x843A, 0x4DA9, { 0x86, 0x5B, 0x9D, 0x26, 0xE5, 0xDF, 0xAD, 0x7B } };
static const GUID IID_IDeferral_    = { 0xD6269732, 0x3B7F, 0x46A7, { 0xB4, 0x0B, 0x4F, 0xDC, 0xA2, 0xA2, 0xC6, 0x93 } };
static const GUID IID_IClosable_    = { 0x30D5A829, 0x7FA4, 0x4026, { 0x83, 0xBB, 0xD7, 0x5B, 0xAE, 0x4E, 0xA9, 0x9E } };

/* Each interface in each namespace: [0] Microsoft.UI.Dispatching, [1] Windows.System */
enum { NS_MUI, NS_SYS, NS_COUNT };
static const GUID IID_IDispatcherQueue_[NS_COUNT] = {
    { 0xF6EBF8FA, 0xBE1C, 0x5BF6, { 0xA4, 0x67, 0x73, 0xDA, 0x28, 0x73, 0x8A, 0xE8 } },
    { 0x603E88E4, 0xA338, 0x4FFE, { 0xA4, 0x57, 0xA5, 0xCF, 0xB9, 0xCE, 0xB8, 0x99 } } };
static const GUID IID_IDispatcherQueue2_[NS_COUNT] = {
    { 0x0CF48751, 0xF1AC, 0x59B8, { 0xBA, 0x52, 0x6C, 0xE7, 0xA1, 0x44, 0x4D, 0x6F } },
    { 0xC822C647, 0x30EF, 0x506E, { 0xBD, 0x1E, 0xA6, 0x47, 0xAE, 0x66, 0x75, 0xFF } } };
static const GUID IID_IDispatcherQueue3_ =      /* (Microsoft.UI's only: the event loop) */
    { 0x14A7A175, 0x5C27, 0x5A35, { 0xB0, 0x79, 0x21, 0x96, 0x0C, 0xF7, 0x64, 0xA8 } };
static const GUID IID_IDispatcherQueueController_[NS_COUNT] = {
    { 0xBCE8178D, 0x2183, 0x584C, { 0x9E, 0x5B, 0xF9, 0x36, 0x6F, 0x6A, 0xE4, 0x84 } },
    { 0x22F34E66, 0x50DB, 0x4E36, { 0xA9, 0x8D, 0x61, 0xC0, 0x1B, 0x38, 0x4D, 0x20 } } };
static const GUID IID_IDispatcherQueueController2_ =    /* (Microsoft.UI's only: ShutdownQueue) */
    { 0x4C68EE2A, 0x1CB1, 0x5591, { 0xA3, 0xA2, 0x9B, 0x59, 0x0B, 0x8F, 0x8B, 0x9A } };
static const GUID IID_IDispatcherQueueControllerStatics_[NS_COUNT] = {
    { 0xF18D6145, 0x722B, 0x593D, { 0xBC, 0xF2, 0xA6, 0x1E, 0x71, 0x3F, 0x00, 0x37 } },
    { 0x0A6C98E0, 0x5198, 0x49A2, { 0xA3, 0x13, 0x3F, 0x70, 0xD1, 0xF1, 0x3C, 0x27 } } };
static const GUID IID_IDispatcherQueueStatics_[NS_COUNT] = {
    { 0xCD3382EA, 0xA455, 0x5124, { 0xB6, 0x3A, 0xCA, 0x40, 0xD3, 0x4C, 0xA2, 0x3C } },
    { 0xA96D83D7, 0x9371, 0x4517, { 0x92, 0x45, 0xD0, 0x82, 0x4A, 0xC1, 0x2C, 0x74 } } };
static const GUID IID_IDispatcherQueueTimer_[NS_COUNT] = {
    { 0xAD4D63FD, 0x88FE, 0x541F, { 0xAC, 0x11, 0xBF, 0x2D, 0xC1, 0xED, 0x2C, 0xE5 } },
    { 0x5FEABB1D, 0xA31C, 0x4727, { 0xB1, 0xAC, 0x37, 0x45, 0x46, 0x49, 0xD5, 0x6A } } };
static const GUID IID_IDispatcherQueueShutdownStartingEventArgs_[NS_COUNT] = {
    { 0x32519BE5, 0x072B, 0x5660, { 0xA7, 0x0E, 0x88, 0x35, 0xC9, 0xB8, 0x15, 0x7D } },
    { 0xC4724C4C, 0xFF97, 0x40C0, { 0xA2, 0x26, 0xCC, 0x0A, 0xAA, 0x54, 0x5E, 0x89 } } };
static const WCHAR *const NS_PREFIX[NS_COUNT] = { L"Microsoft.UI.Dispatching.", L"Windows.System." };

/* Calls into the caller's objects by vtable slot (IUnknown's three first) */
typedef HRESULT (STDMETHODCALLTYPE *Fn0)(void *);
typedef HRESULT (STDMETHODCALLTYPE *FnPP)(void *, void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *FnPI)(void *, void *, int);
#define SLOT(o, i) (((void **)*(void **)(o))[i])
static ULONG obj_addref(void *o) { return ((ULONG (STDMETHODCALLTYPE *)(void *))SLOT(o, 1))(o); }
static ULONG obj_release(void *o) { return ((ULONG (STDMETHODCALLTYPE *)(void *))SLOT(o, 2))(o); }

static void *mem_alloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void mem_free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

static HRESULT class_name(int ns, const WCHAR *cls, HSTRING *out)
{
    WCHAR buf[96];
    UINT32 n = 0;
    if (!out) return E_POINTER;
    for (const WCHAR *s = NS_PREFIX[ns]; *s; s++) buf[n++] = *s;
    for (const WCHAR *s = cls; *s && n < 95; s++) buf[n++] = *s;
    return WindowsCreateString(buf, n, out);
}

static BOOL is_base(REFIID iid)
{
    return IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_);
}
/* the namespace whose @iids holds @iid, or -1 */
static int ns_of(const GUID *iids, REFIID iid)
{
    for (int ns = 0; ns < NS_COUNT; ns++)
        if (IsEqualGUID(iid, &iids[ns])) return ns;
    return -1;
}

/* Every object here is a set of interface pointers (Itf), each knowing its
 * object and namespace; a method gets the Itf and finds the object */
typedef struct { const void *const *vt; void *self; int ns; } Itf;
#define SELF(T, p) ((T *)((Itf *)(p))->self)
#define NS(p) (((Itf *)(p))->ns)

static HRESULT STDMETHODCALLTYPE no_iids(void *o, ULONG *n, GUID **iids) { (void)o; if (!n || !iids) return E_POINTER; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE base_trust(void *o, int *t) { (void)o; if (!t) return E_POINTER; *t = BaseTrust; return S_OK; }

/* ---- event handler lists ----------------------------------------------
 * A handler (TypedEventHandler<S, A>) is a delegate: Invoke(sender, args)
 * is the slot after IUnknown's three. */
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
        Sub *s = mem_alloc(cap * sizeof *s);
        if (!s) { ReleaseSRWLockExclusive(&g_ev_lock); return E_OUTOFMEMORY; }
        if (e->subs) { for (UINT32 i = 0; i < e->n; i++) s[i] = e->subs[i]; mem_free(e->subs); }
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
    return S_OK;
}
/* Calls each handler with (sender, args), on a copy of the list */
static void ev_fire(Event *e, void *sender, void *args)
{
    void *local[16], **hs = local;
    AcquireSRWLockShared(&g_ev_lock);
    UINT32 n = e->n;
    if (n > 16) hs = mem_alloc(n * sizeof *hs);
    if (!hs) { ReleaseSRWLockShared(&g_ev_lock); return; }
    for (UINT32 i = 0; i < n; i++) { hs[i] = e->subs[i].handler; obj_addref(hs[i]); }
    ReleaseSRWLockShared(&g_ev_lock);
    for (UINT32 i = 0; i < n; i++) {
        ((FnPP)SLOT(hs[i], 3))(hs[i], sender, args);
        obj_release(hs[i]);
    }
    if (hs != local) mem_free(hs);
}
static void ev_clear(Event *e)
{
    AcquireSRWLockExclusive(&g_ev_lock);
    Sub *s = e->subs;
    UINT32 n = e->n;
    e->subs = 0;
    e->n = e->cap = 0;
    ReleaseSRWLockExclusive(&g_ev_lock);
    for (UINT32 i = 0; i < n; i++) obj_release(s[i].handler);
    mem_free(s);
}

/* ---- the queue ---------------------------------------------------------- */
typedef struct Work { struct Work *next; void *handler; } Work;
typedef struct Timer Timer;
typedef struct Queue {
    Itf q[NS_COUNT], q2[NS_COUNT], q3;  /* IDispatcherQueue, IDispatcherQueue2, (Microsoft.UI's) IDispatcherQueue3 */
    LONG refs;
    DWORD tid;
    HWND hwnd;                          /* message-only, on the queue's thread */
    CRITICAL_SECTION lock;
    Work *head[3], *tail[3];            /* by priority: high, normal, low */
    BOOL posted;                        /* a WM_NOVA_DQ_WORK is on its way */
    BOOL shut;                          /* shut down: takes no more work */
    BOOL dedicated;                     /* runs on a thread of its own */
    BOOL exit_loop;                     /* EnqueueEventLoopExit asked RunEventLoop to return */
    Timer *timers;                      /* the running timers (each holds a reference while it runs) */
    Event starting, completed, fw_starting, fw_completed;
} Queue;
struct Timer {
    Itf t[NS_COUNT];                    /* IDispatcherQueueTimer */
    LONG refs;
    Queue *queue;                       /* (counted) */
    INT64 interval;                     /* in 100 ns */
    boolean_t repeating, running;
    Timer *next;                        /* in the queue's running timers */
    Event tick;
};

static DWORD g_tls = TLS_OUT_OF_INDEXES;
static const WCHAR WND_CLASS[] = L"Nova.DispatcherQueue";

static Queue *current_queue(void) { return g_tls == TLS_OUT_OF_INDEXES ? 0 : TlsGetValue(g_tls); }

static ULONG queue_addref(Queue *q) { return (ULONG)InterlockedIncrement(&q->refs); }
static ULONG queue_release(Queue *q)
{
    LONG r = InterlockedDecrement(&q->refs);
    if (!r) {
        ev_clear(&q->starting); ev_clear(&q->completed);
        ev_clear(&q->fw_starting); ev_clear(&q->fw_completed);
        DeleteCriticalSection(&q->lock);
        mem_free(q);
    }
    return (ULONG)r;
}

/* the queue's interface for the namespace @ns, as the sender of its events */
static void *queue_itf(Queue *q, int ns) { return &q->q[ns]; }

static HRESULT STDMETHODCALLTYPE q_qi(void *p, REFIID iid, void **out)
{
    Queue *q = SELF(Queue, p);
    int ns;
    if (!out) return E_POINTER;
    if (is_base(iid)) *out = p;
    else if ((ns = ns_of(IID_IDispatcherQueue_, iid)) >= 0) *out = &q->q[ns];
    else if ((ns = ns_of(IID_IDispatcherQueue2_, iid)) >= 0) *out = &q->q2[ns];
    else if (IsEqualGUID(iid, &IID_IDispatcherQueue3_)) *out = &q->q3;
    else { *out = 0; return E_NOINTERFACE; }
    queue_addref(q);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE q_addref(void *p) { return queue_addref(SELF(Queue, p)); }
static ULONG STDMETHODCALLTYPE q_release(void *p) { return queue_release(SELF(Queue, p)); }
static HRESULT STDMETHODCALLTYPE q_name(void *p, HSTRING *n) { return class_name(NS(p), L"DispatcherQueue", n); }

/* Runs the queue's work, high priority first; work enqueued meanwhile runs
 * in the same pass (as each item is taken from the front) */
static void queue_run(Queue *q)
{
    for (;;) {
        Work *w = 0;
        EnterCriticalSection(&q->lock);
        q->posted = FALSE;
        for (int i = 0; i < 3 && !w; i++)
            if ((w = q->head[i])) {
                q->head[i] = w->next;
                if (!w->next) q->tail[i] = 0;
            }
        LeaveCriticalSection(&q->lock);
        if (!w) return;
        ((Fn0)SLOT(w->handler, 3))(w->handler);         /* DispatcherQueueHandler::Invoke() */
        obj_release(w->handler);
        mem_free(w);
    }
}

static HRESULT STDMETHODCALLTYPE q_try_enqueue_priority(void *p, int priority, void *handler, boolean_t *result)
{
    Queue *q = SELF(Queue, p);
    if (!handler || !result) return E_INVALIDARG;
    /* DispatcherQueuePriority: Low -10, Normal 0, High 10 */
    int band = priority > 0 ? 0 : priority < 0 ? 2 : 1;
    Work *w = mem_alloc(sizeof *w);
    if (!w) return E_OUTOFMEMORY;
    obj_addref(handler);
    w->handler = handler;
    EnterCriticalSection(&q->lock);
    if (q->shut) {
        LeaveCriticalSection(&q->lock);
        obj_release(handler);
        mem_free(w);
        *result = 0;
        return S_OK;
    }
    if (q->tail[band]) q->tail[band]->next = w; else q->head[band] = w;
    q->tail[band] = w;
    BOOL post = !q->posted;
    q->posted = TRUE;
    LeaveCriticalSection(&q->lock);
    if (post) PostMessageW(q->hwnd, WM_NOVA_DQ_WORK, 0, 0);
    *result = 1;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE q_try_enqueue(void *p, void *handler, boolean_t *result)
{
    return q_try_enqueue_priority(p, 0, handler, result);
}
static HRESULT timer_create(Queue *q, int ns, void **out);
static HRESULT STDMETHODCALLTYPE q_create_timer(void *p, void **out) { return timer_create(SELF(Queue, p), NS(p), out); }
static HRESULT STDMETHODCALLTYPE q_add_starting(void *p, void *h, EventRegistrationToken *t) { return ev_add(&SELF(Queue, p)->starting, h, t); }
static HRESULT STDMETHODCALLTYPE q_remove_starting(void *p, EventRegistrationToken t) { return ev_remove(&SELF(Queue, p)->starting, t); }
static HRESULT STDMETHODCALLTYPE q_add_completed(void *p, void *h, EventRegistrationToken *t) { return ev_add(&SELF(Queue, p)->completed, h, t); }
static HRESULT STDMETHODCALLTYPE q_remove_completed(void *p, EventRegistrationToken t) { return ev_remove(&SELF(Queue, p)->completed, t); }
static HRESULT STDMETHODCALLTYPE q_has_thread_access(void *p, boolean_t *v)
{
    if (!v) return E_POINTER;
    *v = SELF(Queue, p)->tid == GetCurrentThreadId();
    return S_OK;
}

/* IDispatcherQueue3: the thread's own event loop */
static HRESULT STDMETHODCALLTYPE q_enqueue_loop_exit(void *p)
{
    Queue *q = SELF(Queue, p);
    q->exit_loop = TRUE;
    PostMessageW(q->hwnd, WM_NULL, 0, 0);               /* (wakes the loop) */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE q_ensure_system(void *p) { (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE q_run_loop(void *p)
{
    Queue *q = SELF(Queue, p);
    MSG msg;
    if (q->tid != GetCurrentThreadId()) return RPC_E_WRONG_THREAD_;
    q->exit_loop = FALSE;
    while (!q->exit_loop && GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    q->exit_loop = FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE q_run_loop_options(void *p, int options, void *deferral)
{
    (void)options; (void)deferral;
    return q_run_loop(p);
}
static HRESULT STDMETHODCALLTYPE q_add_fw_starting(void *p, void *h, EventRegistrationToken *t) { return ev_add(&SELF(Queue, p)->fw_starting, h, t); }
static HRESULT STDMETHODCALLTYPE q_remove_fw_starting(void *p, EventRegistrationToken t) { return ev_remove(&SELF(Queue, p)->fw_starting, t); }
static HRESULT STDMETHODCALLTYPE q_add_fw_completed(void *p, void *h, EventRegistrationToken *t) { return ev_add(&SELF(Queue, p)->fw_completed, h, t); }
static HRESULT STDMETHODCALLTYPE q_remove_fw_completed(void *p, EventRegistrationToken t) { return ev_remove(&SELF(Queue, p)->fw_completed, t); }

#define Q_FNS (void *)q_qi, (void *)q_addref, (void *)q_release, (void *)no_iids, (void *)q_name, (void *)base_trust
static const void *const queue_vt[] = { Q_FNS, (void *)q_create_timer, (void *)q_try_enqueue, (void *)q_try_enqueue_priority,
                                        (void *)q_add_starting, (void *)q_remove_starting,
                                        (void *)q_add_completed, (void *)q_remove_completed };
static const void *const queue2_vt[] = { Q_FNS, (void *)q_has_thread_access };
static const void *const queue3_vt[] = { Q_FNS, (void *)q_enqueue_loop_exit, (void *)q_ensure_system, (void *)q_run_loop,
                                         (void *)q_run_loop_options, (void *)q_add_fw_starting, (void *)q_remove_fw_starting,
                                         (void *)q_add_fw_completed, (void *)q_remove_fw_completed };

/* ---- timers ------------------------------------------------------------- */
static ULONG timer_addref(Timer *t) { return (ULONG)InterlockedIncrement(&t->refs); }
static ULONG timer_release(Timer *t)
{
    LONG r = InterlockedDecrement(&t->refs);
    if (!r) {
        ev_clear(&t->tick);
        queue_release(t->queue);
        mem_free(t);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE t_qi(void *p, REFIID iid, void **out)
{
    Timer *t = SELF(Timer, p);
    int ns;
    if (!out) return E_POINTER;
    if (is_base(iid)) *out = p;
    else if ((ns = ns_of(IID_IDispatcherQueueTimer_, iid)) >= 0) *out = &t->t[ns];
    else { *out = 0; return E_NOINTERFACE; }
    timer_addref(t);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE t_addref(void *p) { return timer_addref(SELF(Timer, p)); }
static ULONG STDMETHODCALLTYPE t_release(void *p) { return timer_release(SELF(Timer, p)); }
static HRESULT STDMETHODCALLTYPE t_name(void *p, HSTRING *n) { return class_name(NS(p), L"DispatcherQueueTimer", n); }

/* the user32 timer: its interval in milliseconds (zero is user32's least) */
static UINT timer_ms(Timer *t)
{
    INT64 ms = t->interval / 10000;
    return ms <= 0 ? TIMER_MIN_MS : ms > 0x7FFFFFFF ? 0x7FFFFFFF : (UINT)ms;
}
/* Takes a running timer off its queue's list and its window (the list's
 * reference is the caller's to drop); FALSE if it was not running */
static BOOL timer_unlink(Timer *t)
{
    Queue *q = t->queue;
    BOOL found = FALSE;
    EnterCriticalSection(&q->lock);
    for (Timer **pp = &q->timers; *pp; pp = &(*pp)->next)
        if (*pp == t) { *pp = t->next; found = TRUE; break; }
    t->running = 0;
    LeaveCriticalSection(&q->lock);
    if (found) KillTimer(q->hwnd, (UINT_PTR)t);
    return found;
}
static HRESULT STDMETHODCALLTYPE t_get_interval(void *p, INT64 *v) { if (!v) return E_POINTER; *v = SELF(Timer, p)->interval; return S_OK; }
static HRESULT STDMETHODCALLTYPE t_put_interval(void *p, INT64 v)
{
    Timer *t = SELF(Timer, p);
    if (v < 0) return E_INVALIDARG;
    t->interval = v;
    if (t->running) SetTimer(t->queue->hwnd, (UINT_PTR)t, timer_ms(t), 0);     /* (restarts it) */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE t_get_running(void *p, boolean_t *v) { if (!v) return E_POINTER; *v = SELF(Timer, p)->running; return S_OK; }
static HRESULT STDMETHODCALLTYPE t_get_repeating(void *p, boolean_t *v) { if (!v) return E_POINTER; *v = SELF(Timer, p)->repeating; return S_OK; }
static HRESULT STDMETHODCALLTYPE t_put_repeating(void *p, boolean_t v) { SELF(Timer, p)->repeating = v ? 1 : 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE t_start(void *p)
{
    Timer *t = SELF(Timer, p);
    Queue *q = t->queue;
    EnterCriticalSection(&q->lock);
    if (q->shut) { LeaveCriticalSection(&q->lock); return E_ILLEGAL_METHOD_CALL_; }
    if (!t->running) {
        t->running = 1;
        t->next = q->timers;
        q->timers = t;
        timer_addref(t);                                /* (the running list's) */
    }
    LeaveCriticalSection(&q->lock);
    SetTimer(q->hwnd, (UINT_PTR)t, timer_ms(t), 0);     /* (a running timer starts its interval over) */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE t_stop(void *p)
{
    Timer *t = SELF(Timer, p);
    if (timer_unlink(t)) timer_release(t);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE t_add_tick(void *p, void *h, EventRegistrationToken *tok) { return ev_add(&SELF(Timer, p)->tick, h, tok); }
static HRESULT STDMETHODCALLTYPE t_remove_tick(void *p, EventRegistrationToken tok) { return ev_remove(&SELF(Timer, p)->tick, tok); }
static const void *const timer_vt[] = { (void *)t_qi, (void *)t_addref, (void *)t_release, (void *)no_iids, (void *)t_name,
                                        (void *)base_trust, (void *)t_get_interval, (void *)t_put_interval, (void *)t_get_running,
                                        (void *)t_get_repeating, (void *)t_put_repeating, (void *)t_start, (void *)t_stop,
                                        (void *)t_add_tick, (void *)t_remove_tick };

static HRESULT timer_create(Queue *q, int ns, void **out)
{
    if (!out) return E_POINTER;
    Timer *t = mem_alloc(sizeof *t);
    if (!t) return E_OUTOFMEMORY;
    for (int i = 0; i < NS_COUNT; i++) t->t[i] = (Itf){ timer_vt, t, i };
    t->refs = 1;
    t->queue = q;
    queue_addref(q);
    t->repeating = 1;                                   /* (as Windows' are) */
    *out = &t->t[ns];
    return S_OK;
}

/* A timer's WM_TIMER: a one-shot timer stops before its Tick */
static void timer_fire(Queue *q, Timer *t)
{
    BOOL found = FALSE;
    EnterCriticalSection(&q->lock);
    for (Timer *x = q->timers; x; x = x->next)
        if (x == t) { found = TRUE; timer_addref(t); break; }
    LeaveCriticalSection(&q->lock);
    if (!found) { KillTimer(q->hwnd, (UINT_PTR)t); return; }
    if (!t->repeating && timer_unlink(t)) timer_release(t);
    /* the sender is the timer, in the namespace of the Tick's handlers (they
     * take either interface, the vtables being the same) */
    ev_fire(&t->tick, &t->t[NS_MUI], 0);
    timer_release(t);
}

/* ---- the queue's window and thread ------------------------------------ */
static void queue_shutdown(Queue *q);
static LRESULT CALLBACK queue_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Queue *q = (Queue *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (q) {
        if (msg == WM_NOVA_DQ_WORK) { queue_run(q); return 0; }
        if (msg == WM_TIMER) { timer_fire(q, (Timer *)wp); return 0; }
        if (msg == WM_NOVA_DQ_SHUTDOWN) { queue_shutdown(q); PostQuitMessage(0); return 0; }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HRESULT queue_create(Queue **out)
{
    static LONG registered;
    if (current_queue()) return RPC_E_WRONG_THREAD_;   /* (one queue per thread, as Windows has it) */
    if (!InterlockedExchange(&registered, 1)) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = queue_wndproc;
        wc.hInstance = GetModuleHandleW(0);
        wc.lpszClassName = WND_CLASS;
        RegisterClassW(&wc);
    }
    Queue *q = mem_alloc(sizeof *q);
    if (!q) return E_OUTOFMEMORY;
    for (int i = 0; i < NS_COUNT; i++) {
        q->q[i] = (Itf){ queue_vt, q, i };
        q->q2[i] = (Itf){ queue2_vt, q, i };
    }
    q->q3 = (Itf){ queue3_vt, q, NS_MUI };
    q->refs = 1;                                        /* (the thread's, until it shuts down) */
    q->tid = GetCurrentThreadId();
    InitializeCriticalSection(&q->lock);
    q->hwnd = CreateWindowExW(0, WND_CLASS, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, GetModuleHandleW(0), 0);
    if (!q->hwnd) {
        DeleteCriticalSection(&q->lock);
        mem_free(q);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    SetWindowLongPtrW(q->hwnd, GWLP_USERDATA, (LONG_PTR)q);
    TlsSetValue(g_tls, q);
    *out = q;
    return S_OK;
}

/* ---- ShutdownStarting's arguments and their deferral ------------------- */
typedef struct { Itf a[NS_COUNT]; LONG refs; } StartArgs;
typedef struct { Itf d, c; LONG refs; } Deferral;     /* IDeferral, IClosable */
static HRESULT STDMETHODCALLTYPE d_qi(void *p, REFIID iid, void **out)
{
    Deferral *d = SELF(Deferral, p);
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IDeferral_)) *out = &d->d;
    else if (IsEqualGUID(iid, &IID_IClosable_)) *out = &d->c;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&d->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE d_addref(void *p) { return (ULONG)InterlockedIncrement(&SELF(Deferral, p)->refs); }
static ULONG STDMETHODCALLTYPE d_release(void *p)
{
    Deferral *d = SELF(Deferral, p);
    LONG r = InterlockedDecrement(&d->refs);
    if (!r) mem_free(d);
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE d_name(void *p, HSTRING *n) { (void)p; return WindowsCreateString(L"Windows.Foundation.Deferral", 27, n); }
static HRESULT STDMETHODCALLTYPE d_complete(void *p) { (void)p; return S_OK; }
static const void *const deferral_vt[] = { (void *)d_qi, (void *)d_addref, (void *)d_release, (void *)no_iids, (void *)d_name,
                                           (void *)base_trust, (void *)d_complete };

static HRESULT STDMETHODCALLTYPE sa_qi(void *p, REFIID iid, void **out)
{
    StartArgs *a = SELF(StartArgs, p);
    int ns;
    if (!out) return E_POINTER;
    if (is_base(iid)) *out = p;
    else if ((ns = ns_of(IID_IDispatcherQueueShutdownStartingEventArgs_, iid)) >= 0) *out = &a->a[ns];
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&a->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE sa_addref(void *p) { return (ULONG)InterlockedIncrement(&SELF(StartArgs, p)->refs); }
static ULONG STDMETHODCALLTYPE sa_release(void *p)
{
    StartArgs *a = SELF(StartArgs, p);
    LONG r = InterlockedDecrement(&a->refs);
    if (!r) mem_free(a);
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE sa_name(void *p, HSTRING *n) { return class_name(NS(p), L"DispatcherQueueShutdownStartingEventArgs", n); }
static HRESULT STDMETHODCALLTYPE sa_deferral(void *p, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    Deferral *d = mem_alloc(sizeof *d);
    if (!d) return E_OUTOFMEMORY;
    d->d = (Itf){ deferral_vt, d, 0 };
    d->c = (Itf){ deferral_vt, d, 0 };                  /* (IClosable's Close is IDeferral's Complete's slot) */
    d->refs = 1;
    *out = &d->d;
    return S_OK;
}
static const void *const start_args_vt[] = { (void *)sa_qi, (void *)sa_addref, (void *)sa_release, (void *)no_iids, (void *)sa_name,
                                             (void *)base_trust, (void *)sa_deferral };

/* Shuts the queue down on its own thread: the framework's then the app's
 * ShutdownStarting, the work and timers dropped, the window gone, then the
 * framework's and the app's ShutdownCompleted */
static void queue_shutdown(Queue *q)
{
    EnterCriticalSection(&q->lock);
    BOOL was = q->shut;
    LeaveCriticalSection(&q->lock);
    if (was) return;
    StartArgs *a = mem_alloc(sizeof *a);
    if (a) {
        for (int i = 0; i < NS_COUNT; i++) a->a[i] = (Itf){ start_args_vt, a, i };
        a->refs = 1;
        ev_fire(&q->fw_starting, queue_itf(q, NS_MUI), &a->a[NS_MUI]);
        ev_fire(&q->starting, queue_itf(q, NS_MUI), &a->a[NS_MUI]);
        sa_release(&a->a[0]);
    }
    queue_run(q);                                       /* (work enqueued so far still runs) */
    EnterCriticalSection(&q->lock);
    q->shut = TRUE;
    Timer *timers = q->timers;
    q->timers = 0;
    LeaveCriticalSection(&q->lock);
    while (timers) {
        Timer *next = timers->next;
        KillTimer(q->hwnd, (UINT_PTR)timers);
        timers->running = 0;
        timer_release(timers);
        timers = next;
    }
    SetWindowLongPtrW(q->hwnd, GWLP_USERDATA, 0);
    DestroyWindow(q->hwnd);
    if (current_queue() == q) TlsSetValue(g_tls, 0);
    ev_fire(&q->fw_completed, queue_itf(q, NS_MUI), 0);
    ev_fire(&q->completed, queue_itf(q, NS_MUI), 0);
    queue_release(q);                                   /* (the thread's reference) */
}

/* ---- a completed IAsyncAction (ShutdownQueueAsync's) ------------------- */
typedef struct { Itf op, info; LONG refs; void *completed; } Action;
static HRESULT STDMETHODCALLTYPE act_qi(void *p, REFIID iid, void **out)
{
    Action *a = SELF(Action, p);
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IAsyncAction_)) *out = &a->op;
    else if (IsEqualGUID(iid, &IID_IAsyncInfo_)) *out = &a->info;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&a->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE act_addref(void *p) { return (ULONG)InterlockedIncrement(&SELF(Action, p)->refs); }
static ULONG STDMETHODCALLTYPE act_release(void *p)
{
    Action *a = SELF(Action, p);
    LONG r = InterlockedDecrement(&a->refs);
    if (!r) {
        if (a->completed) obj_release(a->completed);
        mem_free(a);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE act_name(void *p, HSTRING *n) { (void)p; return WindowsCreateString(L"Windows.Foundation.IAsyncAction", 31, n); }
/* the action is complete, so a handler is called at once */
static HRESULT STDMETHODCALLTYPE act_put_completed(void *p, void *h)
{
    Action *a = SELF(Action, p);
    if (!h) return E_INVALIDARG;
    if (a->completed) return E_ILLEGAL_METHOD_CALL_;
    obj_addref(h);
    a->completed = h;
    ((FnPI)SLOT(h, 3))(h, &a->op, 1);                  /* AsyncStatus Completed */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE act_get_completed(void *p, void **h)
{
    Action *a = SELF(Action, p);
    if (!h) return E_POINTER;
    *h = a->completed;
    if (*h) obj_addref(*h);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE act_results(void *p) { (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE ai_id(void *p, UINT32 *v) { (void)p; if (!v) return E_POINTER; *v = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE ai_status(void *p, int *v) { (void)p; if (!v) return E_POINTER; *v = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE ai_error(void *p, HRESULT *v) { (void)p; if (!v) return E_POINTER; *v = S_OK; return S_OK; }
static HRESULT STDMETHODCALLTYPE ai_nothing(void *p) { (void)p; return S_OK; }
#define ACT_FNS (void *)act_qi, (void *)act_addref, (void *)act_release, (void *)no_iids, (void *)act_name, (void *)base_trust
static const void *const action_vt[] = { ACT_FNS, (void *)act_put_completed, (void *)act_get_completed, (void *)act_results };
static const void *const action_info_vt[] = { ACT_FNS, (void *)ai_id, (void *)ai_status, (void *)ai_error, (void *)ai_nothing,
                                              (void *)ai_nothing };

static HRESULT completed_action(void **out)
{
    if (!out) return E_POINTER;
    Action *a = mem_alloc(sizeof *a);
    if (!a) return E_OUTOFMEMORY;
    a->op = (Itf){ action_vt, a, 0 };
    a->info = (Itf){ action_info_vt, a, 0 };
    a->refs = 1;
    *out = &a->op;
    return S_OK;
}

/* ---- the controller ----------------------------------------------------- */
typedef struct {
    Itf c[NS_COUNT], c2;                /* IDispatcherQueueController, (Microsoft.UI's) IDispatcherQueueController2 */
    LONG refs;
    Queue *queue;                       /* (counted) */
    HANDLE thread;                      /* a dedicated queue's thread */
} Controller;

static HRESULT STDMETHODCALLTYPE c_qi(void *p, REFIID iid, void **out)
{
    Controller *c = SELF(Controller, p);
    int ns;
    if (!out) return E_POINTER;
    if (is_base(iid)) *out = p;
    else if ((ns = ns_of(IID_IDispatcherQueueController_, iid)) >= 0) *out = &c->c[ns];
    else if (IsEqualGUID(iid, &IID_IDispatcherQueueController2_)) *out = &c->c2;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&c->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE c_addref(void *p) { return (ULONG)InterlockedIncrement(&SELF(Controller, p)->refs); }
static ULONG STDMETHODCALLTYPE c_release(void *p)
{
    Controller *c = SELF(Controller, p);
    LONG r = InterlockedDecrement(&c->refs);
    if (!r) {
        if (c->thread) CloseHandle(c->thread);
        queue_release(c->queue);
        mem_free(c);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE c_name(void *p, HSTRING *n) { return class_name(NS(p), L"DispatcherQueueController", n); }
static HRESULT STDMETHODCALLTYPE c_queue(void *p, void **out)
{
    Controller *c = SELF(Controller, p);
    if (!out) return E_POINTER;
    queue_addref(c->queue);
    *out = queue_itf(c->queue, NS(p));
    return S_OK;
}
/* A dedicated queue's thread shuts it down itself and ends; a current
 * thread's queue is shut down on that thread */
static HRESULT controller_shutdown(Controller *c)
{
    Queue *q = c->queue;
    if (c->thread) {
        PostMessageW(q->hwnd, WM_NOVA_DQ_SHUTDOWN, 0, 0);
        if (GetCurrentThreadId() != q->tid) WaitForSingleObject(c->thread, INFINITE);
        return S_OK;
    }
    if (q->tid != GetCurrentThreadId()) return RPC_E_WRONG_THREAD_;
    queue_shutdown(q);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_shutdown_async(void *p, void **out)
{
    HRESULT hr = controller_shutdown(SELF(Controller, p));
    return FAILED(hr) ? hr : completed_action(out);
}
static HRESULT STDMETHODCALLTYPE c_shutdown(void *p) { return controller_shutdown(SELF(Controller, p)); }
#define C_FNS (void *)c_qi, (void *)c_addref, (void *)c_release, (void *)no_iids, (void *)c_name, (void *)base_trust
static const void *const controller_vt[] = { C_FNS, (void *)c_queue, (void *)c_shutdown_async };
static const void *const controller2_vt[] = { C_FNS, (void *)c_shutdown };

static HRESULT controller_make(Queue *q, HANDLE thread, int ns, void **out)
{
    Controller *c = mem_alloc(sizeof *c);
    if (!c) return E_OUTOFMEMORY;
    for (int i = 0; i < NS_COUNT; i++) c->c[i] = (Itf){ controller_vt, c, i };
    c->c2 = (Itf){ controller2_vt, c, NS_MUI };
    c->refs = 1;
    c->queue = q;
    queue_addref(q);
    c->thread = thread;
    *out = &c->c[ns];
    return S_OK;
}

static HRESULT create_on_current_thread(int ns, void **out)
{
    Queue *q;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = 0;
    if (FAILED(hr = queue_create(&q))) return hr;
    return controller_make(q, 0, ns, out);
}

/* The dedicated thread: makes its queue, hands it over, runs it until shut down */
typedef struct { HANDLE ready; Queue *queue; HRESULT hr; } Dedicated;
static DWORD WINAPI dedicated_main(void *arg)
{
    Dedicated *d = arg;
    Queue *q = 0;
    d->hr = queue_create(&q);
    if (SUCCEEDED(d->hr)) { q->dedicated = TRUE; queue_addref(q); }
    d->queue = q;
    SetEvent(d->ready);                                 /* (d is the creator's, gone after this) */
    if (!q) return 1;
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {            /* (until the shutdown's WM_QUIT) */
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    queue_release(q);
    return 0;
}
static HRESULT create_on_dedicated_thread(int ns, void **out)
{
    Dedicated d = { 0 };
    if (!out) return E_POINTER;
    *out = 0;
    d.ready = CreateEventW(0, TRUE, FALSE, 0);
    HANDLE th = CreateThread(0, 0, dedicated_main, &d, 0, 0);
    if (!th) { CloseHandle(d.ready); return HRESULT_FROM_WIN32(GetLastError()); }
    WaitForSingleObject(d.ready, INFINITE);
    CloseHandle(d.ready);
    if (FAILED(d.hr)) { WaitForSingleObject(th, INFINITE); CloseHandle(th); return d.hr; }
    HRESULT hr = controller_make(d.queue, th, ns, out);
    queue_release(d.queue);                             /* (the reference the thread took for us) */
    return hr;
}

/* ---- activation factories ----------------------------------------------
 * Each class's factory is static: IActivationFactory (no ActivateInstance)
 * with the class's statics beside it. */
typedef struct { Itf f[NS_COUNT], s[NS_COUNT]; const GUID *statics_iids; const WCHAR *cls; } Factory;
static HRESULT STDMETHODCALLTYPE f_qi(void *p, REFIID iid, void **out)
{
    Factory *f = SELF(Factory, p);
    int ns;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &f->f[NS(p)];
    else if ((ns = ns_of(f->statics_iids, iid)) >= 0) *out = &f->s[ns];
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static ULONG STDMETHODCALLTYPE static_ref(void *p) { (void)p; return 2; }
static HRESULT STDMETHODCALLTYPE f_name(void *p, HSTRING *n) { return class_name(NS(p), SELF(Factory, p)->cls, n); }
static HRESULT STDMETHODCALLTYPE f_activate(void *p, void **out) { (void)p; if (out) *out = 0; return E_NOTIMPL; }
#define F_FNS (void *)f_qi, (void *)static_ref, (void *)static_ref, (void *)no_iids, (void *)f_name, (void *)base_trust

static HRESULT STDMETHODCALLTYPE qs_get_for_current_thread(void *p, void **out)
{
    if (!out) return E_POINTER;
    Queue *q = current_queue();
    *out = 0;
    if (q) { queue_addref(q); *out = queue_itf(q, NS(p)); }
    return S_OK;                                        /* (null on a thread with no queue, as on Windows) */
}
static HRESULT STDMETHODCALLTYPE cs_dedicated(void *p, void **out) { return create_on_dedicated_thread(NS(p), out); }
static HRESULT STDMETHODCALLTYPE cs_current(void *p, void **out) { return create_on_current_thread(NS(p), out); }
static const void *const factory_vt[] = { F_FNS, (void *)f_activate };
static const void *const queue_statics_vt[] = { F_FNS, (void *)qs_get_for_current_thread };
static const void *const controller_statics_vt[] = { F_FNS, (void *)cs_dedicated, (void *)cs_current };

static Factory g_queue_factory = {
    { { factory_vt, &g_queue_factory, NS_MUI }, { factory_vt, &g_queue_factory, NS_SYS } },
    { { queue_statics_vt, &g_queue_factory, NS_MUI }, { queue_statics_vt, &g_queue_factory, NS_SYS } },
    IID_IDispatcherQueueStatics_, L"DispatcherQueue" };
static Factory g_controller_factory = {
    { { factory_vt, &g_controller_factory, NS_MUI }, { factory_vt, &g_controller_factory, NS_SYS } },
    { { controller_statics_vt, &g_controller_factory, NS_MUI }, { controller_statics_vt, &g_controller_factory, NS_SYS } },
    IID_IDispatcherQueueControllerStatics_, L"DispatcherQueueController" };

static BOOL name_is(HSTRING s, int ns, const WCHAR *cls)
{
    UINT32 n;
    const WCHAR *p = WindowsGetStringRawBuffer(s, &n), *a = NS_PREFIX[ns];
    UINT32 i = 0;
    for (; *a; a++, i++) if (i >= n || p[i] != *a) return FALSE;
    for (; *cls; cls++, i++) if (i >= n || p[i] != *cls) return FALSE;
    return i == n;
}

CM_API HRESULT WINAPI DllGetActivationFactory(HSTRING cls, void **factory)
{
    if (!factory) return E_POINTER;
    for (int ns = 0; ns < NS_COUNT; ns++) {
        if (name_is(cls, ns, L"DispatcherQueue")) { *factory = &g_queue_factory.f[ns]; return S_OK; }
        if (name_is(cls, ns, L"DispatcherQueueController")) { *factory = &g_controller_factory.f[ns]; return S_OK; }
    }
    *factory = 0;
    return CLASS_E_CLASSNOTAVAILABLE;
}
CM_API HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

/* Windows.System's C entry point (DispatcherQueueOptions: dwSize,
 * threadType DQTYPE_THREAD_DEDICATED 1 or DQTYPE_THREAD_CURRENT 2,
 * apartmentType, which a NovaOS thread does not need) */
typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;
CM_API HRESULT WINAPI CreateDispatcherQueueController(DispatcherQueueOptions_ options, void **controller)
{
    if (!controller) return E_POINTER;
    if (options.dwSize != sizeof options) return E_INVALIDARG;
    if (options.threadType == 1) return create_on_dedicated_thread(NS_SYS, controller);
    if (options.threadType == 2) return create_on_current_thread(NS_SYS, controller);
    return E_INVALIDARG;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) g_tls = TlsAlloc();
    return TRUE;
}
