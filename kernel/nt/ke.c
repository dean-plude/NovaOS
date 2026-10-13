/*
 * ke.c — the kernel's own API for drivers: IRQL, DPCs, spin locks, the
 * objects drivers wait on (events, semaphores, timers), fast mutexes,
 * time, and KeBugCheckEx
 *
 * The IRQL is CR8, as on Windows x64 (ke/irql.h): drivers' inline
 * KeRaiseIrql and KeLowerIrql write it directly, and the exports here do
 * the same.  A DPC is queued on the CPU that asks for it and runs from the
 * DPC interrupt (VECTOR_DPC), which the local APIC holds back until that
 * CPU's IRQL is below DISPATCH_LEVEL: queued from a dispatch routine at
 * PASSIVE_LEVEL it runs at once; queued at DISPATCH_LEVEL (holding a spin
 * lock) it runs when the lock is let go.  DPCs run at DISPATCH_LEVEL with
 * interrupts on, and the interrupted code's SSE registers are kept (the
 * driver's code may use them; NovaOS's interrupt entry does not save them).
 *
 * Waits sleep the thread (sched_sleep_until_tsc) with a wait block in the
 * object's WaitListHead, under one dispatcher lock, as Windows' dispatcher
 * database lock.  Timers run off the 100 Hz clock tick (KiTimerTick): due,
 * a timer is signalled and its DPC queued.
 */
#include "nt.h"
#include "../ke/irql.h"
#include "../ke/printf.h"
#include "../ke/kpcr.h"
#include "../ke/scheduler.h"
#include "../ke/smp.h"
#include "../ke/spinlock.h"
#include "../arch/x86_64/apic.h"
#include "../arch/x86_64/cpu.h"
#include "../lib/string.h"
#include "../um/um_internal.h"

#define SYNCH_LEVEL  12

/* -----------------------------------------------------------------------
 * IRQL
 * ----------------------------------------------------------------------- */
NT_API UINT8 nt_KeGetCurrentIrql(void) { return KeIrql(); }

NT_API UINT8 nt_KfRaiseIrql(UINT8 irql)
{
    UINT8 old = KeIrql();
    if (irql < old) kprintf("[NT] IRQL_NOT_GREATER_OR_EQUAL: KeRaiseIrql to %u from %u\n", irql, old);
    KeIrqlSet(irql);
    return old;
}

NT_API void nt_KeLowerIrql(UINT8 irql)
{
    if (irql > KeIrql()) kprintf("[NT] IRQL_NOT_LESS_OR_EQUAL: KeLowerIrql to %u from %u\n", irql, KeIrql());
    KeIrqlSet(irql);
}

NT_API UINT8 nt_KeRaiseIrqlToDpcLevel(void) { return nt_KfRaiseIrql(DISPATCH_LEVEL); }
NT_API UINT8 nt_KeRaiseIrqlToSynchLevel(void) { return nt_KfRaiseIrql(SYNCH_LEVEL); }

/* -----------------------------------------------------------------------
 * DPCs
 * ----------------------------------------------------------------------- */
typedef struct {
    KSpinLock lock;
    NT_KDPC  *head, *tail;
} DpcQueue;

static DpcQueue g_dpcq[MAX_CPUS];

void KiRequestDispatchInterrupt(void)
{
    PKPCR k = KiGetCurrentKpcr();
    /* (from a driver's interrupt routine: sent once the IRQL is back down,
     * intr.c, so that no interrupt is pending below the raised IRQL when it
     * drops with interrupts off; KVM nested on some hosts fails the VM entry
     * after that CR8 write, "KVM: entry failed, hardware error 0x0") */
    if (k->InIsr) { k->DpcPending = 1; return; }
    apic_send_ipi(k->ApicId, APIC_IPI_FIXED | VECTOR_DPC);
}

NT_API void nt_KeInitializeDpc(NT_KDPC *dpc, NT_KDEFERRED_ROUTINE routine, void *ctx)
{
    memset(dpc, 0, sizeof(*dpc));
    dpc->Type = 19;                                  /* DpcObject */
    dpc->Importance = 1;                             /* MediumImportance */
    dpc->DeferredRoutine = (void *)routine;
    dpc->DeferredContext = ctx;
}

NT_API void nt_KeInitializeThreadedDpc(NT_KDPC *dpc, NT_KDEFERRED_ROUTINE routine, void *ctx)
{
    nt_KeInitializeDpc(dpc, routine, ctx);           /* (runs as a normal DPC, as with threaded DPCs off) */
    dpc->Type = 24;
}

NT_API void nt_KeSetImportanceDpc(NT_KDPC *dpc, int importance) { dpc->Importance = (unsigned char)importance; }
NT_API void nt_KeSetTargetProcessorDpc(NT_KDPC *dpc, char cpu) { (void)dpc; (void)cpu; }   /* (queued where asked from) */

