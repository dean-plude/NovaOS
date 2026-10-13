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
 * Message-signalled interrupts: a device whose driver opted in (INF:
 * MessageSignaledInterruptProperties\MSISupported) and that has an MSI or
 * MSI-X capability is started with a message interrupt descriptor instead
 * (pnp.c reserves an aligned block of vectors here, NtIntrMsiAlloc).  The
 * driver's IoConnectInterruptEx(CONNECT_MESSAGE_BASED) connects its
 * message routine to each vector in the block, programs the device
 * (NtPnpMsiEnable writes the capability's address and data) and returns the
 * IO_INTERRUPT_MESSAGE_INFO table, as Windows does; no I/O APIC is involved.
 * A driver that asks for messages with a fallback on a device that was not
 * given them gets its line-based routine connected, as Windows does on a
 * device without MSI.
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
#define MAX_LINES   96              /* (a line per vector at most) */

/* KINTERRUPT, as far as NovaOS needs it (drivers only pass it back) */
typedef struct NtInterrupt {
    short                Type, Size;            /* 22 (InterruptObject) */
    struct NtInterrupt  *next;                  /* on its vector */
    NT_KSERVICE_ROUTINE  isr;                   /* (a NT_KMESSAGE_SERVICE_ROUTINE when msg) */
    bool                 msg;                   /* message-signalled: isr gets msg_id */
    UINT32               msg_id;
    void                *ctx;
    NT_KSPIN_LOCK       *lock;                  /* the driver's, or own_lock */
    NT_KSPIN_LOCK        own_lock;
    UINT8                vector, irql, sync_irql;
    UINT32               gsi;
    UINT64               count;                 /* interrupts it claimed */
} NtInterrupt;

typedef struct {
    UINT32       gsi;
    UINT8        vector;                        /* (0: the slot is free) */
    bool         level, low;
    bool         msi;                           /* a message vector: no I/O APIC input */
    NtInterrupt *list;                          /* the routines connected to it */
} Line;

static KSpinLock g_lock = KSPINLOCK_INIT;
static Line      g_line[MAX_LINES];
static int       g_lines;
static bool      g_used[256];                   /* vectors handed out (lines never give theirs back) */

/* @count vectors in a row from a multiple of @count (a power of two, at
 * most 16, so they share an IRQL), marked used; the first, or 0.  g_lock held. */
static UINT8 vec_alloc(unsigned count)
{
    for (unsigned v = (VEC_FIRST + count - 1) & ~(count - 1); v + count - 1 <= VEC_LAST; v += count) {
        bool free_ = true;
        for (unsigned i = 0; i < count; i++) if (g_used[v + i]) { free_ = false; break; }
        if (!free_) continue;
        for (unsigned i = 0; i < count; i++) g_used[v + i] = true;
        return (UINT8)v;
    }
    return 0;
}

static Line *line_slot(void)
{
    for (int i = 0; i < g_lines; i++) if (!g_line[i].vector) return &g_line[i];
    return g_lines < MAX_LINES ? &g_line[g_lines++] : NULL;
}

UINT8 NtIntrVector(UINT32 gsi, bool level, bool low)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    UINT8 v = 0;
    for (int i = 0; i < g_lines && !v; i++) if (g_line[i].vector && !g_line[i].msi && g_line[i].gsi == gsi) v = g_line[i].vector;
    Line *l = v ? NULL : line_slot();
    if (l) {
        UINT8 want = vec_alloc(1);
        UINT32 routed;
        if (want && IoApicRouteIrq(gsi, level, low, want, &routed)) {
            IoApicMask(routed, true);                         /* (until a driver connects) */
            *l = (Line){ routed, want, level, low, false, NULL };
            v = want;
        } else if (want) {
            g_used[want] = false;
        }
    }
    spin_unlock_irqrestore(&g_lock, s);
    if (!v) kprintf("[NT] interrupt: GSI %u cannot be routed\n", gsi);
    return v;
}

UINT8 NtIntrMsiAlloc(unsigned count)
{
    if (!count || count > 16 || (count & (count - 1))) return 0;
    IrqState s = spin_lock_irqsave(&g_lock);
    UINT8 first = 0;
    int slots = 0;
    for (int i = 0; i < g_lines; i++) if (!g_line[i].vector) slots++;
    if (slots + (MAX_LINES - g_lines) >= (int)count) first = vec_alloc(count);
    for (unsigned i = 0; first && i < count; i++) {
        Line *l = line_slot();
        *l = (Line){ 0, (UINT8)(first + i), false, false, true, NULL };
    }
    spin_unlock_irqrestore(&g_lock, s);
    return first;
}

