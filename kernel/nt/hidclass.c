/*
 * hidclass.c — the HID class driver Windows HID minidrivers plug into
 *
 * On Windows a HID device's function driver is a minidriver (hidusb.sys
 * for USB, hidi2c.sys, a vendor's touchpad or pen driver, a virtual
 * device's) that only moves reports; hidclass.sys does the rest.  NovaOS
 * ships none of Microsoft's HID stack: this file is hidclass.sys's side,
 * exported as hidclass.sys for minidrivers to import.
 *
 *   - HidRegisterMinidriver: called from a minidriver's DriverEntry, it
 *     takes over the driver's AddDevice and dispatch table, as hidclass
 *     does.  AddDevice makes the functional device object, whose device
 *     extension starts with HID_DEVICE_EXTENSION (the PDO, the next
 *     device down, and the minidriver's own extension after it), and then
 *     calls the minidriver's AddDevice with it.  Every request to the FDO
 *     goes to the minidriver from here, on a stack location of its own.
 *   - Starting the device: once the minidriver has started it, its HID
 *     descriptor, report descriptor and attributes (IOCTL_HID_GET_*,
 *     internal), and a child PDO per top-level collection, which the PnP
 *     manager learns of through BusRelations and starts raw (as Windows
 *     starts a collection no driver claims): HID\VID_v&PID_p[&Colnn],
 *     interface GUID_DEVINTERFACE_HID.
 *   - Input: a thread keeps one IOCTL_HID_READ_REPORT outstanding and hands
 *     each report to the collection whose report it is, into every open
 *     handle's queue (32 reports by default, the oldest dropped first,
 *     IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS); a ReadFile takes as many whole
 *     reports as fit, or waits for the next one.
 *   - What programs send a collection (hid.dll's HidD_*): the collection's
 *     information and preparsed data, feature, input and output reports
 *     (to the minidriver, internal, as HID_XFER_PACKETs), WriteFile as an
 *     output report, strings, the queue's size and flushing it.
 *
 * Preparsed data is NovaOS's own ("HidP KDR", userland/include/novahidp.h),
 * which hid.dll's HidP_* read; a program only passes it back.  A report
 * always starts with its report ID byte (0 when the device has none), as
 * programs see them on Windows; a minidriver of a device without IDs reads
 * and writes reports without that byte.
 *
 * Not done yet: polled minidrivers are read in the same loop (no per-handle
 * polling interval), selective suspend (IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST
 * is never sent), and the keyboard and mouse class drivers on top
 * (kbdhid/mouhid): NovaOS's own input drivers keep those devices.
 */
#include "ntapi.h"
#include "hidclass.h"
#include "../ke/printf.h"
#include "../ke/probe.h"
#include "../ke/scheduler.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

#define NOVA_HIDP_KERNEL
#include "../../userland/include/novahidp.h"

NT_API void *nt_MmMapLockedPagesSpecifyCache(NT_MDL *m, char mode, int cache, void *requested, UINT32 bugcheck,
                                             UINT32 priority);
NT_API UINT8 nt_IoCancelIrp(NT_IRP *irp);
NT_API void  nt_IoReleaseCancelSpinLock(UINT8 irql);
NT_API NT_IRP *nt_IoAllocateIrp(char stack_size, UINT8 charge_quota);
NT_API void  nt_IoFreeIrp(NT_IRP *irp);
NT_API NT_STATUS nt_IoRegisterDeviceInterface(NT_DEVICE_OBJECT *pdo, const NT_GUID *guid, NT_UNICODE_STRING *ref,
                                              NT_UNICODE_STRING *out);
NT_API NT_STATUS nt_IoSetDeviceInterfaceState(NT_UNICODE_STRING *name, UINT8 enable);
NT_API void  nt_IoInvalidateDeviceRelations(NT_DEVICE_OBJECT *pdo, int type);

#define NT_IRP_MJ_INTERNAL_DEVICE_CONTROL 0x0F
#define TAG_HID          0x20646948u              /* 'Hid ' */
#define MAX_MINI         8
#define MAX_FDO          16
#define MAX_COLL         8
#define DEF_BUFFERS      32
#define MAX_BUFFERS      512
#define ST_DEVICE_NOT_CONNECTED ((NT_STATUS)0xC000009D)
#define ST_INVALID_BUFFER_SIZE  ((NT_STATUS)0xC0000206)
#define ST_REVISION_MISMATCH    ((NT_STATUS)0xC0000059)

static const NT_GUID g_hid_iface = { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

/* A minidriver: what HidRegisterMinidriver took over */
typedef struct {
    NT_DRIVER_OBJECT    *drv;
    NT_DRIVER_ADD_DEVICE add;
    NT_DRIVER_DISPATCH   major[NT_IRP_MJ_MAXIMUM_FUNCTION + 1];
    UINT32               ext_size;
    bool                 polled;
} Mini;

typedef struct Coll Coll;

/* A device (its FDO) */
typedef struct {
    NT_DEVICE_OBJECT   *fdo;
    Mini               *mini;
    HidDeviceAttributes attr;
    char                mi[8];          /* "&MI_nn": a composite device's function (its PDO's hardware ID), or "" */
    UINT8              *desc;           /* the report descriptor */
    UINT32              desc_len;
    Coll               *coll[MAX_COLL];
    int                 ncoll;
    bool                ids;            /* its reports start with an ID byte */
    UINT32              max_in;         /* the longest input report (with the ID byte) */
    volatile bool       stopping;
    bool                pump;           /* the read thread runs */
    NT_IRP             *read_irp;       /* its request at the minidriver */
    NT_KEVENT           pump_done;
} Fdo;

/* A top-level collection (its PDO's extension) */
#define COLL_MAGIC 0x6C6F4348u           /* "HCol" */
struct Coll {
    UINT32            magic;
    NT_DEVICE_OBJECT *pdo;
    Fdo              *f;                /* NULL once the device is gone */
    int               index;
    NovaHidP         *pp;               /* its preparsed data */
    UINT32            pp_len;
    UINT16            in_len, out_len, feat_len;
    bool              gone;
    NT_LIST_ENTRY     files;
};

/* An open handle on a collection (its FILE_OBJECT's FsContext) */
typedef struct {
    NT_LIST_ENTRY link;
    Coll         *c;
    UINT8        *ring;                 /* cap reports of c->in_len bytes */
    int           cap, head, count;
    NT_LIST_ENTRY reads;                /* waiting IRP_MJ_READs (Tail.Overlay.ListEntry) */
} File;

static Mini      g_mini[MAX_MINI];
static Fdo      *g_fdo[MAX_FDO];
static KSpinLock g_lock = KSPINLOCK_INIT;  /* the tables, collections' handles and their queues */
static NT_KMUTEX g_build;                  /* (nova_hidp_build_tlc's tables) */
static bool      g_build_init;

static void list_init(NT_LIST_ENTRY *h) { h->Flink = h->Blink = h; }
static bool list_empty(NT_LIST_ENTRY *h) { return h->Flink == h; }
static void list_del(NT_LIST_ENTRY *e) { e->Blink->Flink = e->Flink; e->Flink->Blink = e->Blink; list_init(e); }
static void list_add_tail(NT_LIST_ENTRY *h, NT_LIST_ENTRY *e)
{
    e->Flink = h;
    e->Blink = h->Blink;
    h->Blink->Flink = e;
    h->Blink = e;
}
#define CONTAINER(p, type, field) ((type *)((UINT8 *)(p) - __builtin_offsetof(type, field)))

static Mini *mini_of(NT_DRIVER_OBJECT *drv)
{
    for (int i = 0; i < MAX_MINI; i++) if (g_mini[i].drv == drv) return &g_mini[i];
    return NULL;
}

static Fdo *fdo_of(NT_DEVICE_OBJECT *d)
{
    for (int i = 0; i < MAX_FDO; i++) if (g_fdo[i] && g_fdo[i]->fdo == d) return g_fdo[i];
    return NULL;
}

static Coll *coll_of(NT_DEVICE_OBJECT *d)
{
    Coll *c = d->DeviceExtension;
    return c && c->magic == COLL_MAGIC && c->pdo == d ? c : NULL;
}

static HidDeviceExtension *hdx(Fdo *f) { return f->fdo->DeviceExtension; }

static void complete(NT_IRP *irp, NT_STATUS st, NT_ULONG_PTR info)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    nt_IofCompleteRequest(irp, 0);
}

