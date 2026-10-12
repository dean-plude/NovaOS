/*
 * avxtest.exe — the AVX state, as programs see it when the kernel turns
 * XSAVE on (kernel/arch/x86_64/xstate.h):
 *
 *   CPUID and XGETBV        OSXSAVE, AVX and F16C set; XCR0 holds x87, SSE
 *                           and AVX; GetEnabledXStateFeatures and
 *                           IsProcessorFeaturePresent(PF_AVX) agree
 *   F16C                    a half-float round trip
 *   context switches        four threads on two CPUs, each holding its own
 *                           values in YMM0-15 across time slices
 *   exceptions              the CONTEXT a vectored handler gets carries the
 *                           YMM upper halves (CONTEXT_XSTATE,
 *                           LocateXStateFeature); the handler clobbers them
 *                           and NtContinue puts them back
 *   GetThreadContext        another thread's YMM registers, suspended
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define CONTEXT_XSTATE_ (0x00100000 | 0x40)
#define XSTATE_AVX_ 2

WINBASEAPI DWORD64 WINAPI GetEnabledXStateFeatures(void);
WINBASEAPI BOOL WINAPI InitializeContext(PVOID buf, DWORD flags, PCONTEXT *ctx, PDWORD len);
WINBASEAPI BOOL WINAPI SetXStateFeaturesMask(PCONTEXT ctx, DWORD64 mask);
WINBASEAPI PVOID WINAPI LocateXStateFeature(PCONTEXT ctx, DWORD id, PDWORD len);

static int g_pass, g_fail;

static void check(int ok, const char *what)
{
    if (ok) g_pass++;
    else { g_fail++; printf("FAIL: %s\n", what); }
}

typedef struct { unsigned v[8]; } Ymm;             /* one YMM register */

static void fill(Ymm *r, unsigned seed)
{
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 8; j++) r[i].v[j] = seed * 0x01000193u ^ (unsigned)(i * 8 + j) * 0x9E3779B9u;
}

#define YMM_CLOBBERS "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", \
                     "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
#define LOAD16(r) \
    "vmovdqu 0x000(" r "), %%ymm0\n\t"  "vmovdqu 0x020(" r "), %%ymm1\n\t" \
    "vmovdqu 0x040(" r "), %%ymm2\n\t"  "vmovdqu 0x060(" r "), %%ymm3\n\t" \
    "vmovdqu 0x080(" r "), %%ymm4\n\t"  "vmovdqu 0x0A0(" r "), %%ymm5\n\t" \
    "vmovdqu 0x0C0(" r "), %%ymm6\n\t"  "vmovdqu 0x0E0(" r "), %%ymm7\n\t" \
    "vmovdqu 0x100(" r "), %%ymm8\n\t"  "vmovdqu 0x120(" r "), %%ymm9\n\t" \
    "vmovdqu 0x140(" r "), %%ymm10\n\t" "vmovdqu 0x160(" r "), %%ymm11\n\t" \
    "vmovdqu 0x180(" r "), %%ymm12\n\t" "vmovdqu 0x1A0(" r "), %%ymm13\n\t" \
    "vmovdqu 0x1C0(" r "), %%ymm14\n\t" "vmovdqu 0x1E0(" r "), %%ymm15\n\t"
#define STORE16(r) \
    "vmovdqu %%ymm0, 0x000(" r ")\n\t"  "vmovdqu %%ymm1, 0x020(" r ")\n\t" \
    "vmovdqu %%ymm2, 0x040(" r ")\n\t"  "vmovdqu %%ymm3, 0x060(" r ")\n\t" \
    "vmovdqu %%ymm4, 0x080(" r ")\n\t"  "vmovdqu %%ymm5, 0x0A0(" r ")\n\t" \
    "vmovdqu %%ymm6, 0x0C0(" r ")\n\t"  "vmovdqu %%ymm7, 0x0E0(" r ")\n\t" \
    "vmovdqu %%ymm8, 0x100(" r ")\n\t"  "vmovdqu %%ymm9, 0x120(" r ")\n\t" \
    "vmovdqu %%ymm10, 0x140(" r ")\n\t" "vmovdqu %%ymm11, 0x160(" r ")\n\t" \
    "vmovdqu %%ymm12, 0x180(" r ")\n\t" "vmovdqu %%ymm13, 0x1A0(" r ")\n\t" \
    "vmovdqu %%ymm14, 0x1C0(" r ")\n\t" "vmovdqu %%ymm15, 0x1E0(" r ")\n\t" \
    "vzeroupper\n\t"

#ifdef _WIN64
/* Hold @in in YMM0-15 until *@stop is set (a 0 stop: until the interrupt
 * time passes @until), then store them to @out */
