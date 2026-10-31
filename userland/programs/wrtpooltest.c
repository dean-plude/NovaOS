/* wrtpooltest.exe — the Windows.System classes a UWP app's start-up asks
 * for (Windows Calculator's, and its XAML's): MemoryManager from its
 * activation factory (RoGetActivationFactory finds twinapi.appcore.dll in
 * the registry's ActivatableClassId keys) with the process's memory use,
 * its limit and level, a usage event's handler taken and removed, the app
 * and process memory reports and a refused TrySetAppMemoryUsageLimit; and
 * Windows.System.Threading's ThreadPool: RunAsync's work item on a pool
 * thread given its own IAsyncAction, completing it (Completed set before
 * and after the end), at High priority, failing with its HRESULT, and
 * cancelled while it runs (its Status shows it); and ThreadPoolTimer: a
 * one-shot timer firing after its Delay though the caller let it go, then
 * destroyed; a periodic one ticking until cancelled and no more after; and
 * a one-shot cancelled before it fires; and
 * Windows.ApplicationModel.UserActivities' UserActivityRequestManager
 * (Calculator's MainPage asks it): the view's manager, the same each time,
 * taking and removing a UserActivityRequested handler */
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
#define RELEASE(o) do { if (o) VT(o, 2, Rel_t)(o); (o) = NULL; } while (0)

