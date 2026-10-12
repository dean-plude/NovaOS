/*
 * pnp.c — the Plug and Play device tree (pnp.h)
 *
 * Nodes live in a table by ID (never reused, so a program's DEVINST stays
 * valid); a removed device stays as a phantom and a device with the same
 * instance ID takes its node back.  Arrivals and removals go into a ring
 * of events that readers follow by sequence number.
 */

#include "pnp.h"
#include "../hal/pci.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../ke/scheduler.h"
#include "../um/um.h"

#define MAX_NODES   512
#define RING        256

static KSpinLock g_lock = KSPINLOCK_INIT;
static PnpNode  *g_node[MAX_NODES + 1];     /* by ID; [0] unused */
static UINT32    g_count;                   /* highest ID given out */
static PnpEvent  g_ring[RING];
static UINT32    g_seq;                     /* newest event */
static bool      g_live;                    /* past PnpInit: log arrivals */

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = src ? strnlen(src, cap - 1) : 0;
    memcpy(dst, src ? src : "", n);
    dst[n] = 0;
}

/* Record an event (lock held) */
static void event_locked(UINT32 kind, PnpNode *n)
{
    PnpEvent *e = &g_ring[++g_seq % RING];
    e->seq = g_seq;
    e->kind = kind;
    e->id = n->id;
    e->reserved = 0;
    n->seq = g_seq;
}

UINT32 PnpAdd(UINT32 parent, const PnpNode *in)
{
    PnpNode *fresh = kzalloc(sizeof(PnpNode));          /* (allocated outside the lock; freed if unused) */
    IrqState s = spin_lock_irqsave(&g_lock);
    PnpNode *n = NULL;
    for (UINT32 i = 1; i <= g_count && !n; i++)
        if (g_node[i] && !strcmp(g_node[i]->instance, in->instance)) n = g_node[i];
    if (!n && fresh && g_count < MAX_NODES) {
        n = fresh;
        fresh = NULL;
        n->id = ++g_count;
        g_node[n->id] = n;
    }
    UINT32 id = 0, after = g_seq;
    if (n) {
        id = n->id;
        UINT32 seq = n->seq;
        *n = *in;
        n->id = id;
        n->seq = seq;
        n->parent = parent;
        n->flags |= PNP_PRESENT;
        if (!(n->flags & PNP_STARTED) && !n->problem) n->problem = 28;    /* CM_PROB_FAILED_INSTALL: no driver */
        if (n->flags & PNP_STARTED) n->problem = 0;
        if (id != PNP_ROOT) event_locked(PNP_ARRIVAL, n);
    }
    spin_unlock_irqrestore(&g_lock, s);
    kfree(fresh);
    if (id && id != PNP_ROOT && g_live) UmGuiDevicesChanged(after);
    if (id && g_live)
        kprintf("[PNP] arrival: %s (%s%s%s)\n", in->instance, in->cls[0] ? in->cls : "no class",
                in->service[0] ? ", " : "", in->service);
    else if (!id)
        kprintf("[PNP] no room for %s\n", in->instance);
    return id;
}

/* Remove @id's subtree, the deepest nodes first (lock held) */
static void remove_locked(UINT32 id, int depth)
{
    if (depth > 16 || !id || id > g_count || !g_node[id]) return;
    for (UINT32 i = 1; i <= g_count; i++)
        if (g_node[i] && g_node[i]->parent == id && (g_node[i]->flags & PNP_PRESENT)) remove_locked(i, depth + 1);
    PnpNode *n = g_node[id];
    if (!(n->flags & PNP_PRESENT)) return;
    n->flags &= ~(UINT32)(PNP_PRESENT | PNP_STARTED | PNP_WINDOWS);
    n->problem = 0;
    event_locked(PNP_REMOVAL, n);
}

void PnpRemove(UINT32 id)
{
    if (!id || id == PNP_ROOT) return;
    char inst[128] = "";
    IrqState s = spin_lock_irqsave(&g_lock);
    UINT32 after = g_seq;
    if (id <= g_count && g_node[id]) copy_str(inst, sizeof(inst), g_node[id]->instance);
    remove_locked(id, 0);
    spin_unlock_irqrestore(&g_lock, s);
    if (inst[0]) kprintf("[PNP] removal: %s\n", inst);
    if (g_seq != after) UmGuiDevicesChanged(after);
}

static void set_driver(UINT32 id, const char *service, UINT32 flags)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    if (id && id <= g_count && g_node[id]) {
        PnpNode *n = g_node[id];
        copy_str(n->service, sizeof(n->service), service);
        n->flags = (n->flags & ~(UINT32)PNP_WINDOWS) | PNP_STARTED | flags;
        n->problem = 0;
    }
    spin_unlock_irqrestore(&g_lock, s);
}

void PnpSetDriver(UINT32 id, const char *service)
{
    set_driver(id, service, 0);
}