static void hold(const Ymm *in, Ymm *out, volatile LONG *stop, ULONGLONG until)
{
    __asm__ volatile(LOAD16("%0")
                     "1:\n\t"
                     "pause\n\t"
                     "testq %2, %2\n\t"
                     "jz 2f\n\t"
                     "cmpl $0, (%2)\n\t"
                     "je 1b\n\t"
                     "jmp 3f\n"
                     "2:\n\t"
                     "cmpq %3, 0x7FFE0008\n\t"         /* KUSER_SHARED_DATA.InterruptTime */
                     "jb 1b\n"
                     "3:\n\t"
                     STORE16("%1")
                     : : "r"(in), "r"(out), "r"(stop), "r"(until) : YMM_CLOBBERS, "memory", "cc");
}

static ULONGLONG now(void) { return *(volatile ULONGLONG *)0x7FFE0008; }   /* 100 ns units */

/* Context switches: four threads on the two CPUs, 0.6 s each */
static int g_bad[4];
static DWORD WINAPI switch_thread(void *arg)
{
    int n = (int)(ULONG_PTR)arg;
    Ymm in[16], out[16];
    for (int round = 0; round < 3; round++) {
        fill(in, (unsigned)(n * 16 + round + 1));
        memset(out, 0, sizeof(out));
        hold(in, out, NULL, now() + 2000000);
        if (memcmp(in, out, sizeof(in))) g_bad[n]++;
    }
    return 0;
}

/* Exceptions: ud2 with YMM0-15 loaded; the handler checks its CONTEXT's
 * YMM upper halves and clobbers the registers */
static Ymm g_exc_in[16];
static int g_seen, g_flags_ok, g_ctx_ok;

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
    if (ep->ExceptionRecord->ExceptionCode != 0xC000001D) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = ep->ContextRecord;
    g_seen++;
    g_flags_ok = (c->ContextFlags & CONTEXT_XSTATE_) == CONTEXT_XSTATE_;
    DWORD len = 0;
    BYTE *hi = g_flags_ok ? LocateXStateFeature(c, XSTATE_AVX_, &len) : NULL;
    int ok = hi && len == 256;
    for (int i = 0; ok && i < 16; i++)                  /* YMM_Hi128 and FltSave's XMM: the two halves */
        ok = !memcmp(hi + 16 * i, &g_exc_in[i].v[4], 16) && !memcmp(&c->FltSave.XmmRegisters[i], &g_exc_in[i].v[0], 16);
    g_ctx_ok = ok;
    __asm__ volatile("vcmpps $0x0F, %%ymm0, %%ymm0, %%ymm0\n\t"           /* all ones (AVX; vpcmpeqd on YMM is AVX2) */
                     "vmovdqa %%ymm0, %%ymm1\n\t"  "vmovdqa %%ymm0, %%ymm2\n\t"  "vmovdqa %%ymm0, %%ymm3\n\t"
                     "vmovdqa %%ymm0, %%ymm4\n\t"  "vmovdqa %%ymm0, %%ymm5\n\t"  "vmovdqa %%ymm0, %%ymm6\n\t"
                     "vmovdqa %%ymm0, %%ymm7\n\t"  "vmovdqa %%ymm0, %%ymm8\n\t"  "vmovdqa %%ymm0, %%ymm9\n\t"
                     "vmovdqa %%ymm0, %%ymm10\n\t" "vmovdqa %%ymm0, %%ymm11\n\t" "vmovdqa %%ymm0, %%ymm12\n\t"
                     "vmovdqa %%ymm0, %%ymm13\n\t" "vmovdqa %%ymm0, %%ymm14\n\t" "vmovdqa %%ymm0, %%ymm15"
                     : : : YMM_CLOBBERS);
    c->Rip += 2;                                        /* past the ud2 */
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void exception_test(void)
{
    Ymm out[16];
    fill(g_exc_in, 77);
    memset(out, 0, sizeof(out));
    PVOID h = AddVectoredExceptionHandler(1, veh);
    __asm__ volatile(LOAD16("%0") "ud2\n\t" STORE16("%1") : : "r"(g_exc_in), "r"(out) : YMM_CLOBBERS, "memory");
    RemoveVectoredExceptionHandler(h);
    check(g_seen == 1, "the ud2 reached the vectored handler");
    check(g_flags_ok, "the exception CONTEXT has CONTEXT_XSTATE");
    check(g_ctx_ok, "the exception CONTEXT holds YMM0-15 (FltSave + LocateXStateFeature)");
    check(!memcmp(out, g_exc_in, sizeof(out)), "NtContinue restores YMM0-15 the handler clobbered");
}

