/*
 * po.c — the power framework (PoFx) a driver registers its device with,
 * and device power IRPs
 *
 * A driver that manages its device's power by components registers it
 * (PoFxRegisterDevice) with callbacks the framework calls when a
 * component must be active (ComponentActiveConditionCallback), when it
 * may go idle (ComponentIdleConditionCallback), to move an idle component
 * between its F-states (ComponentIdleStateCallback: F0 is on, F1 and
 * deeper save more power) and when the whole device is or is not needed
 * in D0 (DevicePowerRequiredCallback, DevicePowerNotRequiredCallback).
 * The driver takes an activation reference before it touches the hardware
 * (PoFxActivateComponent) and drops it after (PoFxIdleComponent).
 * KMDF does all of this for a driver whose S0 idle settings say
 * SystemManagedIdleTimeout.
 *
 * Runtime idle, as Windows runs it:
 *   - a component starts active, in F0, and the device's power is
 *     required; nothing moves before PoFxStartDevicePowerManagement;
 *   - when a component's last activation reference goes it gets the idle
 *     condition callback, and once the driver completes that
 *     (PoFxCompleteIdleCondition) it is moved to its deepest F-state
 *     (PoFxCompleteIdleState ends each move);
 *   - when every component is idle for the device's idle timeout
 *     (PoFxSetDeviceIdleTimeout; none by default), device power is not
 *     required: the driver may ask for a D-IRP to Dx, and completes the
 *     callback with PoFxCompleteDevicePowerNotRequired;
 *   - an activation of an idle component first has device power required
 *     again (the driver brings the device to D0 and calls
 *     PoFxReportDevicePoweredOn), then the component moved to F0, then the
 *     active condition callback.  A driver asking with PO_FX_FLAG_BLOCKING
 *     at PASSIVE_LEVEL waits for that; else the call returns and the
 *     callback comes later (at once, from the caller, when the component
 *     only has to be told).
 * The device and F-state callbacks are made from the "nt-power" thread at
 * PASSIVE_LEVEL, one at a time per device; condition callbacks from the
 * caller where they need nothing else.  The F-state taken is the deepest
 * the component describes (NovaOS has no latency tolerances to weigh);
 * directed power-down (Modern Standby's DFx) is never asked for.
 */
#include "nt.h"
#include "ntapi.h"
#include "../hal/aml.h"
#include "../ke/irql.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

typedef void (NT_API *PoFxCondition)(void *ctx, UINT32 component);
typedef void (NT_API *PoFxIdleState)(void *ctx, UINT32 component, UINT32 state);
typedef void (NT_API *PoFxDevicePower)(void *ctx);

#define MAX_COMPONENTS 32
#define PO_FX_FLAG_BLOCKING 0x1

typedef struct {
    int    refs;                                /* activation references */
    bool   active;                              /* in the active condition (as the driver was told) */
    bool   idling;                              /* told it is idle; PoFxCompleteIdleCondition not yet */
    UINT32 fstate, deepest;                     /* its F-state now, and the deepest it has */
} FxComponent;

enum { FX_NONE, FX_REQUIRED, FX_NOT_REQUIRED, FX_STATE };      /* the callback whose completion is awaited */
enum { DO_NOTHING, DO_ACTIVE, DO_IDLE, DO_STATE, DO_REQUIRED, DO_NOT_REQUIRED };

typedef struct NtPoFx {
    struct NtPoFx    *next;
    NT_DEVICE_OBJECT *pdo;
    PoFxCondition     active, idle;
    PoFxIdleState     state;
    PoFxDevicePower   required, not_required;
    void             *ctx;                      /* DeviceContext */
    UINT32            count;
    FxComponent       c[MAX_COMPONENTS];
    bool              started;                  /* PoFxStartDevicePowerManagement */
    bool              powered;                  /* device power required */
    bool              gone;                     /* unregistered: the power thread frees it */
    int               pending;                  /* FX_* */
    UINT32            pending_component, pending_state;
    UINT64            idle_since;               /* sched_ticks() + 1 when every component was idle (0: not) */
    UINT64            timeout;                  /* idle ticks before device power is not required */
    NT_KEVENT         changed;                  /* a component went active (PO_FX_FLAG_BLOCKING waits) */
    KSpinLock         lock;
} NtPoFx;

static NtPoFx   *g_fx;                          /* registered devices */
static KSpinLock g_fx_lock;                     /* (zeroed: KSPINLOCK_INIT) */

