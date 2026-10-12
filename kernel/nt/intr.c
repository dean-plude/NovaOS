/*
 * intr.c — device interrupts for Windows drivers: IoConnectInterrupt(Ex)
 *
 * A PCI function's interrupt pin is wired to an I/O APIC input (a GSI,
 * from ACPI's _PRT).  When the PnP manager starts a Windows driver on the
 * function (pnp.c) it routes that GSI, masked, to a vector of its own and
 * gives the driver the interrupt as Windows does: raw (Level and Vector
 * the GSI) and translated (Vector the CPU's vector, Level the IRQL it runs
 * at, vector / 16 as on Windows x64, which is what the APIC's task
 * priority (CR8) holds back).  The driver connects its service routine
 * with IoConnectInterrupt or IoConnectInterruptEx: the input is unmasked,
 * and each interrupt runs the routines connected to its vector at that
 * IRQL, each under its interrupt spin lock, until one says the interrupt
 * was its device's (shared lines).  KeSynchronizeExecution and
 * KeAcquireInterruptSpinLock take the same lock at the same IRQL, so a
 * driver's code and its service routine never run at once.
 *
 * Message-signalled interrupts (MSI) are not offered yet: a driver that
 * asks for them with a fallback (CONNECT_MESSAGE_BASED) gets its line-based
 * routine connected, as Windows does on a device without MSI.
 */
#include "ntapi.h"
#include "../ke/irql.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../hal/ioapic.h"

#define VEC_FIRST   0x60            /* IRQL 6 */
#define VEC_LAST    0xBF            /* IRQL 11, the highest device IRQL */
#define MAX_LINES   16

/* KINTERRUPT, as far as NovaOS needs it (drivers only pass it back) */
typedef struct NtInterrupt {
    short                Type, Size;            /* 22 (InterruptObject) */
    struct NtInterrupt  *next;                  /* on its vector */
    NT_KSERVICE_ROUTINE  isr;
    void                *ctx;
    NT_KSPIN_LOCK       *lock;                  /* the driver's, or own_lock */
    NT_KSPIN_LOCK        own_lock;
    UINT8                vector, irql, sync_irql;
    UINT32               gsi;
    UINT64               count;                 /* interrupts it claimed */
} NtInterrupt;

typedef struct {
    UINT32       gsi;
    UINT8        vector;
    bool         level, low;
    NtInterrupt *list;                          /* the routines connected to it */
} Line;

static KSpinLock g_lock = KSPINLOCK_INIT;
static Line      g_line[MAX_LINES];
static int       g_lines;
static UINT8     g_next = VEC_FIRST;

UINT8 NtIntrVector(UINT32 gsi, bool level, bool low)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    UINT8 v = 0;
    for (int i = 0; i < g_lines && !v; i++) if (g_line[i].gsi == gsi) v = g_line[i].vector;
    if (!v && g_lines < MAX_LINES && g_next <= VEC_LAST) {
        UINT32 routed;
        if (IoApicRouteIrq(gsi, level, low, g_next, &routed)) {
            IoApicMask(routed, true);                         /* (until a driver connects) */
            g_line[g_lines++] = (Line){ routed, g_next, level, low, NULL };
            v = g_next;
            g_next++;
        }
    }
    spin_unlock_irqrestore(&g_lock, s);
    if (!v) kprintf("[NT] interrupt: GSI %u cannot be routed\n", gsi);
    return v;
}

static Line *line_of(UINT8 vector)
{
    for (int i = 0; i < g_lines; i++) if (g_line[i].vector == vector) return &g_line[i];
    return NULL;
}

/* The vector's interrupt: each connected routine at its IRQL, under its
 * lock, until one claims it (interrupt context, interrupts off) */
static void dispatch(void *arg)
{
    Line *l = arg;
    UINT8 old = KeIrql();
    UINT8 fx[512] __attribute__((aligned(16)));
    __asm__ volatile ("fxsave64 (%0)" : : "r"(fx) : "memory");   /* (driver code may use SSE) */
    for (NtInterrupt *in = l->list; in; in = in->next) {
        KeIrqlSet(in->sync_irql);
        nt_KeAcquireSpinLockAtDpcLevel(in->lock);
        UINT8 mine = in->isr(in, in->ctx);
        nt_KeReleaseSpinLockFromDpcLevel(in->lock);
        if (mine) { in->count++; break; }
    }
    __asm__ volatile ("fxrstor64 (%0)" : : "r"(fx) : "memory");
    KeIrqlSet(old);
}

static NTSTATUS connect(NtInterrupt **out, NT_KSERVICE_ROUTINE isr, void *ctx, NT_KSPIN_LOCK *lock, UINT32 vector,
                        UINT8 irql, UINT8 sync_irql)
{
    *out = NULL;
    Line *l = vector <= 0xFF ? line_of((UINT8)vector) : NULL;
    if (!l || !isr || irql != (vector >> 4) || sync_irql < irql) return NT_STATUS_INVALID_PARAMETER;
    NtInterrupt *in = kzalloc(sizeof(*in));
    if (!in) return NT_STATUS_INSUFFICIENT_RESOURCES;
    in->Type = 22;
    in->Size = (short)sizeof(*in);
    in->isr = isr;
    in->ctx = ctx;
    in->lock = lock ? lock : &in->own_lock;
    in->vector = (UINT8)vector;
    in->irql = irql;
    in->sync_irql = sync_irql;
    in->gsi = l->gsi;
    IrqState s = spin_lock_irqsave(&g_lock);
    bool first = !l->list;
    NtInterrupt **pp = &l->list;
    while (*pp) pp = &(*pp)->next;
    *pp = in;
    spin_unlock_irqrestore(&g_lock, s);
    if (first) {
        IrqInstall(l->vector, dispatch, l);
        IoApicMask(l->gsi, false);
    }
    *out = in;
    return NT_STATUS_SUCCESS;
}

