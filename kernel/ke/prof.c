/*
 * prof.c — a sampling profiler on the timer tick
 *
 * Each CPU's timer tick (100 Hz) notes where it interrupted: in a program,
 * halted waiting for the big kernel lock, idle, or the kernel function and
 * its caller (frame pointers: see ksym.c).  "profile on" in the terminal
 * starts it, "profile" prints the heaviest functions and callers.
 */
#include "prof.h"
#include "ksym.h"
#include "printf.h"
#include "../lib/string.h"
#include "scheduler.h"

#define PROF_MAX 32768
enum { P_USER = 1, P_LOCKWAIT, P_IDLE };

static volatile bool g_on;
static UINT64 g_from, g_to;                 /* the window (ticks; g_to 0: open-ended) */
static bool in_window(void);
static volatile UINT32 g_n;
static UINT64 g_rip[PROF_MAX], g_caller[PROF_MAX];
#define PROF_CALLS 0x400                    /* system calls, then 0x300 + interrupt vectors */
static UINT32 g_calls[PROF_CALLS];
static UINT32 g_frames, g_whole, g_spoiled, g_slow;           /* the desktop's frames (ProfFrame) */
static UINT64 g_frame_tsc, g_frame_max;

static void serial_line(void *ctx, const char *line) { (void)ctx; (void)line; }
static volatile UINT32 g_gen;               /* which ProfStart a reporter belongs to */

/* A window with an end: report it on the serial log when it closes, so a
 * program holding the keyboard (a browser starting) needs no "profile" */
static void reporter(void *arg)
{
    UINT32 gen = (UINT32)(uintptr_t)arg;
    while (g_on && g_gen == gen && sched_ticks() < g_to) sched_sleep_until(NULL, g_to);
    if (g_on && g_gen == gen) ProfReport(serial_line, NULL);
    sched_exit_current();
}

void ProfStart(UINT64 delay, UINT64 len)
{
    g_on = false;
    g_n = 0;
    g_from = sched_ticks() + delay;
    g_to = len ? g_from + len : 0;
    memset(g_calls, 0, sizeof(g_calls));
    g_frames = g_whole = g_spoiled = g_slow = 0;
    g_frame_tsc = g_frame_max = 0;
    UINT32 gen = __atomic_add_fetch(&g_gen, 1, __ATOMIC_RELAXED);
    g_on = true;
    if (len) sched_create_thread("profile", reporter, (void *)(uintptr_t)gen, 8);
}

void ProfInterrupt(UINT64 vector)
{
    if (vector < 256 && in_window()) __atomic_add_fetch(&g_calls[0x300 + vector], 1, __ATOMIC_RELAXED);
}

void ProfFrame(UINT64 tsc, bool whole, bool spoiled)
{
    if (!in_window()) return;                       /* (the desktop thread only: no atomics) */
    g_frames++;
    g_whole += whole || spoiled;
    g_spoiled += spoiled;
    g_frame_tsc += tsc;
    if (tsc > g_frame_max) g_frame_max = tsc;
    if (tsc > (sched_tick_tsc(10) - sched_tick_tsc(0))) g_slow++;   /* (over 100 ms) */
}

void ProfSyscall(UINT64 num)
{
    if (num < PROF_CALLS && in_window()) __atomic_add_fetch(&g_calls[num], 1, __ATOMIC_RELAXED);
}

static bool in_window(void)
{
    UINT64 t = sched_ticks();
    return g_on && t >= g_from && (!g_to || t < g_to);
}

void ProfSample(UINT64 rip, UINT64 rbp, bool user, bool lock_wait, bool idle)
{
    if (!in_window()) return;
    UINT32 i = __atomic_fetch_add(&g_n, 1, __ATOMIC_RELAXED);
    if (i >= PROF_MAX) return;
    UINT64 caller = 0;
    if (lock_wait) {                                        /* who wants the lock: past the lock's own frames */
        for (int d = 0; d < 6 && rbp >= UINT64_C(0xFFFF800000000000) && !(rbp & 7); d++) {
            UINT64 ret = ((const UINT64 *)(uintptr_t)rbp)[1], off;
            const char *f = KsymLookup(ret, &off);
            if (f && strncmp(f, "raw_", 4) && strncmp(f, "bkl_", 4)) { caller = ret; break; }
            rbp = ((const UINT64 *)(uintptr_t)rbp)[0];
        }
    }
    if (user) { caller = rip; rip = P_USER; }       /* (the program's address, in caller) */
    else if (lock_wait) rip = P_LOCKWAIT;
    else if (idle) rip = P_IDLE;
    else if (rbp >= UINT64_C(0xFFFF800000000000) && !(rbp & 7))
        caller = ((const UINT64 *)(uintptr_t)rbp)[1];       /* the return address of the frame */
    g_caller[i] = caller;
    g_rip[i] = rip;
}

typedef struct { const char *a, *b; UINT32 n; } Row;
typedef struct { UINT64 a; UINT32 n; } Addr;

static int add_addr(Addr *rows, int n, int cap, UINT64 a)
{
    for (int i = 0; i < n; i++) if (rows[i].a == a) { rows[i].n++; return n; }
    if (n == cap) return n;
    rows[n].a = a; rows[n].n = 1;
    return n + 1;
}