static void po_start(void);
static void po_kick(void);
NT_API void nt_PoFxCompleteIdleCondition(NtPoFx *h, UINT32 c);
NT_API void nt_PoFxCompleteIdleState(NtPoFx *h, UINT32 c);
NT_API void nt_PoFxReportDevicePoweredOn(NtPoFx *h);
NT_API void nt_PoFxCompleteDevicePowerNotRequired(NtPoFx *h);

/* PO_FX_DEVICE (x64): V1 is Version, ComponentCount, the six callbacks at
 * 8, DeviceContext at 56 and its PO_FX_COMPONENT_V1s (32 bytes: Id,
 * IdleStateCount at 16) at 64.  V2 puts a ULONGLONG Flags at 8 (callbacks
 * at 16, DeviceContext at 64) and ComponentCount after DeviceContext (72,
 * components at 80); V3 adds the directed power callbacks and their
 * timeout before DeviceContext (88; ComponentCount 96, components 104).
 * PO_FX_COMPONENT_V2 is 56 bytes: Id, Flags, DeepestWakeableIdleState,
 * IdleStateCount at 28. */
NT_API NTSTATUS nt_PoFxRegisterDevice(NT_DEVICE_OBJECT *pdo, const UINT8 *dev, NtPoFx **handle)
{
    if (!pdo || !dev || !handle) return NT_STATUS_INVALID_PARAMETER;
    UINT32 version = *(const UINT32 *)dev;
    if (version < 1 || version > 3) return NT_STATUS_INVALID_PARAMETER;
    UINT32 cb = version == 1 ? 8 : 16, ctx = version == 1 ? 56 : version == 2 ? 64 : 88;
    UINT32 count_at = version == 1 ? 4 : ctx + 8, comp = version == 1 ? 64 : ctx + 16;
    UINT32 stride = version == 1 ? 32 : 56, states_at = version == 1 ? 16 : 28;
    UINT32 components = *(const UINT32 *)(dev + count_at);
    if (!components) return NT_STATUS_INVALID_PARAMETER;
    NtPoFx *h = kzalloc(sizeof(*h));
    if (!h) return NT_STATUS_INSUFFICIENT_RESOURCES;
    h->pdo = pdo;
    h->active = *(PoFxCondition const *)(dev + cb);
    h->idle = *(PoFxCondition const *)(dev + cb + 8);
    h->state = *(PoFxIdleState const *)(dev + cb + 16);
    h->required = *(PoFxDevicePower const *)(dev + cb + 24);
    h->not_required = *(PoFxDevicePower const *)(dev + cb + 32);
    h->ctx = *(void *const *)(dev + ctx);
    h->count = components < MAX_COMPONENTS ? components : MAX_COMPONENTS;
    for (UINT32 i = 0; i < h->count; i++) {
        UINT32 n = *(const UINT32 *)(dev + comp + i * stride + states_at);
        h->c[i] = (FxComponent){ 0, true, false, 0, n > 1 ? n - 1 : 0 };
    }
    h->powered = true;
    nt_KeInitializeEvent(&h->changed, NT_SynchronizationEvent, 0);
    /* (a zeroed KSpinLock is KSPINLOCK_INIT) */
    po_start();
    IrqState s = spin_lock_irqsave(&g_fx_lock);
    h->next = g_fx;
    g_fx = h;
    spin_unlock_irqrestore(&g_fx_lock, s);
    *handle = h;
    return NT_STATUS_SUCCESS;
}

/* (the power thread frees it, after any callback it is making) */
NT_API void nt_PoFxUnregisterDevice(NtPoFx *h)
{
    if (!h) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    h->gone = true;
    spin_unlock_irqrestore(&h->lock, s);
    po_kick();
}

/* The next thing @h needs (its lock held).  Off the power thread only the
 * condition callbacks are made: the rest waits for the thread. */
