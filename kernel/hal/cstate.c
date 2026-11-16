/*
 * cstate.c — processor power states (ACPI 6.5, section 8.4.1): _CST
 *
 * Each processor object (Processor, or an ACPI0007 device) may describe
 * its C-states in _CST: per state a register (how to enter it), its type
 * (C1, C2, C3), worst-case latency and average power.  Or, newer, in _LPI
 * (Low Power Idle states, ACPI 6.5 section 8.4.4): per state its minimum
 * residency, wake latency, whether it is enabled, the deepest state of the
 * parent processor container (ACPI0010, a package or cluster) that may be
 * entered with it, and its entry register.  Firmware decides
 * what to describe from what the OS says it can do, so first the
 * processor is told, as Linux and Windows tell it, through _OSC with
 * Intel's processor UUID (4077A616-290C-47BE-9EBD-D87058713953) or else
 * _PDC: C1 with HLT, C2 and C3 through I/O, C1-C3 through MWAIT
 * ("FFixedHW", when the CPU has MWAIT), with software coordination across
 * processors (Intel's Processor Vendor-Specific ACPI interface, bits 1, 3,
 * 4, 8, 9).  No other bit is set, so the P-state and T-state descriptions
 * (_PSS, _PTC: throttle.c) are what firmware gives an OS that asks
 * nothing of them.
 *
 * The register of each state:
 *   - FFixedHW, vendor 1 (Intel), class 2: MWAIT with the address as its
 *     hint; any other FFixedHW C1: HLT;
 *   - System I/O: a read of that port (P_LVLx), HLT for a C1;
 * and ke/idle.c keeps the states this CPU can enter safely.  _CST changes
 * (Notify 0x81: on AC or battery, say) are read again.
 *
 * _LPI is used, in place of _CST, when the platform agreed to it: the
 * platform-wide _OSC (\_SB._OSC, UUID 0811B06E-4A27-44F9-8D60-
 * 3CBBC22E7B48) is told that OSPM supports platform-coordinated LPI (bit
 * 7), and the firmware's answer keeps the bit, as Linux and Windows ask.
 * Platform-coordinated means the OS picks a state for the processor and
 * the platform decides what its container may do from all the requests,
 * as an Intel package's C-states follow its cores'.  So the container's
 * states are flattened into each processor's list, as Linux flattens
 * them: a local state whose Enabled Parent State allows container state P
 * gives a composite state, entered with P's register (or, where P's entry
 * is an Integer, the local register's address plus it), its latency and
 * residency the two added.  The states are typed by depth (the first C1,
 * the second C2, deeper ones C3) for ke/idle.c's checks and counters;
 * Integer entries of a processor's own states (ARM's) are not used.
 */

#include "acpi.h"
#include "throttle.h"
#include "../arch/x86_64/cpu.h"
#include "../ke/idle.h"
#include "../ke/kpcr.h"
#include "../ke/printf.h"
#include "../lib/string.h"

#include <uacpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/notify.h>
#include <uacpi/utilities.h>

#define PDC_C_C1_HALT     0x0002
#define PDC_SMP_C1PT      0x0008
#define PDC_SMP_C2C3      0x0010
#define PDC_C_C1_FFH      0x0100
#define PDC_C_C2C3_FFH    0x0200

static UINT32 caps(void)
{
    UINT32 c = PDC_C_C1_HALT | PDC_SMP_C1PT | PDC_SMP_C2C3;
    if (IdleMwaitOk(0)) c |= PDC_C_C1_FFH | PDC_C_C2C3_FFH;
    return c;
}