static const GUID IID_IInspectable_T = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAsyncInfo_T = { 0x00000036, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IMemoryManagerStatics_T  = { 0x5C6C279C, 0xD7CA, 0x4779, { 0x91, 0x88, 0x40, 0x57, 0x21, 0x9C, 0xE6, 0x4C } };
static const GUID IID_IMemoryManagerStatics2_T = { 0x6EEE351F, 0x6D62, 0x423F, { 0x94, 0x79, 0xB0, 0x1F, 0x9C, 0x9F, 0x76, 0x69 } };
static const GUID IID_IMemoryManagerStatics3_T = { 0x149B59CE, 0x92AD, 0x4E35, { 0x89, 0xEB, 0x50, 0xDF, 0xB4, 0xC0, 0xD9, 0x1C } };
static const GUID IID_IMemoryManagerStatics4_T = { 0xC5A94828, 0xE84E, 0x4886, { 0x8A, 0x0D, 0x44, 0xB3, 0x19, 0x0E, 0x3B, 0x72 } };
static const GUID IID_IAppMemoryReport2_T      = { 0x5F7F3738, 0x51B7, 0x42DC, { 0xB7, 0xED, 0x79, 0xBA, 0x46, 0xD2, 0x88, 0x57 } };
static const GUID IID_IThreadPoolStatics_T      = { 0xB6BF67DD, 0x84BD, 0x44F8, { 0xAC, 0x1C, 0x93, 0xEB, 0xCB, 0x9D, 0xBA, 0x91 } };
static const GUID IID_IUserActivityRequestManagerStatics_T = { 0xC0392DF1, 0x224A, 0x432C, { 0x81, 0xE5, 0x0C, 0x76, 0xB4, 0xC4, 0xCE, 0xFA } };
static const GUID IID_IUserActivityRequestManager_T = { 0x0C30BE4E, 0x903D, 0x48D6, { 0x82, 0xD4, 0x40, 0x43, 0xED, 0x57, 0x79, 0x1B } };
static const GUID IID_IThreadPoolTimerStatics_T = { 0x1A8A9D02, 0xE482, 0x461B, { 0xB8, 0xC7, 0x8E, 0xFA, 0xD1, 0xCC, 0xE5, 0x90 } };

/* ---- delegates: Invoke is the slot after IUnknown's three ----------- */
typedef struct Handler Handler;
struct Handler { const void *const *vt; HRESULT (*fn)(Handler *, void *, int); volatile LONG calls; HANDLE done; };
static HRESULT STDMETHODCALLTYPE h_qi(Handler *h, const GUID *iid, void **out) { (void)iid; *out = h; return S_OK; }
static ULONG STDMETHODCALLTYPE h_ref(Handler *h) { (void)h; return 2; }
static HRESULT STDMETHODCALLTYPE h_invoke1(Handler *h, void *a) { HRESULT hr = h->fn ? h->fn(h, a, 0) : S_OK; InterlockedIncrement(&h->calls); if (h->done) SetEvent(h->done); return hr; }
static HRESULT STDMETHODCALLTYPE h_invoke2(Handler *h, void *a, int b) { HRESULT hr = h->fn ? h->fn(h, a, b) : S_OK; InterlockedIncrement(&h->calls); if (h->done) SetEvent(h->done); return hr; }
static const void *const handler1_vt[] = { (void *)h_qi, (void *)h_ref, (void *)h_ref, (void *)h_invoke1 };
static const void *const handler2_vt[] = { (void *)h_qi, (void *)h_ref, (void *)h_ref, (void *)h_invoke2 };
static Handler handler(const void *const *vt, HRESULT (*fn)(Handler *, void *, int))
{
    Handler h = { vt, fn, 0, CreateEventW(0, FALSE, FALSE, 0) };
    return h;
}

static HRESULT factory(const WCHAR *cls, const GUID *iid, void **out)
{
    HSTRING s = 0;
    WindowsCreateString(cls, (UINT32)wcslen(cls), &s);
    HRESULT hr = RoGetActivationFactory(s, iid, out);
    WindowsDeleteString(s);
    if (hr != S_OK) printf("wrtpooltest: RoGetActivationFactory(%ls): 0x%08lX\n", cls, (unsigned long)hr);
    return hr;
}
static UINT64 get64(void *o, int slot) { UINT64 v = 0; VT(o, slot, Get_t)(o, &v); return v; }

/* ---- MemoryManager --------------------------------------------------- */
static void memory_manager(void)
{
    void *mm = 0, *mm2 = 0, *mm3 = 0, *mm4 = 0, *report = 0, *report2 = 0, *proc = 0;
    CHECK("MemoryManager's statics", factory(L"Windows.System.MemoryManager", &IID_IMemoryManagerStatics_T, &mm) == S_OK && mm);
    if (!mm) return;
    UINT64 used = get64(mm, 6), limit = get64(mm, 7);
    int level = -1;
    VT(mm, 8, Get_t)(mm, &level);
    printf("wrtpooltest: AppMemoryUsage %llu KB of %llu KB, level %d\n", used >> 10, limit >> 10, level);
    CHECK("AppMemoryUsage: some", used > 0);
    CHECK("AppMemoryUsageLimit: above it", limit > used);
    CHECK("AppMemoryUsageLevel: Low (a small program)", level == 0);
    Handler h = handler(handler2_vt, 0);
    EventRegistrationToken tok = { 0 };
    CHECK("add_AppMemoryUsageIncreased", VT(mm, 9, HRESULT (STDMETHODCALLTYPE *)(void *, void *, EventRegistrationToken *))(mm, &h, &tok) == S_OK
                                         && tok.value);
    CHECK("remove_AppMemoryUsageIncreased", VT(mm, 10, HRESULT (STDMETHODCALLTYPE *)(void *, EventRegistrationToken))(mm, tok) == S_OK);
    CHECK("add_AppMemoryUsageLimitChanging", VT(mm, 13, HRESULT (STDMETHODCALLTYPE *)(void *, void *, EventRegistrationToken *))(mm, &h, &tok) == S_OK);
    VT(mm, 14, HRESULT (STDMETHODCALLTYPE *)(void *, EventRegistrationToken))(mm, tok);

    CHECK("IMemoryManagerStatics2", SUCCEEDED(VT(mm, 0, QI_t)(mm, &IID_IMemoryManagerStatics2_T, &mm2)) && mm2);
    if (mm2) {
        CHECK("GetAppMemoryReport", VT(mm2, 6, Get_t)(mm2, &report) == S_OK && report);
        if (report) {
            CHECK("PrivateCommitUsage: some", get64(report, 6) > 0);
            CHECK("PeakPrivateCommitUsage: at least it", get64(report, 7) >= get64(report, 6));
            CHECK("TotalCommitLimit: the usage limit", get64(report, 9) == limit);
            HSTRING n = 0;
            VT(report, 4, Get_t)(report, &n);
            CHECK("its class: Windows.System.AppMemoryReport", !wcscmp(WindowsGetStringRawBuffer(n, 0), L"Windows.System.AppMemoryReport"));
            WindowsDeleteString(n);
            CHECK("IAppMemoryReport2", SUCCEEDED(VT(report, 0, QI_t)(report, &IID_IAppMemoryReport2_T, &report2)) && report2);
            if (report2) CHECK("ExpectedTotalCommitLimit", get64(report2, 6) == limit);
        }
        CHECK("GetProcessMemoryReport", VT(mm2, 7, Get_t)(mm2, &proc) == S_OK && proc);
        if (proc) CHECK("TotalWorkingSetUsage: some", get64(proc, 7) > 0);
    }
    CHECK("IMemoryManagerStatics3", SUCCEEDED(VT(mm, 0, QI_t)(mm, &IID_IMemoryManagerStatics3_T, &mm3)) && mm3);
    if (mm3) {
        BYTE ok = 1;
        CHECK("TrySetAppMemoryUsageLimit: refused (a desktop app)",
              VT(mm3, 6, HRESULT (STDMETHODCALLTYPE *)(void *, UINT64, BYTE *))(mm3, limit / 2, &ok) == S_OK && !ok);
    }
    CHECK("IMemoryManagerStatics4", SUCCEEDED(VT(mm, 0, QI_t)(mm, &IID_IMemoryManagerStatics4_T, &mm4)) && mm4);
    if (mm4) CHECK("ExpectedAppMemoryUsageLimit: the limit", get64(mm4, 6) == limit);
    RELEASE(report2); RELEASE(report); RELEASE(proc);
    RELEASE(mm4); RELEASE(mm3); RELEASE(mm2); RELEASE(mm);
}

/* ---- ThreadPool ------------------------------------------------------ */
static DWORD g_main_tid, g_work_tid;
static void *g_work_op;
static int g_work_prio = -99;
static HANDLE g_release, g_started;
static int g_status_seen = -1;
static HRESULT work_record(Handler *h, void *op, int unused)
{
    (void)h; (void)unused;
    g_work_tid = GetCurrentThreadId();
    g_work_op = op;
    g_work_prio = GetThreadPriority(GetCurrentThread());
    return S_OK;
}
static HRESULT work_fail(Handler *h, void *op, int unused) { (void)h; (void)op; (void)unused; return E_ACCESSDENIED; }
/* says it has started, waits until the main thread has cancelled it, then
 * looks at its Status */
static HRESULT work_wait(Handler *h, void *op, int unused)
{
    (void)h; (void)unused;
    SetEvent(g_started);
    WaitForSingleObject(g_release, 5000);
    void *info = 0;
    if (SUCCEEDED(VT(op, 0, QI_t)(op, &IID_IAsyncInfo_T, &info)) && info) {
        VT(info, 7, Get_t)(info, &g_status_seen);
        RELEASE(info);
    }
    return S_OK;
}
static int g_completed_status = -1;
static HRESULT on_completed(Handler *h, void *op, int status) { (void)h; (void)op; g_completed_status = status; return S_OK; }

static void *run(void *pool, Handler *work, int priority)
{
    void *op = 0;
    HRESULT hr = priority ? VT(pool, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void *, int, void **))(pool, work, priority, &op)
                          : VT(pool, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))(pool, work, &op);
    CHECK("RunAsync", hr == S_OK && op);
    return op;
}
static int status_of(void *op)
{
    void *info = 0;
    int st = -1;
    if (SUCCEEDED(VT(op, 0, QI_t)(op, &IID_IAsyncInfo_T, &info)) && info) { VT(info, 7, Get_t)(info, &st); RELEASE(info); }
    return st;
}
static HRESULT error_of(void *op)
{
    void *info = 0;
    HRESULT e = 1;
    if (SUCCEEDED(VT(op, 0, QI_t)(op, &IID_IAsyncInfo_T, &info)) && info) { VT(info, 8, Get_t)(info, &e); RELEASE(info); }
    return e;
}
static void thread_pool(void)
{
    void *pool = 0;
    CHECK("ThreadPool's statics", factory(L"Windows.System.Threading.ThreadPool", &IID_IThreadPoolStatics_T, &pool) == S_OK && pool);
    if (!pool) return;
    typedef HRESULT (STDMETHODCALLTYPE *PutCompleted_t)(void *, void *);

    /* a work item, Completed set before it ends */
    Handler work = handler(handler1_vt, work_record), done = handler(handler2_vt, on_completed);
    void *op = run(pool, &work, 0);
    if (op) {
        VT(op, 6, PutCompleted_t)(op, &done);
        CHECK("the work item ran", WaitForSingleObject(work.done, 5000) == WAIT_OBJECT_0);
        CHECK("its Completed handler called", WaitForSingleObject(done.done, 5000) == WAIT_OBJECT_0);
        CHECK("on a pool thread", g_work_tid && g_work_tid != g_main_tid);
        CHECK("given its own IAsyncAction", g_work_op == op);
        CHECK("Completed (status 1)", g_completed_status == 1 && status_of(op) == 1);
        CHECK("GetResults", VT(op, 8, Call_t)(op) == S_OK);
        CHECK("a second Completed handler refused", VT(op, 6, PutCompleted_t)(op, &done) != S_OK);
        RELEASE(op);
    }

    /* High priority; Completed set after the end is called at once */
    g_completed_status = -1;
    ResetEvent(work.done);
    op = run(pool, &work, 1);
    if (op) {
        CHECK("the High work item ran", WaitForSingleObject(work.done, 5000) == WAIT_OBJECT_0);
        Sleep(50);
        VT(op, 6, PutCompleted_t)(op, &done);
        CHECK("Completed set afterwards: called at once", g_completed_status == 1);
        CHECK("at above-normal priority", g_work_prio == THREAD_PRIORITY_ABOVE_NORMAL);
        RELEASE(op);
    }

    /* a failing work item: Error, with its HRESULT */
    Handler failing = handler(handler1_vt, work_fail);
    g_completed_status = -1;
    ResetEvent(done.done);
    op = run(pool, &failing, 0);
    if (op) {
        VT(op, 6, PutCompleted_t)(op, &done);
        WaitForSingleObject(done.done, 5000);
        CHECK("a failing work item: Error (status 3)", g_completed_status == 3 && status_of(op) == 3);
        CHECK("its ErrorCode: the work's HRESULT", error_of(op) == E_ACCESSDENIED);
        CHECK("GetResults: the HRESULT", VT(op, 8, Call_t)(op) == E_ACCESSDENIED);
        RELEASE(op);
    }

    /* cancelled while it runs */
    g_release = CreateEventW(0, TRUE, FALSE, 0);
    g_started = CreateEventW(0, TRUE, FALSE, 0);
    Handler waiting = handler(handler1_vt, work_wait);
    g_completed_status = -1;
    ResetEvent(done.done);
    op = run(pool, &waiting, 0);
    if (op) {
        VT(op, 6, PutCompleted_t)(op, &done);
        CHECK("the work item started", WaitForSingleObject(g_started, 5000) == WAIT_OBJECT_0);
        void *info = 0;
        VT(op, 0, QI_t)(op, &IID_IAsyncInfo_T, &info);
        CHECK("Cancel", info && VT(info, 9, Call_t)(info) == S_OK);
        RELEASE(info);
        SetEvent(g_release);
        WaitForSingleObject(done.done, 5000);
        CHECK("the running work sees Status Canceled", g_status_seen == 2);
        CHECK("and it ends Canceled (status 2)", g_completed_status == 2);
        RELEASE(op);
    }
    RELEASE(pool);
}

