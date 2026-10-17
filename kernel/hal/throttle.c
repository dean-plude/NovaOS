/*
 * throttle.c — processor throttling (ACPI 6.5, section 8.4.5): T-states
 * for passive cooling
 *
 * A thermal zone at or above its passive trip point (_PSV) cools the
 * processors in its _PSL by slowing them: aml.c works out how much
 * performance to give up with the zone's _TC1, _TC2 and _TSP (section
 * 11.1.5) and asks here.  Each processor is throttled the way its firmware
 * describes:
 *
 *   - _PTC (control and status registers), _TSS (the T-states: percent of
 *     full performance, and the value to write for each) and _TPC (the
 *     shallowest state the platform allows right now; Notify 0x82 when it
 *     changes).  A register in I/O or memory space is written and its
 *     status read back; one in FFixedHW is Intel's IA32_CLOCK_MODULATION
 *     MSR, which each CPU writes for itself on its next timer tick;
 *   - else P_CNT in the processor's P_BLK, with the FADT's duty cycle
 *     field (DUTY_OFFSET, DUTY_WIDTH): THT_EN off, the duty value, then
 *     THT_EN on.
 *
 * Every zone that lists a processor asks for a performance; the processor
 * runs at the lowest of them, in the shallowest T-state that is at or
 * below it, and never shallower than _TPC.  ThrottlePercent gives
 * NtPowerInformation(ProcessorInformation) each CPU's MhzLimit.
 */

#include "throttle.h"
#include "acpi.h"
#include "../arch/x86_64/cpu.h"
#include "../ke/kpcr.h"
#include "../ke/printf.h"
#include "../lib/string.h"

#include <uacpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/io.h>
#include <uacpi/notify.h>
#include <uacpi/tables.h>
#include <uacpi/utilities.h>

#define MAX_PROCS   MAX_CPUS
#define MAX_TSTATES 16
#define MAX_ZONES   8
#define MSR_IA32_CLOCK_MODULATION 0x19A

typedef enum { CTL_NONE, CTL_GAS, CTL_MSR, CTL_PCNT } CtlKind;

typedef struct { UINT32 pct, control, status; } TState;

typedef struct {
    uacpi_namespace_node *node;
    char                  path[40];
    int                   cpu;                   /* its CPU (g_kpcr index); -1: not found in the MADT */
    CtlKind               kind;
    struct acpi_gas       ctl, sts;              /* CTL_GAS; CTL_PCNT: ctl is P_CNT */
    TState                t[MAX_TSTATES];
    int                   nt;
    int                   limit;                 /* _TPC */
    int                   cur;                   /* the state in force */
    UINT8                 want[MAX_ZONES];       /* each zone's ask, percent (0 = none: 100) */
} Proc;

static Proc            g_proc[MAX_PROCS];
static int             g_nproc;
static UINT8           g_duty_off, g_duty_width;
static volatile UINT32 g_pct[MAX_CPUS];          /* per CPU, for ThrottlePercent (0: 100) */
static bool            g_msr;                    /* some processor throttles through the MSR */
static volatile UINT64 g_msr_want[MAX_CPUS];     /* bit 63: set; else the value to write */
static UINT64          g_msr_have[MAX_CPUS];

/* -----------------------------------------------------------------------
 * Finding the processors
 * ----------------------------------------------------------------------- */

/* The CPU whose MADT entry has processor UID @uid */
static int cpu_of(UINT32 uid)
{
    const UINT8 *m = AcpiFindTable("APIC");
    if (!m) return -1;
    const UINT8 *p = m + 36 + 8, *end = m + *(const UINT32 *)(m + 4);
    UINT32 apic = 0xFFFFFFFF;
    for (; p + 2 <= end && p[1] >= 2; p += p[1]) {
        if (p[0] == 0 && p[1] >= 8 && p[2] == uid) { apic = p[3]; break; }                    /* local APIC */
        if (p[0] == 9 && p[1] >= 16 && *(const UINT32 *)(p + 12) == uid) { apic = *(const UINT32 *)(p + 4); break; }   /* x2APIC */
    }
    for (UINT32 i = 0; i < g_cpu_count && i < MAX_CPUS; i++)
        if (g_kpcr[i].ApicId == apic) return (int)i;
    return -1;
}