NT_API UINT8 nt_KeInsertQueueDpc(NT_KDPC *dpc, void *a1, void *a2)
{
    IrqState s = irq_save();
    DpcQueue *q = &g_dpcq[KiGetCurrentKpcr()->CpuNumber];
    spin_lock(&q->lock);
    bool queued = dpc->DpcData != NULL;
    if (!queued) {
        dpc->DpcData = q;
        dpc->SystemArgument1 = a1;
        dpc->SystemArgument2 = a2;
        dpc->DpcListEntry = NULL;
        if (dpc->Importance == 2 && q->head) {       /* HighImportance: first */
            dpc->DpcListEntry = q->head;
            q->head = dpc;
        } else {
            if (q->tail) q->tail->DpcListEntry = dpc;
            else q->head = dpc;
            q->tail = dpc;
        }
    }
    spin_unlock(&q->lock);
    if (!queued) KiRequestDispatchInterrupt();
    irq_restore(s);
    return !queued;
}

NT_API UINT8 nt_KeRemoveQueueDpc(NT_KDPC *dpc)
{
    IrqState s = irq_save();
    DpcQueue *q = dpc->DpcData;
    bool removed = false;
    if (q) {
        spin_lock(&q->lock);
        NT_KDPC *prev = NULL;
        for (NT_KDPC *d = q->head; d; prev = d, d = d->DpcListEntry) {
            if (d != dpc) continue;
            if (prev) prev->DpcListEntry = d->DpcListEntry;
            else q->head = d->DpcListEntry;
            if (q->tail == d) q->tail = prev;
            d->DpcData = NULL;
            removed = true;
            break;
        }
        spin_unlock(&q->lock);
    }
    irq_restore(s);
    return removed;
}

/* Wait until the DPCs queued now have run (every CPU's) */
NT_API void nt_KeFlushQueuedDpcs(void)
{
    for (UINT32 c = 0; c < g_cpu_count; c++)
        while (__atomic_load_n(&g_dpcq[c].head, __ATOMIC_ACQUIRE)) sched_yield();
}

void KiDispatchInterrupt(void)
{
    UINT8 old = KeIrql();
    KeIrqlSet(DISPATCH_LEVEL);
    DpcQueue *q = &g_dpcq[KiGetCurrentKpcr()->CpuNumber];
    UINT8 fx[512] __attribute__((aligned(16)));
    bool saved = false;
    for (;;) {
        spin_lock(&q->lock);
        NT_KDPC *d = q->head;
        if (d) {
            q->head = d->DpcListEntry;
            if (!q->head) q->tail = NULL;
            d->DpcData = NULL;
        }
        spin_unlock(&q->lock);
        if (!d) break;
        if (!saved) { __asm__ volatile ("fxsave64 (%0)" : : "r"(fx) : "memory"); saved = true; }
        NT_KDEFERRED_ROUTINE fn = (NT_KDEFERRED_ROUTINE)d->DeferredRoutine;
        void *ctx = d->DeferredContext, *a1 = d->SystemArgument1, *a2 = d->SystemArgument2;
        sti();
        fn(d, ctx, a1, a2);
        cli();
        if (KeIrql() != DISPATCH_LEVEL) {
            kprintf("[NT] IRQL_NOT_LESS_OR_EQUAL: DPC routine %p returned at IRQL %u\n", (void *)fn, KeIrql());
            KeIrqlSet(DISPATCH_LEVEL);
        }
    }
    if (saved) __asm__ volatile ("fxrstor64 (%0)" : : "r"(fx) : "memory");
    KeIrqlSet(old);
    sched_resched_ipi();                             /* (a switch put off at DISPATCH_LEVEL) */
}

/* -----------------------------------------------------------------------
 * Spin locks: KSPIN_LOCK is one pointer-sized word, bit 0 the lock
 * ----------------------------------------------------------------------- */
static void lock_word(NT_KSPIN_LOCK *l)
{
    while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE)) {
        /* (a driver's lock is not waited for with the big kernel lock held:
         * its holder may be on another CPU taking an interrupt, which wants
         * the big lock, while this one would spin with it for ever) */
        uint32_t bkl = interrupts_enabled() ? bkl_drop() : 0;
        while (__atomic_load_n(l, __ATOMIC_RELAXED)) __builtin_ia32_pause();
        bkl_restore(bkl);
    }
}

static void unlock_word(NT_KSPIN_LOCK *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }

static UINT8 raise_to_dpc(void)
{
    UINT8 old = KeIrql();
    if (old < DISPATCH_LEVEL) KeIrqlSet(DISPATCH_LEVEL);
    return old;
}