/* ---- ThreadPoolTimer ------------------------------------------------- */
static void *g_fired_timer;
static DWORD g_fired_at;
static HRESULT on_elapsed(Handler *h, void *timer, int unused) { (void)h; (void)unused; g_fired_timer = timer; g_fired_at = GetTickCount(); return S_OK; }
static void timers(void)
{
    void *statics = 0;
    CHECK("ThreadPoolTimer's statics", factory(L"Windows.System.Threading.ThreadPoolTimer", &IID_IThreadPoolTimerStatics_T, &statics) == S_OK && statics);
    if (!statics) return;
    typedef HRESULT (STDMETHODCALLTYPE *Create_t)(void *, void *, INT64, void **);
    typedef HRESULT (STDMETHODCALLTYPE *CreateD_t)(void *, void *, INT64, void *, void **);

    /* a one-shot, 100 ms, the caller's reference let go at once */
    Handler elapsed = handler(handler1_vt, on_elapsed), destroyed = handler(handler1_vt, 0);
    void *t = 0;
    DWORD start = GetTickCount();
    CHECK("CreateTimer", VT(statics, 9, CreateD_t)(statics, &elapsed, 1000000, &destroyed, &t) == S_OK && t);
    if (t) {
        INT64 delay = 0, period = -1;
        VT(t, 7, Get_t)(t, &delay);
        VT(t, 6, Get_t)(t, &period);
        CHECK("its Delay and Period", delay == 1000000 && period == 0);
        void *was = t;
        RELEASE(t);
        CHECK("it fires though let go", WaitForSingleObject(elapsed.done, 5000) == WAIT_OBJECT_0);
        CHECK("after its Delay", g_fired_at - start >= 90);
        CHECK("its handler given the timer", g_fired_timer == was);
        CHECK("then destroyed", WaitForSingleObject(destroyed.done, 5000) == WAIT_OBJECT_0 && destroyed.calls == 1);
        Sleep(200);
        CHECK("once", elapsed.calls == 1);
    }

    /* periodic, every 30 ms, until cancelled */
    Handler tick = handler(handler1_vt, 0), tick_destroyed = handler(handler1_vt, 0);
    CHECK("CreatePeriodicTimer", VT(statics, 8, CreateD_t)(statics, &tick, 300000, &tick_destroyed, &t) == S_OK && t);
    if (t) {
        DWORD until = GetTickCount() + 5000;
        while (tick.calls < 3 && GetTickCount() < until) Sleep(10);
        CHECK("it ticks again and again", tick.calls >= 3);
        CHECK("Cancel", VT(t, 8, Call_t)(t) == S_OK);
        CHECK("cancelled: destroyed", tick_destroyed.calls == 1);
        LONG n = tick.calls;
        Sleep(200);
        CHECK("and no more ticks", tick.calls == n);
        RELEASE(t);
    }

    /* a one-shot cancelled before it fires */
    Handler late = handler(handler1_vt, 0), late_destroyed = handler(handler1_vt, 0);
    CHECK("CreateTimer (1 s, no destroyed handler)", VT(statics, 7, Create_t)(statics, &late, 10000000, &t) == S_OK && t);
    if (t) {
        CHECK("Cancel before it fires", VT(t, 8, Call_t)(t) == S_OK);
        RELEASE(t);
    }
    void *t2 = 0;
    CHECK("CreateTimer (1 s)", VT(statics, 9, CreateD_t)(statics, &late, 10000000, &late_destroyed, &t2) == S_OK && t2);
    if (t2) {
        VT(t2, 8, Call_t)(t2);
        CHECK("cancelled: destroyed at once", late_destroyed.calls == 1);
        RELEASE(t2);
    }
    Sleep(1300);
    CHECK("neither fired", late.calls == 0);
    void *junk = 0;
    CHECK("CreatePeriodicTimer with no period: E_INVALIDARG", VT(statics, 6, Create_t)(statics, &late, 0, &junk) == E_INVALIDARG && !junk);
    RELEASE(statics);
}