/* A Generic Register descriptor (ResourceTemplate () { Register (...) }) */
static bool gas_of(uacpi_object *o, struct acpi_gas *g)
{
    uacpi_data_view v;
    if (uacpi_object_get_buffer(o, &v) != UACPI_STATUS_OK || v.length < 15) return false;
    const UINT8 *b = v.const_bytes;
    if (b[0] != 0x82 || (b[1] | b[2] << 8) < 12) return false;
    g->address_space_id = b[3];
    g->register_bit_width = b[4];
    g->register_bit_offset = b[5];
    g->access_size = b[6];
    memcpy(&g->address, b + 7, 8);
    return true;
}

static bool pkg_u32(uacpi_object_array *a, uacpi_size i, UINT32 *out)
{
    uacpi_u64 v;
    if (i >= a->count || uacpi_object_get_integer(a->objects[i], &v) != UACPI_STATUS_OK) return false;
    *out = (UINT32)v;
    return true;
}

static bool read_ptc(Proc *p)
{
    uacpi_object *o = NULL;
    uacpi_object_array a;
    bool ok = false;
    if (uacpi_eval_simple_package(p->node, "_PTC", &o) != UACPI_STATUS_OK) return false;
    if (uacpi_object_get_package(o, &a) == UACPI_STATUS_OK && a.count >= 2 &&
        gas_of(a.objects[0], &p->ctl) && gas_of(a.objects[1], &p->sts)) {
        UINT8 sp = p->ctl.address_space_id;
        if (sp == UACPI_ADDRESS_SPACE_FFIXEDHW) {
            CpuidResult r = cpuid(1, 0);
            ok = (r.edx >> 22 & 1) != 0;                 /* ACPI: thermal monitor and clock modulation */
            if (!ok) kprintf("[ACPI] %s: throttled through IA32_CLOCK_MODULATION, which this CPU lacks\n", p->path);
            p->kind = CTL_MSR;
        } else if (sp == UACPI_ADDRESS_SPACE_SYSTEM_IO || sp == UACPI_ADDRESS_SPACE_SYSTEM_MEMORY) {
            ok = true;
            p->kind = CTL_GAS;
        }
    }
    uacpi_object_unref(o);
    return ok;
}

static bool read_tss(Proc *p)
{
    uacpi_object *o = NULL;
    uacpi_object_array a, e;
    if (uacpi_eval_simple_package(p->node, "_TSS", &o) != UACPI_STATUS_OK) return false;
    p->nt = 0;
    if (uacpi_object_get_package(o, &a) == UACPI_STATUS_OK)
        for (uacpi_size i = 0; i < a.count && p->nt < MAX_TSTATES; i++) {
            TState t;
            UINT32 power, latency;
            if (uacpi_object_get_package(a.objects[i], &e) != UACPI_STATUS_OK ||
                !pkg_u32(&e, 0, &t.pct) || !pkg_u32(&e, 1, &power) || !pkg_u32(&e, 2, &latency) ||
                !pkg_u32(&e, 3, &t.control) || !pkg_u32(&e, 4, &t.status) || !t.pct || t.pct > 100)
                break;
            p->t[p->nt++] = t;
        }
    uacpi_object_unref(o);
    return p->nt >= 2;
}

static void read_tpc(Proc *p)
{
    uacpi_u64 v;
    p->limit = uacpi_eval_simple_integer(p->node, "_TPC", &v) == UACPI_STATUS_OK && v < (uacpi_u64)p->nt ? (int)v : 0;
}

/* P_CNT (P_BLK + 0): the duty cycle in 2^DUTY_WIDTH steps */
static bool pcnt_states(Proc *p, UINT32 pblk, UINT8 len)
{
    if (!pblk || len < 6 || !g_duty_width || g_duty_width > 4 ||
        (g_duty_off <= 4 && g_duty_off + g_duty_width > 4))    /* (it may not cover THT_EN) */
        return false;
    memset(&p->ctl, 0, sizeof(p->ctl));
    p->ctl.address_space_id = UACPI_ADDRESS_SPACE_SYSTEM_IO;
    p->ctl.register_bit_width = 32;
    p->ctl.address = pblk;
    UINT32 n = 1u << g_duty_width;
    p->nt = 0;
    for (UINT32 k = 0; k < n && p->nt < MAX_TSTATES; k++) {      /* T0 off, then duty n-1 .. 1 */
        UINT32 duty = k ? n - k : 0;
        p->t[p->nt++] = (TState){ k ? duty * 100 / n : 100, duty, duty };
    }
    p->kind = CTL_PCNT;
    p->limit = 0;
    return true;
}

static void set_state(Proc *p, int s, bool force);

