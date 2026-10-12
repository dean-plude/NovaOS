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
