/*
 * idletest.exe — processor idle states and the idle CPUs' tick (step 28.4)
 *
 * Run by the devices suite's "laptop" boot, whose processors describe
 * their C-states in _CST (tests/acpi/laptop.asl): C1 and C3 through MWAIT
 * and C2 through a port on a CPU with MWAIT, else C1 with HLT and C2
 * through the port (a KVM guest has no MWAIT).
 *
 *   idletest N   every CPU reports the same deepest idle state, N or
 *                deeper (ProcessorInformation's MaxIdleState: 1 for C2,
 *                2 for C3); while this program sleeps 3 s, both CPUs enter
 *                and leave that state (its transitions in
 *                SystemProcessorIdleInformation), the CPUs together spend
 *                over a second in C-states, CPU 1 stops its tick while
 *                idle (NovaOS counts the stops in SystemProcessorIdle-
 *                Information's Padding) and CPU 0 does not.  Each CPU's
 *                interrupts are printed (SystemProcessorPerformance-
 *                Information's InterruptCount), not checked: timed
 *                sleepers and IPIs still wake an idle CPU often.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef LONG (WINAPI *CallNtPowerInformationFn)(int, PVOID, ULONG, PVOID, ULONG);
typedef LONG (WINAPI *NtQuerySystemInformationFn)(ULONG, PVOID, ULONG, PULONG);

typedef struct { ULONG Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState; } PPI;
typedef struct {
    ULONGLONG IdleTime, C1Time, C2Time, C3Time;
    ULONG     C1Transitions, C2Transitions, C3Transitions, Padding;   /* (NovaOS: tick stops) */
} IDLE_INFO;
typedef struct {
    LARGE_INTEGER IdleTime, KernelTime, UserTime, DpcTime, InterruptTime;
    ULONG         InterruptCount;
} PERF_INFO;

static int passed, failed;

static void check(const char *what, BOOL ok)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) passed++; else failed++;
}

int main(int argc, char **argv)
{
    ULONG least = argc > 1 ? (ULONG)atoi(argv[1]) : 1;
    HMODULE pp = LoadLibraryA("powrprof.dll");
    CallNtPowerInformationFn cnpi = pp ? (CallNtPowerInformationFn)GetProcAddress(pp, "CallNtPowerInformation") : NULL;
    NtQuerySystemInformationFn nqsi =
        (NtQuerySystemInformationFn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");
    if (!cnpi || !nqsi) { printf("FAIL: powrprof or ntdll\n"); return 1; }
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    ULONG n = si.dwNumberOfProcessors;
    check("two CPUs or more", n >= 2 && n <= 64);
    if (n < 2 || n > 64) goto out;

    PPI p[64];
    check("ProcessorInformation", cnpi(11, NULL, 0, p, n * sizeof(PPI)) == 0);
    ULONG deepest = p[0].MaxIdleState;
    BOOL all = deepest >= least && deepest <= 2;
    for (ULONG i = 0; i < n; i++) {
        printf("CPU %lu: deepest idle state %lu, last entered %lu\n", i, p[i].MaxIdleState, p[i].CurrentIdleState);
        all &= p[i].MaxIdleState == deepest;
    }
    if (deepest < 1 || deepest > 2) deepest = 2;
    check("every CPU has the C-states _CST describes", all);

    Sleep(100);
    IDLE_INFO a[64], b[64];
    PERF_INFO pa[64], pb[64];
    ULONG len;
    BOOL ok = nqsi(42, a, n * sizeof(IDLE_INFO), &len) == 0 && nqsi(8, pa, n * sizeof(PERF_INFO), &len) == 0;
    DWORD t0 = GetTickCount();
    Sleep(3000);
    DWORD ms = GetTickCount() - t0;
    ok &= nqsi(42, b, n * sizeof(IDLE_INFO), &len) == 0 && nqsi(8, pb, n * sizeof(PERF_INFO), &len) == 0;
    check("SystemProcessorIdleInformation and SystemProcessorPerformanceInformation", ok);
    if (!ok) goto out;
    ULONGLONG cstate = 0;
    for (ULONG i = 0; i < n; i++) {
        ULONG c1 = b[i].C1Transitions - a[i].C1Transitions, c2 = b[i].C2Transitions - a[i].C2Transitions,
              c3 = b[i].C3Transitions - a[i].C3Transitions, irq = pb[i].InterruptCount - pa[i].InterruptCount;
        ULONGLONG t = (b[i].C1Time - a[i].C1Time) + (b[i].C2Time - a[i].C2Time) + (b[i].C3Time - a[i].C3Time);
        cstate += t;
        printf("CPU %lu over %lu ms: C1 %lu, C2 %lu, C3 %lu times, %llu ms in C-states, %lu interrupts, tick stopped %lu times\n",
               i, ms, c1, c2, c3, t / 10000, irq, b[i].Padding - a[i].Padding);
    }
#define DEEPEST(x) (deepest == 2 ? (x).C3Transitions : (x).C2Transitions)
    check("CPU 0 entered and left its deepest state", DEEPEST(b[0]) > DEEPEST(a[0]));
    check("CPU 1 entered and left its deepest state", DEEPEST(b[1]) > DEEPEST(a[1]));
    check("over a second in C-states", cstate > 10000000);
    check("CPU 0 kept its tick", b[0].Padding == a[0].Padding);
    check("CPU 1 stopped its tick while idle", b[1].Padding > a[1].Padding);
out:
    printf("idletest: %d passed, %d failed\n", passed, failed);
    return failed != 0;
}