NT_API void nt_KeInitializeSpinLock(NT_KSPIN_LOCK *l) { *l = 0; }
NT_API UINT8 nt_KeTestSpinLock(NT_KSPIN_LOCK *l) { return !__atomic_load_n(l, __ATOMIC_RELAXED); }

NT_API UINT8 nt_KeAcquireSpinLockRaiseToDpc(NT_KSPIN_LOCK *l)
{
    UINT8 old = raise_to_dpc();
    lock_word(l);
    return old;
}

NT_API void nt_KeReleaseSpinLock(NT_KSPIN_LOCK *l, UINT8 old)
{
    unlock_word(l);
    KeIrqlSet(old);
}

NT_API void nt_KeAcquireSpinLockAtDpcLevel(NT_KSPIN_LOCK *l) { lock_word(l); }
NT_API void nt_KeReleaseSpinLockFromDpcLevel(NT_KSPIN_LOCK *l) { unlock_word(l); }

NT_API UINT8 nt_KeTryToAcquireSpinLockAtDpcLevel(NT_KSPIN_LOCK *l)
{
    return !__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE);
}

NT_API UINT8 nt_KeAcquireSpinLockForDpc(NT_KSPIN_LOCK *l) { return nt_KeAcquireSpinLockRaiseToDpc(l); }
NT_API void nt_KeReleaseSpinLockForDpc(NT_KSPIN_LOCK *l, UINT8 old) { nt_KeReleaseSpinLock(l, old); }

/* In-stack queued spin locks: the same lock word, the old IRQL in the handle */
NT_API void nt_KeAcquireInStackQueuedSpinLock(NT_KSPIN_LOCK *l, NT_KLOCK_QUEUE_HANDLE *h)
{
    h->LockQueue.Lock = l;
    h->LockQueue.Next = NULL;
    h->OldIrql = raise_to_dpc();
    lock_word(l);
}

NT_API void nt_KeReleaseInStackQueuedSpinLock(NT_KLOCK_QUEUE_HANDLE *h)
{
    unlock_word(h->LockQueue.Lock);
    KeIrqlSet(h->OldIrql);
}

NT_API void nt_KeAcquireInStackQueuedSpinLockAtDpcLevel(NT_KSPIN_LOCK *l, NT_KLOCK_QUEUE_HANDLE *h)
{
    h->LockQueue.Lock = l;
    h->LockQueue.Next = NULL;
    lock_word(l);
}

NT_API void nt_KeReleaseInStackQueuedSpinLockFromDpcLevel(NT_KLOCK_QUEUE_HANDLE *h) { unlock_word(h->LockQueue.Lock); }

/* The interlocked list helpers (interrupts off while the lock is held) */
static void list_lock(NT_KSPIN_LOCK *l, IrqState *s) { *s = irq_save(); lock_word(l); }
static void list_unlock(NT_KSPIN_LOCK *l, IrqState s) { unlock_word(l); irq_restore(s); }

NT_API NT_LIST_ENTRY *nt_ExInterlockedInsertHeadList(NT_LIST_ENTRY *head, NT_LIST_ENTRY *e, NT_KSPIN_LOCK *l)
{
    IrqState s;
    list_lock(l, &s);
    NT_LIST_ENTRY *first = head->Flink;
    e->Flink = first;
    e->Blink = head;
    first->Blink = e;
    head->Flink = e;
    list_unlock(l, s);
    return first == head ? NULL : first;
}

NT_API NT_LIST_ENTRY *nt_ExInterlockedInsertTailList(NT_LIST_ENTRY *head, NT_LIST_ENTRY *e, NT_KSPIN_LOCK *l)
{
    IrqState s;
    list_lock(l, &s);
    NT_LIST_ENTRY *last = head->Blink;
    e->Flink = head;
    e->Blink = last;
    last->Flink = e;
    head->Blink = e;
    list_unlock(l, s);
    return last == head ? NULL : last;
}

NT_API NT_LIST_ENTRY *nt_ExInterlockedRemoveHeadList(NT_LIST_ENTRY *head, NT_KSPIN_LOCK *l)
{
    IrqState s;
    list_lock(l, &s);
    NT_LIST_ENTRY *e = head->Flink;
    if (e == head) e = NULL;
    else {
        head->Flink = e->Flink;
        e->Flink->Blink = head;
    }
    list_unlock(l, s);
    return e;
}

/* -----------------------------------------------------------------------
 * Waiting: events, semaphores, timers
 * ----------------------------------------------------------------------- */
typedef struct {
    NT_LIST_ENTRY    link;              /* in the object's WaitListHead (first: the list's entries are these) */
    Thread          *thread;
    volatile UINT32  done;              /* the object was given to this waiter */
} Waiter;

static KSpinLock g_disp = KSPINLOCK_INIT;

static void list_init(NT_LIST_ENTRY *h) { h->Flink = h->Blink = h; }