void NtIntrMsiFree(UINT8 first, unsigned count)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    for (unsigned i = 0; i < count; i++) {
        for (int k = 0; k < g_lines; k++)
            if (g_line[k].vector == first + i && g_line[k].msi) g_line[k].vector = 0;
        g_used[first + i] = false;
    }
    spin_unlock_irqrestore(&g_lock, s);
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
        UINT8 mine = in->msg ? ((NT_KMESSAGE_SERVICE_ROUTINE)(void *)in->isr)(in, in->ctx, in->msg_id) : in->isr(in, in->ctx);
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
        if (!l->msi) IoApicMask(l->gsi, false);
    }
    *out = in;
    return NT_STATUS_SUCCESS;
}

NT_API void nt_IoDisconnectInterrupt(void *obj);

#define MSG_NOT_OFFERED ((NTSTATUS)0xC0000225)    /* STATUS_NOT_FOUND: the device has no messages */

/* The header before a message-based connection's IO_INTERRUPT_MESSAGE_INFO */
typedef struct {
    NT_DEVICE_OBJECT *pdo;
    NT_KSPIN_LOCK     lock;             /* (the messages' lock, unless the driver gave one) */
} MsgConn;

static NTSTATUS connect_messages(NT_IO_CONNECT_INTERRUPT_PARAMETERS *p)
{
    UINT8 first, irql;
    UINT32 n;
    if (!NtPnpMessagesOf(p->MessageBased.PhysicalDeviceObject, &first, &n, &irql)) return MSG_NOT_OFFERED;
    if (!p->MessageBased.MessageServiceRoutine) return NT_STATUS_INVALID_PARAMETER;
    size_t size = sizeof(MsgConn) + sizeof(NT_IO_INTERRUPT_MESSAGE_INFO) + (n - 1) * sizeof(NT_IO_INTERRUPT_MESSAGE_INFO_ENTRY);
    MsgConn *c = kzalloc(size);
    if (!c) return NT_STATUS_INSUFFICIENT_RESOURCES;
    c->pdo = p->MessageBased.PhysicalDeviceObject;
    NT_IO_INTERRUPT_MESSAGE_INFO *t = (NT_IO_INTERRUPT_MESSAGE_INFO *)(c + 1);
    t->UnifiedIrql = irql;
    t->MessageCount = n;
    NT_KSPIN_LOCK *lock = p->MessageBased.SpinLock ? p->MessageBased.SpinLock : &c->lock;
    UINT8 sync = p->MessageBased.SynchronizeIrql > irql ? p->MessageBased.SynchronizeIrql : irql;
    for (UINT32 i = 0; i < n; i++) {
        NtInterrupt *in;
        NTSTATUS st = connect(&in, (NT_KSERVICE_ROUTINE)p->MessageBased.MessageServiceRoutine, p->MessageBased.ServiceContext,
                              lock, (UINT32)first + i, irql, sync);
        if (!NT_SUCCESS(st)) {
            for (UINT32 k = 0; k < i; k++) nt_IoDisconnectInterrupt(t->MessageInfo[k].InterruptObject);
            kfree(c);
            return st;
        }
        in->msg = true;
        in->msg_id = i;
        NT_IO_INTERRUPT_MESSAGE_INFO_ENTRY *e = &t->MessageInfo[i];
        UINT64 addr;
        UINT32 data;
        IoApicMsiMessage((UINT8)(first + i), &addr, &data);
        e->MessageAddress = (long long)addr;
        e->TargetProcessorSet = 1;
        e->InterruptObject = in;
        e->MessageData = data;
        e->Vector = first + i;
        e->Irql = irql;
        e->Mode = 1;                                    /* Latched */
    }
    NtPnpMsiEnable(c->pdo, true);                       /* (already on, since it started) */
    *p->MessageBased.ConnectionContext = t;
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
        NTSTATUS st = connect_messages(p);              /* (the device got messages, or the fallback) */
        if (st != MSG_NOT_OFFERED) return st;
        if (!p->MessageBased.FallBackServiceRoutine) return NT_STATUS_INVALID_PARAMETER;
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
        if (!l->msi) IoApicMask(l->gsi, true);
        IrqInstall(l->vector, NULL, NULL);
    }
    kfree(in);
}

NT_API void nt_IoDisconnectInterruptEx(NT_IO_DISCONNECT_INTERRUPT_PARAMETERS *p)
{
    if (!p) return;
    if (p->Version != NT_CONNECT_MESSAGE_BASED) {
        nt_IoDisconnectInterrupt(p->ConnectionContext);
        return;
    }
    NT_IO_INTERRUPT_MESSAGE_INFO *t = p->ConnectionContext;
    if (!t) return;
    MsgConn *c = (MsgConn *)t - 1;
    for (UINT32 i = 0; i < t->MessageCount; i++) nt_IoDisconnectInterrupt(t->MessageInfo[i].InterruptObject);
    kfree(c);
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
