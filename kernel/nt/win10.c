/*
 * win10.c — kernel routines Windows 8 and 10 added, which the driver
 * frameworks import (KMDF's Wdf01000.sys links against all of these)
 *
 *   Ex timers   ExAllocateTimer / ExSetTimer / ExCancelTimer /
 *               ExDeleteTimer: a KTIMER and its DPC calling the
 *               EXT_CALLBACK at DISPATCH_LEVEL (high-resolution and no-wake
 *               attributes change nothing: NovaOS's timers tick at 10 ms)
 *   Ke          coalescable timers (KeSetTimerEx: the tolerable delay is
 *               not used), the APC-disabled test, the shared-data alignment,
 *               the processor count, bug check reason callbacks (accepted:
 *               NovaOS writes no crash dump for them to add to)
 *   Io          activity IDs on IRPs (none kept), device properties (the
 *               unified property store arrives with PnP: none found), the
 *               initiator and requestor processes, a driver's full path
 *   Po          the power framework (PoFx): components are kept active, so
 *               an activation reports the active condition at once and an
 *               idle request the idle condition; the device is never powered
 *               down by it (no runtime power management yet)
 *   Ps          a process's ID
 */
#include "nt.h"
#include "ntapi.h"
#include "../ke/printf.h"
#include "../ke/irql.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../um/um_internal.h"

NT_API void      nt_KeInitializeTimerEx(NT_KTIMER *t, int type);
NT_API UINT8     nt_KeSetTimerEx(NT_KTIMER *t, INT64 due, int period_ms, NT_KDPC *dpc);
NT_API UINT8     nt_KeCancelTimer(NT_KTIMER *t);
NT_API void      nt_KeInitializeDpc(NT_KDPC *dpc, NT_KDEFERRED_ROUTINE routine, void *ctx);
NT_API UINT8     nt_KeRemoveQueueDpc(NT_KDPC *dpc);
NT_API void      nt_KeFlushQueuedDpcs(void);
NT_API UINT32    nt_KeQueryActiveProcessorCountEx(UINT16 group);
NT_API NTSTATUS  nt_IoUnregisterPlugPlayNotification(void *handle);
NT_API void     *nt_ExAllocatePoolWithTag(int type, UINT64 size, UINT32 tag);

#define ST_NOT_FOUND  ((NTSTATUS)0xC0000225)

/* -----------------------------------------------------------------------
 * Ex timers
 * ----------------------------------------------------------------------- */
typedef void (NT_API *ExtCallback)(void *timer, void *ctx);
typedef void (NT_API *ExtDeleteCallback)(void *ctx);
typedef struct { UINT32 Version, Reserved; ExtDeleteCallback DeleteCallback; void *DeleteContext; } ExtDeleteParameters;

typedef struct {
    NT_KTIMER    timer;
    NT_KDPC      dpc;
    ExtCallback  callback;
    void        *ctx;
    NT_KSPIN_LOCK lock;
    bool         running, deleted;
    ExtDeleteCallback on_delete;
    void        *on_delete_ctx;
} ExTimer;

static void ex_timer_free(ExTimer *t)
{
    if (t->on_delete) t->on_delete(t->on_delete_ctx);
    kfree(t);
}

static void NT_API ex_timer_dpc(NT_KDPC *dpc, void *ctx, void *a1, void *a2)
{
    (void)dpc; (void)a1; (void)a2;
    ExTimer *t = ctx;
    UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&t->lock);
    if (t->deleted) { nt_KeReleaseSpinLock(&t->lock, old); return; }
    t->running = true;
    nt_KeReleaseSpinLock(&t->lock, old);
    t->callback(t, t->ctx);
    old = nt_KeAcquireSpinLockRaiseToDpc(&t->lock);
    t->running = false;
    bool gone = t->deleted;
    nt_KeReleaseSpinLock(&t->lock, old);
    if (gone) ex_timer_free(t);                    /* (deleted while its callback ran) */
}

NT_API void *nt_ExAllocateTimer(ExtCallback callback, void *ctx, UINT32 attributes)
{
    (void)attributes;
    ExTimer *t = kzalloc(sizeof(*t));
    if (!t) return NULL;
    t->callback = callback;
    t->ctx = ctx;
    nt_KeInitializeTimerEx(&t->timer, 0);
    nt_KeInitializeDpc(&t->dpc, ex_timer_dpc, t);
    return t;
}