/* GetThreadContext: a thread spinning with YMM0-15 loaded, suspended */
static Ymm g_spin_in[16], g_spin_out[16];
static volatile LONG g_spin_stop;
static DWORD WINAPI spin_thread(void *arg)
{
    (void)arg;
    hold(g_spin_in, g_spin_out, &g_spin_stop, 0);
    return 0;
}

static void thread_context_test(void)
{
    fill(g_spin_in, 4242);
    HANDLE t = CreateThread(NULL, 0, spin_thread, NULL, 0, NULL);
    Sleep(300);                                         /* (in its loop) */
    DWORD len = 0;
    InitializeContext(NULL, CONTEXT_ALL | CONTEXT_XSTATE_, NULL, &len);
    void *buf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
    CONTEXT *c = NULL;
    BOOL init = InitializeContext(buf, CONTEXT_ALL | CONTEXT_XSTATE_, &c, &len);
    check(init && c, "InitializeContext(CONTEXT_ALL | CONTEXT_XSTATE)");
    if (!init || !c) return;
    SetXStateFeaturesMask(c, 1ull << XSTATE_AVX_);
    SuspendThread(t);
    BOOL got = GetThreadContext(t, c);
    ResumeThread(t);
    check(got, "GetThreadContext of a suspended thread");
    DWORD ylen = 0;
    BYTE *hi = LocateXStateFeature(c, XSTATE_AVX_, &ylen);
    check(hi && ylen == 256, "LocateXStateFeature(XSTATE_AVX) in its CONTEXT");
    int ok = got && hi && ylen == 256 && (c->ContextFlags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT;
    for (int i = 0; ok && i < 16; i++)
        ok = !memcmp(hi + 16 * i, &g_spin_in[i].v[4], 16) && !memcmp(&c->FltSave.XmmRegisters[i], &g_spin_in[i].v[0], 16);
    check(ok, "GetThreadContext: the thread's YMM0-15");
    g_spin_stop = 1;
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
    check(!memcmp(g_spin_in, g_spin_out, sizeof(g_spin_in)), "the suspended thread's YMM0-15 survive");
    HeapFree(GetProcessHeap(), 0, buf);
}
#endif

int main(void)
{
    unsigned a, b, c, d;
    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    check((c >> 26) & 1, "CPUID: XSAVE");
    check((c >> 27) & 1, "CPUID: OSXSAVE (the kernel set CR4.OSXSAVE)");
    check((c >> 28) & 1, "CPUID: AVX");
    check((c >> 29) & 1, "CPUID: F16C");
    if (!((c >> 27) & 1) || !((c >> 28) & 1)) {
        printf("avxtest: %d passed, %d failed (no AVX to test)\n", g_pass, g_fail);
        return 1;
    }
    unsigned lo, hi;
    __asm__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    check((lo & 7) == 7, "XCR0: x87, SSE and AVX");
    check((GetEnabledXStateFeatures() & 7) == 7, "GetEnabledXStateFeatures: x87, SSE and AVX");
    check(IsProcessorFeaturePresent(39), "IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE)");

    unsigned half = 0;
    float back = 0, f = 1.5f;
    __asm__ volatile("vmovss %2, %%xmm0\n\t"
                     "vcvtps2ph $0, %%xmm0, %%xmm1\n\t"
                     "vmovd %%xmm1, %0\n\t"
                     "vcvtph2ps %%xmm1, %%xmm2\n\t"
                     "vmovss %%xmm2, %1\n\t"
                     : "=r"(half), "=m"(back) : "m"(f) : "xmm0", "xmm1", "xmm2");
    check((half & 0xFFFF) == 0x3E00 && back == 1.5f, "F16C: 1.5 to half (0x3E00) and back");

    /* A large heap block is 64-byte aligned, as on Windows: lavapipe keeps
     * Vulkan memory in malloc blocks and loads them with vmovaps ymm */
    char *big = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 8 << 20);
    check(big && !((ULONG_PTR)big & 63), "an 8 MiB HeapAlloc block is 64-byte aligned");
    if (big && !((ULONG_PTR)big & 31)) {
        __asm__ volatile("vmovaps (%0), %%ymm0\n\tvmovaps %%ymm0, 32(%0)" : : "r"(big) : "xmm0", "memory");
        HeapFree(GetProcessHeap(), 0, big);
    }

#ifdef _WIN64
    HANDLE t[4];
    for (int i = 0; i < 4; i++) t[i] = CreateThread(NULL, 0, switch_thread, (void *)(ULONG_PTR)i, 0, NULL);
    WaitForMultipleObjects(4, t, TRUE, INFINITE);
    for (int i = 0; i < 4; i++) {
        char what[64];
        snprintf(what, sizeof(what), "thread %d keeps its YMM0-15 across switches", i);
        check(!g_bad[i], what);
        CloseHandle(t[i]);
    }
    exception_test();
    thread_context_test();
#endif
    printf("avxtest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
