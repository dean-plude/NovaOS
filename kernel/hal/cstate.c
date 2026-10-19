/*
 * cstate.c — processor power states (ACPI 6.5, section 8.4.1): _CST
 *
 * Each processor object (Processor, or an ACPI0007 device) may describe
 * its C-states in _CST: per state a register (how to enter it), its type
 * (C1, C2, C3), worst-case latency and average power.  Firmware decides
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
            IdleState st = { IDLE_HLT, (UINT8)type, 0, (UINT32)latency, (UINT32)power };
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

/* 0x81: the C-states changed */
static uacpi_status proc_notify(uacpi_handle ctx, uacpi_namespace_node *node, uacpi_u64 value)
{
    if (value == 0x81) read_cst(node, (int)(uintptr_t)ctx);
    return UACPI_STATUS_OK;
}

static void add_proc(uacpi_namespace_node *node)
{
    int cpu = ThrottleCpuOf(node);
    if (cpu < 0) return;
    declare(node);
    read_cst(node, cpu);
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