/* 0x82: _TPC changed */
static uacpi_status proc_notify(uacpi_handle ctx, uacpi_namespace_node *node, uacpi_u64 value)
{
    (void)node;
    Proc *p = ctx;
    if (value != 0x82 || (p->kind != CTL_GAS && p->kind != CTL_MSR)) return UACPI_STATUS_OK;
    int was = p->limit;
    read_tpc(p);
    if (p->limit != was) {
        kprintf("[ACPI] %s: throttling limit (_TPC) now T%d\n", p->path, p->limit);
        set_state(p, p->cur < p->limit ? p->limit : p->cur, false);
    }
    return UACPI_STATUS_OK;
}

static void add_proc(uacpi_namespace_node *node, UINT32 uid, UINT32 pblk, UINT8 pblk_len)
{
    if (g_nproc == MAX_PROCS) return;
    for (int i = 0; i < g_nproc; i++) if (g_proc[i].node == node) return;
    Proc *p = &g_proc[g_nproc];
    memset(p, 0, sizeof(*p));
    p->node = node;
    const uacpi_char *path = uacpi_namespace_node_generate_absolute_path(node);
    strncpy(p->path, path ? path : "?", sizeof(p->path) - 1);
    uacpi_free_absolute_path(path);
    p->cpu = cpu_of(uid);
    if (read_ptc(p) && read_tss(p)) {
        read_tpc(p);
    } else if (!pcnt_states(p, pblk, pblk_len)) {
        return;                                            /* (not throttled: not kept) */
    }
    g_nproc++;
    char states[96];
    int k = 0;
    for (int i = 0; i < p->nt && k < (int)sizeof(states) - 8; i++)
        k += ksnprintf(states + k, sizeof(states) - k, "%s%u%%", i ? " " : "", p->t[i].pct);
    const char *how = p->kind == CTL_MSR ? "IA32_CLOCK_MODULATION" :
                      p->kind == CTL_PCNT ? "P_CNT" :
                      p->ctl.address_space_id == UACPI_ADDRESS_SPACE_SYSTEM_IO ? "_PTC, I/O" : "_PTC, memory";
    char cpu[16] = "?";
    if (p->cpu >= 0) ksnprintf(cpu, sizeof(cpu), "%d", p->cpu);
    kprintf("[ACPI] Processor %s (CPU %s): %d T-states (%s), through %s 0x%llx, limit T%d\n", p->path, cpu, p->nt,
            states, how, (unsigned long long)p->ctl.address, p->limit);
    uacpi_install_notify_handler(node, proc_notify, p);
    if (p->limit) set_state(p, p->limit, true);
}

static uacpi_iteration_decision found_processor_object(void *user, uacpi_namespace_node *node, uacpi_u32 depth)
{
    (void)user; (void)depth;
    uacpi_object *o = NULL;
    uacpi_processor_info info;
    if (uacpi_eval(node, NULL, NULL, &o) == UACPI_STATUS_OK &&
        uacpi_object_get_processor_info(o, &info) == UACPI_STATUS_OK)
        add_proc(node, info.id, info.block_address, info.block_length);
    uacpi_object_unref(o);
    return UACPI_ITERATION_DECISION_CONTINUE;
}

static uacpi_iteration_decision found_processor_device(void *user, uacpi_namespace_node *node, uacpi_u32 depth)
{
    (void)user; (void)depth;
    uacpi_u64 uid = 0xFFFFFFFF;
    uacpi_eval_simple_integer(node, "_UID", &uid);
    add_proc(node, (UINT32)uid, 0, 0);
    return UACPI_ITERATION_DECISION_CONTINUE;
}

int ThrottleCpuOf(void *node)
{
    for (int i = 0; i < g_nproc; i++) if (g_proc[i].node == node) return g_proc[i].cpu;
    uacpi_object *o = NULL;
    uacpi_processor_info info;
    uacpi_u64 uid;
    int cpu = -1;
    if (uacpi_eval(node, NULL, NULL, &o) == UACPI_STATUS_OK &&
        uacpi_object_get_processor_info(o, &info) == UACPI_STATUS_OK)
        cpu = cpu_of(info.id);
    else if (uacpi_eval_simple_integer(node, "_UID", &uid) == UACPI_STATUS_OK)
        cpu = cpu_of((UINT32)uid);
    uacpi_object_unref(o);
    return cpu;
}