static int fx_next(NtPoFx *h, bool thread, UINT32 *comp, UINT32 *state)
{
    if (!h->started || h->gone) return DO_NOTHING;
    for (UINT32 i = 0; i < h->count; i++) {
        FxComponent *x = &h->c[i];
        if (x->refs <= 0 || x->active || x->idling) continue;
        if (h->powered && !x->fstate) {
            x->active = true;
            *comp = i;
            return DO_ACTIVE;
        }
        if (!thread || h->pending) continue;
        if (!h->powered) {
            h->pending = FX_REQUIRED;
            return DO_REQUIRED;
        }
        h->pending = FX_STATE;
        *comp = h->pending_component = i;
        *state = h->pending_state = 0;
        return DO_STATE;
    }
    bool all_idle = true;
    for (UINT32 i = 0; i < h->count; i++) {
        FxComponent *x = &h->c[i];
        if (x->refs <= 0 && x->active) {
            x->active = false;
            x->idling = true;
            *comp = i;
            return DO_IDLE;
        }
        if (x->refs > 0 || x->active || x->idling) all_idle = false;
    }
    if (!all_idle) h->idle_since = 0;
    if (!thread || h->pending || !h->powered || !all_idle) return DO_NOTHING;
    for (UINT32 i = 0; i < h->count; i++)
        if (h->c[i].fstate != h->c[i].deepest) {
            h->pending = FX_STATE;
            *comp = h->pending_component = i;
            *state = h->pending_state = h->c[i].deepest;
            return DO_STATE;
        }
    UINT64 now = sched_ticks() + 1;
    if (!h->idle_since) h->idle_since = now;
    if (now - h->idle_since < h->timeout) return DO_NOTHING;
    h->pending = FX_NOT_REQUIRED;
    return DO_NOT_REQUIRED;
}

/* Make @h's callbacks until it needs nothing more (or waits on the driver) */
static void fx_run(NtPoFx *h, bool thread)
{
    for (;;) {
        UINT32 c = 0, st = 0;
        IrqState s = spin_lock_irqsave(&h->lock);
        int what = fx_next(h, thread, &c, &st);
        spin_unlock_irqrestore(&h->lock, s);
        const char *who = NtPnpInstanceOf(h->pdo);
        switch (what) {
        case DO_NOTHING:
            return;
        case DO_ACTIVE:
            if (h->active) h->active(h->ctx, c);
            nt_KeSetEvent(&h->changed, 0, 0);
            break;
        case DO_IDLE:
            if (h->idle) h->idle(h->ctx, c);
            else nt_PoFxCompleteIdleCondition(h, c);
            break;
        case DO_STATE:
            kprintf("[PO] %s: component %u to F%u\n", who ? who : "?", c, st);
            if (h->state) h->state(h->ctx, c, st);
            else nt_PoFxCompleteIdleState(h, c);
            break;
        case DO_REQUIRED:
            kprintf("[PO] %s: device power required (a component is active)\n", who ? who : "?");
            if (h->required) h->required(h->ctx);
            else nt_PoFxReportDevicePoweredOn(h);
            break;
        case DO_NOT_REQUIRED:
            kprintf("[PO] %s: device power not required (every component idle)\n", who ? who : "?");
            if (h->not_required) h->not_required(h->ctx);
            else nt_PoFxCompleteDevicePowerNotRequired(h);
            break;
        }
    }
}

/* The power thread's round: every registered device (and the unregistered
 * ones freed) */
static void fx_round(void)
{
    for (;;) {
        IrqState s = spin_lock_irqsave(&g_fx_lock);
        NtPoFx **pp = &g_fx;
        while (*pp && !(*pp)->gone) pp = &(*pp)->next;
        NtPoFx *dead = *pp;
        if (dead) *pp = dead->next;
        spin_unlock_irqrestore(&g_fx_lock, s);
        if (!dead) break;
        kfree(dead);
    }
    /* (only this thread unlinks: the list stays valid while it walks) */
    for (NtPoFx *h = __atomic_load_n(&g_fx, __ATOMIC_ACQUIRE); h; h = h->next) fx_run(h, true);
}

NT_API void nt_PoFxStartDevicePowerManagement(NtPoFx *h)
{
    if (!h) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    h->started = true;
    spin_unlock_irqrestore(&h->lock, s);
    fx_run(h, false);
    po_kick();
}

NT_API void nt_PoFxActivateComponent(NtPoFx *h, UINT32 c, UINT32 flags)
{
    if (!h || c >= h->count) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    h->c[c].refs++;
    h->idle_since = 0;
    bool ready = h->c[c].active;
    spin_unlock_irqrestore(&h->lock, s);
    if (ready) return;
    fx_run(h, false);
    s = spin_lock_irqsave(&h->lock);
    ready = h->c[c].active || !h->started;
    spin_unlock_irqrestore(&h->lock, s);
    if (ready) return;
    po_kick();
    if (!(flags & PO_FX_FLAG_BLOCKING) || KeIrql() != PASSIVE_LEVEL) return;
    for (;;) {                                  /* (PO_FX_FLAG_BLOCKING: until it is active) */
        INT64 tick = -100000;                   /* (10 ms, relative) */
        nt_KeWaitForSingleObject(&h->changed, 0, 0, 0, &tick);
        s = spin_lock_irqsave(&h->lock);
        ready = h->c[c].active || h->c[c].refs <= 0 || h->gone;
        spin_unlock_irqrestore(&h->lock, s);
        if (ready) return;
    }
}

