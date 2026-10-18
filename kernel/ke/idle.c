/*
 * idle.c — processor idle states (C-states)
 *
 * A CPU with nothing to run waits in a C-state: C1 stops its clock, deeper
 * states turn off more of it (caches, its voltage) and take longer to
 * leave.  Each CPU has a table of the states it may enter, shallowest
 * first:
 *
 *   - its processor object's _CST (hal/cstate.c), the way Windows and
 *     Linux read it: a state entered with MWAIT and a hint (FFixedHW,
 *     Intel's "native C-state instruction"), by reading a port (System
 *     I/O: the chipset's P_LVLx registers) or with HLT (C1);
 *   - else the C-states CPUID leaf 5 enumerates MWAIT sub-states for
 *     (C1 hint 0x00, C2 0x10, C3 0x20, ...), with nominal latencies;
 *   - else HLT alone.
 *
 * At each idle period the CPU takes the deepest state whose exit latency
 * is well inside the time until its timer is next due (twice the latency,
 * as Linux's ACPI driver takes for a state's target residency); with the
 * tick stopped (scheduler.c) that is often the deepest state.
 *
 * States deeper than C1 stop the local APIC timer on CPUs without an
 * always-running APIC timer (CPUID.6:EAX[2], ARAT), and C3 and deeper the
 * TSC on CPUs without an invariant one (CPUID.80000007:EDX[8]); NovaOS's
 * clock and scheduler run on both, so such a CPU keeps to the states that
 * leave them running.  In a virtual machine (CPUID.1:ECX[31]) both are the
 * hypervisor's, which keeps them running whatever the guest's CPU waits in.  A System I/O state of type C3 needs the bus masters
 * held off and the caches flushed (ACPI 8.1.4), which NovaOS does not do:
 * those are dropped too (modern CPUs describe their deep states as MWAIT).
 *
 * Leaving a state:
 *   - MWAIT runs with interrupts enabled ("sti; mwait", ECX 0), so the
 *     interrupt that ends it is taken at once, as HLT's is.  It watches
 *     the CPU's KPCR.Idle, which smp_kick clears (with an IPI as well).
 *     An MWAIT that returns with neither (QEMU's TCG treats MWAIT as PAUSE
 *     when there are several CPUs; real CPUs may wake spuriously) is
 *     followed by HLT, so the CPU still waits for its interrupt;
 *   - a P_LVLx read returns once the chipset brought the CPU back, with
 *     the interrupt pending; "sti; hlt" then takes it at once (and waits
 *     for it where reading the port did nothing, as in QEMU).
 *
 * Every interrupt clears KPCR.Idle (interrupt_dispatch, idt.c) and calls
 * IdleWake first, which charges the time spent to the state — before the
 * interrupt may switch to another thread — and restarts a stopped tick.
 * Counts and times per ACPI type are what NtQuerySystemInformation's
 * SystemProcessorIdleInformation reports, as Windows does.
 */

#include "idle.h"
#include "kpcr.h"
#include "printf.h"
#include "scheduler.h"
#include "smp.h"
#include "spinlock.h"
#include "../arch/x86_64/cpu.h"
#include "../lib/string.h"

extern uint64_t g_tsc_per_tick;

typedef struct {
    IdleState       s[IDLE_MAX_STATES];
    volatile int    n;                       /* 0: HLT only */
    UINT64          enter_tsc;
    int             in;                      /* the state being waited in, +1 (0: none) */
    UINT32          last;
    UINT64          tsc[4];                  /* per ACPI type */
    UINT32          count[4];
    UINT32          tick_stops;
    KSpinLock       lock;                    /* IdleSetStates (zeroed: KSPINLOCK_INIT) */
} CpuIdle;

static CpuIdle g_idle[MAX_CPUS];
static bool    g_mwait, g_arat, g_invariant_tsc, g_vm;
static UINT32  g_mwait_edx;                  /* CPUID.5:EDX, sub-states per C-state (0: not enumerated) */

static __always_inline void monitor(const volatile void *p)
{
    __asm__ volatile ("monitor" :: "a"(p), "c"(0), "d"(0) : "memory");
}