static void list_remove(NT_LIST_ENTRY *e)
{
    e->Blink->Flink = e->Flink;
    e->Flink->Blink = e->Blink;
    e->Flink = e->Blink = e;
}

static void header_init(NT_DISPATCHER_HEADER *h, UINT8 type, UINT8 size_dwords, int state)
{
    h->Type = type;
    h->Signalling = 0;
    h->Size = size_dwords;
    h->Reserved1 = 0;
    h->SignalState = state;
    list_init(&h->WaitListHead);
}

/* KMUTANT.OwnerThread: past the dispatcher header and MutantListEntry */
static void **mutant_owner(NT_DISPATCHER_HEADER *h) { return (void **)((UINT8 *)h + 0x28); }

/* Take the object for the calling thread if it is available (g_disp held) */
static bool take(NT_DISPATCHER_HEADER *h)
{
    if (h->Type == NT_MutantObject) {                /* 1 free, <= 0 held (the negative recursion) */
        void **owner = mutant_owner(h);
        void *me = (void *)sched_current();
        if (h->SignalState == 1) { h->SignalState = 0; *owner = me; return true; }
        if (h->SignalState <= 0 && *owner == me) { h->SignalState--; return true; }   /* recursion */
        return false;
    }
    if (h->SignalState <= 0) return false;
    if (h->Type == NT_SynchronizationEvent || h->Type == NT_TimerSynchronizationObject) h->SignalState = 0;
    else if (h->Type == NT_SemaphoreObject) h->SignalState--;
    return true;
}

/* Give the signalled object to its waiters, first come first served
 * (g_disp held) */
static void satisfy(NT_DISPATCHER_HEADER *h)
{
    if (!h->WaitListHead.Flink) list_init(&h->WaitListHead);   /* (a header a driver zeroed itself) */
    if (h->Type == NT_MutantObject) {
        /* Ownership passes to the woken waiter, not to whoever released it,
         * so take() cannot run in the releaser's context: hand it over here. */
        while (h->WaitListHead.Flink != &h->WaitListHead && h->SignalState == 1) {
            Waiter *w = (Waiter *)h->WaitListHead.Flink;
            list_remove(&w->link);
            h->SignalState = 0;
            *mutant_owner(h) = w->thread;
            __atomic_store_n(&w->done, 1, __ATOMIC_RELEASE);
            sched_unblock(w->thread);
        }
        return;
    }
    while (h->WaitListHead.Flink != &h->WaitListHead && take(h)) {
        Waiter *w = (Waiter *)h->WaitListHead.Flink;
        list_remove(&w->link);
        __atomic_store_n(&w->done, 1, __ATOMIC_RELEASE);
        sched_unblock(w->thread);
    }
}

/* A wait's deadline from a Windows timeout: NULL waits for ever, a
 * negative one is relative, a positive one absolute system time
 * (100 ns units); UINT64_MAX for ever, 0 do not wait */
static UINT64 deadline(const INT64 *timeout)
{
    if (!timeout) return UINT64_MAX;
    INT64 t = *timeout;
    if (t < 0) return sched_tsc_after((UINT64)-t);
    UINT64 now = um_now_100ns();
    return (UINT64)t <= now ? 0 : sched_tsc_after((UINT64)t - now);
}

static NTSTATUS wait_one(NT_DISPATCHER_HEADER *h, const INT64 *timeout)
{
    UINT64 until = deadline(timeout);
    IrqState s = spin_lock_irqsave(&g_disp);
    if (!h->WaitListHead.Flink) list_init(&h->WaitListHead);
    if (take(h)) { spin_unlock_irqrestore(&g_disp, s); return NT_STATUS_SUCCESS; }
    if (!until) { spin_unlock_irqrestore(&g_disp, s); return NT_STATUS_TIMEOUT; }
    if (KeIrql() >= DISPATCH_LEVEL) {
        spin_unlock_irqrestore(&g_disp, s);
        kprintf("[NT] IRQL_NOT_LESS_OR_EQUAL: a wait at IRQL %u\n", KeIrql());
        return NT_STATUS_TIMEOUT;
    }
    Waiter w = { { NULL, NULL }, sched_current(), 0 };
    NT_LIST_ENTRY *last = h->WaitListHead.Blink;
    w.link.Flink = &h->WaitListHead;
    w.link.Blink = last;
    last->Flink = &w.link;
    h->WaitListHead.Blink = &w.link;
    spin_unlock_irqrestore(&g_disp, s);
    for (;;) {
        sched_sleep_until_tsc(&w.done, until);
        s = spin_lock_irqsave(&g_disp);
        if (w.done) { spin_unlock_irqrestore(&g_disp, s); return NT_STATUS_SUCCESS; }
        if (until != UINT64_MAX && rdtsc() >= until) {
            list_remove(&w.link);
            spin_unlock_irqrestore(&g_disp, s);
            return NT_STATUS_TIMEOUT;
        }
        spin_unlock_irqrestore(&g_disp, s);             /* (woken early: wait on) */
    }
}