NT_API void nt_PoFxIdleComponent(NtPoFx *h, UINT32 c, UINT32 flags)
{
    (void)flags;
    if (!h || c >= h->count) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    if (h->c[c].refs <= 0) {
        spin_unlock_irqrestore(&h->lock, s);
        kprintf("[NT] PoFxIdleComponent: component %u has no activation reference\n", c);
        return;
    }
    bool idle = --h->c[c].refs == 0;
    spin_unlock_irqrestore(&h->lock, s);
    if (!idle) return;
    fx_run(h, false);
    po_kick();
}

/* The driver's answers to the callbacks */
NT_API void nt_PoFxCompleteIdleCondition(NtPoFx *h, UINT32 c)
{
    if (!h || c >= h->count) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    h->c[c].idling = false;
    spin_unlock_irqrestore(&h->lock, s);
    po_kick();
}

NT_API void nt_PoFxCompleteIdleState(NtPoFx *h, UINT32 c)
{
    if (!h) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    if (h->pending == FX_STATE && h->pending_component == c) {
        h->c[c].fstate = h->pending_state;
        h->pending = FX_NONE;
    }
    spin_unlock_irqrestore(&h->lock, s);
    po_kick();
}

NT_API void nt_PoFxReportDevicePoweredOn(NtPoFx *h)
{
    if (!h) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    h->powered = true;
    if (h->pending == FX_REQUIRED) h->pending = FX_NONE;
    spin_unlock_irqrestore(&h->lock, s);
    po_kick();
}

NT_API void nt_PoFxCompleteDevicePowerNotRequired(NtPoFx *h)
{
    if (!h) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    if (h->pending == FX_NOT_REQUIRED) {
        h->powered = false;
        h->pending = FX_NONE;
    }
    spin_unlock_irqrestore(&h->lock, s);
    po_kick();
}

/* PoFxSetDeviceIdleTimeout(Handle, IdleTimeout in 100 ns units) */
NT_API void nt_PoFxSetDeviceIdleTimeout(NtPoFx *h, UINT64 timeout)
{
    if (!h) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    h->timeout = timeout / 100000;              /* (10 ms ticks) */
    spin_unlock_irqrestore(&h->lock, s);
}

NT_API void nt_PoFxCompleteDirectedPowerDown(NtPoFx *h) { (void)h; }    /* (never asked for) */

/* -----------------------------------------------------------------------
 * Device power IRPs
 *
 * A device's power policy owner (its function driver; KMDF for a KMDF
 * driver) asks for a device power state with PoRequestPowerIrp: an
 * IRP_MN_SET_POWER (or QUERY_POWER) D-IRP goes down the device's whole
 * stack, from its top, and the caller's completion function hears how it
 * went.  As on Windows the IRP is sent at PASSIVE_LEVEL from a thread of
 * the power manager's own (the "nt-power" thread here), never from the
 * caller's context: KMDF asks from its idle timer's DPC, and its power
 * state machine expects the IRP to arrive later.  The bus driver at the
 * bottom (pnp.c, for a PCI function) moves the hardware.
 *
 * WDM drivers that leave idle detection to the power manager register
 * with PoRegisterDeviceForIdleDetection and mark their device busy as
 * they use it (PoSetDeviceBusy: the idle counter back to zero).  The
 * power thread counts idle seconds and, once a device has been idle for
 * its timeout (the conservation one on battery, the performance one on
 * mains), asks for the state the driver gave.  Powering the device back
 * up is the driver's own business, as on Windows.
 * ----------------------------------------------------------------------- */
typedef void (NT_API *PoRequestComplete)(NT_DEVICE_OBJECT *dev, UINT8 minor, UINT32 state, void *ctx,
                                         NT_IO_STATUS_BLOCK *iosb);

typedef struct PoReq {
    struct PoReq     *next;
    NT_DEVICE_OBJECT *dev, *top;
    NT_IRP           *irp;
    UINT8             minor;
    UINT32            state;
    PoRequestComplete done;
    void             *ctx;
} PoReq;