/* Whether @s can be entered safely on this machine */
static bool usable(const IdleState *s)
{
    if (s->entry == IDLE_MWAIT && !IdleMwaitOk(s->arg)) return false;
    if (s->type >= 2 && !g_arat && !g_vm) return false;       /* (the APIC timer would stop) */
    if (s->type >= 3 && !g_invariant_tsc && !g_vm) return false;   /* (the TSC would stop) */
    if (s->type >= 3 && s->entry == IDLE_IO) return false;    /* (bus master control and cache flush) */
    return true;
}

static const char *describe(const IdleState *s, char *buf, size_t len)
{
    if (s->entry == IDLE_MWAIT) ksnprintf(buf, len, "MWAIT 0x%02x", s->arg);
    else if (s->entry == IDLE_IO) ksnprintf(buf, len, "I/O 0x%x", s->arg);
    else ksnprintf(buf, len, "HLT");
    return buf;
}

static void log_states(UINT32 cpu, const char *source)
{
    CpuIdle *c = &g_idle[cpu];
    char line[256], how[24];
    int k = 0;
    if (!c->n) k = ksnprintf(line, sizeof(line), "C1 (HLT)");
    for (int i = 0; i < c->n && k < (int)sizeof(line) - 48; i++)
        k += ksnprintf(line + k, sizeof(line) - k, "%sC%u (%s, %u us)", i ? ", " : "", c->s[i].type,
                       describe(&c->s[i], how, sizeof(how)), c->s[i].latency_us);
    kprintf("[IDLE] CPU %u: %s, from %s\n", cpu, line, source);
}

bool IdleMwaitOk(UINT32 hint)
{
    if (!g_mwait) return false;
    if (!g_mwait_edx) return true;           /* (the firmware's word for it) */
    UINT32 cstate = ((hint >> 4) & 0xF) + 1, sub = hint & 0xF;
    return cstate < 8 && sub < ((g_mwait_edx >> (cstate * 4)) & 0xF);
}

void IdleInit(void)
{
    CpuidResult r = cpuid(0, 0);
    UINT32 max = r.eax;
    r = cpuid(1, 0);
    bool hypervisor = g_vm = (r.ecx >> 31) & 1;
    g_mwait = (r.ecx >> 3) & 1;
    if (g_mwait && max >= 5) {
        CpuidResult m = cpuid(5, 0);
        if (!(m.ecx & 2)) g_mwait = false;   /* (interrupts must break MWAIT: ECX[1]) */
        else if (m.ecx & 1) g_mwait_edx = m.edx;
    } else {
        g_mwait = false;
    }
    g_arat = max >= 6 && (cpuid(6, 0).eax >> 2 & 1);
    g_invariant_tsc = cpuid(0x80000000, 0).eax >= 0x80000007 && (cpuid(0x80000007, 0).edx >> 8 & 1);
    kprintf("[IDLE] MWAIT %s%s, ARAT %s, invariant TSC %s%s\n", g_mwait ? "yes" : "no",
            g_mwait && g_mwait_edx ? "" : g_mwait ? " (no sub-states enumerated)" : "",
            g_arat ? "yes" : "no", g_invariant_tsc ? "yes" : "no",
            hypervisor ? ", in a virtual machine (its timer and TSC run on in any C-state)" : "");
    /* No firmware table yet: the sub-states CPUID enumerates (C1 and
     * deeper; a hypervisor's are left alone, as Linux's intel_idle does) */
    if (!g_mwait || !g_mwait_edx || hypervisor) return;
    static const UINT32 latency[8] = { 0, 2, 20, 100, 150, 200, 300, 500 };
    IdleState s[IDLE_MAX_STATES];
    int n = 0;
    for (UINT32 c = 1; c < 8 && n < IDLE_MAX_STATES; c++)
        if ((g_mwait_edx >> (c * 4)) & 0xF)
            s[n++] = (IdleState){ IDLE_MWAIT, (UINT8)(c < 3 ? c : 3), (c - 1) << 4, latency[c], 0 };
    for (UINT32 i = 0; i < MAX_CPUS; i++) IdleSetStates(i, s, n, i ? NULL : "CPUID leaf 5");
}