NT_API NT_STATUS nt_KeWaitForSingleObject(void *obj, int reason, char mode, UINT8 alertable, const INT64 *timeout)
{
    (void)reason; (void)mode; (void)alertable;
    return wait_one(obj, timeout);
}

NT_API NT_STATUS nt_KeDelayExecutionThread(char mode, UINT8 alertable, const INT64 *interval)
{
    (void)mode; (void)alertable;
    UINT64 until = deadline(interval);
    if (until && KeIrql() < DISPATCH_LEVEL) sched_sleep_until_tsc(NULL, until);
    return NT_STATUS_SUCCESS;
}

NT_API void nt_KeInitializeEvent(NT_KEVENT *ev, int type, UINT8 state)
{
    header_init(&ev->Header, (UINT8)type, sizeof(*ev) / 4, state ? 1 : 0);
}

NT_API int nt_KeSetEvent(NT_KEVENT *ev, int increment, UINT8 wait)
{
    (void)increment; (void)wait;
    IrqState s = spin_lock_irqsave(&g_disp);
    int prev = ev->Header.SignalState;
    ev->Header.SignalState = 1;
    satisfy(&ev->Header);
    spin_unlock_irqrestore(&g_disp, s);
    return prev;
}

NT_API int nt_KeResetEvent(NT_KEVENT *ev)
{
    return __atomic_exchange_n(&ev->Header.SignalState, 0, __ATOMIC_ACQ_REL);
}

NT_API void nt_KeClearEvent(NT_KEVENT *ev) { __atomic_store_n(&ev->Header.SignalState, 0, __ATOMIC_RELEASE); }
NT_API int nt_KeReadStateEvent(NT_KEVENT *ev) { return __atomic_load_n(&ev->Header.SignalState, __ATOMIC_ACQUIRE); }

NT_API void nt_KeInitializeSemaphore(NT_KSEMAPHORE *sem, int count, int limit)
{
    header_init(&sem->Header, NT_SemaphoreObject, sizeof(*sem) / 4, count);
    sem->Limit = limit;
}

NT_API int nt_KeReleaseSemaphore(NT_KSEMAPHORE *sem, int increment, int adjustment, UINT8 wait)
{
    (void)increment; (void)wait;
    IrqState s = spin_lock_irqsave(&g_disp);
    int prev = sem->Header.SignalState;
    if (adjustment <= 0 || prev + adjustment > sem->Limit) {
        spin_unlock_irqrestore(&g_disp, s);
        kprintf("[NT] KeReleaseSemaphore: STATUS_SEMAPHORE_LIMIT_EXCEEDED (count %d + %d, limit %d)\n", prev, adjustment,
                sem->Limit);
        return prev;
    }
    sem->Header.SignalState = prev + adjustment;
    satisfy(&sem->Header);
    spin_unlock_irqrestore(&g_disp, s);
    return prev;
}

NT_API int nt_KeReadStateSemaphore(NT_KSEMAPHORE *sem) { return sem->Header.SignalState; }

/* -----------------------------------------------------------------------
 * Timers: the armed ones in a list (g_disp), due at a clock tick
 * (DueTime: NovaOS keeps the tick count it is due at)
 * ----------------------------------------------------------------------- */
static NT_LIST_ENTRY g_timers = { &g_timers, &g_timers };
static volatile UINT32 g_timer_count;

#define TICK_100NS  100000ull                         /* 10 ms */

NT_API void nt_KeInitializeTimerEx(NT_KTIMER *t, int type)
{
    header_init(&t->Header, type ? NT_TimerSynchronizationObject : NT_TimerNotificationObject, sizeof(*t) / 4, 0);
    t->DueTime = 0;
    t->TimerListEntry.Flink = t->TimerListEntry.Blink = NULL;
    t->Dpc = NULL;
    t->Period = 0;
}

NT_API void nt_KeInitializeTimer(NT_KTIMER *t) { nt_KeInitializeTimerEx(t, 0); }

/* (g_disp held) */
static bool timer_unlink(NT_KTIMER *t)
{
    if (!t->TimerListEntry.Flink) return false;
    list_remove(&t->TimerListEntry);
    t->TimerListEntry.Flink = t->TimerListEntry.Blink = NULL;
    __atomic_fetch_sub(&g_timer_count, 1, __ATOMIC_RELAXED);
    return true;
}

static UINT64 due_tick(INT64 due)
{
    UINT64 rel;
    if (due < 0) rel = (UINT64)-due;
    else {
        UINT64 now = um_now_100ns();
        rel = (UINT64)due > now ? (UINT64)due - now : 0;
    }
    /* (the tick now is partly gone: one more, so a timer never goes off early) */
    return sched_ticks() + (rel + TICK_100NS - 1) / TICK_100NS + 1;
}