typedef struct PoIdle {
    struct PoIdle    *next;
    NT_DEVICE_OBJECT *dev;
    volatile UINT32   count;                    /* idle seconds (PoSetDeviceBusy zeroes it) */
    UINT32            conservation, performance;
    UINT32            state;                    /* the state to ask for */
    bool              asked;
} PoIdle;

static PoReq    *g_reqs, **g_reqs_tail = &g_reqs;
static PoIdle   *g_idle;
static KSpinLock g_po_lock;                     /* (zeroed: KSPINLOCK_INIT) */
static NT_KEVENT g_po_event;
static volatile int g_po_thread;                /* 0 none, 1 starting, 2 running */

NT_API NTSTATUS nt_IofCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp);
NT_API NT_IRP  *nt_IoAllocateIrp(char stack_size, UINT8 charge_quota);
NT_API void     nt_IoFreeIrp(NT_IRP *irp);
NT_API NT_DEVICE_OBJECT *nt_IoGetAttachedDevice(NT_DEVICE_OBJECT *d);
NT_API UINT32   nt_PoSetPowerState(NT_DEVICE_OBJECT *dev, int type, UINT32 state);
UINT32          NtDevicePowerState(NT_DEVICE_OBJECT *dev);

static NT_API NTSTATUS po_irp_done(NT_DEVICE_OBJECT *dev, NT_IRP *irp, void *ctx)
{
    (void)dev;
    PoReq *r = ctx;
    if (r->done) r->done(r->dev, r->minor, r->state, r->ctx, &irp->IoStatus);
    nt_IoFreeIrp(irp);
    kfree(r);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;  /* (the IRP is ours, and gone) */
}

static void po_send(PoReq *r)
{
    NT_IO_STACK_LOCATION *sl = NtIrpNextStack(r->irp);
    sl->MajorFunction = NT_IRP_MJ_POWER;
    sl->MinorFunction = r->minor;
    if (r->minor == 0x00) {                     /* IRP_MN_WAIT_WAKE: PowerState (a system state) */
        sl->Parameters.Others.Argument1 = (void *)(UINT64)r->state;
    } else {                                    /* SET_POWER, QUERY_POWER: SystemContext, Type, State, ShutdownType */
        sl->Parameters.Others.Argument1 = NULL;
        sl->Parameters.Others.Argument2 = (void *)(UINT64)1;     /* DevicePowerState */
        sl->Parameters.Others.Argument3 = (void *)(UINT64)r->state;
        sl->Parameters.Others.Argument4 = NULL;                  /* PowerActionNone */
    }
    sl->CompletionRoutine = po_irp_done;
    sl->Context = r;
    sl->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    r->irp->IoStatus.Status = NT_STATUS_NOT_SUPPORTED;
    nt_IofCallDriver(r->top, r->irp);
}

static NTSTATUS po_request(NT_DEVICE_OBJECT *dev, UINT8 minor, UINT32 state, PoRequestComplete done, void *ctx,
                           NT_IRP **out);

/* Once a second: the devices idle for their timeout */
static void po_idle_scan(void)
{
    AmlBatteryState b;
    AmlGetBatteryState(&b);
    bool battery = b.battery_present && !b.ac_online;
    PoIdle *due = NULL;
    IrqState s = spin_lock_irqsave(&g_po_lock);
    for (PoIdle *i = g_idle; i; i = i->next) {
        UINT32 limit = battery ? i->conservation : i->performance;
        UINT32 n = __atomic_add_fetch(&i->count, 1, __ATOMIC_RELAXED);
        if (n == 1) i->asked = false;                 /* (busy since the last look: it may be asked again) */
        if (!due && limit && n >= limit && !i->asked && NtDevicePowerState(i->dev) < i->state) {
            i->asked = true;
            due = i;
        }
    }
    NT_DEVICE_OBJECT *dev = due ? due->dev : NULL;
    UINT32 state = due ? due->state : 0;
    spin_unlock_irqrestore(&g_po_lock, s);
    if (!dev) return;                                 /* (one request a second is plenty) */
    kprintf("[PO] Device %p idle for its timeout: asking for D%u\n", (void *)dev, state - 1);
    po_request(dev, 0x02, state, NULL, NULL, NULL);
}

static void po_thread(void *arg)
{
    (void)arg;
    for (;;) {
        INT64 second = -10000000;                     /* (relative, 100 ns) */
        nt_KeWaitForSingleObject(&g_po_event, 0, 0, 0, &second);
        for (;;) {
            IrqState s = spin_lock_irqsave(&g_po_lock);
            PoReq *r = g_reqs;
            if (r && !(g_reqs = r->next)) g_reqs_tail = &g_reqs;
            spin_unlock_irqrestore(&g_po_lock, s);
            if (!r) break;
            po_send(r);
        }
        fx_round();
        static UINT64 last;
        UINT64 now = sched_ticks();
        if (now - last >= 100) {
            last = now;
            po_idle_scan();
        }
    }
}

