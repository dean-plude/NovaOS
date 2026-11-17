/* stacktest.exe — a thread's stack the way Windows lays it out
 *
 * Runtimes look at their stack's pages: the .NET runtime, before it
 * prints an unhandled exception, looks for the guard page (VirtualQuery,
 * PAGE_GUARD) between the stack's reservation and its base, and without
 * one reports every unhandled exception as "Stack overflow.".  So, on
 * the first thread and on a new one:
 *   - VirtualQuery finds one committed PAGE_GUARD page on the stack,
 *     inside [DeallocationStack, StackBase), below the running frame;
 *   - the TEB's StackLimit sits right on top of it;
 *   - as the stack grows (frames touching one page after another) the
 *     guard moves down ahead of it and StackLimit follows, so a guard
 *     page is still there afterwards.
 * "stacktest" runs as a 64-bit program and C:\Programs\x86\stacktest.exe
 * as a 32-bit one.
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <winternl.h>

#ifndef PAGE_GUARD
#define PAGE_GUARD 0x100
#endif

WINBASEAPI VOID WINAPI GetCurrentThreadStackLimits(PULONG_PTR low, PULONG_PTR high);

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s: %s (line %d)\n", g_who, what, __LINE__); } } while (0)
static const char *g_who = "?";

#ifdef _WIN64
#define OFF_STACK_LIMIT 0x10
#else
#define OFF_STACK_LIMIT 0x08
#endif

static ULONG_PTR stack_limit(void) { return *(ULONG_PTR *)(NtCurrentTebBytes() + OFF_STACK_LIMIT); }

/* The first PAGE_GUARD page between @low and @high (0: none), walked
 * region by region as the .NET runtime does */
static ULONG_PTR find_guard(ULONG_PTR low, ULONG_PTR high, int *nguard)
{
    ULONG_PTR found = 0;
    *nguard = 0;
    for (ULONG_PTR a = low; a < high; ) {
        MEMORY_BASIC_INFORMATION mi;
        if (VirtualQuery((void *)a, &mi, sizeof(mi)) != sizeof(mi) || !mi.RegionSize) break;
        if (mi.State == MEM_COMMIT && (mi.Protect & PAGE_GUARD)) {
            if (!found) found = (ULONG_PTR)mi.BaseAddress;
            *nguard += (int)(mi.RegionSize / 4096);
        }
        a = (ULONG_PTR)mi.BaseAddress + mi.RegionSize;
    }
    return found;
}

/* Frames of about 1 KB, each touching its own locals: the stack grows a
 * page at a time, as compiled code grows it */
static __attribute__((noinline)) ULONG_PTR grow(int depth)
{
    volatile char buf[1000];
    buf[0] = (char)depth;
    buf[sizeof(buf) - 1] = (char)depth;
    ULONG_PTR at = (ULONG_PTR)&buf[0];
    if (depth <= 0) return at;
    ULONG_PTR deepest = grow(depth - 1);
    return deepest + (ULONG_PTR)(buf[0] - (char)depth);    /* (keeps the frame) */
}

static void check_stack(const char *who)
{
    g_who = who;
    ULONG_PTR low = 0, high = 0, here = (ULONG_PTR)&low;
    GetCurrentThreadStackLimits(&low, &high);
    CHECK("the stack's limits hold the running frame", low < here && here < high);

    int n = 0;
    ULONG_PTR guard = find_guard(low + 4096, high, &n);
    CHECK("VirtualQuery finds a PAGE_GUARD page on the stack", guard != 0);
    CHECK("exactly one guard page", n == 1);
    CHECK("the guard page is below the running frame", guard && guard + 4096 <= here);
    CHECK("StackLimit sits on top of the guard page", guard && stack_limit() == guard + 4096);
    MEMORY_BASIC_INFORMATION mi;
    CHECK("the guard page is committed read/write + PAGE_GUARD",
          guard && VirtualQuery((void *)guard, &mi, sizeof(mi)) == sizeof(mi) &&
          mi.State == MEM_COMMIT && mi.Protect == (PAGE_READWRITE | PAGE_GUARD) &&
          (ULONG_PTR)mi.AllocationBase == low);

    /* grow the stack by about 128 KB */
    ULONG_PTR deepest = grow(128);
    CHECK("the frames went below the first guard page", guard && deepest < guard);
    ULONG_PTR guard2 = find_guard(low + 4096, high, &n);
    CHECK("a guard page is still there after the stack grew", guard2 != 0 && n == 1);
    CHECK("it moved down, below the deepest frame", guard2 && guard2 < guard && guard2 + 4096 <= deepest);
    CHECK("StackLimit followed it", guard2 && stack_limit() == guard2 + 4096);
    printf("stacktest: %s: stack %lu KB, guard %lu KB below the base, then %lu KB after growing\n", who,
           (unsigned long)((high - low) >> 10), (unsigned long)((high - guard) >> 10),
           (unsigned long)((high - guard2) >> 10));
}

static DWORD WINAPI thread_main(LPVOID arg)
{
    (void)arg;
    check_stack("new thread");
    return 0;
}

int main(void)
{
    check_stack("first thread");
    HANDLE h = CreateThread(NULL, 0, thread_main, NULL, 0, NULL);
    g_who = "CreateThread";
    CHECK("CreateThread", h != NULL);
    if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
    printf("stacktest: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