void PnpSetWindowsDriver(UINT32 id, const char *service)
{
    set_driver(id, service, PNP_WINDOWS);
}

void PnpSetProblem(UINT32 id, UINT32 problem)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    if (id && id <= g_count && g_node[id]) {
        g_node[id]->flags &= ~(UINT32)(PNP_STARTED | PNP_WINDOWS);
        g_node[id]->problem = problem;
        g_node[id]->service[0] = 0;
    }
    spin_unlock_irqrestore(&g_lock, s);
}

bool PnpGet(UINT32 id, PnpNode *out)
{
    bool ok = false;
    IrqState s = spin_lock_irqsave(&g_lock);
    if (id && id <= g_count && g_node[id]) { *out = *g_node[id]; ok = true; }
    spin_unlock_irqrestore(&g_lock, s);
    return ok;
}

UINT32 PnpCount(void) { return __atomic_load_n(&g_count, __ATOMIC_ACQUIRE); }

int PnpWaitEvents(UINT32 after, PnpEvent *out, int max, UINT64 ticks, UINT32 *newest)
{
    UINT64 end = sched_ticks() + ticks;
    for (;;) {
        int n = 0;
        IrqState s = spin_lock_irqsave(&g_lock);
        UINT32 first = g_seq > RING ? g_seq - RING + 1 : 1;     /* the oldest kept */
        if (after + 1 < first) after = first - 1;
        if (after > g_seq) after = g_seq;                         /* (a reader from before a restart) */
        for (UINT32 q = after + 1; q <= g_seq && n < max; q++) out[n++] = g_ring[q % RING];
        *newest = g_seq;
        spin_unlock_irqrestore(&g_lock, s);
        if (n || !max || sched_ticks() >= end) return n;
        sched_sleep_until(NULL, sched_ticks() + 1);
    }
}

/* ---- PCI ---- */

UINT32 PnpPciNode(UINT8 bus, UINT8 dev, UINT8 func)
{
    char tail[16];
    ksnprintf(tail, sizeof(tail), "\\3&0&%x&%02X", bus, (dev << 3) | func);
    size_t tl = strlen(tail);
    UINT32 id = 0;
    IrqState s = spin_lock_irqsave(&g_lock);
    for (UINT32 i = 1; i <= g_count && !id; i++) {
        PnpNode *n = g_node[i];
        if (!n || strncmp(n->instance, "PCI\\", 4)) continue;
        size_t l = strlen(n->instance);
        if (l > tl && !strcmp(n->instance + l - tl, tail)) id = i;
    }
    spin_unlock_irqrestore(&g_lock, s);
    return id;
}

static const char *vendor_of(UINT16 v)
{
    switch (v) {
    case 0x8086: return "Intel Corporation";
    case 0x1022: case 0x1002: return "Advanced Micro Devices, Inc.";
    case 0x10DE: return "NVIDIA";
    case 0x10EC: return "Realtek";
    case 0x1AF4: case 0x1B36: return "Red Hat, Inc.";
    case 0x1234: return "QEMU";
    case 0x15AD: return "VMware, Inc.";
    case 0x1414: return "Microsoft";
    case 0x1013: return "Cirrus Logic";
    default:     return "(Standard system devices)";
    }
}

/* The setup class and description of a PCI function */
static void pci_kind(const PciDevice *d, PnpNode *n)
{
    UINT8 c = d->class_code, sc = d->subclass, pi = d->prog_if;
    const char *cls = "System", *desc = NULL;
    switch (c) {
    case 0x01:
        cls = sc == 0x08 ? "SCSIAdapter" : "HDC";
        desc = sc == 0x06 ? "Standard SATA AHCI Controller" : sc == 0x08 ? "Standard NVM Express Controller" :
               sc == 0x01 ? "Standard Dual Channel PCI IDE Controller" : NULL;
        break;
    case 0x02: cls = "Net"; desc = sc == 0x80 ? "Network Controller" : "Ethernet Controller"; break;
    case 0x03: cls = "Display"; desc = "Video Controller (VGA Compatible)"; break;
    case 0x04: cls = sc == 0x03 ? "System" : "MEDIA";
               desc = sc == 0x03 ? "High Definition Audio Controller" : "Multimedia Audio Controller"; break;
    case 0x06:
        desc = sc == 0x00 ? "Host bridge" : sc == 0x01 ? "PCI standard ISA bridge" :
               sc == 0x04 ? "PCI Express Root Port" : "PCI standard bridge";
        break;
    case 0x0C:
        if (sc == 0x03) {
            cls = "USB";
            desc = pi == 0x30 ? "USB xHCI Compliant Host Controller" : pi == 0x20 ? "Standard Enhanced PCI to USB Host Controller" :
                   pi == 0x10 ? "Standard OpenHCD USB Host Controller" : "Standard Universal PCI to USB Host Controller";
        } else if (sc == 0x05) desc = "SMBus Controller";
        break;
    case 0x0D: cls = "Net"; desc = "Wireless Network Controller"; break;
    }
    copy_str(n->cls, sizeof(n->cls), cls);
    copy_str(n->desc, sizeof(n->desc), desc ? desc : PciClassName(d));
}

