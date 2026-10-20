/* dispatchqtest.exe — the DispatcherQueue (CoreMessaging.dll), as
 * Microsoft's XAML framework and Windows App SDK apps use it: the
 * Microsoft.UI.Dispatching classes through RoGetActivationFactory (the
 * registry's ActivatableClassId keys), a queue made on the current thread
 * (one per thread, found again by GetForCurrentThread), work run by the
 * thread's message loop in priority order, one-shot and repeating timers,
 * RunEventLoop ended by EnqueueEventLoopExit, a queue on a dedicated thread
 * shut down from another with its ShutdownStarting and ShutdownCompleted
 * events, and Windows.System's DispatcherQueue through
 * CreateDispatcherQueueController.
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <roapi.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define VT(o, i, T) ((T)((void **)*(void **)(o))[i])
typedef ULONG (STDMETHODCALLTYPE *Rel_t)(void *);
typedef HRESULT (STDMETHODCALLTYPE *QI_t)(void *, const GUID *, void **);
typedef HRESULT (STDMETHODCALLTYPE *Get_t)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *Call_t)(void *);
typedef HRESULT (STDMETHODCALLTYPE *PutB_t)(void *, BYTE);
typedef HRESULT (STDMETHODCALLTYPE *PutT_t)(void *, INT64);
typedef HRESULT (STDMETHODCALLTYPE *Enq_t)(void *, void *, BYTE *);
typedef HRESULT (STDMETHODCALLTYPE *EnqP_t)(void *, int, void *, BYTE *);
typedef HRESULT (STDMETHODCALLTYPE *Add_t)(void *, void *, INT64 *);
#define RELEASE(o) do { if (o) VT(o, 2, Rel_t)(o); (o) = NULL; } while (0)

static const GUID IID_IUnknown_T = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IAsyncInfo_T = { 0x00000036, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_MUI_IDispatcherQueue2 = { 0x0CF48751, 0xF1AC, 0x59B8, { 0xBA, 0x52, 0x6C, 0xE7, 0xA1, 0x44, 0x4D, 0x6F } };
static const GUID IID_MUI_IDispatcherQueue3 = { 0x14A7A175, 0x5C27, 0x5A35, { 0xB0, 0x79, 0x21, 0x96, 0x0C, 0xF7, 0x64, 0xA8 } };
static const GUID IID_MUI_IDispatcherQueueStatics = { 0xCD3382EA, 0xA455, 0x5124, { 0xB6, 0x3A, 0xCA, 0x40, 0xD3, 0x4C, 0xA2, 0x3C } };
static const GUID IID_MUI_IDispatcherQueueControllerStatics = { 0xF18D6145, 0x722B, 0x593D, { 0xBC, 0xF2, 0xA6, 0x1E, 0x71, 0x3F, 0x00, 0x37 } };
static const GUID IID_MUI_IDispatcherQueueController2 = { 0x4C68EE2A, 0x1CB1, 0x5591, { 0xA3, 0xA2, 0x9B, 0x59, 0x0B, 0x8F, 0x8B, 0x9A } };
static const GUID IID_MUI_IDispatcherQueueTimer = { 0xAD4D63FD, 0x88FE, 0x541F, { 0xAC, 0x11, 0xBF, 0x2D, 0xC1, 0xED, 0x2C, 0xE5 } };
static const GUID IID_SYS_IDispatcherQueue = { 0x603E88E4, 0xA338, 0x4FFE, { 0xA4, 0x57, 0xA5, 0xCF, 0xB9, 0xCE, 0xB8, 0x99 } };

/* IDispatcherQueue's slots (IInspectable's six first), IDispatcherQueue2's,
 * IDispatcherQueue3's, the controller's, the timer's */