static void po_start(void)
{
    int was = 0;
    if (!__atomic_compare_exchange_n(&g_po_thread, &was, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    nt_KeInitializeEvent(&g_po_event, NT_SynchronizationEvent, 0);
    if (sched_create_thread("nt-power", po_thread, NULL, 9)) __atomic_store_n(&g_po_thread, 2, __ATOMIC_RELEASE);
    else kprintf("[PO] no power thread: device power requests fail\n");
}

static void po_kick(void)
{
    if (__atomic_load_n(&g_po_thread, __ATOMIC_ACQUIRE) == 2) nt_KeSetEvent(&g_po_event, 0, 0);
}

static NTSTATUS po_request(NT_DEVICE_OBJECT *dev, UINT8 minor, UINT32 state, PoRequestComplete done, void *ctx,
                           NT_IRP **out)
{
    if (out) *out = NULL;
    if (!dev || (minor != 0x00 && minor != 0x02 && minor != 0x03)) return NT_STATUS_INVALID_PARAMETER;
    po_start();
    if (__atomic_load_n(&g_po_thread, __ATOMIC_ACQUIRE) != 2) return NT_STATUS_INSUFFICIENT_RESOURCES;
    NT_DEVICE_OBJECT *top = nt_IoGetAttachedDevice(dev);
    PoReq *r = kzalloc(sizeof(*r));
    NT_IRP *irp = r ? nt_IoAllocateIrp(top->StackSize, 0) : NULL;
    if (!irp) { kfree(r); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    *r = (PoReq){ NULL, dev, top, irp, minor, state, done, ctx };
    if (out) *out = irp;
    IrqState s = spin_lock_irqsave(&g_po_lock);
    *g_reqs_tail = r;
    g_reqs_tail = &r->next;
    spin_unlock_irqrestore(&g_po_lock, s);
    nt_KeSetEvent(&g_po_event, 0, 0);
    return NT_STATUS_PENDING;
}

/* PoRequestPowerIrp(DeviceObject, MinorFunction, POWER_STATE, CompletionFunction, Context, Irp) */
NT_API NTSTATUS nt_PoRequestPowerIrp(NT_DEVICE_OBJECT *dev, UINT8 minor, UINT32 state, PoRequestComplete done,
                                     void *ctx, NT_IRP **irp)
{
    return po_request(dev, minor, state, done, ctx, irp);
}

/* PoRegisterDeviceForIdleDetection(DeviceObject, ConservationIdleTime,
 * PerformanceIdleTime (seconds; 0: never), State): the counter the driver
 * zeroes as it uses the device (PoSetDeviceBusy); NULL with both times 0
 * cancels the registration */
NT_API volatile UINT32 *nt_PoRegisterDeviceForIdleDetection(NT_DEVICE_OBJECT *dev, UINT32 conservation,
                                                            UINT32 performance, UINT32 state)
{
    if (!dev) return NULL;
    IrqState s = spin_lock_irqsave(&g_po_lock);
    PoIdle **pp = &g_idle;
    while (*pp && (*pp)->dev != dev) pp = &(*pp)->next;
    PoIdle *i = *pp;
    if (!conservation && !performance) {
        if (i) *pp = i->next;
        spin_unlock_irqrestore(&g_po_lock, s);
        kfree(i);
        return NULL;
    }
    spin_unlock_irqrestore(&g_po_lock, s);
    if (!i && !(i = kzalloc(sizeof(*i)))) return NULL;
    s = spin_lock_irqsave(&g_po_lock);
    if (!i->dev) {
        i->dev = dev;
        i->next = g_idle;
        g_idle = i;
    }
    i->conservation = conservation;
    i->performance = performance;
    i->state = state >= 2 && state <= 4 ? state : 4;
    i->count = 0;
    spin_unlock_irqrestore(&g_po_lock, s);
    po_start();
    return &i->count;
}

/* PoSetDeviceBusyEx(IdlePointer): the WDK's PoSetDeviceBusy */
NT_API void nt_PoSetDeviceBusyEx(volatile UINT32 *idle)
{
    if (idle) __atomic_store_n(idle, 0, __ATOMIC_RELAXED);
}
