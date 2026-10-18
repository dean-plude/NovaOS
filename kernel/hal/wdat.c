/*
 * wdat.c — the ACPI WDAT watchdog (see wdat.h)
 *
 * Written from Microsoft's "Hardware Watchdog Timers Design Specification"
 * (the WDAT table's layout, its actions and instructions) and the ACPI
 * specification's Generic Address Structure.  An action is the list of
 * the table's instructions for it, run in order:
 *
 *   READ_VALUE       (reg >> bit offset) & mask == value: the result
 *   READ_COUNTDOWN   (reg >> bit offset) & mask: the result
 *   WRITE_VALUE      value & mask, shifted to the bit offset
 *   WRITE_COUNTDOWN  the count & mask, shifted to the bit offset
 *
 * with PRESERVE_REGISTER keeping the register's other bits on a write.
 */

#include "wdat.h"
#include "acpi.h"
#include "pci.h"
#include "../arch/x86_64/cpu.h"
#include "../ke/printf.h"
#include "../lib/string.h"

/* Actions */
#define A_RESET                 0x01    /* reload the count */
#define A_SET_COUNTDOWN         0x06
#define A_SET_RUNNING           0x09
#define A_SET_STOPPED           0x0B
#define A_SET_REBOOT            0x11    /* expiry resets the PC (rather than shutting it down) */
#define A_SET_STATUS            0x21    /* clear "the watchdog fired" */

/* Instructions (the low bits of InstructionFlags) */
#define I_READ_VALUE            0
#define I_READ_COUNTDOWN        1
#define I_WRITE_VALUE           2
#define I_WRITE_COUNTDOWN       3
#define I_PRESERVE              0x80

#define WDAT_ENABLED            0x01    /* WatchdogFlags */

#pragma pack(push, 1)
typedef struct {
    UINT8  space, bit_width, bit_offset, access;    /* (space: 0 memory, 1 I/O, 2 PCI configuration) */
    UINT64 address;
} Gas;

typedef struct {
    UINT8  action, flags;
    UINT16 reserved;
    Gas    reg;
    UINT32 value, mask;
} WdatEntry;

typedef struct {
    char   sig[4];
    UINT32 length;
    UINT8  revision, checksum;
    char   oem[6], oem_table[8];
    UINT32 oem_rev, creator, creator_rev;
    UINT32 header_length;                           /* (of what follows the ACPI header, up to the entries) */
    UINT16 pci_segment;
    UINT8  pci_bus, pci_dev, pci_func, reserved[3];
    UINT32 period_ms;                               /* one count */
    UINT32 max_count, min_count;
    UINT8  flags, reserved2[3];
    UINT32 entries;
} Wdat;
#pragma pack(pop)

_Static_assert(sizeof(Gas) == 12, "GAS layout");
_Static_assert(sizeof(WdatEntry) == 24, "WDAT entry layout");
_Static_assert(sizeof(Wdat) == 68, "WDAT header layout");

static const Wdat      *g_wdat;
static const WdatEntry *g_ent;
static UINT32           g_n;
static bool             g_running, g_looked;

/* The register's width in bits: the access size, else the bit width */
static int width_of(const Gas *g)
{
    switch (g->access) {
    case 1: return 8;
    case 2: return 16;
    case 3: return 32;
    case 4: return 64;
    }
    return g->bit_width + g->bit_offset <= 8 ? 8 : g->bit_width + g->bit_offset <= 16 ? 16 :
           g->bit_width + g->bit_offset <= 32 ? 32 : 64;
}

/* Memory registers, mapped once per page (PciMapPhysical never unmaps,
 * and the update's watch reloads every second) */
static volatile void *map(UINT64 pa)
{
    static struct { UINT64 page; volatile UINT8 *va; } cache[8];
    UINT64 page = pa & ~0xFFFull;
    for (int i = 0; i < 8; i++) {
        if (cache[i].va && cache[i].page == page) return cache[i].va + (pa - page);
        if (!cache[i].va) {
            cache[i].va = (volatile UINT8 *)PciMapPhysical(page, 4096);
            if (!cache[i].va) return NULL;
            cache[i].page = page;
            return cache[i].va + (pa - page);
        }
    }
    return NULL;                                        /* (registers on more than 8 pages) */
}

/* PCI configuration space of bus 0: device in bits 32-47, function in
 * 16-31, the register in 0-15 (as ACPI's GAS defines it) */
static UINT32 pci_rw(UINT64 a, int bits, bool write, UINT32 val)
{
    UINT8 dev = (UINT8)(a >> 32), func = (UINT8)(a >> 16), off = (UINT8)a;
    UINT8 al = off & 0xFC;
    int sh = (off & 3) * 8;
    UINT32 m = bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1) << sh;
    UINT32 d = PciRead32(0, dev, func, al);
    if (!write) return (d & m) >> sh;
    PciWrite32(0, dev, func, al, (d & ~m) | ((val << sh) & m));
    return 0;
}

static bool reg_read(const Gas *g, UINT64 *out)
{
    int bits = width_of(g);
    switch (g->space) {
    case 0: {
        volatile void *p = map(g->address);
        if (!p) return false;
        *out = bits == 8 ? *(volatile UINT8 *)p : bits == 16 ? *(volatile UINT16 *)p :
               bits == 32 ? *(volatile UINT32 *)p : *(volatile UINT64 *)p;
        return true;
    }
    case 1: {
        UINT16 port = (UINT16)g->address;
        if (bits > 32) return false;
        *out = bits == 8 ? inb(port) : bits == 16 ? inw(port) : inl(port);
        return true;
    }
    case 2:
        if (bits > 32) return false;
        *out = pci_rw(g->address, bits, false, 0);
        return true;
    }
    return false;
}

