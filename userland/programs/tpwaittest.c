/* tpwaittest.exe — thread-pool waits (CreateThreadpoolWait) as Windows runs
 * them: a wait calls back once when its object is signaled, or with
 * WAIT_TIMEOUT when its time runs out; a callback may set its own wait
 * again; setting a wait never waits for a running callback (a thread
 * holding a lock the callback takes may set it: the pattern UMDF's work
 * items use); cancelling stops a wait that has not fired; and a wait may be
 * closed from its own callback. */
#include <windows.h>
#include <stdio.h>

typedef VOID (WINAPI *TpWaitFn)(PVOID instance, PVOID ctx, PVOID wait, DWORD result);
WINBASEAPI PVOID WINAPI CreateThreadpoolWait(TpWaitFn fn, PVOID ctx, PVOID env);
WINBASEAPI VOID WINAPI SetThreadpoolWait(PVOID wait, HANDLE obj, PFILETIME timeout);
WINBASEAPI VOID WINAPI WaitForThreadpoolWaitCallbacks(PVOID wait, BOOL cancel);
WINBASEAPI VOID WINAPI CloseThreadpoolWait(PVOID wait);

static int passed, failed;

static void check(int ok, const char *what)
{
    if (ok) passed++;
    else { failed++; printf("FAIL: %s\n", what); }
}

/* 1. once per signal, with WAIT_OBJECT_0 */
static volatile LONG g_calls;
static volatile DWORD g_result;
static HANDLE g_fired;

static VOID WINAPI count_cb(PVOID inst, PVOID ctx, PVOID wait, DWORD result)
{
    (void)inst; (void)wait;
    check(ctx == (PVOID)&g_calls, "the callback gets its context");
    g_result = result;
    InterlockedIncrement(&g_calls);
    SetEvent(g_fired);
}

/* 2. a callback that sets its own wait again, 100 times */
static PVOID g_rearm;
static HANDLE g_rearm_ev;
static volatile LONG g_rearms;

static VOID WINAPI rearm_cb(PVOID inst, PVOID ctx, PVOID wait, DWORD result)
{
    (void)inst; (void)ctx; (void)result;
    if (InterlockedIncrement(&g_rearms) < 100) {
        SetThreadpoolWait(wait, g_rearm_ev, NULL);
        SetEvent(g_rearm_ev);
    } else {
        SetEvent(g_fired);
    }
}

/* 3. a callback that takes a lock its setter holds */
static CRITICAL_SECTION g_lock;
static HANDLE g_in_callback;
static volatile LONG g_locked_calls;

static VOID WINAPI locked_cb(PVOID inst, PVOID ctx, PVOID wait, DWORD result)
{
    (void)inst; (void)ctx; (void)wait; (void)result;
    SetEvent(g_in_callback);
    EnterCriticalSection(&g_lock);
    InterlockedIncrement(&g_locked_calls);
    LeaveCriticalSection(&g_lock);
    SetEvent(g_fired);
}

static HANDLE g_lock_ev;
static PVOID g_locked;
static volatile LONG g_setter_done;

static DWORD WINAPI setter(LPVOID unused)
{
    (void)unused;
    EnterCriticalSection(&g_lock);
    SetThreadpoolWait(g_locked, g_lock_ev, NULL);       /* the first callback runs and blocks on the lock */
    SetEvent(g_lock_ev);
    WaitForSingleObject(g_in_callback, 5000);
    SetThreadpoolWait(g_locked, g_lock_ev, NULL);       /* while it runs: must not wait for it */
    g_setter_done = 1;
    LeaveCriticalSection(&g_lock);
    return 0;
}

/* 5. closed by its own callback */
static VOID WINAPI close_cb(PVOID inst, PVOID ctx, PVOID wait, DWORD result)
{
    (void)inst; (void)ctx; (void)result;
    CloseThreadpoolWait(wait);
    SetEvent(g_fired);
}

int main(void)
{
    g_fired = CreateEventW(NULL, FALSE, FALSE, NULL);

    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    PVOID w = CreateThreadpoolWait(count_cb, (PVOID)&g_calls, NULL);
    check(w != NULL, "CreateThreadpoolWait");
    SetThreadpoolWait(w, ev, NULL);
    SetEvent(ev);
    check(WaitForSingleObject(g_fired, 5000) == WAIT_OBJECT_0, "the wait calls back when its event is set");
    check(g_result == WAIT_OBJECT_0, "with WAIT_OBJECT_0");
    SetEvent(ev);
    Sleep(200);
    check(g_calls == 1, "once: an unset wait does not call back again");

    FILETIME due;
    ULARGE_INTEGER rel;
    rel.QuadPart = (ULONGLONG)(-100LL * 10000);         /* 100 ms from now */
    due.dwLowDateTime = rel.LowPart;
    due.dwHighDateTime = rel.HighPart;
    HANDLE never = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetThreadpoolWait(w, never, &due);
    check(WaitForSingleObject(g_fired, 5000) == WAIT_OBJECT_0 && g_result == WAIT_TIMEOUT,
          "a timeout calls back with WAIT_TIMEOUT");

    SetThreadpoolWait(w, never, NULL);
    WaitForThreadpoolWaitCallbacks(w, TRUE);
    SetEvent(never);
    Sleep(200);
    check(g_calls == 2, "a cancelled wait does not call back");
    CloseThreadpoolWait(w);

    g_rearm_ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_rearm = CreateThreadpoolWait(rearm_cb, NULL, NULL);
    SetThreadpoolWait(g_rearm, g_rearm_ev, NULL);
    SetEvent(g_rearm_ev);
    check(WaitForSingleObject(g_fired, 10000) == WAIT_OBJECT_0 && g_rearms == 100,
          "a callback sets its own wait again (100 times)");
    WaitForThreadpoolWaitCallbacks(g_rearm, FALSE);
    CloseThreadpoolWait(g_rearm);

    InitializeCriticalSection(&g_lock);
    g_in_callback = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_lock_ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_locked = CreateThreadpoolWait(locked_cb, NULL, NULL);
    HANDLE t = CreateThread(NULL, 0, setter, NULL, 0, NULL);
    check(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0 && g_setter_done,
          "setting a wait does not wait for its running callback");
    check(WaitForSingleObject(g_fired, 5000) == WAIT_OBJECT_0, "the blocked callback finished");
    SetEvent(g_lock_ev);
    check(WaitForSingleObject(g_fired, 5000) == WAIT_OBJECT_0 && g_locked_calls == 2, "the wait set meanwhile fired");
    CloseHandle(t);
    WaitForThreadpoolWaitCallbacks(g_locked, FALSE);
    CloseThreadpoolWait(g_locked);

    PVOID c = CreateThreadpoolWait(close_cb, NULL, NULL);
    SetThreadpoolWait(c, ev, NULL);
    SetEvent(ev);
    check(WaitForSingleObject(g_fired, 5000) == WAIT_OBJECT_0, "a callback closes its own wait");

    printf("tpwaittest: %d passed, %d failed\n", passed, failed);
    return failed;
}