void ThrottleProbe(void)
{
    struct acpi_fadt *fadt = NULL;
    if (uacpi_table_fadt(&fadt) == UACPI_STATUS_OK && fadt) {
        g_duty_off = fadt->duty_offset;
        g_duty_width = fadt->duty_width;
    }
    uacpi_namespace_for_each_child(uacpi_namespace_root(), found_processor_object, NULL,
                                   UACPI_OBJECT_PROCESSOR_BIT, UACPI_MAX_DEPTH_ANY, NULL);
    uacpi_find_devices("ACPI0007", found_processor_device, NULL);
}

/* -----------------------------------------------------------------------
 * Setting a T-state
 * ----------------------------------------------------------------------- */
static void set_state(Proc *p, int s, bool force)
{
    if (s >= p->nt) s = p->nt - 1;
    if (s < p->limit) s = p->limit;
    if (s == p->cur && !force) return;
    TState *t = &p->t[s];
    char note[64] = "";
    switch (p->kind) {
    case CTL_GAS: {
        uacpi_gas_write(&p->ctl, t->control);
        uacpi_u64 st = 0;
        if (uacpi_gas_read(&p->sts, &st) == UACPI_STATUS_OK && (UINT32)st != t->status)
            ksnprintf(note, sizeof(note), " (its status reads 0x%x, not 0x%x)", (UINT32)st, t->status);
        break;
    }
    case CTL_PCNT: {
        uacpi_u64 v = 0;
        uacpi_gas_read(&p->ctl, &v);
        UINT32 mask = ((1u << g_duty_width) - 1) << g_duty_off;
        v &= ~(UINT64)(1u << 4);                           /* THT_EN off before the duty value changes */
        uacpi_gas_write(&p->ctl, v);
        if (t->control) {
            v = (v & ~(UINT64)mask) | (UINT64)t->control << g_duty_off;
            uacpi_gas_write(&p->ctl, v);
            uacpi_gas_write(&p->ctl, v | 1u << 4);
        }
        break;
    }
    case CTL_MSR:
        if (p->cpu >= 0) {
            g_msr = true;
            __atomic_store_n(&g_msr_want[p->cpu], (UINT64)t->control | 1ull << 63, __ATOMIC_RELEASE);
        }
        break;
    default:
        return;
    }
    if (s != p->cur)
        kprintf("[ACPI] %s: T%d (%u%% performance)%s%s\n", p->path, s, t->pct,
                s ? " for passive cooling" : ", not throttled", note);
    p->cur = s;
    if (p->cpu >= 0) __atomic_store_n(&g_pct[p->cpu], t->pct, __ATOMIC_RELEASE);
}

/* The shallowest state at or below @perf percent */
static int state_for(const Proc *p, UINT32 perf)
{
    for (int i = 0; i < p->nt; i++) if (p->t[i].pct <= perf) return i;
    return p->nt - 1;
}

void ThrottlePassive(int zone, void *const *procs, int n, UINT32 perf)
{
    if (zone < 0 || zone >= MAX_ZONES) return;
    if (perf > 100) perf = 100;
    for (int i = 0; i < g_nproc; i++) {
        Proc *p = &g_proc[i];
        bool listed = n == 0;
        for (int j = 0; j < n && !listed; j++) listed = procs[j] == (void *)p->node;
        if (!listed) continue;
        p->want[zone] = perf < 100 ? (UINT8)(perf ? perf : 1) : 0;
        UINT32 lowest = 100;
        for (int z = 0; z < MAX_ZONES; z++) if (p->want[z] && p->want[z] < lowest) lowest = p->want[z];
        set_state(p, state_for(p, lowest), false);
    }
}

void ThrottleResume(void)
{
    for (int i = 0; i < g_nproc; i++)
        if (g_proc[i].cur) set_state(&g_proc[i], g_proc[i].cur, true);
    for (int c = 0; c < MAX_CPUS; c++) g_msr_have[c] = 0;     /* (the MSR is back to its reset value) */
}

UINT32 ThrottlePercent(UINT32 cpu)
{
    UINT32 v = cpu < MAX_CPUS ? __atomic_load_n(&g_pct[cpu], __ATOMIC_ACQUIRE) : 0;
    return v ? v : 100;
}

void ThrottleTick(void)
{
    if (!g_msr) return;
    UINT32 c = KiGetCurrentKpcr()->CpuNumber;
    if (c >= MAX_CPUS) return;
    UINT64 want = __atomic_load_n(&g_msr_want[c], __ATOMIC_ACQUIRE);
    if (!want || want == g_msr_have[c]) return;
    wrmsr(MSR_IA32_CLOCK_MODULATION, (rdmsr(MSR_IA32_CLOCK_MODULATION) & ~0x1Full) | (want & 0x1F));
    g_msr_have[c] = want;
}