/* _OSC (Intel's processor UUID), else _PDC: what OSPM supports */
static void declare(uacpi_namespace_node *node)
{
    static const UINT8 uuid[16] = { 0x16, 0xA6, 0x77, 0x40, 0x0C, 0x29, 0xBE, 0x47,
                                    0x9E, 0xBD, 0xD8, 0x70, 0x58, 0x71, 0x39, 0x53 };
    UINT32 osc[2] = { 0, caps() }, pdc[3] = { 1, 1, caps() };
    uacpi_namespace_node *m = NULL;
    if (uacpi_namespace_node_find(node, "_OSC", &m) == UACPI_STATUS_OK && m) {
        uacpi_object *args[4] = {
            uacpi_object_create_buffer((uacpi_data_view){ .const_bytes = uuid, .length = 16 }),
            uacpi_object_create_integer(1),
            uacpi_object_create_integer(2),
            uacpi_object_create_buffer((uacpi_data_view){ .const_data = osc, .length = sizeof(osc) }),
        };
        uacpi_object_array a = { args, 4 };
        uacpi_object *ret = NULL;
        uacpi_status st = uacpi_eval(node, "_OSC", &a, &ret);
        uacpi_object_unref(ret);
        for (int i = 0; i < 4; i++) uacpi_object_unref(args[i]);
        if (st == UACPI_STATUS_OK) return;
    }
    if (uacpi_namespace_node_find(node, "_PDC", &m) != UACPI_STATUS_OK || !m) return;
    uacpi_object *arg = uacpi_object_create_buffer((uacpi_data_view){ .const_data = pdc, .length = sizeof(pdc) });
    uacpi_object_array a = { &arg, 1 };
    uacpi_execute(node, "_PDC", &a);
    uacpi_object_unref(arg);
}

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

/* The platform-wide _OSC: platform-coordinated LPI (bit 7), if the
 * firmware agrees */
#define OSC_SB_PCLPI 0x80
static int g_lpi = -1;                          /* -1: not asked yet */

static bool lpi_agreed(void)
{
    if (g_lpi >= 0) return g_lpi;
    g_lpi = 0;
    static const UINT8 uuid[16] = { 0x6E, 0xB0, 0x11, 0x08, 0x27, 0x4A, 0xF9, 0x44,
                                    0x8D, 0x60, 0x3C, 0xBB, 0xC2, 0x2E, 0x7B, 0x48 };
    uacpi_namespace_node *sb = uacpi_namespace_get_predefined(UACPI_PREDEFINED_NAMESPACE_SB), *m = NULL;
    if (!sb || uacpi_namespace_node_find(sb, "_OSC", &m) != UACPI_STATUS_OK || !m) return false;
    UINT32 osc[2] = { 0, OSC_SB_PCLPI };
    uacpi_object *args[4] = {
        uacpi_object_create_buffer((uacpi_data_view){ .const_bytes = uuid, .length = 16 }),
        uacpi_object_create_integer(1),
        uacpi_object_create_integer(2),
        uacpi_object_create_buffer((uacpi_data_view){ .const_data = osc, .length = sizeof(osc) }),
    };
    uacpi_object_array a = { args, 4 };
    uacpi_object *ret = NULL;
    uacpi_data_view v;
    if (uacpi_eval(sb, "_OSC", &a, &ret) == UACPI_STATUS_OK && ret &&
        uacpi_object_get_buffer(ret, &v) == UACPI_STATUS_OK && v.length >= 8) {
        UINT32 r[2];
        memcpy(r, v.const_bytes, 8);
        g_lpi = !(r[0] & 0x0E) && (r[1] & OSC_SB_PCLPI);     /* (no failure, unrecognised UUID or revision) */
    }
    uacpi_object_unref(ret);
    for (int i = 0; i < 4; i++) uacpi_object_unref(args[i]);
    kprintf("[ACPI] \\_SB._OSC: platform-coordinated low-power idle states (_LPI) %s\n",
            g_lpi ? "supported" : "not supported: _CST it is");
    return g_lpi;
}

/* One _LPI state as read */
typedef struct {
    UINT32 residency, latency, parent;          /* parent: the deepest container state allowed with it (1-based, 0: none) */
    bool   integer;                             /* entry is an Integer (@entry.address the value) */
    struct acpi_gas entry;
} LpiState;

/* _LPI: Package { Revision, LevelID, Count, Package { MinResidency,
 * WorstCaseWakeLatency, Flags, ArchContextLostFlags,
 * ResidencyCounterFrequency, EnabledParentState, EntryMethod,
 * ResidencyCounterRegister, UsageCounterRegister, StateName }, ... };
 * the enabled states, shallowest first */
