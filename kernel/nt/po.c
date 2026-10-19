/*
 * po.c — the power framework (PoFx) a driver registers its device with
 *
 * A driver that manages its device's power by components registers it
 * (PoFxRegisterDevice) with callbacks the framework calls when a
 * component must be active (ComponentActiveConditionCallback) and when it
 * may go idle (ComponentIdleConditionCallback).  The driver takes an
 * activation reference before it touches the hardware (PoFxActivateComponent)
 * and drops it after (PoFxIdleComponent).
 *
 * NovaOS does not power devices down: every component stays in F0 and the
 * device in D0.  The conditions are still kept the way Windows keeps them,
 * because drivers wait on them: a component starts active; when its last
 * activation reference goes it gets the idle-condition callback (the
 * driver answers with PoFxCompleteIdleCondition), and the next activation
 * gets the active-condition callback again before the call returns.  No
 * idle-state change is ever asked for, so PoFxCompleteIdleState and the
 * directed power-down calls have nothing to complete.
 */
#include "nt.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

typedef void (NT_API *PoFxCondition)(void *ctx, UINT32 component);

typedef struct {
    PoFxCondition active, idle;                 /* ComponentActive/IdleConditionCallback */
    void         *ctx;                          /* DeviceContext */
} NtPoFxCallbacks;

#define MAX_COMPONENTS 32

typedef struct {
    NT_DEVICE_OBJECT *pdo;
    NtPoFxCallbacks   cb;
    UINT32            count;
    int               refs[MAX_COMPONENTS];     /* activation references */
    bool              idle[MAX_COMPONENTS];     /* in the idle condition */
    KSpinLock         lock;
} NtPoFx;

/* PO_FX_DEVICE: V1 has Version, ComponentCount, then the callbacks at 8
 * and DeviceContext at 56; V2 and V3 put a ULONGLONG Flags after
 * ComponentCount (callbacks at 16, DeviceContext at 64 and, after V3's
 * directed power callbacks, 88) */
NT_API NTSTATUS nt_PoFxRegisterDevice(NT_DEVICE_OBJECT *pdo, const UINT8 *dev, NtPoFx **handle)
{
    if (!pdo || !dev || !handle) return NT_STATUS_INVALID_PARAMETER;
    UINT32 version = *(const UINT32 *)dev, components = *(const UINT32 *)(dev + 4);
    if (version < 1 || version > 3 || !components) return NT_STATUS_INVALID_PARAMETER;
    UINT32 cb = version == 1 ? 8 : 16, ctx = version == 1 ? 56 : version == 2 ? 64 : 88;
    NtPoFx *h = kzalloc(sizeof(*h));
    if (!h) return NT_STATUS_INSUFFICIENT_RESOURCES;
    h->pdo = pdo;
    h->cb.active = *(PoFxCondition const *)(dev + cb);
    h->cb.idle = *(PoFxCondition const *)(dev + cb + 8);
    h->cb.ctx = *(void *const *)(dev + ctx);
    h->count = components < MAX_COMPONENTS ? components : MAX_COMPONENTS;
    /* (a zeroed KSpinLock is KSPINLOCK_INIT) */
    *handle = h;
    return NT_STATUS_SUCCESS;
}

NT_API void nt_PoFxUnregisterDevice(NtPoFx *h) { kfree(h); }

NT_API void nt_PoFxStartDevicePowerManagement(NtPoFx *h) { (void)h; }   /* (components start active, in F0) */

NT_API void nt_PoFxActivateComponent(NtPoFx *h, UINT32 c, UINT32 flags)
{
    (void)flags;
    if (!h || c >= h->count) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    bool wake = h->refs[c]++ == 0 && h->idle[c];
    if (wake) h->idle[c] = false;
    spin_unlock_irqrestore(&h->lock, s);
    if (wake && h->cb.active) h->cb.active(h->cb.ctx, c);
}

NT_API void nt_PoFxIdleComponent(NtPoFx *h, UINT32 c, UINT32 flags)
{
    (void)flags;
    if (!h || c >= h->count) return;
    IrqState s = spin_lock_irqsave(&h->lock);
    if (h->refs[c] <= 0) {
        spin_unlock_irqrestore(&h->lock, s);
        kprintf("[NT] PoFxIdleComponent: component %u has no activation reference\n", c);
        return;
    }
    bool idle = --h->refs[c] == 0;
    if (idle) h->idle[c] = true;
    spin_unlock_irqrestore(&h->lock, s);
    if (idle && h->cb.idle) h->cb.idle(h->cb.ctx, c);
}

/* The driver's answers to callbacks NovaOS makes (idle condition) or never
 * makes (idle state, directed power-down): nothing waits on them */
NT_API void nt_PoFxCompleteIdleCondition(NtPoFx *h, UINT32 c) { (void)h; (void)c; }
NT_API void nt_PoFxCompleteIdleState(NtPoFx *h, UINT32 c) { (void)h; (void)c; }
NT_API void nt_PoFxCompleteDirectedPowerDown(NtPoFx *h) { (void)h; }
NT_API void nt_PoFxReportDevicePoweredOn(NtPoFx *h) { (void)h; }

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
#include "ntapi.h"
#include "../hal/aml.h"
#include "../ke/scheduler.h"

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