/* ---- calling the minidriver ---- */

/* IoSetNextIrpStackLocation, then the minidriver's dispatch routine for it,
 * as the location's owner (the FDO) */
static NT_STATUS call_mini(Fdo *f, NT_IRP *irp)
{
    irp->CurrentLocation--;
    irp->Tail.Overlay.CurrentStackLocation--;
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    sl->DeviceObject = f->fdo;
    return f->mini->major[sl->MajorFunction](f->fdo, irp);
}

/* IoCopyCurrentIrpStackLocationToNext */
static void copy_down(NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *cur = NtIrpCurrentStack(irp), *next = NtIrpNextStack(irp);
    memcpy(next, cur, __builtin_offsetof(NT_IO_STACK_LOCATION, CompletionRoutine));
    next->Control = 0;
    next->CompletionRoutine = NULL;
    next->Context = NULL;
}

static NT_API NT_STATUS sync_done(NT_DEVICE_OBJECT *d, NT_IRP *irp, void *ctx)
{
    (void)d; (void)irp;
    nt_KeSetEvent(ctx, 0, 0);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;
}

/* An IRP sent to the FDO, to the minidriver and back: its status (the IRP
 * is ours again, still to be completed) */
static NT_STATUS forward_sync(Fdo *f, NT_IRP *irp)
{
    NT_KEVENT ev;
    nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    copy_down(irp);
    NT_IO_STACK_LOCATION *next = NtIrpNextStack(irp);
    next->CompletionRoutine = sync_done;
    next->Context = &ev;
    next->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    call_mini(f, irp);
    nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    return irp->IoStatus.Status;
}

/* An internal request of hidclass's own to the minidriver: @in its
 * Type3InputBuffer, @out its UserBuffer (@out_len), @mdl its MdlAddress;
 * waits for it.  *@info the bytes it moved. */
static NT_STATUS mini_ioctl(Fdo *f, UINT32 code, void *in, UINT32 in_len, void *out, UINT32 out_len, NT_MDL *mdl,
                            NT_ULONG_PTR *info)
{
    NT_IRP *irp = nt_IoAllocateIrp(f->fdo->StackSize, 0);
    if (!irp) return NT_STATUS_INSUFFICIENT_RESOURCES;
    NT_KEVENT ev;
    NT_IO_STATUS_BLOCK iosb = { { 0 }, 0 };
    nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    irp->UserEvent = &ev;
    irp->UserIosb = &iosb;
    irp->UserBuffer = out;
    irp->MdlAddress = mdl;
    irp->RequestorMode = 0;
    irp->IoStatus.Status = NT_STATUS_NOT_SUPPORTED;
    NT_IO_STACK_LOCATION *sl = NtIrpNextStack(irp);
    sl->MajorFunction = NT_IRP_MJ_INTERNAL_DEVICE_CONTROL;
    sl->Parameters.DeviceIoControl.IoControlCode = code;
    sl->Parameters.DeviceIoControl.InputBufferLength = in_len;
    sl->Parameters.DeviceIoControl.OutputBufferLength = out_len;
    sl->Parameters.DeviceIoControl.Type3InputBuffer = in;
    call_mini(f, irp);
    nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    if (info) *info = iosb.Information;
    irp->MdlAddress = NULL;
    nt_IoFreeIrp(irp);
    return iosb.Status;
}

/* ---- input reports ---- */

/* Whether collection @c has input reports with ID @id */
static bool has_report(const Coll *c, int type, UINT8 id)
{
    if (!c->f || !c->f->ids) return true;
    for (int k = 0; k < c->pp->ncaps; k++) if (c->pp->caps[k].rtype == type && c->pp->caps[k].id == id) return true;
    return false;
}

/* Whole reports from @fl's queue into @buf (@len bytes): the bytes (g_lock held) */
static UINT32 take(File *fl, UINT8 *buf, UINT32 len)
{
    UINT32 n = 0, rl = fl->c->in_len;
    while (fl->count && n + rl <= len) {
        memcpy(buf + n, fl->ring + (size_t)fl->head * rl, rl);
        fl->head = (fl->head + 1) % fl->cap;
        fl->count--;
        n += rl;
    }
    return n;
}

static NT_API void read_cancel(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    (void)dev;
    nt_IoReleaseCancelSpinLock(irp->CancelIrql);
    IrqState s = spin_lock_irqsave(&g_lock);
    list_del(&irp->Tail.Overlay.ListEntry);
    spin_unlock_irqrestore(&g_lock, s);
    complete(irp, NT_STATUS_CANCELLED, 0);
}

/* The waiting reads of @fl that can be finished now onto @done (g_lock
 * held); with @st set, all of them, failing with it */
static void finish_reads(File *fl, NT_LIST_ENTRY *done, NT_STATUS st)
{
    for (NT_LIST_ENTRY *e = fl->reads.Flink, *next; e != &fl->reads; e = next) {
        next = e->Flink;
        if (!st && !fl->count) break;
        NT_IRP *irp = CONTAINER(e, NT_IRP, Tail.Overlay.ListEntry);
        if (!__atomic_exchange_n(&irp->CancelRoutine, NULL, __ATOMIC_ACQ_REL)) continue;   /* (being cancelled) */
        list_del(e);
        if (st) {
            irp->IoStatus.Status = st;
            irp->IoStatus.Information = 0;
        } else {
            NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
            irp->IoStatus.Information = take(fl, irp->AssociatedIrp.SystemBuffer, sl->Parameters.Read.Length);
            irp->IoStatus.Status = NT_STATUS_SUCCESS;
        }
        list_add_tail(done, e);
    }
}