void IdleSetStates(UINT32 cpu, const IdleState *s, int n, const char *source)
{
    if (cpu >= MAX_CPUS) return;
    CpuIdle *c = &g_idle[cpu];
    IdleState keep[IDLE_MAX_STATES];
    int k = 0;
    for (int i = 0; i < n && k < IDLE_MAX_STATES; i++)
        if (usable(&s[i]) && (!k || s[i].type >= keep[k - 1].type)) keep[k++] = s[i];
    IrqState st = spin_lock_irqsave(&c->lock);
    __atomic_store_n(&c->n, 0, __ATOMIC_RELEASE);   /* (an idle period starting meanwhile uses HLT) */
    memcpy(c->s, keep, sizeof(keep[0]) * (size_t)k);
    __atomic_store_n(&c->n, k, __ATOMIC_RELEASE);
    spin_unlock_irqrestore(&c->lock, st);
    if (source) log_states(cpu, source);
}

/* The deepest state worth entering for @us microseconds of idle time */
static int pick(const CpuIdle *c, int n, UINT64 us)
{
    int best = 0;
    for (int i = 1; i < n; i++)
        if ((UINT64)c->s[i].latency_us * 2 <= us) best = i;
    return best;
}

void IdleEnter(void)
{
    PKPCR k = KiGetCurrentKpcr();
    UINT32 cpu = k->CpuNumber;
    CpuIdle *c = &g_idle[cpu];
    int n = __atomic_load_n(&c->n, __ATOMIC_ACQUIRE);
    UINT64 now = rdtsc(), until = sched_timer_armed(cpu);
    UINT64 us = until > now && g_tsc_per_tick ? (until - now) * 10000 / g_tsc_per_tick : 0;
    int i = n ? pick(c, n, us) : -1;
    const IdleState *s = i >= 0 ? &c->s[i] : NULL;
    c->last = i >= 0 ? (UINT32)i : 0;
    c->enter_tsc = now;
    c->in = (s ? s->type : 1);
    if (s && s->entry == IDLE_MWAIT) {
        monitor(&k->Idle);
        if (k->Idle && !sched_work_waiting())
            __asm__ volatile ("sti; mwait; cli" :: "a"(s->arg), "c"(0) : "memory");
        if (k->Idle && !sched_work_waiting())      /* (no interrupt and no kick: wait for one) */
            __asm__ volatile ("sti; hlt; cli" ::: "memory");
    } else {
        if (s && s->entry == IDLE_IO) (void)inb((uint16_t)s->arg);
        __asm__ volatile ("sti; hlt; cli" ::: "memory");
    }
    IdleWake();                                    /* (woken by a kick, not an interrupt) */
}

void IdleWake(void)
{
    UINT32 cpu = KiGetCurrentKpcr()->CpuNumber;
    CpuIdle *c = &g_idle[cpu];
    if (c->in) {
        UINT64 d = rdtsc() - c->enter_tsc;
        c->tsc[c->in] += d;
        c->count[c->in]++;
        c->in = 0;
    }
    sched_idle_restart_tick();
}

void IdleCountTickStop(UINT32 cpu)
{
    if (cpu < MAX_CPUS) g_idle[cpu].tick_stops++;
}

void IdleGetStats(UINT32 cpu, IdleStats *out)
{
    memset(out, 0, sizeof(*out));
    if (cpu >= MAX_CPUS) return;
    const CpuIdle *c = &g_idle[cpu];
    for (int t = 1; t <= 3; t++) {
        out->time[t] = g_tsc_per_tick ? c->tsc[t] / g_tsc_per_tick * 100000 + c->tsc[t] % g_tsc_per_tick * 100000 / g_tsc_per_tick : 0;
        out->count[t] = c->count[t];
    }
    out->states = c->n ? (UINT32)c->n : 1;
    out->last = c->last;
    out->tick_stops = c->tick_stops;
}