enum { Q_CREATE_TIMER = 6, Q_TRY_ENQUEUE, Q_TRY_ENQUEUE_PRIORITY, Q_ADD_STARTING, Q_REMOVE_STARTING, Q_ADD_COMPLETED };
enum { Q2_HAS_ACCESS = 6 };
enum { Q3_EXIT_LOOP = 6, Q3_RUN_LOOP = 8 };
enum { C_QUEUE = 6, C_SHUTDOWN_ASYNC, C2_SHUTDOWN = 6 };
enum { CS_DEDICATED = 6, CS_CURRENT };
enum { T_PUT_INTERVAL = 7, T_RUNNING, T_PUT_REPEATING = 10, T_START, T_STOP, T_ADD_TICK };

/* A delegate (DispatcherQueueHandler, TypedEventHandler): IUnknown, Invoke */
typedef struct Handler {
    const void *const *vt;
    LONG refs;
    void (*fn)(struct Handler *, void *sender, void *args);
    int tag;
    int *log, *n;                       /* (work's order) */
    DWORD tid;                          /* the thread it ran on */
    HANDLE done;
} Handler;
static HRESULT STDMETHODCALLTYPE h_qi(Handler *h, const GUID *iid, void **out) { (void)iid; *out = h; InterlockedIncrement(&h->refs); return S_OK; }
static ULONG STDMETHODCALLTYPE h_addref(Handler *h) { return (ULONG)InterlockedIncrement(&h->refs); }
static ULONG STDMETHODCALLTYPE h_release(Handler *h) { return (ULONG)InterlockedDecrement(&h->refs); }
static HRESULT STDMETHODCALLTYPE h_invoke(Handler *h, void *sender, void *args)
{
    h->tid = GetCurrentThreadId();
    h->fn(h, sender, args);
    return S_OK;
}
static const void *const handler_vt[] = { (void *)h_qi, (void *)h_addref, (void *)h_release, (void *)h_invoke };
static Handler handler(void (*fn)(Handler *, void *, void *), int tag)
{
    Handler h = { handler_vt, 1, fn, tag, 0, 0, 0, 0 };
    return h;
}

static void log_tag(Handler *h, void *s, void *a) { (void)s; (void)a; h->log[(*h->n)++] = h->tag; }
static void count(Handler *h, void *s, void *a) { (void)s; (void)a; h->tag++; if (h->done) SetEvent(h->done); }

/* Runs the thread's messages until @flag is set or @ms pass */
static void pump_until(volatile int *flag, DWORD ms)
{
    DWORD start = GetTickCount();
    MSG msg;
    while (!*flag && GetTickCount() - start < ms) {
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (!*flag) MsgWaitForMultipleObjects(0, 0, FALSE, 10, QS_ALLINPUT);
    }
}

static void *factory(const WCHAR *cls, const GUID *iid)
{
    HSTRING s = 0;
    void *f = 0;
    WindowsCreateString(cls, (UINT32)wcslen(cls), &s);
    HRESULT hr = RoGetActivationFactory(s, iid, &f);
    WindowsDeleteString(s);
    return SUCCEEDED(hr) ? f : 0;
}
static BOOL class_is(void *o, const WCHAR *want)
{
    HSTRING n = 0;
    if (FAILED(VT(o, 4, Get_t)(o, &n))) return FALSE;
    BOOL same = wcscmp(WindowsGetStringRawBuffer(n, 0), want) == 0;
    WindowsDeleteString(n);
    return same;
}
static void *qi(void *o, const GUID *iid)
{
    void *r = 0;
    return o && SUCCEEDED(VT(o, 0, QI_t)(o, iid, &r)) ? r : 0;
}
static BOOL same_object(void *a, void *b)
{
    void *ua = qi(a, &IID_IUnknown_T), *ub = qi(b, &IID_IUnknown_T);
    BOOL same = ua && ua == ub;
    RELEASE(ua);
    RELEASE(ub);
    return same;
}

static void *g_queue3;
static void exit_loop(Handler *h, void *s, void *a) { (void)s; (void)a; h->tag = 1; VT(g_queue3, Q3_EXIT_LOOP, Call_t)(g_queue3); }