/* ---- UserActivityRequestManager -------------------------------------- */
static void activity_requests(void)
{
    void *statics = 0, *mgr = 0, *again = 0, *as_mgr = 0;
    CHECK("UserActivityRequestManager's statics", factory(L"Windows.ApplicationModel.UserActivities.UserActivityRequestManager",
                                                          &IID_IUserActivityRequestManagerStatics_T, &statics) == S_OK && statics);
    if (!statics) return;
    CHECK("GetForCurrentView", VT(statics, 6, Get_t)(statics, &mgr) == S_OK && mgr);
    CHECK("GetForCurrentView: the same manager again", VT(statics, 6, Get_t)(statics, &again) == S_OK && again == mgr);
    if (mgr) {
        CHECK("IUserActivityRequestManager", SUCCEEDED(VT(mgr, 0, QI_t)(mgr, &IID_IUserActivityRequestManager_T, &as_mgr)) && as_mgr == mgr);
        HSTRING n = 0;
        VT(mgr, 4, Get_t)(mgr, &n);
        CHECK("its class", !wcscmp(WindowsGetStringRawBuffer(n, 0), L"Windows.ApplicationModel.UserActivities.UserActivityRequestManager"));
        WindowsDeleteString(n);
        Handler h = handler(handler2_vt, 0);
        EventRegistrationToken tok = { 0 };
        CHECK("add_UserActivityRequested", VT(mgr, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void *, EventRegistrationToken *))(mgr, &h, &tok) == S_OK
                                           && tok.value);
        CHECK("remove_UserActivityRequested", VT(mgr, 7, HRESULT (STDMETHODCALLTYPE *)(void *, EventRegistrationToken))(mgr, tok) == S_OK);
        CHECK("its handler never called (no Timeline)", h.calls == 0);
    }
    RELEASE(as_mgr); RELEASE(again); RELEASE(mgr); RELEASE(statics);
}

int main(void)
{
    g_main_tid = GetCurrentThreadId();
    CHECK("RoInitialize", SUCCEEDED(RoInitialize(RO_INIT_MULTITHREADED)));
    void *junk = 0;
    CHECK("an unknown Windows.System class: REGDB_E_CLASSNOTREG", factory(L"Windows.System.NoSuchClass", &IID_IInspectable_T, &junk) == REGDB_E_CLASSNOTREG);
    memory_manager();
    thread_pool();
    timers();
    activity_requests();
    printf("wrtpooltest: %d passed, %d failed (%d-bit)\n", pass, fail, (int)sizeof(void *) * 8);
    fflush(stdout);
    return fail != 0;
}