static void complete_list(NT_LIST_ENTRY *done)
{
    while (!list_empty(done)) {
        NT_LIST_ENTRY *e = done->Flink;
        list_del(e);
        nt_IofCompleteRequest(CONTAINER(e, NT_IRP, Tail.Overlay.ListEntry), 0);
    }
}

/* One input report (@r, @len bytes with its ID byte) to the collection it
 * belongs to: every handle's queue, and the reads waiting */
static void deliver(Fdo *f, const UINT8 *r, UINT32 len)
{
    NT_LIST_ENTRY done;
    list_init(&done);
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < f->ncoll; i++) {
        Coll *c = f->coll[i];
        if (!c || c->gone || !has_report(c, 0, r[0])) continue;
        for (NT_LIST_ENTRY *e = c->files.Flink; e != &c->files; e = e->Flink) {
            File *fl = CONTAINER(e, File, link);
            int at = (fl->head + fl->count) % fl->cap;
            if (fl->count == fl->cap) {                       /* full: the oldest goes */
                fl->head = (fl->head + 1) % fl->cap;
                at = (fl->head + fl->count - 1) % fl->cap;
            } else {
                fl->count++;
            }
            UINT8 *slot = fl->ring + (size_t)at * c->in_len;
            UINT32 n = len < c->in_len ? len : c->in_len;
            memcpy(slot, r, n);
            memset(slot + n, 0, c->in_len - n);
            finish_reads(fl, &done, 0);
        }
        break;
    }
    spin_unlock_irqrestore(&g_lock, s);
    complete_list(&done);
}

/* The read thread: one IOCTL_HID_READ_REPORT at the minidriver at a time */
static void pump(void *arg)
{
    Fdo *f = arg;
    UINT8 *buf = kmalloc(f->max_in + 1);
    int errors = 0;
    while (buf && !f->stopping) {
        NT_IRP *irp = nt_IoAllocateIrp(f->fdo->StackSize, 0);
        if (!irp) break;
        NT_KEVENT ev;
        NT_IO_STATUS_BLOCK iosb = { { 0 }, 0 };
        nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
        irp->UserEvent = &ev;
        irp->UserIosb = &iosb;
        irp->UserBuffer = f->ids ? buf : buf + 1;            /* (no IDs: the report goes after a 0 ID byte) */
        irp->IoStatus.Status = NT_STATUS_NOT_SUPPORTED;
        NT_IO_STACK_LOCATION *sl = NtIrpNextStack(irp);
        sl->MajorFunction = NT_IRP_MJ_INTERNAL_DEVICE_CONTROL;
        sl->Parameters.DeviceIoControl.IoControlCode = IOCTL_HID_READ_REPORT;
        sl->Parameters.DeviceIoControl.OutputBufferLength = f->ids ? f->max_in : f->max_in - 1;
        IrqState s = spin_lock_irqsave(&g_lock);
        f->read_irp = irp;
        spin_unlock_irqrestore(&g_lock, s);
        call_mini(f, irp);
        nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
        s = spin_lock_irqsave(&g_lock);
        f->read_irp = NULL;
        spin_unlock_irqrestore(&g_lock, s);
        nt_IoFreeIrp(irp);
        if (NT_SUCCESS(iosb.Status) && iosb.Information) {
            errors = 0;
            buf[0] = f->ids ? buf[0] : 0;
            deliver(f, buf, (UINT32)iosb.Information + (f->ids ? 0 : 1));
        } else if (!f->stopping && !NT_SUCCESS(iosb.Status)) {
            INT64 wait = -10000LL * (++errors > 10 ? 1000 : 50);   /* (gone, or failing: until it is removed) */
            nt_KeDelayExecutionThread(0, 0, &wait);
        }
        if (f->mini->polled && !f->stopping) {
            INT64 wait = -10000LL * 8;
            nt_KeDelayExecutionThread(0, 0, &wait);
        }
    }
    kfree(buf);
    nt_KeSetEvent(&f->pump_done, 0, 0);
}

static void pump_stop(Fdo *f)
{
    if (!f->pump) return;
    f->stopping = true;
    IrqState s = spin_lock_irqsave(&g_lock);
    if (f->read_irp) nt_IoCancelIrp(f->read_irp);
    spin_unlock_irqrestore(&g_lock, s);
    nt_KeWaitForSingleObject(&f->pump_done, 0, 0, 0, NULL);
    f->pump = false;
}

/* ---- collections ---- */

/* Collection @c is no more: its handles' reads fail, and new ones */
static void coll_gone(Coll *c)
{
    NT_LIST_ENTRY done;
    list_init(&done);
    IrqState s = spin_lock_irqsave(&g_lock);
    c->gone = true;
    for (NT_LIST_ENTRY *e = c->files.Flink; e != &c->files; e = e->Flink)
        finish_reads(CONTAINER(e, File, link), &done, ST_DEVICE_NOT_CONNECTED);
    Fdo *f = c->f;
    for (int i = 0; f && i < f->ncoll; i++) if (f->coll[i] == c) f->coll[i] = NULL;
    c->f = NULL;
    spin_unlock_irqrestore(&g_lock, s);
    complete_list(&done);
}

static NT_WCHAR *pool_wide(const char *s, size_t n)
{
    NT_WCHAR *w = nt_ExAllocatePoolWithTag(1, 2 * (n + 1), TAG_HID);
    if (!w) return NULL;
    for (size_t i = 0; i < n; i++) w[i] = (UINT8)s[i];
    w[n] = 0;
    return w;
}

/* The collection's IDs: "HID\VID_v&PID_p[&MI_nn][&Colnn]" (@rev: with &REV_r) */
static void coll_id(const Coll *c, const Fdo *f, bool rev, char *out, int cap)
{
    int n = ksnprintf(out, cap, "HID\\VID_%04X&PID_%04X", f->attr.VendorID, f->attr.ProductID);
    if (rev) n += ksnprintf(out + n, cap - n, "&REV_%04X", f->attr.VersionNumber);
    n += ksnprintf(out + n, cap - n, "%s", f->mi);
    if (f->ncoll > 1) ksnprintf(out + n, cap - n, "&Col%02d", c->index + 1);
}

/* What Windows calls a collection by its usage (Device Manager's name) */
static const char *coll_name(const Coll *c)
{
    UINT16 page = c->pp->page, u = c->pp->usage;
    if (page == 1 && (u == 4 || u == 5)) return "HID-compliant game controller";
    if (page == 1 && u == 2) return "HID-compliant mouse";
    if (page == 1 && u == 6) return "HID Keyboard Device";
    if (page == 1 && u == 0x80) return "HID-compliant system controller";
    if (page == 0x0C) return "HID-compliant consumer control device";
    if (page == 0x0D) return "HID-compliant touch screen";
    if (page >= 0xFF00) return "HID-compliant vendor-defined device";
    return "HID-compliant device";
}