NT_API UINT8 nt_KeSetTimerEx(NT_KTIMER *t, INT64 due, int period_ms, NT_KDPC *dpc)
{
    IrqState s = spin_lock_irqsave(&g_disp);
    bool was = timer_unlink(t);
    t->Header.SignalState = 0;
    t->DueTime = due_tick(due);
    t->Period = period_ms > 0 ? (unsigned)period_ms : 0;
    t->Dpc = dpc;
    NT_LIST_ENTRY *last = g_timers.Blink;
    t->TimerListEntry.Flink = &g_timers;
    t->TimerListEntry.Blink = last;
    last->Flink = &t->TimerListEntry;
    g_timers.Blink = &t->TimerListEntry;
    __atomic_fetch_add(&g_timer_count, 1, __ATOMIC_RELAXED);
    spin_unlock_irqrestore(&g_disp, s);
    return was;
}

NT_API UINT8 nt_KeSetTimer(NT_KTIMER *t, INT64 due, NT_KDPC *dpc) { return nt_KeSetTimerEx(t, due, 0, dpc); }

NT_API UINT8 nt_KeCancelTimer(NT_KTIMER *t)
{
    IrqState s = spin_lock_irqsave(&g_disp);
    bool was = timer_unlink(t);
    spin_unlock_irqrestore(&g_disp, s);
    return was;
}

NT_API UINT8 nt_KeReadStateTimer(NT_KTIMER *t) { return t->Header.SignalState > 0; }

void KiTimerTick(UINT64 tick)
{
    if (!__atomic_load_n(&g_timer_count, __ATOMIC_RELAXED)) return;
    spin_lock(&g_disp);                              /* (interrupts are off: the clock interrupt) */
    for (NT_LIST_ENTRY *e = g_timers.Flink; e != &g_timers;) {
        NT_KTIMER *t = (NT_KTIMER *)((UINT8 *)e - __builtin_offsetof(NT_KTIMER, TimerListEntry));
        e = e->Flink;
        if (t->DueTime > tick) continue;
        timer_unlink(t);
        t->Header.SignalState = 1;
        satisfy(&t->Header);
        if (t->Period) {                             /* periodic: armed again */
            t->DueTime = tick + (t->Period + 9) / 10;
            NT_LIST_ENTRY *last = g_timers.Blink;
            t->TimerListEntry.Flink = &g_timers;
            t->TimerListEntry.Blink = last;
            last->Flink = &t->TimerListEntry;
            g_timers.Blink = &t->TimerListEntry;
            __atomic_fetch_add(&g_timer_count, 1, __ATOMIC_RELAXED);
        }
        if (t->Dpc) {
            UINT64 now = um_now_100ns();
            nt_KeInsertQueueDpc(t->Dpc, (void *)(uintptr_t)(UINT32)now, (void *)(uintptr_t)(now >> 32));
        }
    }
    spin_unlock(&g_disp);
}

/* -----------------------------------------------------------------------
 * Fast mutexes (APC_LEVEL while held) and critical regions
 * ----------------------------------------------------------------------- */
static void fm_acquire(NT_FAST_MUTEX *m)
{
    while (!(__atomic_fetch_and(&m->Count, ~1, __ATOMIC_ACQUIRE) & 1)) {
        __atomic_fetch_add(&m->Contention, 1, __ATOMIC_RELAXED);
        if (!(__atomic_load_n(&m->Count, __ATOMIC_ACQUIRE) & 1)) wait_one(&m->Event.Header, NULL);
        __atomic_fetch_sub(&m->Contention, 1, __ATOMIC_RELAXED);
    }
    m->Owner = sched_current();
}

static void fm_release(NT_FAST_MUTEX *m)
{
    m->Owner = NULL;
    __atomic_fetch_or(&m->Count, 1, __ATOMIC_RELEASE);
    if (__atomic_load_n(&m->Contention, __ATOMIC_ACQUIRE)) nt_KeSetEvent(&m->Event, 0, 0);
}

NT_API void nt_ExAcquireFastMutex(NT_FAST_MUTEX *m)
{
    UINT8 old = KeIrql();
    if (old < APC_LEVEL) KeIrqlSet(APC_LEVEL);
    fm_acquire(m);
    m->OldIrql = old;
}

NT_API void nt_ExReleaseFastMutex(NT_FAST_MUTEX *m)
{
    UINT8 old = (UINT8)m->OldIrql;
    fm_release(m);
    KeIrqlSet(old);
}