static bool reg_write(const Gas *g, UINT64 v)
{
    int bits = width_of(g);
    switch (g->space) {
    case 0: {
        volatile void *p = map(g->address);
        if (!p) return false;
        if (bits == 8) *(volatile UINT8 *)p = (UINT8)v;
        else if (bits == 16) *(volatile UINT16 *)p = (UINT16)v;
        else if (bits == 32) *(volatile UINT32 *)p = (UINT32)v;
        else *(volatile UINT64 *)p = v;
        return true;
    }
    case 1: {
        UINT16 port = (UINT16)g->address;
        if (bits == 8) outb(port, (UINT8)v);
        else if (bits == 16) outw(port, (UINT16)v);
        else if (bits == 32) outl(port, (UINT32)v);
        else return false;
        return true;
    }
    case 2:
        if (bits > 32) return false;
        pci_rw(g->address, bits, true, (UINT32)v);
        return true;
    }
    return false;
}

/* Run @action's instructions; false if the table has none for it or a
 * register can't be reached.  @param: the count, for WRITE_COUNTDOWN. */
static bool run(UINT8 action, UINT32 param)
{
    bool any = false;
    for (UINT32 i = 0; i < g_n; i++) {
        const WdatEntry *e = &g_ent[i];
        if (e->action != action) continue;
        any = true;
        UINT8 ins = e->flags & ~I_PRESERVE;
        UINT64 x, mask = (UINT64)e->mask << e->reg.bit_offset;
        if (ins == I_READ_VALUE || ins == I_READ_COUNTDOWN) {
            if (!reg_read(&e->reg, &x)) return false;       /* (results are not needed by the actions used) */
            continue;
        }
        if (ins != I_WRITE_VALUE && ins != I_WRITE_COUNTDOWN) return false;
        x = (UINT64)((ins == I_WRITE_VALUE ? e->value : param) & e->mask) << e->reg.bit_offset;
        if (e->flags & I_PRESERVE) {
            UINT64 y;
            if (!reg_read(&e->reg, &y)) return false;
            x |= y & ~mask;
        }
        if (!reg_write(&e->reg, x)) return false;
    }
    return any;
}

static bool has(UINT8 action)
{
    for (UINT32 i = 0; i < g_n; i++) if (g_ent[i].action == action) return true;
    return false;
}

/* The table, checked: the entries fit, and the firmware has the watchdog on */
static bool find(void)
{
    if (g_looked) return g_wdat != NULL;
    g_looked = true;
    const Wdat *w = AcpiFindTable("WDAT");
    if (!w) return false;
    UINT32 at = 36 + w->header_length;
    if (w->length < sizeof(Wdat) || at < sizeof(Wdat) || at > w->length ||
        (UINT64)w->entries * sizeof(WdatEntry) > w->length - at || !w->period_ms || !w->max_count) {
        kprintf("[WDAT] The firmware's WDAT table is malformed: not used\n");
        return false;
    }
    if (!(w->flags & WDAT_ENABLED)) {
        kprintf("[WDAT] The firmware's watchdog is switched off (WDAT flags %x)\n", w->flags);
        return false;
    }
    g_wdat = w;
    g_ent = (const WdatEntry *)((const UINT8 *)w + at);
    g_n = w->entries;
    if (!has(A_RESET) || !has(A_SET_COUNTDOWN) || !has(A_SET_RUNNING) || !has(A_SET_STOPPED)) {
        kprintf("[WDAT] The firmware's WDAT table lacks an action NovaOS needs: not used\n");
        g_wdat = NULL;
        return false;
    }
    return true;
}

bool WdatStart(UINT32 seconds)
{
    if (!find()) return false;
    UINT64 count = ((UINT64)seconds * 1000 + g_wdat->period_ms - 1) / g_wdat->period_ms;
    if (count < g_wdat->min_count) count = g_wdat->min_count;
    if (count > g_wdat->max_count) count = g_wdat->max_count;
    run(A_SET_STOPPED, 0);
    bool ok = (!has(A_SET_REBOOT) || run(A_SET_REBOOT, 0)) && run(A_SET_COUNTDOWN, (UINT32)count);
    if (ok && has(A_SET_STATUS)) run(A_SET_STATUS, 0);
    ok = ok && run(A_RESET, 0) && run(A_SET_RUNNING, 0);
    if (!ok) {
        kprintf("[WDAT] The firmware's watchdog could not be started (a register it names is out of reach)\n");
        return false;
    }
    g_running = true;
    UINT64 ms = count * g_wdat->period_ms;
    kprintf("[WDAT] Watchdog running (the firmware's WDAT table): the PC resets %u.%u s after the last reload\n",
            (unsigned)(ms / 1000), (unsigned)(ms % 1000 / 100));
    return true;
}

bool WdatRunning(void) { return g_running; }

void WdatReload(void)
{
    if (g_running) run(A_RESET, 0);
}

void WdatStop(void)
{
    if (!g_running) return;
    run(A_SET_STOPPED, 0);
    g_running = false;
    kprintf("[WDAT] Watchdog stopped\n");
}

void WdatDescribe(char *out, int cap)
{
    out[0] = '\0';
    if (!find()) return;
    ksnprintf(out, (size_t)cap, "ACPI WDAT: counts of %u ms, %u to %u, %s", g_wdat->period_ms, g_wdat->min_count,
              g_wdat->max_count, has(A_SET_REBOOT) ? "resets the PC" : "the firmware's expiry action");
}