/* @due: 100 ns units, negative relative; @period: 100 ns units */
NT_API UINT8 nt_ExSetTimer(ExTimer *t, INT64 due, INT64 period, void *params)
{
    (void)params;
    INT64 ms = period > 0 ? (period + 9999) / 10000 : 0;
    return nt_KeSetTimerEx(&t->timer, due, (int)(ms > 0x7FFFFFFF ? 0x7FFFFFFF : ms), &t->dpc);
}

NT_API UINT8 nt_ExCancelTimer(ExTimer *t, void *params)
{
    (void)params;
    UINT8 was = nt_KeCancelTimer(&t->timer);
    if (nt_KeRemoveQueueDpc(&t->dpc)) was = 1;
    return was;
}

/* With @wait the caller is at PASSIVE_LEVEL and the timer goes once a
 * callback in progress has returned; without, a running callback frees it
 * when it returns (the framework stops its timers before deleting them) */
NT_API UINT8 nt_ExDeleteTimer(ExTimer *t, UINT8 cancel, UINT8 wait, ExtDeleteParameters *params)
{
    UINT8 was = 0;
    if (params) { t->on_delete = params->DeleteCallback; t->on_delete_ctx = params->DeleteContext; }
    if (cancel || wait) was = nt_ExCancelTimer(t, NULL);
    UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&t->lock);
    t->deleted = true;
    bool running = t->running;
    nt_KeReleaseSpinLock(&t->lock, old);
    if (wait) {
        nt_KeFlushQueuedDpcs();
        while (__atomic_load_n(&t->running, __ATOMIC_ACQUIRE)) sched_yield();
        ex_timer_free(t);
    } else if (!running)
        ex_timer_free(t);
    return was;
}

/* -----------------------------------------------------------------------
 * Ke
 * ----------------------------------------------------------------------- */
NT_API UINT8 nt_KeSetCoalescableTimer(NT_KTIMER *t, INT64 due, UINT32 period_ms, UINT32 tolerable_delay, NT_KDPC *dpc)
{
    (void)tolerable_delay;
    return nt_KeSetTimerEx(t, due, (int)period_ms, dpc);
}

/* (NovaOS delivers no kernel APCs; a thread at APC_LEVEL or above has them off) */
NT_API UINT8 nt_KeAreApcsDisabled(void) { return KeIrql() >= APC_LEVEL; }

NT_API UINT32 nt_KeGetRecommendedSharedDataAlignment(void) { return 64; }    /* the cache line */

NT_API UINT32 nt_KeQueryMaximumProcessorCountEx(UINT16 group) { return nt_KeQueryActiveProcessorCountEx(group); }

NT_API UINT8 nt_KeRegisterBugCheckReasonCallback(void *record, void *routine, int reason, const char *component)
{
    (void)record; (void)routine; (void)reason; (void)component;
    return 1;
}

NT_API UINT8 nt_KeDeregisterBugCheckReasonCallback(void *record) { (void)record; return 1; }

/* -----------------------------------------------------------------------
 * Io
 * ----------------------------------------------------------------------- */
NT_API NTSTATUS nt_IoGetActivityIdIrp(NT_IRP *irp, void *guid) { (void)irp; (void)guid; return ST_NOT_FOUND; }
NT_API NTSTATUS nt_IoSetActivityIdIrp(NT_IRP *irp, const void *guid) { (void)irp; (void)guid; return NT_STATUS_SUCCESS; }