NT_API UINT8 nt_ExTryToAcquireFastMutex(NT_FAST_MUTEX *m)
{
    UINT8 old = KeIrql();
    if (old < APC_LEVEL) KeIrqlSet(APC_LEVEL);
    if (!(__atomic_fetch_and(&m->Count, ~1, __ATOMIC_ACQUIRE) & 1)) { KeIrqlSet(old); return 0; }
    m->Owner = sched_current();
    m->OldIrql = old;
    return 1;
}

NT_API void nt_ExAcquireFastMutexUnsafe(NT_FAST_MUTEX *m) { fm_acquire(m); }
NT_API void nt_ExReleaseFastMutexUnsafe(NT_FAST_MUTEX *m) { fm_release(m); }

/* (no APCs reach drivers yet: nothing to hold off) */
NT_API void nt_KeEnterCriticalRegion(void) {}
NT_API void nt_KeLeaveCriticalRegion(void) {}

/* -----------------------------------------------------------------------
 * Time and processors
 * ----------------------------------------------------------------------- */
NT_API UINT32 nt_KeQueryTimeIncrement(void) { return (UINT32)TICK_100NS; }

NT_API INT64 nt_KeQueryPerformanceCounter(INT64 *freq)
{
    if (freq) *freq = (INT64)(g_tsc_per_tick * 100);
    return (INT64)rdtsc();
}

NT_API void nt_KeQuerySystemTimePrecise(INT64 *t) { *t = (INT64)um_now_100ns(); }
NT_API UINT64 nt_KeQueryInterruptTimePrecise(UINT64 *qpc)
{
    UINT64 tsc = rdtsc();
    if (qpc) *qpc = tsc;
    return g_tsc_per_tick ? tsc / g_tsc_per_tick * TICK_100NS + tsc % g_tsc_per_tick * TICK_100NS / g_tsc_per_tick : 0;
}

NT_API UINT32 nt_KeGetCurrentProcessorNumberEx(void *number)
{
    UINT32 cpu = KiGetCurrentKpcr()->CpuNumber;
    if (number) {                                    /* PROCESSOR_NUMBER: Group, Number, Reserved */
        UINT16 *g = number;
        g[0] = 0;
        ((UINT8 *)number)[2] = (UINT8)cpu;
        ((UINT8 *)number)[3] = 0;
    }
    return cpu;
}

NT_API UINT32 nt_KeQueryActiveProcessorCountEx(UINT16 group)
{
    return group == 0 || group == 0xFFFF ? g_cpu_count : 0;
}

/* PROCESSOR_NUMBER (Group, Number, Reserved) <-> a system-wide index: one
 * group, so the index is the number */
NT_API UINT32 nt_KeGetProcessorIndexFromNumber(const void *number)
{
    const UINT8 *n = number;
    return n && !*(const UINT16 *)n && n[2] < g_cpu_count ? n[2] : 0xFFFFFFFFu;    /* (INVALID_PROCESSOR_INDEX) */
}