static int read_lpi(uacpi_namespace_node *node, LpiState *out, int max)
{
    uacpi_object *o = NULL;
    uacpi_object_array a, e;
    if (uacpi_eval_simple_package(node, "_LPI", &o) != UACPI_STATUS_OK) return -1;
    int n = 0;
    if (uacpi_object_get_package(o, &a) == UACPI_STATUS_OK)
        for (uacpi_size i = 3; i < a.count && n < max; i++) {
            uacpi_u64 v[6];
            bool ok = uacpi_object_get_package(a.objects[i], &e) == UACPI_STATUS_OK && e.count >= 7;
            for (int k = 0; ok && k < 6; k++) ok = uacpi_object_get_integer(e.objects[k], &v[k]) == UACPI_STATUS_OK;
            if (!ok || !(v[2] & 1)) continue;               /* (Flags bit 0: enabled) */
            LpiState st = { (UINT32)v[0], (UINT32)v[1], (UINT32)v[5], false, { 0 } };
            uacpi_u64 entry;
            if (uacpi_object_is(e.objects[6], UACPI_OBJECT_INTEGER) &&
                uacpi_object_get_integer(e.objects[6], &entry) == UACPI_STATUS_OK) {
                st.integer = true;
                st.entry.address = entry;
            } else if (!gas_of(e.objects[6], &st.entry)) {
                continue;
            }
            out[n++] = st;
        }
    uacpi_object_unref(o);
    return n;
}

/* An _LPI state's register as an idle state of depth @depth (0: the
 * shallowest) */
static bool lpi_entry(const struct acpi_gas *g, int depth, UINT32 latency, UINT32 residency, IdleState *st)
{
    UINT8 type = depth < 2 ? (UINT8)(depth + 1) : 3;
    *st = (IdleState){ IDLE_HLT, type, 0, latency, 0, residency };
    if (g->address_space_id == UACPI_ADDRESS_SPACE_FFIXEDHW && g->register_bit_width == 1 && g->register_bit_offset == 2) {
        st->entry = IDLE_MWAIT;
        st->arg = (UINT32)g->address;
    } else if (g->address_space_id == UACPI_ADDRESS_SPACE_SYSTEM_IO && type > 1) {
        st->entry = IDLE_IO;
        st->arg = (UINT32)g->address;
    } else if (type > 1) {
        return false;
    }
    return true;
}

/* The processor's _LPI with its container's states flattened in; false
 * when it has none */
static bool read_lpi_states(uacpi_namespace_node *node, int cpu)
{
    LpiState local[IDLE_MAX_STATES], up[IDLE_MAX_STATES];
    int nl = read_lpi(node, local, IDLE_MAX_STATES);
    if (nl < 0) return false;
    uacpi_namespace_node *parent = uacpi_namespace_node_parent(node);
    int np = parent ? read_lpi(parent, up, IDLE_MAX_STATES) : -1;
    IdleState s[IDLE_MAX_STATES];
    int n = 0;
    for (int i = 0; i < nl && n < IDLE_MAX_STATES; i++) {
        if (local[i].integer) continue;
        if (lpi_entry(&local[i].entry, n, local[i].latency, local[i].residency, &s[n])) n++;
    }
    /* Composite states: each container state with the shallowest local
     * state that allows it */
    for (int p = 0; p < np && n < IDLE_MAX_STATES; p++)
        for (int i = 0; i < nl; i++) {
            if (local[i].integer || local[i].parent < (UINT32)p + 1) continue;
            struct acpi_gas g = local[i].entry;
            if (up[p].integer) g.address += up[p].entry.address;
            else g = up[p].entry;
            if (lpi_entry(&g, n, local[i].latency + up[p].latency, local[i].residency + up[p].residency, &s[n])) n++;
            break;
        }
    const uacpi_char *path = uacpi_namespace_node_generate_absolute_path(node);
    char src[80];
    if (np > 0) {
        const uacpi_char *pp = uacpi_namespace_node_generate_absolute_path(parent);
        ksnprintf(src, sizeof(src), "%s._LPI and %s._LPI", path ? path : "?", pp ? pp : "?");
        uacpi_free_absolute_path(pp);
    } else {
        ksnprintf(src, sizeof(src), "%s._LPI", path ? path : "?");
    }
    uacpi_free_absolute_path(path);
    if (n) IdleSetStates((UINT32)cpu, s, n, src);
    return true;
}