NT_API NT_STATUS nt_IoConnectInterrupt(void **out, NT_KSERVICE_ROUTINE isr, void *ctx, NT_KSPIN_LOCK *lock,
                                       UINT32 vector, UINT8 irql, UINT8 sync_irql, int mode, UINT8 share,
                                       UINT64 cpus, UINT8 floating)
{
    (void)mode; (void)share; (void)cpus; (void)floating;
    return connect((NtInterrupt **)out, isr, ctx, lock, vector, irql, sync_irql);
}

NT_API NT_STATUS nt_IoConnectInterruptEx(NT_IO_CONNECT_INTERRUPT_PARAMETERS *p)
{
    if (!p) return NT_STATUS_INVALID_PARAMETER;
    if (p->Version == NT_CONNECT_FULLY_SPECIFIED || p->Version == NT_CONNECT_FULLY_SPECIFIED_GROUP) {   /* (one group) */
        return connect((NtInterrupt **)p->FullySpecified.InterruptObject, p->FullySpecified.ServiceRoutine,
                       p->FullySpecified.ServiceContext, p->FullySpecified.SpinLock, p->FullySpecified.Vector,
                       p->FullySpecified.Irql,
                       p->FullySpecified.SynchronizeIrql ? p->FullySpecified.SynchronizeIrql : p->FullySpecified.Irql);
    }
    NT_KSERVICE_ROUTINE isr = p->LineBased.ServiceRoutine;
    if (p->Version == NT_CONNECT_MESSAGE_BASED) {
        if (!p->MessageBased.FallBackServiceRoutine) return NT_STATUS_INVALID_PARAMETER;   /* (no MSI yet) */
        isr = p->MessageBased.FallBackServiceRoutine;
    } else if (p->Version != NT_CONNECT_LINE_BASED) {
        return NT_STATUS_INVALID_PARAMETER;
    }
    /* the line the PnP manager gave the device */
    UINT32 vector;
    UINT8 irql;
    if (!NtPnpInterruptOf(p->LineBased.PhysicalDeviceObject, &vector, &irql)) return NT_STATUS_INVALID_PARAMETER;
    UINT8 sync = p->LineBased.SynchronizeIrql > irql ? p->LineBased.SynchronizeIrql : irql;
    NTSTATUS st = connect((NtInterrupt **)p->LineBased.InterruptObject, isr, p->LineBased.ServiceContext,
                          p->LineBased.SpinLock, vector, irql, sync);
    if (NT_SUCCESS(st) && p->Version == NT_CONNECT_MESSAGE_BASED) p->Version = NT_CONNECT_LINE_BASED;
    return st;
}

NT_API void nt_IoDisconnectInterrupt(void *obj)
{
    NtInterrupt *in = obj;
    if (!in) return;
    Line *l = line_of(in->vector);
    if (!l) return;
    IrqState s = spin_lock_irqsave(&g_lock);
    for (NtInterrupt **pp = &l->list; *pp; pp = &(*pp)->next)
        if (*pp == in) { *pp = in->next; break; }
    bool last = !l->list;
    spin_unlock_irqrestore(&g_lock, s);
    if (last) {
        IoApicMask(l->gsi, true);
        IrqInstall(l->vector, NULL, NULL);
    }
    kfree(in);
}

NT_API void nt_IoDisconnectInterruptEx(NT_IO_DISCONNECT_INTERRUPT_PARAMETERS *p)
{
    if (p) nt_IoDisconnectInterrupt(p->ConnectionContext);
}

NT_API UINT8 nt_KeSynchronizeExecution(void *obj, NT_KSYNCHRONIZE_ROUTINE fn, void *ctx)
{
    NtInterrupt *in = obj;
    UINT8 old = KeIrql();
    if (old < in->sync_irql) KeIrqlSet(in->sync_irql);
    nt_KeAcquireSpinLockAtDpcLevel(in->lock);
    UINT8 r = fn(ctx);
    nt_KeReleaseSpinLockFromDpcLevel(in->lock);
    KeIrqlSet(old);
    return r;
}

NT_API UINT8 nt_KeAcquireInterruptSpinLock(void *obj)
{
    NtInterrupt *in = obj;
    UINT8 old = KeIrql();
    if (old < in->sync_irql) KeIrqlSet(in->sync_irql);
    nt_KeAcquireSpinLockAtDpcLevel(in->lock);
    return old;
}

NT_API void nt_KeReleaseInterruptSpinLock(void *obj, UINT8 old)
{
    NtInterrupt *in = obj;
    nt_KeReleaseSpinLockFromDpcLevel(in->lock);
    KeIrqlSet(old);
}
