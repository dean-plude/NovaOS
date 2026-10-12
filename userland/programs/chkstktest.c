/*
 * chkstktest.exe — the helpers ntdll exports for compiled code that is
 * not linked against a C runtime (LLVM's JIT in Mesa's lavapipe and
 * llvmpipe looks them up among the loaded modules; MSVC-built programs
 * import the stack probe):
 *
 *   __chkstk (x64) / _chkstk (x86)   the stack probe: touch each page of
 *                                    the EAX/RAX bytes about to be taken,
 *                                    on a thread of its own (1 MB of stack),
 *                                    then use the lowest byte
 *   __extendhfsf2, __extendhfdf2,    half-precision conversions, as
 *   __truncsfhf2, __truncdfhf2       compiler-rt names them
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

static void check(int ok, const char *what)
{
    if (ok) g_pass++;
    else { g_fail++; printf("FAIL: %s\n", what); }
}

static FARPROC g_probe;
static const unsigned SIZES[] = { 100, 0x1000, 0x2345, 0x40000 };

/* each size: the stack pointer after the probe, whether the helper kept
 * the registers it must, and a write to the lowest byte wanted */
static DWORD WINAPI probe_thread(void *arg)
{
    (void)arg;
    char what[96];
    for (unsigned i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); i++) {
        unsigned n = SIZES[i];
#ifdef _WIN64
        unsigned long long left, rcx;
        __asm__ volatile("movq $0x12345678, %%rcx\n\t"
                         "movq %q2, %%rax\n\t"
                         "call *%3\n\t"
                         "movq %%rax, %0\n\t"                  /* (RAX kept) */
                         "subq %%rax, %%rsp\n\t"
                         "movb $1, (%%rsp)\n\t"
                         "addq %%rax, %%rsp"
                         : "=&r"(left), "=&c"(rcx) : "r"((unsigned long long)n), "r"(g_probe)
                         : "rax", "r10", "r11", "memory");
        snprintf(what, sizeof(what), "__chkstk(%#x)", n);
        check(left == n && rcx == 0x12345678, what);
#else
        unsigned before, after, ecx;
        __asm__ volatile("movl %%esp, %%esi\n\t"
                         "movl $0x12345678, %%ecx\n\t"
                         "movl %3, %%eax\n\t"
                         "call *%4\n\t"
                         "movl %%esp, %%edi\n\t"
                         "movb $1, (%%esp)\n\t"
                         "movl %%esi, %%esp"
                         : "=S"(before), "=D"(after), "=c"(ecx) : "d"(n), "b"(g_probe)
                         : "eax", "memory");
        snprintf(what, sizeof(what), "_chkstk(%#x)", n);
        check(before - after == n && ecx == 0x12345678, what);
#endif
    }
    return 0;
}

typedef float (*ExtSF)(_Float16);
typedef double (*ExtDF)(_Float16);
typedef _Float16 (*TruncSF)(float);
typedef _Float16 (*TruncDF)(double);

static _Float16 half(unsigned short h) { _Float16 r; memcpy(&r, &h, 2); return r; }
static unsigned short bits(_Float16 x) { unsigned short h; memcpy(&h, &x, 2); return h; }
static float fbits(unsigned b) { float f; memcpy(&f, &b, 4); return f; }

int main(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
#ifdef _WIN64
    g_probe = GetProcAddress(nt, "__chkstk");
    check(g_probe != NULL, "ntdll exports __chkstk");
#else
    g_probe = GetProcAddress(nt, "_chkstk");
    check(g_probe != NULL, "ntdll exports _chkstk");
#endif
    if (g_probe) {
        HANDLE t = CreateThread(NULL, 1 << 20, probe_thread, NULL, 0, NULL);
        DWORD code = 1;
        check(t && WaitForSingleObject(t, 10000) == WAIT_OBJECT_0 && GetExitCodeThread(t, &code) && code == 0,
              "the probing thread ran to its end");
    }

    ExtSF ext_sf = (ExtSF)GetProcAddress(nt, "__extendhfsf2");
    ExtDF ext_df = (ExtDF)GetProcAddress(nt, "__extendhfdf2");
    TruncSF trunc_sf = (TruncSF)GetProcAddress(nt, "__truncsfhf2");
    TruncDF trunc_df = (TruncDF)GetProcAddress(nt, "__truncdfhf2");
    check(ext_sf && ext_df && trunc_sf && trunc_df, "ntdll exports the four half-precision conversions");
    if (ext_sf && ext_df && trunc_sf && trunc_df) {
        static const struct { unsigned short h; float f; } EXT[] = {
            { 0x3C00, 1.0f }, { 0xC000, -2.0f }, { 0x7BFF, 65504.0f }, { 0x0001, 5.9604644775390625e-8f },
            { 0x3555, 0.333251953125f }, { 0x0000, 0.0f },
        };
        char what[96];
        for (unsigned i = 0; i < sizeof(EXT) / sizeof(EXT[0]); i++) {
            snprintf(what, sizeof(what), "__extendhfsf2(%#06x) = %g", EXT[i].h, EXT[i].f);
            check(ext_sf(half(EXT[i].h)) == EXT[i].f, what);
            snprintf(what, sizeof(what), "__extendhfdf2(%#06x) = %g", EXT[i].h, EXT[i].f);
            check(ext_df(half(EXT[i].h)) == (double)EXT[i].f, what);
        }
        float inf = fbits(0x7F800000);
        check(ext_sf(half(0xFC00)) == -inf, "__extendhfsf2(0xfc00) = -inf");
        float nan = ext_sf(half(0x7E00));
        check(nan != nan, "__extendhfsf2(0x7e00) is NaN");

        static const struct { float f; unsigned short h; } TR[] = {
            { 1.0f, 0x3C00 }, { -2.0f, 0xC000 }, { 65504.0f, 0x7BFF }, { 65520.0f, 0x7C00 },   /* (rounds to infinity) */
            { 2049.0f, 0x6800 }, { 2051.0f, 0x6802 },                                         /* ties to even */
            { 5.9604644775390625e-8f, 0x0001 }, { 1e-9f, 0x0000 }, { -0.0f, 0x8000 },
        };
        for (unsigned i = 0; i < sizeof(TR) / sizeof(TR[0]); i++) {
            snprintf(what, sizeof(what), "__truncsfhf2(%g) = %#06x", TR[i].f, TR[i].h);
            check(bits(trunc_sf(TR[i].f)) == TR[i].h, what);
            snprintf(what, sizeof(what), "__truncdfhf2(%g) = %#06x", TR[i].f, TR[i].h);
            check(bits(trunc_df((double)TR[i].f)) == TR[i].h, what);
        }
        check(bits(trunc_sf(inf)) == 0x7C00, "__truncsfhf2(inf) = 0x7c00");
        check(bits(trunc_sf(fbits(0x7FC00000))) == 0x7E00, "__truncsfhf2(NaN) = 0x7e00");
        /* 1 + 2^-11 + 2^-40: through a float it would be a tie and round down */
        check(bits(trunc_df(1.0 + 1.0 / 2048 + 1.0 / (1ull << 40))) == 0x3C01, "__truncdfhf2 rounds once");
    }
    printf("chkstktest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