static void test_current(void *qstatics, void *cstatics)
{
    void *q = (void *)1, *ctl = 0, *q2 = 0;
    CHECK("GetForCurrentThread before a queue: S_OK, null",
          SUCCEEDED(VT(qstatics, 6, Get_t)(qstatics, &q)) && q == 0);
    CHECK("CreateOnCurrentThread", SUCCEEDED(VT(cstatics, CS_CURRENT, Get_t)(cstatics, &ctl)) && ctl);
    if (!ctl) return;
    CHECK("the controller's class name", class_is(ctl, L"Microsoft.UI.Dispatching.DispatcherQueueController"));
    CHECK("a second queue on the thread is refused", FAILED(VT(cstatics, CS_CURRENT, Get_t)(cstatics, &q2)) && !q2);
    CHECK("the controller's queue", SUCCEEDED(VT(ctl, C_QUEUE, Get_t)(ctl, &q)) && q);
    CHECK("GetForCurrentThread gives it", SUCCEEDED(VT(qstatics, 6, Get_t)(qstatics, &q2)) && q2 && same_object(q, q2));
    RELEASE(q2);
    CHECK("the queue's class name", class_is(q, L"Microsoft.UI.Dispatching.DispatcherQueue"));
    void *qa = qi(q, &IID_MUI_IDispatcherQueue2);
    BYTE access = 0;
    CHECK("HasThreadAccess on its thread", qa && SUCCEEDED(VT(qa, Q2_HAS_ACCESS, Get_t)(qa, &access)) && access);
    RELEASE(qa);

    /* work runs from the thread's message loop, high priority first */
    int log[8], n = 0;
    Handler low = handler(log_tag, 1), normal = handler(log_tag, 2), high = handler(log_tag, 3);
    low.log = normal.log = high.log = log;
    low.n = normal.n = high.n = &n;
    BYTE ok1 = 0, ok2 = 0, ok3 = 0;
    VT(q, Q_TRY_ENQUEUE_PRIORITY, EnqP_t)(q, -10, &low, &ok1);
    VT(q, Q_TRY_ENQUEUE, Enq_t)(q, &normal, &ok2);
    VT(q, Q_TRY_ENQUEUE_PRIORITY, EnqP_t)(q, 10, &high, &ok3);
    CHECK("TryEnqueue takes the work", ok1 && ok2 && ok3);
    CHECK("nothing runs before the message loop", n == 0);
    volatile int three = 0;
    DWORD start = GetTickCount();
    while (n < 3 && GetTickCount() - start < 5000) pump_until(&three, 20);
    CHECK("the work ran by priority: high, normal, low", n == 3 && log[0] == 3 && log[1] == 2 && log[2] == 1);
    CHECK("the queue let go of the handlers", low.refs == 1 && normal.refs == 1 && high.refs == 1);

    /* a one-shot timer ticks once and stops; a repeating one until stopped */
    void *t = 0;
    CHECK("CreateTimer", SUCCEEDED(VT(q, Q_CREATE_TIMER, Get_t)(q, &t)) && t);
    if (t) {
        void *tt = qi(t, &IID_MUI_IDispatcherQueueTimer);
        CHECK("the timer's interface and class name", tt && class_is(t, L"Microsoft.UI.Dispatching.DispatcherQueueTimer"));
        RELEASE(tt);
        Handler tick = handler(count, 0);
        INT64 tok = 0;
        VT(t, T_PUT_INTERVAL, PutT_t)(t, 20 * 10000);
        VT(t, T_PUT_REPEATING, PutB_t)(t, 0);
        VT(t, T_ADD_TICK, Add_t)(t, &tick, &tok);
        VT(t, T_START, Call_t)(t);
        BYTE running = 0;
        VT(t, T_RUNNING, Get_t)(t, &running);
        CHECK("a started timer is running", running);
        pump_until(&tick.tag, 3000);
        pump_until(&three, 150);                        /* (time for a second tick, which must not come) */
        VT(t, T_RUNNING, Get_t)(t, &running);
        CHECK("a one-shot timer ticked once and stopped", tick.tag == 1 && !running);
        tick.tag = 0;
        VT(t, T_PUT_INTERVAL, PutT_t)(t, 0);
        VT(t, T_PUT_REPEATING, PutB_t)(t, 1);
        VT(t, T_START, Call_t)(t);
        volatile int dummy = 0;
        start = GetTickCount();
        while (tick.tag < 3 && GetTickCount() - start < 5000) pump_until(&dummy, 20);
        VT(t, T_STOP, Call_t)(t);
        int ticks = tick.tag;
        pump_until(&dummy, 100);
        CHECK("a repeating timer ticks until Stop", ticks >= 3 && tick.tag == ticks);
        RELEASE(t);
    }

    /* IDispatcherQueue3: RunEventLoop returns once EnqueueEventLoopExit is called */
    g_queue3 = qi(q, &IID_MUI_IDispatcherQueue3);
    CHECK("IDispatcherQueue3", g_queue3 != 0);
    if (g_queue3) {
        Handler quit = handler(exit_loop, 0);
        BYTE ok = 0;
        VT(q, Q_TRY_ENQUEUE, Enq_t)(q, &quit, &ok);
        CHECK("RunEventLoop runs work and returns on EnqueueEventLoopExit",
              SUCCEEDED(VT(g_queue3, Q3_RUN_LOOP, Call_t)(g_queue3)) && quit.tag == 1);
        RELEASE(g_queue3);
    }

    /* ShutdownQueue ends the queue: its events, no more work, the thread free */
    Handler started = handler(count, 0), completed = handler(count, 0), late = handler(count, 0);
    INT64 tok1 = 0, tok2 = 0;
    VT(q, Q_ADD_STARTING, Add_t)(q, &started, &tok1);
    VT(q, Q_ADD_COMPLETED, Add_t)(q, &completed, &tok2);
    void *c2 = qi(ctl, &IID_MUI_IDispatcherQueueController2);
    CHECK("ShutdownQueue", c2 && SUCCEEDED(VT(c2, C2_SHUTDOWN, Call_t)(c2)));
    RELEASE(c2);
    CHECK("ShutdownStarting then ShutdownCompleted", started.tag == 1 && completed.tag == 1);
    BYTE ok = 1;
    CHECK("a shut down queue takes no work", SUCCEEDED(VT(q, Q_TRY_ENQUEUE, Enq_t)(q, &late, &ok)) && !ok);
    CHECK("GetForCurrentThread after shutdown: null", SUCCEEDED(VT(qstatics, 6, Get_t)(qstatics, &q2)) && !q2);
    RELEASE(q);
    RELEASE(ctl);
}