/* A REG_MULTI_SZ list from strings (an empty one ends it) */
static void multi_sz(char *out, size_t cap, const char *const *v, int count)
{
    size_t at = 0;
    for (int i = 0; i < count && v[i][0]; i++) {
        size_t l = strlen(v[i]);
        if (at + l + 2 > cap) break;
        memcpy(out + at, v[i], l + 1);
        at += l + 1;
    }
    out[at] = 0;
}

static void add_pci(const PciDevice *d, UINT32 parent)
{
    PnpNode *n = kzalloc(sizeof(PnpNode));
    if (!n) return;
    bool type0 = !((PciRead32(d->bus, d->dev, d->func, 0x0C) >> 16) & 0x7F);    /* (bridges have no subsystem there) */
    UINT32 sub = type0 ? PciRead32(d->bus, d->dev, d->func, 0x2C) : 0;
    UINT8 rev = (UINT8)PciRead32(d->bus, d->dev, d->func, 0x08);
    char id[4][64], cc[3][48];
    ksnprintf(id[0], 64, "PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X", d->vendor, d->device, sub, rev);
    ksnprintf(id[1], 64, "PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X", d->vendor, d->device, sub);
    ksnprintf(id[2], 64, "PCI\\VEN_%04X&DEV_%04X&REV_%02X", d->vendor, d->device, rev);
    ksnprintf(id[3], 64, "PCI\\VEN_%04X&DEV_%04X", d->vendor, d->device);
    ksnprintf(cc[0], 48, "PCI\\VEN_%04X&CC_%02X%02X%02X", d->vendor, d->class_code, d->subclass, d->prog_if);
    ksnprintf(cc[1], 48, "PCI\\CC_%02X%02X%02X", d->class_code, d->subclass, d->prog_if);
    ksnprintf(cc[2], 48, "PCI\\CC_%02X%02X", d->class_code, d->subclass);
    const char *ids[4] = { id[0], id[1], id[2], id[3] }, *ccs[3] = { cc[0], cc[1], cc[2] };
    multi_sz(n->hwids, sizeof(n->hwids), ids, 4);
    multi_sz(n->compat, sizeof(n->compat), ccs, 3);
    ksnprintf(n->instance, sizeof(n->instance), "%s\\3&0&%x&%02X", id[1], d->bus, (d->dev << 3) | d->func);
    ksnprintf(n->location, sizeof(n->location), "PCI bus %d, device %d, function %d", d->bus, d->dev, d->func);
    copy_str(n->mfg, sizeof(n->mfg), vendor_of(d->vendor));
    pci_kind(d, n);
    if (d->class_code == 0x06) {                       /* (bridges: the PCI bus driver's) */
        copy_str(n->service, sizeof(n->service), "pci");
        n->flags = PNP_STARTED;
    }
    PnpAdd(parent, n);
    kfree(n);
}

void PnpInit(void)
{
    PnpNode *n = kzalloc(sizeof(PnpNode));
    if (!n) return;
    copy_str(n->instance, sizeof(n->instance), "HTREE\\ROOT\\0");
    copy_str(n->desc, sizeof(n->desc), "NovaOS");
    n->flags = PNP_STARTED;
    PnpAdd(0, n);                                       /* (ID 1: PNP_ROOT) */

    memset(n, 0, sizeof(*n));
    static const char *const bus_ids[] = { "ACPI\\PNP0A08", "*PNP0A08", "" };
    static const char *const bus_cc[] = { "ACPI\\PNP0A03", "*PNP0A03", "" };
    copy_str(n->instance, sizeof(n->instance), "ACPI\\PNP0A08\\0");
    multi_sz(n->hwids, sizeof(n->hwids), bus_ids, 3);
    multi_sz(n->compat, sizeof(n->compat), bus_cc, 3);
    copy_str(n->cls, sizeof(n->cls), "System");
    copy_str(n->desc, sizeof(n->desc), "PCI Express Root Complex");
    copy_str(n->mfg, sizeof(n->mfg), "(Standard system devices)");
    copy_str(n->service, sizeof(n->service), "pci");
    n->flags = PNP_STARTED;
    UINT32 bus = PnpAdd(PNP_ROOT, n);
    kfree(n);

    PciDevice d;
    int count = 0;
    for (int i = 0; PciAt(i, &d, NULL); i++, count++) add_pci(&d, bus);
    g_live = true;
    kprintf("[PNP] device tree: the root, a PCI bus and %d PCI functions\n", count);
}