NT_API NT_STATUS nt_KeGetProcessorNumberFromIndex(UINT32 index, void *number)
{
    if (!number || index >= g_cpu_count) return NT_STATUS_INVALID_PARAMETER;
    *(UINT16 *)number = 0;
    ((UINT8 *)number)[2] = (UINT8)index;
    ((UINT8 *)number)[3] = 0;
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * KeBugCheckEx: a driver found the system in a state it cannot go on in
 * ----------------------------------------------------------------------- */
NT_API void nt_KeBugCheckEx(UINT32 code, UINT64 p1, UINT64 p2, UINT64 p3, UINT64 p4)
{
    extern UINT64 nt_KiBugCheckData[5];
    nt_KiBugCheckData[0] = code; nt_KiBugCheckData[1] = p1; nt_KiBugCheckData[2] = p2;
    nt_KiBugCheckData[3] = p3; nt_KiBugCheckData[4] = p4;
    KPANIC("*** STOP: 0x%08x (0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx) from a driver (return address %p)",
           code, (unsigned long long)p1, (unsigned long long)p2, (unsigned long long)p3, (unsigned long long)p4,
           __builtin_return_address(0));
}

NT_API void nt_KeBugCheck(UINT32 code) { nt_KeBugCheckEx(code, 0, 0, 0, 0); }

/* -----------------------------------------------------------------------
 * Step 30.2 part 2: mutexes (KMUTEX), multi-object waits, processor
 * count, interrupt synchronization, the debugger flags and the bug-check
 * data a driver reads
 * ----------------------------------------------------------------------- */

/* KMUTEX / KMUTANT: a recursive mutex.  Acquire is through
 * KeWaitForSingleObject (wait_one, which take() handles for a mutant);
 * release gives it back, and satisfy() hands ownership to a waiter. */
NT_API void nt_KeInitializeMutex(NT_KMUTEX *m, UINT32 level)
{
    (void)level;
    header_init(&m->Header, NT_MutantObject, sizeof(*m) / 4, 1);     /* 1 = free */
    m->MutantListEntry.Flink = m->MutantListEntry.Blink = &m->MutantListEntry;
    m->OwnerThread = NULL;
}

NT_API int nt_KeReadStateMutex(NT_KMUTEX *m) { return m->Header.SignalState; }
NT_API int nt_KeReadStateMutant(NT_KMUTEX *m) { return m->Header.SignalState; }

NT_API int nt_KeReleaseMutex(NT_KMUTEX *m, UINT8 wait)
{
    (void)wait;
    IrqState s = spin_lock_irqsave(&g_disp);
    int prev = m->Header.SignalState;
    if (m->OwnerThread != (void *)sched_current()) {
        spin_unlock_irqrestore(&g_disp, s);
        kprintf("[NT] KeReleaseMutex: not held by the calling thread (STATUS_MUTANT_NOT_OWNED)\n");
        return prev;
    }
    if (++m->Header.SignalState == 1) {              /* fully released: free, then a waiter may take it */
        m->OwnerThread = NULL;
        satisfy(&m->Header);
    }
    spin_unlock_irqrestore(&g_disp, s);
    return prev;
}

/* Is @h available to the calling thread, without taking it (g_disp held) */
static bool peek(NT_DISPATCHER_HEADER *h)
{
    if (h->Type == NT_MutantObject) {
        void *me = (void *)sched_current();
        return h->SignalState == 1 || (h->SignalState <= 0 && *mutant_owner(h) == me);
    }
    return h->SignalState > 0;
}

/* KeWaitForMultipleObjects: WaitAny returns WAIT_OBJECT_0 + the index
 * taken; WaitAll takes them all once every one is available.  NovaOS
 * polls under the dispatcher lock with a short sleep between tries rather
 * than chaining a wait block onto every object, which a driver cannot
 * tell apart from the outside. */
NT_API NT_STATUS nt_KeWaitForMultipleObjects(UINT32 count, void **objs, int type, int reason, char mode,
                                             UINT8 alertable, const INT64 *timeout, NT_KWAIT_BLOCK *blocks)
{
    (void)reason; (void)mode; (void)alertable; (void)blocks;
    if (count == 0 || count > 64) return NT_STATUS_INVALID_PARAMETER;
    UINT64 until = deadline(timeout);
    for (;;) {
        IrqState s = spin_lock_irqsave(&g_disp);
        if (type == NT_WaitAny) {
            for (UINT32 i = 0; i < count; i++) {
                NT_DISPATCHER_HEADER *h = objs[i];
                if (peek(h) && take(h)) { spin_unlock_irqrestore(&g_disp, s); return NT_STATUS_WAIT_0 + (NT_STATUS)i; }
            }
        } else {                                     /* WaitAll */
            bool all = true;
            for (UINT32 i = 0; i < count && all; i++) all = peek(objs[i]);
            if (all) {
                for (UINT32 i = 0; i < count; i++) take(objs[i]);
                spin_unlock_irqrestore(&g_disp, s);
                return NT_STATUS_WAIT_0;
            }
        }
        spin_unlock_irqrestore(&g_disp, s);
        if (!until) return NT_STATUS_TIMEOUT;
        if (KeIrql() >= DISPATCH_LEVEL) {
            kprintf("[NT] IRQL_NOT_LESS_OR_EQUAL: KeWaitForMultipleObjects at IRQL %u\n", KeIrql());
            return NT_STATUS_TIMEOUT;
        }
        if (until != UINT64_MAX && rdtsc() >= until) return NT_STATUS_TIMEOUT;
        UINT64 slice = sched_tsc_after(10000);       /* poll about every 1 ms */
        sched_sleep_until_tsc(NULL, until != UINT64_MAX && until < slice ? until : slice);
    }
}

/* KeNumberProcessors: a data export (the active CPU count, as a CCHAR),
 * kept current by NtKeInit and when a CPU comes online. */
char nt_KeNumberProcessors = 1;

/* The debugger flags a driver reads: no kernel debugger is attached. */
UINT8  nt_KdDebuggerEnabled = 0;
UINT8  nt_KdDebuggerNotPresent = 1;
NT_API UINT8 nt_KdRefreshDebuggerNotPresent(void) { return nt_KdDebuggerNotPresent; }

/* KiBugCheckData[5]: the bug-check code and parameters of the last stop,
 * which a driver's bug-check callback reads.  Set by KeBugCheckEx. */
UINT64 nt_KiBugCheckData[5];

/* Called from NtDrvInit once the CPUs are up */
void NtKeInit(void)
{
    nt_KeNumberProcessors = (char)(g_cpu_count ? g_cpu_count : 1);
    for (int i = 0; i < 5; i++) nt_KiBugCheckData[i] = 0;
}