static void on_dedicated(Handler *h, void *s, void *a)
{
    (void)s; (void)a;
    h->tag = 1;
    SetEvent(h->done);
}

static void test_dedicated(void *cstatics)
{
    void *ctl = 0, *q = 0;
    CHECK("CreateOnDedicatedThread", SUCCEEDED(VT(cstatics, CS_DEDICATED, Get_t)(cstatics, &ctl)) && ctl);
    if (!ctl) return;
    VT(ctl, C_QUEUE, Get_t)(ctl, &q);
    void *qa = qi(q, &IID_MUI_IDispatcherQueue2);
    BYTE access = 1;
    CHECK("HasThreadAccess from another thread: false", qa && SUCCEEDED(VT(qa, Q2_HAS_ACCESS, Get_t)(qa, &access)) && !access);
    RELEASE(qa);
    Handler work = handler(on_dedicated, 0);
    work.done = CreateEventW(0, FALSE, FALSE, 0);
    BYTE ok = 0;
    VT(q, Q_TRY_ENQUEUE, Enq_t)(q, &work, &ok);
    CHECK("work enqueued from another thread runs on the queue's thread",
          ok && WaitForSingleObject(work.done, 5000) == WAIT_OBJECT_0 && work.tag == 1 && work.tid != GetCurrentThreadId());
    Handler started = handler(count, 0), completed = handler(count, 0);
    INT64 tok1 = 0, tok2 = 0;
    VT(q, Q_ADD_STARTING, Add_t)(q, &started, &tok1);
    VT(q, Q_ADD_COMPLETED, Add_t)(q, &completed, &tok2);
    void *op = 0, *info = 0;
    int status = 0;
    CHECK("ShutdownQueueAsync from another thread", SUCCEEDED(VT(ctl, C_SHUTDOWN_ASYNC, Get_t)(ctl, &op)) && op);
    info = qi(op, &IID_IAsyncInfo_T);
    CHECK("its action is complete", info && SUCCEEDED(VT(info, 7, Get_t)(info, &status)) && status == 1);
    CHECK("the dedicated thread raised ShutdownStarting and ShutdownCompleted, on itself",
          started.tag == 1 && completed.tag == 1 && started.tid == work.tid && completed.tid == work.tid);
    RELEASE(info);
    RELEASE(op);
    RELEASE(q);
    RELEASE(ctl);
    CloseHandle(work.done);
}