static NT_STATUS coll_pnp(Coll *c, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    NT_STATUS st = irp->IoStatus.Status;
    Fdo *f = c->f;
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE: {
        if (c->gone || !f) { st = ST_DEVICE_NOT_CONNECTED; break; }
        NT_UNICODE_STRING name = { 0, 0, NULL };
        st = nt_IoRegisterDeviceInterface(c->pdo, &g_hid_iface, NULL, &name);
        if (NT_SUCCESS(st)) {
            st = nt_IoSetDeviceInterfaceState(&name, 1);
            nt_ExFreePool(name.Buffer);
        }
        break;
    }
    case NT_IRP_MN_SURPRISE_REMOVAL:
        coll_gone(c);
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_REMOVE_DEVICE:                            /* (coll_dispatch deletes the PDO) */
        coll_gone(c);
        kfree(c->pp);
        c->pp = NULL;
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_QUERY_REMOVE_DEVICE: case NT_IRP_MN_CANCEL_REMOVE_DEVICE: case NT_IRP_MN_STOP_DEVICE:
    case NT_IRP_MN_QUERY_STOP_DEVICE: case NT_IRP_MN_CANCEL_STOP_DEVICE: case NT_IRP_MN_QUERY_PNP_DEVICE_STATE:
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_QUERY_CAPABILITIES: {
        NT_DEVICE_CAPABILITIES *cap = sl->Parameters.Others.Argument1;
        if (!cap || cap->Size < sizeof(*cap)) { st = NT_STATUS_INVALID_PARAMETER; break; }
        cap->Flags |= 0x100 | 0x200;                         /* RawDeviceOK, SurpriseRemovalOK */
        cap->Address = cap->UINumber = (unsigned)c->index;
        cap->DeviceState[1] = 1;
        for (int i = 2; i < 7; i++) cap->DeviceState[i] = 4;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_ID: {
        if (!f || !c->pp) break;
        int type = (int)(UINT64)sl->Parameters.Others.Argument1;
        char ids[400];
        size_t len = 0;
        if (type == 0) { coll_id(c, f, false, ids, sizeof(ids)); len = strlen(ids); }
        else if (type == 1) {                                /* REG_MULTI_SZ */
            const char *sys = c->pp->page != 1 ? NULL : c->pp->usage == 4 || c->pp->usage == 5 ? "HID_DEVICE_SYSTEM_GAME" :
                              c->pp->usage == 6 ? "HID_DEVICE_SYSTEM_KEYBOARD" : c->pp->usage == 2 ? "HID_DEVICE_SYSTEM_MOUSE" : NULL;
            coll_id(c, f, true, ids, 120);
            len = strlen(ids) + 1;
            coll_id(c, f, false, ids + len, 120);
            len += strlen(ids + len) + 1;
            if (sys) { strcpy(ids + len, sys); len += strlen(sys) + 1; }
            len += (size_t)ksnprintf(ids + len, sizeof(ids) - len, "HID_DEVICE_UP:%04X_U:%04X", c->pp->page, c->pp->usage) + 1;
            strcpy(ids + len, "HID_DEVICE");
            len += strlen("HID_DEVICE") + 1;
            ids[len] = 0;                                    /* (the list's end; the length below counts it) */
        } else if (type == 3) { ksnprintf(ids, sizeof(ids), "%04X", (unsigned)c->index); len = strlen(ids); }
        else break;
        NT_WCHAR *w = pool_wide(ids, len);
        if (!w) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
        irp->IoStatus.Information = (NT_ULONG_PTR)w;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_DEVICE_TEXT: {
        if (!c->pp || (int)(UINT64)sl->Parameters.Others.Argument1 != 0) break;   /* DeviceTextDescription */
        const char *s = coll_name(c);
        NT_WCHAR *w = pool_wide(s, strlen(s));
        if (!w) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
        irp->IoStatus.Information = (NT_ULONG_PTR)w;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_DEVICE_RELATIONS:
        if ((int)(UINT64)sl->Parameters.Others.Argument1 == 4) {          /* TargetDeviceRelation */
            struct { unsigned int Count; NT_DEVICE_OBJECT *Objects[1]; } *r = nt_ExAllocatePoolWithTag(1, sizeof(*r), TAG_HID);
            if (!r) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
            r->Count = 1;
            r->Objects[0] = c->pdo;
            irp->IoStatus.Information = (NT_ULONG_PTR)r;
            st = NT_STATUS_SUCCESS;
        }
        break;
    }
    return st;
}

/* The buffer of a direct-I/O request (its MDL mapped), or NULL */
static UINT8 *mdl_buf(NT_IRP *irp)
{
    return irp->MdlAddress ? nt_MmMapLockedPagesSpecifyCache(irp->MdlAddress, 0, 1, NULL, 0, 16) : NULL;
}

/* A report to or from the minidriver for a program: the report in the
 * request's buffer, its ID its first byte (IOCTL_HID_GET/SET_FEATURE,
 * GET_INPUT_REPORT, SET_OUTPUT_REPORT, GET_OUTPUT_REPORT) */
static NT_STATUS report_ioctl(Coll *c, NT_IRP *irp, UINT32 code, UINT32 in_len, UINT32 out_len, NT_ULONG_PTR *info)
{
    bool set = code == IOCTL_HID_SET_FEATURE || code == IOCTL_HID_SET_OUTPUT_REPORT;
    int type = code == IOCTL_HID_SET_FEATURE || code == IOCTL_HID_GET_FEATURE ? 2 :
               code == IOCTL_HID_GET_INPUT_REPORT ? 0 : 1;
    UINT16 need = type == 2 ? c->feat_len : type == 0 ? c->in_len : c->out_len;
    UINT8 *out = mdl_buf(irp), *buf;
    UINT32 len;
    if (set) {                                               /* (the report: the output buffer, else the input) */
        buf = out && out_len ? out : irp->AssociatedIrp.SystemBuffer;
        len = out && out_len ? out_len : in_len;
    } else {
        buf = out;
        len = out_len;
    }
    if (!need) return NT_STATUS_INVALID_DEVICE_REQUEST;      /* (the collection has no report of that type) */
    if (!buf || len < need) return ST_INVALID_BUFFER_SIZE;
    UINT8 id = !set && in_len && irp->AssociatedIrp.SystemBuffer ? *(UINT8 *)irp->AssociatedIrp.SystemBuffer : buf[0];
    if (!set) buf[0] = id;
    if (!has_report(c, type, id)) return NT_STATUS_INVALID_PARAMETER;
    HidXferPacket pk = { buf, set ? need : len, id };
    NT_ULONG_PTR n = 0;
    /* (the packet in UserBuffer; its size the input length of a report to
     * the device, the output length of one from it, as hidclass.sys sets
     * them: KMDF minidrivers such as vhidmini2 check those) */
    NT_STATUS st = mini_ioctl(c->f, code, NULL, set ? sizeof(pk) : 0, &pk, set ? 0 : sizeof(pk), NULL, &n);
    if (NT_SUCCESS(st)) *info = set ? need : n > len ? len : n;
    return st;
}

/* A string from the minidriver (IOCTL_HID_GET_STRING) as UTF-16 with its NUL */
static NT_STATUS string_ioctl(Coll *c, NT_IRP *irp, int which, UINT32 out_len, NT_ULONG_PTR *info)
{
    UINT8 *out = mdl_buf(irp);
    if (!out || out_len < 2) return ST_INVALID_BUFFER_SIZE;
    NT_ULONG_PTR n = 0;
    NT_STATUS st = mini_ioctl(c->f, IOCTL_HID_GET_STRING, (void *)(NT_ULONG_PTR)(which | 0x0409 << 16), 0, out, out_len,
                              NULL, &n);
    if (!NT_SUCCESS(st)) return st;
    if (n > out_len) n = out_len;
    if (n < 2 || ((NT_WCHAR *)out)[n / 2 - 1]) {             /* (end it) */
        if (n + 2 <= out_len) n += 2;
        ((NT_WCHAR *)out)[n / 2 - 1] = 0;
    }
    *info = n;
    return NT_STATUS_SUCCESS;
}

static NT_STATUS coll_ioctl(Coll *c, NT_IRP *irp, NT_IO_STACK_LOCATION *sl, NT_ULONG_PTR *info)
{
    UINT32 code = sl->Parameters.DeviceIoControl.IoControlCode;
    UINT32 in_len = sl->Parameters.DeviceIoControl.InputBufferLength;
    UINT32 out_len = sl->Parameters.DeviceIoControl.OutputBufferLength;
    void *sys = irp->AssociatedIrp.SystemBuffer;
    File *fl = sl->FileObject ? sl->FileObject->FsContext : NULL;
    Fdo *f = c->f;
    if (c->gone || !f) return ST_DEVICE_NOT_CONNECTED;
    switch (code) {
    case IOCTL_HID_GET_COLLECTION_INFORMATION: {
        if (out_len < sizeof(HidCollectionInformation)) return NT_STATUS_BUFFER_TOO_SMALL;
        HidCollectionInformation *ci = sys;
        ci->DescriptorSize = c->pp_len;
        ci->Polled = f->mini->polled;
        ci->Reserved1[0] = 0;
        ci->VendorID = f->attr.VendorID;
        ci->ProductID = f->attr.ProductID;
        ci->VersionNumber = f->attr.VersionNumber;
        *info = sizeof(*ci);
        return NT_STATUS_SUCCESS;
    }
    case IOCTL_HID_GET_COLLECTION_DESCRIPTOR: {               /* (METHOD_NEITHER: the caller's own buffer) */
        if (out_len < c->pp_len) return NT_STATUS_BUFFER_TOO_SMALL;
        if (irp->RequestorMode) {
            if (!NT_SUCCESS(CopyToUser(irp->UserBuffer, c->pp, c->pp_len))) return NT_STATUS_ACCESS_VIOLATION;
        } else {
            memcpy(irp->UserBuffer, c->pp, c->pp_len);
        }
        *info = c->pp_len;
        return NT_STATUS_SUCCESS;
    }
    case IOCTL_HID_FLUSH_QUEUE: {
        if (!fl) return NT_STATUS_INVALID_PARAMETER;
        IrqState s = spin_lock_irqsave(&g_lock);
        fl->head = fl->count = 0;
        spin_unlock_irqrestore(&g_lock, s);
        return NT_STATUS_SUCCESS;
    }
    case IOCTL_GET_NUM_DEVICE_INPUT_BUFFERS:
        if (!fl) return NT_STATUS_INVALID_PARAMETER;
        if (out_len < 4) return NT_STATUS_BUFFER_TOO_SMALL;
        *(UINT32 *)sys = (UINT32)fl->cap;
        *info = 4;
        return NT_STATUS_SUCCESS;
    case IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS: {
        if (!fl) return NT_STATUS_INVALID_PARAMETER;
        if (in_len < 4) return NT_STATUS_BUFFER_TOO_SMALL;
        UINT32 n = *(UINT32 *)sys;
        if (n < 2 || n > MAX_BUFFERS) return NT_STATUS_INVALID_PARAMETER;
        UINT8 *ring = kzalloc((size_t)n * c->in_len);
        if (!ring) return NT_STATUS_INSUFFICIENT_RESOURCES;
        IrqState s = spin_lock_irqsave(&g_lock);
        int keep = fl->count < (int)n ? fl->count : (int)n;  /* (the newest ones) */
        for (int i = 0; i < keep; i++)
            memcpy(ring + (size_t)i * c->in_len,
                   fl->ring + (size_t)((fl->head + fl->count - keep + i) % fl->cap) * c->in_len, c->in_len);
        UINT8 *old = fl->ring;
        fl->ring = ring;
        fl->cap = (int)n;
        fl->head = 0;
        fl->count = keep;
        spin_unlock_irqrestore(&g_lock, s);
        kfree(old);
        return NT_STATUS_SUCCESS;
    }
    case IOCTL_HID_GET_POLL_FREQUENCY_MSEC:
        if (out_len < 4) return NT_STATUS_BUFFER_TOO_SMALL;
        *(UINT32 *)sys = f->mini->polled ? 8 : 0;
        *info = 4;
        return NT_STATUS_SUCCESS;
    case IOCTL_HID_SET_POLL_FREQUENCY_MSEC:
    case IOCTL_HID_GET_DRIVER_CONFIG:
    case IOCTL_HID_SET_DRIVER_CONFIG:
    case IOCTL_HID_ENABLE_WAKE_ON_SX:
    case IOCTL_HID_SET_S0_IDLE_TIMEOUT:
        return NT_STATUS_SUCCESS;
    case IOCTL_HID_GET_FEATURE: case IOCTL_HID_SET_FEATURE: case IOCTL_HID_GET_INPUT_REPORT:
    case IOCTL_HID_SET_OUTPUT_REPORT: case IOCTL_HID_GET_OUTPUT_REPORT:
        return report_ioctl(c, irp, code, in_len, out_len, info);
    case IOCTL_HID_GET_MANUFACTURER_STRING: return string_ioctl(c, irp, HID_STRING_ID_IMANUFACTURER, out_len, info);
    case IOCTL_HID_GET_PRODUCT_STRING:      return string_ioctl(c, irp, HID_STRING_ID_IPRODUCT, out_len, info);
    case IOCTL_HID_GET_SERIALNUMBER_STRING: return string_ioctl(c, irp, HID_STRING_ID_ISERIALNUMBER, out_len, info);
    case IOCTL_HID_GET_INDEXED_STRING: {
        if (in_len < 4 || !irp->MdlAddress) return NT_STATUS_INVALID_PARAMETER;
        UINT32 index = *(UINT32 *)sys;
        NT_ULONG_PTR n = 0;
        NT_STATUS st = mini_ioctl(f, IOCTL_HID_GET_INDEXED_STRING, (void *)(NT_ULONG_PTR)(index | 0x0409u << 16), 0,
                                  NULL, out_len, irp->MdlAddress, &n);
        if (NT_SUCCESS(st)) *info = n > out_len ? out_len : n;
        return st;
    }
    case IOCTL_GET_PHYSICAL_DESCRIPTOR:
    case IOCTL_HID_GET_MS_GENRE_DESCRIPTOR: {
        NT_ULONG_PTR n = 0;
        NT_STATUS st = mini_ioctl(f, code, NULL, 0, NULL, out_len, irp->MdlAddress, &n);
        if (NT_SUCCESS(st)) *info = n > out_len ? out_len : n;
        return st;
    }
    case IOCTL_HID_GET_HARDWARE_ID: {
        UINT8 *out = mdl_buf(irp);
        char id[64];
        coll_id(c, f, true, id, sizeof(id));
        size_t n = strlen(id);
        if (!out || out_len < 2 * (n + 1)) return ST_INVALID_BUFFER_SIZE;
        for (size_t i = 0; i <= n; i++) ((NT_WCHAR *)out)[i] = (UINT8)id[i];
        *info = 2 * (n + 1);
        return NT_STATUS_SUCCESS;
    }
    }
    return NT_STATUS_INVALID_DEVICE_REQUEST;
}

static NT_STATUS coll_create(Coll *c, NT_IO_STACK_LOCATION *sl)
{
    if (c->gone || !c->f) return ST_DEVICE_NOT_CONNECTED;
    if (!sl->FileObject) return NT_STATUS_INVALID_PARAMETER;
    File *fl = kzalloc(sizeof(*fl));
    UINT8 *ring = kzalloc((size_t)DEF_BUFFERS * (c->in_len ? c->in_len : 1));
    if (!fl || !ring) { kfree(fl); kfree(ring); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    fl->c = c;
    fl->ring = ring;
    fl->cap = DEF_BUFFERS;
    list_init(&fl->reads);
    IrqState s = spin_lock_irqsave(&g_lock);
    list_add_tail(&c->files, &fl->link);
    spin_unlock_irqrestore(&g_lock, s);
    sl->FileObject->FsContext = fl;
    return NT_STATUS_SUCCESS;
}

/* IRP_MJ_READ: whole input reports, or wait for the next one */
static NT_STATUS coll_read(Coll *c, NT_IRP *irp, NT_IO_STACK_LOCATION *sl, bool *pending)
{
    File *fl = sl->FileObject ? sl->FileObject->FsContext : NULL;
    if (!fl) return NT_STATUS_INVALID_PARAMETER;
    if (sl->Parameters.Read.Length < c->in_len || !c->in_len) return ST_INVALID_BUFFER_SIZE;
    NT_STATUS st = NT_STATUS_SUCCESS;
    IrqState s = spin_lock_irqsave(&g_lock);
    if (c->gone) {
        st = ST_DEVICE_NOT_CONNECTED;
    } else if (fl->count) {
        irp->IoStatus.Information = take(fl, irp->AssociatedIrp.SystemBuffer, sl->Parameters.Read.Length);
    } else {
        NtIrpMarkPending(irp);
        list_add_tail(&fl->reads, &irp->Tail.Overlay.ListEntry);
        __atomic_store_n(&irp->CancelRoutine, (void *)read_cancel, __ATOMIC_RELEASE);
        if (irp->Cancel && __atomic_exchange_n(&irp->CancelRoutine, NULL, __ATOMIC_ACQ_REL)) {
            list_del(&irp->Tail.Overlay.ListEntry);           /* (cancelled before it waited) */
            st = NT_STATUS_CANCELLED;
        } else {
            *pending = true;
        }
    }
    spin_unlock_irqrestore(&g_lock, s);
    return st;
}

/* IRP_MJ_WRITE: an output report (its ID first, OutputReportByteLength long) */
static NT_STATUS coll_write(Coll *c, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    UINT32 len = sl->Parameters.Write.Length;
    UINT8 *buf = irp->AssociatedIrp.SystemBuffer;
    if (c->gone || !c->f) return ST_DEVICE_NOT_CONNECTED;
    if (!c->out_len || len != c->out_len || !buf) return ST_INVALID_BUFFER_SIZE;
    if (!has_report(c, 1, buf[0])) return NT_STATUS_INVALID_PARAMETER;
    HidXferPacket pk = { buf, len, buf[0] };
    NT_STATUS st = mini_ioctl(c->f, IOCTL_HID_WRITE_REPORT, NULL, sizeof(pk), &pk, 0, NULL, NULL);
    if (NT_SUCCESS(st)) irp->IoStatus.Information = len;
    return st;
}

static NT_STATUS coll_dispatch(Coll *c, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    NT_STATUS st;
    NT_ULONG_PTR info = 0;
    bool pending = false;
    irp->IoStatus.Information = 0;
    switch (sl->MajorFunction) {
    case NT_IRP_MJ_PNP: {
        bool remove = sl->MinorFunction == NT_IRP_MN_REMOVE_DEVICE;
        st = coll_pnp(c, irp, sl);
        irp->IoStatus.Status = st;
        nt_IofCompleteRequest(irp, 0);
        if (remove) nt_IoDeleteDevice(c->pdo);              /* (handles still open keep it until they close) */
        return st;
    }
    case NT_IRP_MJ_CREATE:  st = coll_create(c, sl); break;
    case NT_IRP_MJ_CLEANUP: {
        File *fl = sl->FileObject ? sl->FileObject->FsContext : NULL;
        NT_LIST_ENTRY done;
        list_init(&done);
        IrqState s = spin_lock_irqsave(&g_lock);
        if (fl) finish_reads(fl, &done, NT_STATUS_CANCELLED);
        spin_unlock_irqrestore(&g_lock, s);
        complete_list(&done);
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MJ_CLOSE: {
        File *fl = sl->FileObject ? sl->FileObject->FsContext : NULL;
        if (fl) {
            IrqState s = spin_lock_irqsave(&g_lock);
            list_del(&fl->link);
            spin_unlock_irqrestore(&g_lock, s);
            kfree(fl->ring);
            kfree(fl);
            sl->FileObject->FsContext = NULL;
        }
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MJ_READ:
        st = coll_read(c, irp, sl, &pending);
        if (pending) return NT_STATUS_PENDING;
        info = irp->IoStatus.Information;
        break;
    case NT_IRP_MJ_WRITE:
        st = coll_write(c, irp, sl);
        info = irp->IoStatus.Information;
        break;
    case NT_IRP_MJ_DEVICE_CONTROL:
    case NT_IRP_MJ_INTERNAL_DEVICE_CONTROL:
        st = coll_ioctl(c, irp, sl, &info);
        break;
    case NT_IRP_MJ_POWER:   st = NT_STATUS_SUCCESS; break;
    case NT_IRP_MJ_SYSTEM_CONTROL: st = irp->IoStatus.Status; break;
    default:                st = NT_STATUS_INVALID_DEVICE_REQUEST; break;
    }
    complete(irp, st, info);
    return st;
}

/* ---- the device (FDO) ---- */

/* The device started: its descriptors, then a PDO per top-level collection */
static NT_STATUS fdo_started(Fdo *f)
{
    HidDescriptor hd;
    NT_ULONG_PTR n = 0;
    memset(&hd, 0, sizeof(hd));
    NT_STATUS st = mini_ioctl(f, IOCTL_HID_GET_DEVICE_DESCRIPTOR, NULL, 0, &hd, sizeof(hd), NULL, &n);
    if (!NT_SUCCESS(st) || hd.bReportType != 0x22 || !hd.wReportLength) {
        kprintf("[HID] no HID descriptor from the minidriver (0x%08x)\n", (UINT32)st);
        return NT_SUCCESS(st) ? NT_STATUS_UNSUCCESSFUL : st;
    }
    f->desc_len = hd.wReportLength;
    f->desc = kmalloc(f->desc_len);
    if (!f->desc) return NT_STATUS_INSUFFICIENT_RESOURCES;
    st = mini_ioctl(f, IOCTL_HID_GET_REPORT_DESCRIPTOR, NULL, 0, f->desc, f->desc_len, NULL, &n);
    if (!NT_SUCCESS(st)) { kprintf("[HID] no report descriptor (0x%08x)\n", (UINT32)st); return st; }
    if (n && n < f->desc_len) f->desc_len = (UINT32)n;
    f->attr.Size = sizeof(f->attr);
    st = mini_ioctl(f, IOCTL_HID_GET_DEVICE_ATTRIBUTES, NULL, 0, &f->attr, sizeof(f->attr), NULL, &n);
    if (!NT_SUCCESS(st)) { kprintf("[HID] no device attributes (0x%08x)\n", (UINT32)st); return st; }

    /* A composite device's function (its PDO USB\VID_v&PID_p&MI_nn): the
     * collections' IDs carry its interface, as Windows' hidclass.sys
     * names them (HID\VID_v&PID_p&MI_nn[&Colnn]) */
    NT_WCHAR hw[170];                                        /* (PnpNode.hwids: 160 characters) */
    UINT32 got = 0;
    f->mi[0] = 0;
    if (NT_SUCCESS(nt_IoGetDeviceProperty(hdx(f)->PhysicalDeviceObject, 1 /* HardwareID */, sizeof(hw), hw, &got))) {
        for (UINT32 i = 0; i + 6 <= got / 2 && hw[i]; i++)
            if (hw[i] == '&' && hw[i + 1] == 'M' && hw[i + 2] == 'I' && hw[i + 3] == '_' && hw[i + 4] && hw[i + 5]) {
                ksnprintf(f->mi, sizeof(f->mi), "&MI_%c%c", (char)hw[i + 4], (char)hw[i + 5]);
                break;
            }
    }

    if (!g_build_init) { nt_KeInitializeMutex(&g_build, 0); g_build_init = true; }
    int ncoll = nova_hidp_collections(f->desc, (int)f->desc_len);
    if (ncoll > MAX_COLL) ncoll = MAX_COLL;
    for (int i = 0; i < ncoll; i++) {
        nt_KeWaitForSingleObject(&g_build, 0, 0, 0, NULL);
        UINT len = nova_hidp_build_tlc(f->desc, (int)f->desc_len, i, NULL, 0);
        NovaHidP *pp = len ? kmalloc(len) : NULL;
        if (pp) nova_hidp_build_tlc(f->desc, (int)f->desc_len, i, pp, len);
        nt_KeReleaseMutex(&g_build, 0);
        if (!pp) continue;
        NT_DEVICE_OBJECT *pdo;
        st = nt_IoCreateDevice(f->mini->drv, sizeof(Coll), NULL, NT_FILE_DEVICE_UNKNOWN, 0x100 /* AUTOGENERATED_NAME */, 0,
                               &pdo);
        if (!NT_SUCCESS(st)) { kfree(pp); continue; }
        pdo->Flags = (pdo->Flags & ~NT_DO_DEVICE_INITIALIZING) | NT_DO_BUFFERED_IO | NT_DO_POWER_PAGABLE;
        Coll *c = pdo->DeviceExtension;
        memset(c, 0, sizeof(*c));
        c->magic = COLL_MAGIC;
        c->pdo = pdo;
        c->f = f;
        c->index = f->ncoll;
        c->pp = pp;
        c->pp_len = len;
        c->in_len = pp->rlen[0];
        c->out_len = pp->rlen[1];
        c->feat_len = pp->rlen[2];
        list_init(&c->files);
        for (int k = 0; k < pp->ncaps; k++) if (pp->caps[k].id) f->ids = true;
        if (c->in_len > f->max_in) f->max_in = c->in_len;
        f->coll[f->ncoll++] = c;
        kprintf("[HID] %04x:%04x collection %d: usage %04x:%04x, reports in %u out %u feature %u\n",
                f->attr.VendorID, f->attr.ProductID, c->index, pp->page, pp->usage, c->in_len, c->out_len, c->feat_len);
    }
    if (!f->ncoll) { kprintf("[HID] %04x:%04x: no top-level collection\n", f->attr.VendorID, f->attr.ProductID); return NT_STATUS_UNSUCCESSFUL; }
    if (f->max_in) {
        nt_KeInitializeEvent(&f->pump_done, NT_NotificationEvent, 0);
        f->stopping = false;
        f->pump = sched_create_thread_ex("hid-read", pump, f, 9, 16 * 1024) != NULL;
    }
    return NT_STATUS_SUCCESS;
}

/* Its collections go (the device is removed) */
static void fdo_drop(Fdo *f)
{
    pump_stop(f);
    for (int i = 0; i < MAX_COLL; i++) {
        IrqState s = spin_lock_irqsave(&g_lock);
        Coll *c = f->coll[i];
        spin_unlock_irqrestore(&g_lock, s);
        if (c) coll_gone(c);
    }
    f->ncoll = 0;
}

static NT_STATUS fdo_pnp(Fdo *f, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    NT_STATUS st;
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE:
        st = forward_sync(f, irp);
        if (NT_SUCCESS(st)) st = fdo_started(f);
        complete(irp, st, irp->IoStatus.Information);
        return st;
    case NT_IRP_MN_QUERY_DEVICE_RELATIONS:
        if ((int)(UINT64)sl->Parameters.Others.Argument1 == 0) {            /* BusRelations: the collections */
            struct { unsigned int Count; NT_DEVICE_OBJECT *Objects[MAX_COLL]; } *r =
                nt_ExAllocatePoolWithTag(1, sizeof(*r), TAG_HID);
            if (!r) { complete(irp, NT_STATUS_INSUFFICIENT_RESOURCES, 0); return NT_STATUS_INSUFFICIENT_RESOURCES; }
            r->Count = 0;
            IrqState s = spin_lock_irqsave(&g_lock);
            for (int i = 0; i < f->ncoll; i++) if (f->coll[i] && !f->coll[i]->gone) r->Objects[r->Count++] = f->coll[i]->pdo;
            spin_unlock_irqrestore(&g_lock, s);
            complete(irp, NT_STATUS_SUCCESS, (NT_ULONG_PTR)r);
            return NT_STATUS_SUCCESS;
        }
        break;
    case NT_IRP_MN_SURPRISE_REMOVAL:
        fdo_drop(f);
        break;
    case NT_IRP_MN_REMOVE_DEVICE: {
        fdo_drop(f);
        st = forward_sync(f, irp);
        complete(irp, st, 0);
        IrqState s = spin_lock_irqsave(&g_lock);
        for (int i = 0; i < MAX_FDO; i++) if (g_fdo[i] == f) g_fdo[i] = NULL;
        spin_unlock_irqrestore(&g_lock, s);
        NT_DEVICE_OBJECT *fdo = f->fdo;
        if (hdx(f)->NextDeviceObject) nt_IoDetachDevice(hdx(f)->NextDeviceObject);
        kfree(f->desc);
        kfree(f);
        nt_IoDeleteDevice(fdo);
        return st;
    }
    }
    copy_down(irp);                                          /* (the rest: the minidriver's) */
    return call_mini(f, irp);
}

static NT_API NT_STATUS hid_dispatch(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    Fdo *f = fdo_of(dev);
    spin_unlock_irqrestore(&g_lock, s);
    if (!f) {
        Coll *c = coll_of(dev);
        if (c) return coll_dispatch(c, irp);
        complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST, 0);
        return NT_STATUS_INVALID_DEVICE_REQUEST;
    }
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    switch (sl->MajorFunction) {
    case NT_IRP_MJ_PNP:
        return fdo_pnp(f, irp, sl);
    case NT_IRP_MJ_POWER:
    case NT_IRP_MJ_SYSTEM_CONTROL:
        copy_down(irp);
        return call_mini(f, irp);
    }
    complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST, 0);      /* (the device itself is not opened: its collections are) */
    return NT_STATUS_INVALID_DEVICE_REQUEST;
}

/* AddDevice for a minidriver's device: the FDO, then the minidriver's own */
static NT_API NT_STATUS hid_add_device(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *pdo)
{
    Mini *m = mini_of(drv);
    if (!m) return NT_STATUS_INVALID_DEVICE_REQUEST;
    Fdo *f = kzalloc(sizeof(*f));
    if (!f) return NT_STATUS_INSUFFICIENT_RESOURCES;
    int slot = -1;
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < MAX_FDO && slot < 0; i++) if (!g_fdo[i]) slot = i;
    spin_unlock_irqrestore(&g_lock, s);
    if (slot < 0) { kfree(f); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    NT_DEVICE_OBJECT *fdo;
    UINT32 ext = (UINT32)sizeof(HidDeviceExtension) + ((m->ext_size + 7) & ~7u);
    NT_STATUS st = nt_IoCreateDevice(drv, ext, NULL, NT_FILE_DEVICE_UNKNOWN, 0x100 /* AUTOGENERATED_NAME */, 0, &fdo);
    if (!NT_SUCCESS(st)) { kfree(f); return st; }
    HidDeviceExtension *x = fdo->DeviceExtension;
    x->PhysicalDeviceObject = pdo;
    x->MiniDeviceExtension = x + 1;
    x->NextDeviceObject = nt_IoAttachDeviceToDeviceStack(fdo, pdo);
    if (!x->NextDeviceObject) { nt_IoDeleteDevice(fdo); kfree(f); return NT_STATUS_UNSUCCESSFUL; }
    fdo->StackSize = (char)(x->NextDeviceObject->StackSize + 2);     /* (hidclass's location, then the minidriver's) */
    fdo->Flags |= NT_DO_POWER_PAGABLE;
    f->fdo = fdo;
    f->mini = m;
    s = spin_lock_irqsave(&g_lock);
    g_fdo[slot] = f;
    spin_unlock_irqrestore(&g_lock, s);
    st = m->add(drv, fdo);
    if (!NT_SUCCESS(st)) {
        s = spin_lock_irqsave(&g_lock);
        g_fdo[slot] = NULL;
        spin_unlock_irqrestore(&g_lock, s);
        nt_IoDetachDevice(x->NextDeviceObject);
        nt_IoDeleteDevice(fdo);
        kfree(f);
        return st;
    }
    fdo->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    return NT_STATUS_SUCCESS;
}

/* HidRegisterMinidriver (hidclass.sys): called from the minidriver's
 * DriverEntry, once its AddDevice and dispatch routines are set */
NT_API NT_STATUS nt_HidRegisterMinidriver(HidMinidriverRegistration *r)
{
    if (!r || r->Revision > HID_REVISION || !r->DriverObject) return ST_REVISION_MISMATCH;
    NT_DRIVER_OBJECT *drv = r->DriverObject;
    IrqState s = spin_lock_irqsave(&g_lock);
    Mini *m = mini_of(drv);
    for (int i = 0; i < MAX_MINI && !m; i++) if (!g_mini[i].drv || !NtIoDriverOf(g_mini[i].drv)) m = &g_mini[i];
    if (!m) { spin_unlock_irqrestore(&g_lock, s); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    if (m->drv != drv) {
        memset(m, 0, sizeof(*m));
        m->drv = drv;
        m->add = drv->DriverExtension->AddDevice;
        for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) m->major[i] = drv->MajorFunction[i];
    }
    m->ext_size = r->DeviceExtensionSize;
    m->polled = r->DevicesArePolled != 0;
    spin_unlock_irqrestore(&g_lock, s);
    drv->DriverExtension->AddDevice = hid_add_device;
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) drv->MajorFunction[i] = hid_dispatch;
    char name[64];
    int k = 0;
    for (int i = 0; i < drv->DriverName.Length / 2 && k < 63; i++) name[k++] = (char)drv->DriverName.Buffer[i];
    name[k] = 0;
    kprintf("[HID] %s registered as a HID minidriver%s\n", name, m->polled ? " (polled)" : "");
    return NT_STATUS_SUCCESS;
}

/* HidNotifyPresence: a virtual device's minidriver says the device came or
 * went; gone, its collections are reported missing */
NT_API NT_STATUS nt_HidNotifyPresence(NT_DEVICE_OBJECT *fdo, UINT8 present)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    Fdo *f = fdo_of(fdo);
    spin_unlock_irqrestore(&g_lock, s);
    if (!f) return NT_STATUS_INVALID_PARAMETER;
    if (!present) {
        for (int i = 0; i < f->ncoll; i++) if (f->coll[i]) f->coll[i]->gone = true;
        nt_IoInvalidateDeviceRelations(hdx(f)->PhysicalDeviceObject, 0);
    }
    return NT_STATUS_SUCCESS;
}