/* _CST: Package { Count, Package { Register, Type, Latency, Power }, ... } */
static void read_cst(uacpi_namespace_node *node, int cpu)
{
    uacpi_object *o = NULL;
    uacpi_object_array a, e;
    if (uacpi_eval_simple_package(node, "_CST", &o) != UACPI_STATUS_OK) return;
    IdleState s[IDLE_MAX_STATES];
    int n = 0;
    if (uacpi_object_get_package(o, &a) == UACPI_STATUS_OK)
        for (uacpi_size i = 1; i < a.count && n < IDLE_MAX_STATES; i++) {
            struct acpi_gas g;
            uacpi_u64 type, latency, power;
            if (uacpi_object_get_package(a.objects[i], &e) != UACPI_STATUS_OK || e.count < 4 || !gas_of(e.objects[0], &g) ||
                uacpi_object_get_integer(e.objects[1], &type) != UACPI_STATUS_OK ||
                uacpi_object_get_integer(e.objects[2], &latency) != UACPI_STATUS_OK ||
                uacpi_object_get_integer(e.objects[3], &power) != UACPI_STATUS_OK || type < 1 || type > 3)
                continue;
            IdleState st = { IDLE_HLT, (UINT8)type, 0, (UINT32)latency, (UINT32)power, 0 };
            if (g.address_space_id == UACPI_ADDRESS_SPACE_FFIXEDHW && g.register_bit_width == 1 &&
                g.register_bit_offset == 2) {
                st.entry = IDLE_MWAIT;
                st.arg = (UINT32)g.address;
            } else if (g.address_space_id == UACPI_ADDRESS_SPACE_SYSTEM_IO && type > 1) {
                st.entry = IDLE_IO;
                st.arg = (UINT32)g.address;
            } else if (type > 1) {
                continue;                                  /* (no way to enter it) */
            }
            s[n++] = st;
        }
    uacpi_object_unref(o);
    if (!n) return;
    const uacpi_char *path = uacpi_namespace_node_generate_absolute_path(node);
    char src[56];
    ksnprintf(src, sizeof(src), "%s._CST", path ? path : "?");
    uacpi_free_absolute_path(path);
    IdleSetStates((UINT32)cpu, s, n, src);
}

static void read_states(uacpi_namespace_node *node, int cpu)
{
    if (!lpi_agreed() || !read_lpi_states(node, cpu)) read_cst(node, cpu);
}

/* 0x81: the C-states changed */
static uacpi_status proc_notify(uacpi_handle ctx, uacpi_namespace_node *node, uacpi_u64 value)
{
    if (value == 0x81) read_states(node, (int)(uintptr_t)ctx);
    return UACPI_STATUS_OK;
}

static void add_proc(uacpi_namespace_node *node)
{
    int cpu = ThrottleCpuOf(node);
    if (cpu < 0) return;
    declare(node);
    read_states(node, cpu);
    uacpi_install_notify_handler(node, proc_notify, (uacpi_handle)(uintptr_t)cpu);
}

static uacpi_iteration_decision found_processor(void *user, uacpi_namespace_node *node, uacpi_u32 depth)
{
    (void)user; (void)depth;
    add_proc(node);
    return UACPI_ITERATION_DECISION_CONTINUE;
}

void CStateProbe(void)
{
    uacpi_namespace_for_each_child(uacpi_namespace_root(), found_processor, NULL,
                                   UACPI_OBJECT_PROCESSOR_BIT, UACPI_MAX_DEPTH_ANY, NULL);
    uacpi_find_devices("ACPI0007", found_processor, NULL);
}