/* Windows.System's queue, the same objects under Windows.System's IIDs */
typedef struct { DWORD dwSize; int threadType; int apartmentType; } DQOptions;
typedef HRESULT (WINAPI *CreateDQC_t)(DQOptions, void **);
static void test_windows_system(void)
{
    HMODULE m = LoadLibraryW(L"CoreMessaging.dll");
    CreateDQC_t create = m ? (CreateDQC_t)GetProcAddress(m, "CreateDispatcherQueueController") : 0;
    CHECK("CoreMessaging.dll exports CreateDispatcherQueueController", create != 0);
    if (!create) return;
    DQOptions o = { sizeof o, 2, 0 };
    void *ctl = 0, *q = 0;
    CHECK("CreateDispatcherQueueController on the current thread", SUCCEEDED(create(o, &ctl)) && ctl);
    if (!ctl) return;
    VT(ctl, C_QUEUE, Get_t)(ctl, &q);
    void *sq = qi(q, &IID_SYS_IDispatcherQueue);
    CHECK("Windows.System.DispatcherQueue", sq && class_is(sq, L"Windows.System.DispatcherQueue"));
    int log[2], n = 0;
    Handler h = handler(log_tag, 7);
    h.log = log;
    h.n = &n;
    BYTE ok = 0;
    if (sq) VT(sq, Q_TRY_ENQUEUE, Enq_t)(sq, &h, &ok);
    volatile int dummy = 0;
    DWORD start = GetTickCount();
    while (!n && GetTickCount() - start < 5000) pump_until(&dummy, 20);
    CHECK("its work runs", ok && n == 1 && log[0] == 7);
    void *op = 0;
    CHECK("ShutdownQueueAsync on its thread", SUCCEEDED(VT(ctl, C_SHUTDOWN_ASYNC, Get_t)(ctl, &op)) && op);
    RELEASE(op);
    RELEASE(sq);
    RELEASE(q);
    RELEASE(ctl);
}

int main(void)
{
    void *qstatics = factory(L"Microsoft.UI.Dispatching.DispatcherQueue", &IID_MUI_IDispatcherQueueStatics);
    void *cstatics = factory(L"Microsoft.UI.Dispatching.DispatcherQueueController", &IID_MUI_IDispatcherQueueControllerStatics);
    CHECK("DispatcherQueue's statics through RoGetActivationFactory", qstatics != 0);
    CHECK("DispatcherQueueController's statics", cstatics != 0);
    if (qstatics && cstatics) {
        test_current(qstatics, cstatics);
        test_dedicated(cstatics);
    }
    test_windows_system();
    RELEASE(qstatics);
    RELEASE(cstatics);
    printf("dispatchqtest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    return fail != 0;
}