NT_API NTSTATUS nt_IoGetDevicePropertyData(NT_DEVICE_OBJECT *pdo, const void *key, UINT32 lcid, UINT32 flags, UINT32 size,
                                           void *data, UINT32 *required, UINT32 *type)
{
    (void)pdo; (void)key; (void)lcid; (void)flags; (void)size; (void)data;
    if (required) *required = 0;
    if (type) *type = 0;                                        /* DEVPROP_TYPE_EMPTY */
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NT_API NTSTATUS nt_IoSetDevicePropertyData(NT_DEVICE_OBJECT *pdo, const void *key, UINT32 lcid, UINT32 flags, UINT32 type,
                                           UINT32 size, void *data)
{
    (void)pdo; (void)key; (void)lcid; (void)flags; (void)type; (void)size; (void)data;
    return NT_STATUS_NOT_IMPLEMENTED;
}

/* A file opened by its own process has no other initiator (NULL), as on Windows */
NT_API void *nt_IoGetInitiatorProcess(NT_FILE_OBJECT *fo) { (void)fo; return NULL; }

/* The process the request came from: NovaOS sends a program's requests to
 * the driver in the program's own thread, where they begin */
NT_API UINT32 nt_IoGetRequestorProcessId(NT_IRP *irp)
{
    (void)irp;
    UmProcess *p = UmCurrent();
    return p ? p->pid : 0;
}

/* The driver's image as an NT path (\??\C:\Windows\System32\drivers\x.sys),
 * in pool the caller frees */
NT_API NTSTATUS nt_IoQueryFullDriverPath(NT_DRIVER_OBJECT *obj, NT_UNICODE_STRING *out)
{
    NtDriver *drv = NtIoDriverOf(obj);
    if (!drv || !out) return NT_STATUS_INVALID_PARAMETER;
    char full[272];
    ksnprintf(full, sizeof(full), "\\??\\%s", drv->path);
    size_t n = strlen(full);
    NT_WCHAR *w = nt_ExAllocatePoolWithTag(1, 2 * (n + 1), 0x50647249u /* 'IrdP' */);
    if (!w) return STATUS_INSUFFICIENT_RESOURCES;
    for (size_t i = 0; i <= n; i++) w[i] = (UINT8)full[i];
    out->Buffer = w;
    out->Length = (unsigned short)(2 * n);
    out->MaximumLength = (unsigned short)(2 * n + 2);
    return NT_STATUS_SUCCESS;
}

NT_API NTSTATUS nt_IoUnregisterPlugPlayNotificationEx(void *handle) { return nt_IoUnregisterPlugPlayNotification(handle); }

/* -----------------------------------------------------------------------
 * Po: the power framework
 * ----------------------------------------------------------------------- */
typedef void (NT_API *PoFxComponentCallback)(void *ctx, UINT32 component);

typedef struct {
    PoFxComponentCallback active, idle;
    void *ctx;
} PoFxHandle;

/* PO_FX_DEVICE: V1 (Version 1) has the callbacks at 8 and the context at
 * 56; V2 and V3 have a ULONGLONG Flags first (callbacks at 16, the context
 * at 64 and, after the directed power callbacks, 88) */
NT_API NTSTATUS nt_PoFxRegisterDevice(NT_DEVICE_OBJECT *pdo, const UINT8 *device, PoFxHandle **out)
{
    (void)pdo;
    UINT32 version = *(const UINT32 *)device;
    UINT32 cb = version == 1 ? 8 : 16, ctx = version == 1 ? 56 : version == 2 ? 64 : 88;
    if (version < 1 || version > 3) return NT_STATUS_INVALID_PARAMETER;
    PoFxHandle *h = kzalloc(sizeof(*h));
    if (!h) return STATUS_INSUFFICIENT_RESOURCES;
    h->active = *(PoFxComponentCallback const *)(device + cb);
    h->idle = *(PoFxComponentCallback const *)(device + cb + 8);
    h->ctx = *(void *const *)(device + ctx);
    *out = h;
    return NT_STATUS_SUCCESS;
}

NT_API void nt_PoFxUnregisterDevice(PoFxHandle *h) { kfree(h); }
NT_API void nt_PoFxStartDevicePowerManagement(PoFxHandle *h) { (void)h; }

NT_API void nt_PoFxActivateComponent(PoFxHandle *h, UINT32 component, UINT32 flags)
{
    (void)flags;
    if (h->active) h->active(h->ctx, component);
}

NT_API void nt_PoFxIdleComponent(PoFxHandle *h, UINT32 component, UINT32 flags)
{
    (void)flags;
    if (h->idle) h->idle(h->ctx, component);
}

NT_API void nt_PoFxCompleteIdleCondition(PoFxHandle *h, UINT32 component) { (void)h; (void)component; }
NT_API void nt_PoFxCompleteIdleState(PoFxHandle *h, UINT32 component) { (void)h; (void)component; }
NT_API void nt_PoFxCompleteDevicePowerNotRequired(PoFxHandle *h) { (void)h; }
NT_API void nt_PoFxCompleteDirectedPowerDown(PoFxHandle *h) { (void)h; }
NT_API void nt_PoFxReportDevicePoweredOn(PoFxHandle *h) { (void)h; }
NT_API void nt_PoFxSetDeviceIdleTimeout(PoFxHandle *h, UINT64 timeout) { (void)h; (void)timeout; }

/* Whether the IRP's system wake (a wait/wake IRP's) was the system's wake
 * source: NovaOS does not sleep */
NT_API void nt_PoSetSystemWake(NT_IRP *irp) { (void)irp; }
NT_API UINT8 nt_PoGetSystemWake(NT_IRP *irp) { (void)irp; return 0; }

/* -----------------------------------------------------------------------
 * Ps
 * ----------------------------------------------------------------------- */
/* (a PEPROCESS NovaOS hands a driver is its process record) */
NT_API void *nt_PsGetProcessId(UmProcess *p) { return p ? (void *)(uintptr_t)p->pid : NULL; }