static int add_row(Row *rows, int n, int cap, const char *a, const char *b)
{
    for (int i = 0; i < n; i++)
        if (rows[i].a == a && rows[i].b == b) { rows[i].n++; return n; }
    if (n == cap) return n;
    rows[n].a = a; rows[n].b = b; rows[n].n = 1;
    return n + 1;
}

static void top(Row *rows, int n, int want, UINT32 total, bool pairs,
                void (*out)(void *, const char *), void *ctx)
{
    char line[160];
    for (int k = 0; k < want && k < n; k++) {
        int best = k;
        for (int i = k + 1; i < n; i++) if (rows[i].n > rows[best].n) best = i;
        Row r = rows[k]; rows[k] = rows[best]; rows[best] = r;
        if (pairs) ksnprintf(line, sizeof(line), "%5u %3u%%  %s <- %s", rows[k].n, rows[k].n * 100 / total, rows[k].a, rows[k].b);
        else ksnprintf(line, sizeof(line), "%5u %3u%%  %s", rows[k].n, rows[k].n * 100 / total, rows[k].a);
        kprintf("[PROF] %s\n", line);
        out(ctx, line);
    }
}

void ProfReport(void (*out)(void *ctx, const char *line), void *ctx)
{
    g_on = false;
    UINT32 n = g_n < PROF_MAX ? g_n : PROF_MAX;
    char line[160];
    if (!n) { out(ctx, "No samples (profile on first)."); return; }
    static Row fn[512], pair[1024];
    static Addr uaddr[256];
    int nu = 0;
    int nf = 0, np = 0;
    UINT32 user = 0, wait = 0, idle = 0;
    for (UINT32 i = 0; i < n; i++) {
        if (g_rip[i] == P_USER) {                           /* by 64-byte line of program code */
            user++;
            nu = add_addr(uaddr, nu, 256, g_caller[i] & ~UINT64_C(63));
            continue;
        }
        if (g_rip[i] == P_LOCKWAIT) {
            wait++;
            UINT64 off;
            const char *b = g_caller[i] ? KsymLookup(g_caller[i], &off) : NULL;
            np = add_row(pair, np, 1024, "(kernel lock)", b ? b : "?");
            continue;
        }
        if (g_rip[i] == P_IDLE) { idle++; continue; }
        UINT64 off;
        const char *a = KsymLookup(g_rip[i], &off), *b = g_caller[i] ? KsymLookup(g_caller[i], &off) : NULL;
        if (!a) a = "?";
        if (!b) b = "?";
        nf = add_row(fn, nf, 512, a, NULL);
        np = add_row(pair, np, 1024, a, b);
    }
    ksnprintf(line, sizeof(line), "%u samples: programs %u%%, kernel %u%%, waiting for the kernel lock %u%%, idle %u%%",
              n, user * 100 / n, (n - user - wait - idle) * 100 / n, wait * 100 / n, idle * 100 / n);
    kprintf("[PROF] %s\n", line);
    out(ctx, line);
    UINT64 tick = sched_tick_tsc(1) - sched_tick_tsc(0);
    if (g_frames && tick) {
        ksnprintf(line, sizeof(line), "Desktop: %u frames (%u of the whole screen, %u of them after trying a part), %llu ms drawing, %u over 100 ms, longest %llu ms",
                  g_frames, g_whole, g_spoiled, (unsigned long long)(g_frame_tsc * 10 / tick), g_slow,
                  (unsigned long long)(g_frame_max * 10 / tick));
        kprintf("[PROF] %s\n", line);
        out(ctx, line);
    }
    top(fn, nf, 15, n, false, out, ctx);
    out(ctx, "Callers:");
    top(pair, np, 15, n, true, out, ctx);
    if (nu) {
        out(ctx, "Program code (64-byte lines):");
        for (int k = 0; k < 10 && k < nu; k++) {
            int best = k;
            for (int i = k + 1; i < nu; i++) if (uaddr[i].n > uaddr[best].n) best = i;
            Addr r = uaddr[k]; uaddr[k] = uaddr[best]; uaddr[best] = r;
            ksnprintf(line, sizeof(line), "%5u %3u%%  %p", uaddr[k].n, uaddr[k].n * 100 / n, (void *)(uintptr_t)uaddr[k].a);
            kprintf("[PROF] %s\n", line);
            out(ctx, line);
        }
    }
    out(ctx, "System calls (3xx: interrupt or exception xx under the kernel lock):");
    for (int k = 0; k < 12; k++) {
        UINT32 best = 0;
        for (UINT32 i = 1; i < PROF_CALLS; i++) if (g_calls[i] > g_calls[best]) best = i;
        if (!g_calls[best]) break;
        ksnprintf(line, sizeof(line), best >= 0x300 ? "%8u  interrupt %u" : "%8u  %03x", g_calls[best], best >= 0x300 ? best - 0x300 : best);
        kprintf("[PROF] %s\n", line);
        out(ctx, line);
        g_calls[best] = 0;
    }
}
