/*
 * WUDFRd.sys — the reflector: the kernel side of a UMDF 2 driver
 *
 * A device whose driver is a UMDF driver has WUDFRd as its kernel
 * function driver, as on Windows (the INF's AddService=WUDFRd): the PnP
 * manager loads it, calls AddDevice, which makes the function device
 * object above the bus driver's PDO and registers the device's host
 * channel (the interface WUDFRD_HOST_GUID, reference string "WudfHost"),
 * and starts it with the device's resources.  The UMDF driver itself runs
 * in a user-mode host process, WUDFHost.exe (tools/umdf/host), which the
 * driver manager service (wudfsvc) starts for the device once it has
 * started; the host opens the channel and from then on the reflector
 * hands it, as messages (tools/umdf/wudfrd.h), the device's Plug and Play
 * events and every request programs make of the device: create, read,
 * write, device control, cleanup and close.  Each such request stays
 * pending here until the host replies, and completes with the host's
 * status and data.  The device interfaces the UMDF driver registers and
 * turns on (WdfDeviceCreateDeviceInterface) are registered here, on the
 * device's PDO, at the host's request.
 *
 * IRP_MN_START_DEVICE completes once the bus driver has started the
 * device; the host starts the UMDF driver after (WDFDEVICE's
 * EvtDevicePrepareHardware with the same resources, EvtDeviceD0Entry), and
 * the driver's interfaces come on only then.  Query-remove asks the host
 * (the driver may veto); remove and surprise removal tell it, wait for it
 * (10 s at most), and the host exits.  If the host goes, every request
 * waiting on it fails with STATUS_DEVICE_NOT_CONNECTED.
 *
 * Programs' requests are buffered (DO_BUFFERED_IO; a device control's
 * METHOD_BUFFERED), with WUDFRD_MAX_DATA bytes of data at most; the other
 * transfer methods fail with STATUS_NOT_SUPPORTED.
 *
 * Built by tools/umdf/build_umdf.py with the kernel's export list, as
 * tools/drivertest/mksys.py builds NovaOS's test drivers, and test-signed
 * as Wdf01000.sys is (docs/drivers.md).
 */
#include "../../kernel/nt/wdm.h"
#include "wudfrd.h"

#define IMPORT __declspec(dllimport) NT_API
IMPORT NT_STATUS IoCreateDevice(NT_DRIVER_OBJECT *drv, unsigned int ext_size, NT_UNICODE_STRING *name, unsigned int type,
                                unsigned int characteristics, unsigned char exclusive, NT_DEVICE_OBJECT **out);
IMPORT void      IoDeleteDevice(NT_DEVICE_OBJECT *dev);
IMPORT NT_DEVICE_OBJECT *IoAttachDeviceToDeviceStack(NT_DEVICE_OBJECT *src, NT_DEVICE_OBJECT *target);
IMPORT void      IoDetachDevice(NT_DEVICE_OBJECT *target);
IMPORT NT_STATUS IofCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp);
IMPORT void      IofCompleteRequest(NT_IRP *irp, char boost);
IMPORT NT_STATUS IoRegisterDeviceInterface(NT_DEVICE_OBJECT *pdo, const NT_GUID *guid, NT_UNICODE_STRING *ref,
                                           NT_UNICODE_STRING *link);
IMPORT NT_STATUS IoSetDeviceInterfaceState(NT_UNICODE_STRING *link, unsigned char enable);
IMPORT unsigned long long IoGetRequestorProcessId(NT_IRP *irp);
IMPORT void      PoStartNextPowerIrp(NT_IRP *irp);
IMPORT NT_STATUS PoCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp);
IMPORT void      RtlFreeUnicodeString(NT_UNICODE_STRING *u);
IMPORT unsigned int DbgPrint(const char *fmt, ...);
IMPORT void      KeInitializeEvent(NT_KEVENT *ev, int type, unsigned char state);
IMPORT int       KeSetEvent(NT_KEVENT *ev, int increment, unsigned char wait);
IMPORT NT_STATUS KeWaitForSingleObject(void *obj, int reason, char mode, unsigned char alertable, long long *timeout);
IMPORT void      KeInitializeSpinLock(NT_KSPIN_LOCK *l);
IMPORT unsigned char KeAcquireSpinLockRaiseToDpc(NT_KSPIN_LOCK *l);
IMPORT void      KeReleaseSpinLock(NT_KSPIN_LOCK *l, unsigned char irql);
IMPORT void     *ExAllocatePoolWithTag(int type, unsigned long long size, unsigned int tag);
IMPORT void      ExFreePoolWithTag(void *p, unsigned int tag);
IMPORT void     *memcpy(void *d, const void *s, unsigned long long n);
IMPORT void     *memset(void *d, int c, unsigned long long n);
IMPORT int       memcmp(const void *a, const void *b, unsigned long long n);

#define NonPagedPoolNx               512
#define TAG                          0x64526657u      /* 'WfRd' */
#define FILE_DEVICE_UNKNOWN          0x22
#define FILE_DEVICE_SECURE_OPEN      0x100
#define STATUS_DEVICE_BUSY           ((NT_STATUS)0x80000011)
#define STATUS_NO_SUCH_DEVICE        ((NT_STATUS)0xC000000E)
#define STATUS_DEVICE_NOT_CONNECTED  ((NT_STATUS)0xC000009D)
#define MAX_IFACES                   8
#define PNP_TIMEOUT                  (-100000000LL)  /* 10 s (100 ns units, relative) */

/* A message for the host: queued until a GET takes it, then sent until
 * the reply comes.  For a program's request @irp is the request; a Plug
 * and Play message has a waiter instead (@done, on the waiter's stack). */
typedef struct {
    NT_LIST_ENTRY      link;
    int                list;                   /* 0 on none, 1 queued, 2 sent */
    NT_IRP            *irp;
    NT_KEVENT         *done;
    NT_STATUS          status;                 /* (the waiter's: the host's reply) */
    unsigned int       size;                   /* the message's bytes: header and data */
    WUDFRD_MSG         msg;                    /* (then its data) */
} Rec;

typedef struct {
    NT_GUID            guid;
    unsigned short     ref[64];
} Iface;

typedef struct {
    NT_DEVICE_OBJECT  *self, *pdo, *lower;
    NT_KSPIN_LOCK      lock;
    NT_LIST_ENTRY      queued, sent, gets;     /* Recs, Recs, the host's pending GETs */
    NT_FILE_OBJECT    *host;                   /* the host's handle (IOCTL_WUDFRD_HELLO) */
    NT_UNICODE_STRING  host_link;
    NT_CM_RESOURCE_LIST *raw, *xlat;           /* (copies) */
    unsigned int       raw_len, xlat_len;
    int                started, removed;
    unsigned long long next_id;
    Iface              on[MAX_IFACES];         /* the driver's interfaces that are on */
    int                n_on;
} Ext;

/* FILE_OBJECT.FsContext: what an open of the device is */
#define OPEN_HOST ((void *)1)                  /* the host channel */
#define OPEN_APP  ((void *)2)                  /* a program's open, served by the host */

static const NT_GUID g_host_guid = WUDFRD_HOST_GUID_INIT;

static void list_init(NT_LIST_ENTRY *h) { h->Flink = h->Blink = h; }
static int  list_empty(NT_LIST_ENTRY *h) { return h->Flink == h; }
static void list_add(NT_LIST_ENTRY *h, NT_LIST_ENTRY *e)
{
    e->Flink = h;
    e->Blink = h->Blink;
    h->Blink->Flink = e;
    h->Blink = e;
}
static void list_del(NT_LIST_ENTRY *e)
{
    e->Blink->Flink = e->Flink;
    e->Flink->Blink = e->Blink;
    e->Flink = e->Blink = e;
}
static NT_LIST_ENTRY *list_pop(NT_LIST_ENTRY *h)
{
    if (list_empty(h)) return 0;
    NT_LIST_ENTRY *e = h->Flink;
    list_del(e);
    return e;
}
#define REC(e)   ((Rec *)((char *)(e) - __builtin_offsetof(Rec, link)))
#define IRP_OF(e) ((NT_IRP *)((char *)(e) - __builtin_offsetof(NT_IRP, Tail.Overlay.ListEntry)))

static NT_STATUS complete(NT_IRP *irp, NT_STATUS st, unsigned long long info)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    IofCompleteRequest(irp, 0);
    return st;
}

static void skip(NT_IRP *irp)
{
    irp->CurrentLocation++;
    irp->Tail.Overlay.CurrentStackLocation++;
}

static NT_API NT_STATUS start_done(NT_DEVICE_OBJECT *dev, NT_IRP *irp, void *ctx)
{
    (void)dev; (void)irp;
    KeSetEvent(ctx, 0, 0);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;
}

/* Send @irp down and wait for the bus driver to finish it */
static NT_STATUS forward_and_wait(Ext *x, NT_IRP *irp)
{
    NT_KEVENT ev;
    KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    NT_IO_STACK_LOCATION *cur = NtIrpCurrentStack(irp), *next = NtIrpNextStack(irp);
    memcpy(next, cur, __builtin_offsetof(NT_IO_STACK_LOCATION, CompletionRoutine));
    next->CompletionRoutine = start_done;
    next->Context = &ev;
    next->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    if (IofCallDriver(x->lower, irp) == NT_STATUS_PENDING) KeWaitForSingleObject(&ev, 0, 0, 0, 0);
    return irp->IoStatus.Status;
}

static NT_STATUS pass_down(Ext *x, NT_IRP *irp)
{
    skip(irp);
    return IofCallDriver(x->lower, irp);
}

static int is_host_name(const NT_UNICODE_STRING *n)
{
    static const char want[] = "\\WudfHost";
    unsigned int len = n->Buffer ? n->Length / 2 : 0;
    if (len != sizeof(want) - 1) return 0;
    for (unsigned int i = 0; i < len; i++) {
        unsigned short c = n->Buffer[i];
        char w = want[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned short)(c + 32);
        if (w >= 'A' && w <= 'Z') w = (char)(w + 32);
        if (c != (unsigned char)w) return 0;
    }
    return 1;
}

/* ---- messages ---- */

static Rec *rec_new(unsigned int data_len)
{
    Rec *r = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(Rec) + data_len, TAG);
    if (!r) return 0;
    memset(r, 0, sizeof(Rec));
    list_init(&r->link);
    r->size = sizeof(WUDFRD_MSG) + data_len;
    r->msg.data_len = data_len;
    return r;
}

/* Hand @r to the host's GET @get (lock not held) */
static void deliver(NT_IRP *get, Rec *r)
{
    memcpy(get->AssociatedIrp.SystemBuffer, &r->msg, r->size);
    complete(get, NT_STATUS_SUCCESS, r->size);
}

/* Queue @r for the host, or give it to a GET that is waiting.  FALSE when
 * there is no host (nothing queued). */
static int post(Ext *x, Rec *r)
{
    NT_IRP *get = 0;
    unsigned char irql = KeAcquireSpinLockRaiseToDpc(&x->lock);
    if (!x->host) {
        KeReleaseSpinLock(&x->lock, irql);
        return 0;
    }
    r->msg.id = ++x->next_id;
    NT_LIST_ENTRY *e = list_pop(&x->gets);
    if (e) {
        get = IRP_OF(e);
        r->list = 2;
        list_add(&x->sent, &r->link);
    } else {
        r->list = 1;
        list_add(&x->queued, &r->link);
    }
    KeReleaseSpinLock(&x->lock, irql);
    if (get) deliver(get, r);
    return 1;
}

/* Send a Plug and Play message and wait for the host's reply: its status,
 * or STATUS_SUCCESS with no host or when it does not answer in time */
static NT_STATUS ask_host(Ext *x, unsigned int type)
{
    Rec *r = rec_new(0);
    if (!r) return NT_STATUS_SUCCESS;
    NT_KEVENT done;
    KeInitializeEvent(&done, NT_NotificationEvent, 0);
    r->msg.type = type;
    r->done = &done;
    r->status = NT_STATUS_SUCCESS;
    if (!post(x, r)) {
        ExFreePoolWithTag(r, TAG);
        return NT_STATUS_SUCCESS;
    }
    long long timeout = PNP_TIMEOUT;
    NT_STATUS w = KeWaitForSingleObject(&done, 0, 0, 0, &timeout);
    unsigned char irql = KeAcquireSpinLockRaiseToDpc(&x->lock);
    if (r->list) {                              /* (no reply: it is still the host's) */
        list_del(&r->link);
        r->list = 0;
    }
    KeReleaseSpinLock(&x->lock, irql);
    NT_STATUS st = r->status;
    if (w == NT_STATUS_TIMEOUT) DbgPrint("WUDFRd: the host did not answer message %u", type);
    ExFreePoolWithTag(r, TAG);
    return st;
}

/* The host is gone: whatever waited on it fails */
static void host_gone(Ext *x, NT_FILE_OBJECT *fo)
{
    NT_LIST_ENTRY recs, gets;
    list_init(&recs);
    list_init(&gets);
    unsigned char irql = KeAcquireSpinLockRaiseToDpc(&x->lock);
    if (fo && x->host != fo) {
        KeReleaseSpinLock(&x->lock, irql);
        return;
    }
    int had = x->host != 0;
    x->host = 0;
    for (NT_LIST_ENTRY *e; (e = list_pop(&x->queued)) || (e = list_pop(&x->sent));) {
        Rec *r = REC(e);
        r->list = 0;
        if (r->done) {
            r->status = NT_STATUS_SUCCESS;
            KeSetEvent(r->done, 0, 0);
        } else {
            list_add(&recs, e);
        }
    }
    for (NT_LIST_ENTRY *e; (e = list_pop(&x->gets));) list_add(&gets, e);
    KeReleaseSpinLock(&x->lock, irql);
    for (NT_LIST_ENTRY *e; (e = list_pop(&recs));) {
        Rec *r = REC(e);
        if (r->irp) complete(r->irp, STATUS_DEVICE_NOT_CONNECTED, 0);
        ExFreePoolWithTag(r, TAG);
    }
    for (NT_LIST_ENTRY *e; (e = list_pop(&gets));) complete(IRP_OF(e), NT_STATUS_CANCELLED, 0);
    if (had) DbgPrint("WUDFRd: the host is gone");
}

/* The START message: the device's resources, raw then translated */
static void post_start(Ext *x)
{
    Rec *r = rec_new(x->raw_len + x->xlat_len);
    if (!r) return;
    r->msg.type = WUDFRD_MSG_START;
    r->msg.raw_len = x->raw_len;
    unsigned char *d = (unsigned char *)(&r->msg + 1);
    if (x->raw_len) memcpy(d, x->raw, x->raw_len);
    if (x->xlat_len) memcpy(d + x->raw_len, x->xlat, x->xlat_len);
    if (!post(x, r)) ExFreePoolWithTag(r, TAG);
}

/* ---- the host channel's requests ---- */

static NT_STATUS host_control(Ext *x, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    unsigned int code = sl->Parameters.DeviceIoControl.IoControlCode;
    unsigned int in_len = sl->Parameters.DeviceIoControl.InputBufferLength;
    unsigned int out_len = sl->Parameters.DeviceIoControl.OutputBufferLength;
    void *buf = irp->AssociatedIrp.SystemBuffer;
    switch (code) {
    case IOCTL_WUDFRD_HELLO: {
        if (out_len < sizeof(WUDFRD_HELLO)) return complete(irp, NT_STATUS_BUFFER_TOO_SMALL, 0);
        unsigned char irql = KeAcquireSpinLockRaiseToDpc(&x->lock);
        int busy = x->host && x->host != sl->FileObject;
        if (!busy) x->host = sl->FileObject;
        int started = x->started;
        KeReleaseSpinLock(&x->lock, irql);
        if (busy) return complete(irp, STATUS_DEVICE_BUSY, 0);
        DbgPrint("WUDFRd: host connected (process %llu)", IoGetRequestorProcessId(irp));
        if (started) post_start(x);
        WUDFRD_HELLO *h = buf;
        h->version = WUDFRD_VERSION;
        h->started = (unsigned int)started;
        return complete(irp, NT_STATUS_SUCCESS, sizeof(*h));
    }
    case IOCTL_WUDFRD_GET: {
        if (out_len < sizeof(WUDFRD_MSG) + WUDFRD_MAX_DATA) return complete(irp, NT_STATUS_BUFFER_TOO_SMALL, 0);
        Rec *r = 0;
        unsigned char irql = KeAcquireSpinLockRaiseToDpc(&x->lock);
        if (!x->host) {                         /* (any of the host's channel handles; HELLO first) */
            KeReleaseSpinLock(&x->lock, irql);
            return complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST, 0);
        }
        NT_LIST_ENTRY *e = list_pop(&x->queued);
        if (e) {
            r = REC(e);
            r->list = 2;
            list_add(&x->sent, e);
        } else {
            NtIrpMarkPending(irp);
            list_add(&x->gets, &irp->Tail.Overlay.ListEntry);
        }
        KeReleaseSpinLock(&x->lock, irql);
        if (!r) return NT_STATUS_PENDING;
        deliver(irp, r);
        return NT_STATUS_SUCCESS;
    }
    case IOCTL_WUDFRD_REPLY: {
        WUDFRD_REPLY *rp = buf;
        if (in_len < sizeof(*rp) || rp->data_len > in_len - sizeof(*rp)) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
        Rec *r = 0;
        unsigned char irql = KeAcquireSpinLockRaiseToDpc(&x->lock);
        for (NT_LIST_ENTRY *e = x->sent.Flink; e != &x->sent; e = e->Flink)
            if (REC(e)->msg.id == rp->id) { r = REC(e); break; }
        if (r) {
            list_del(&r->link);
            r->list = 0;
            if (r->done) {                      /* (a Plug and Play message: its waiter frees it) */
                r->status = rp->status;
                KeSetEvent(r->done, 0, 0);
                r = 0;
            }
        }
        KeReleaseSpinLock(&x->lock, irql);
        if (r && !r->irp) {                     /* (START, which nobody waits for) */
            if (!NT_SUCCESS_(rp->status)) DbgPrint("WUDFRd: the UMDF driver failed to start (%08x)", rp->status);
            ExFreePoolWithTag(r, TAG);
            r = 0;
        }
        if (r) {
            NT_IRP *app = r->irp;
            NT_IO_STACK_LOCATION *asl = NtIrpCurrentStack(app);
            unsigned long long info = rp->information;
            if (r->msg.type == WUDFRD_MSG_READ || r->msg.type == WUDFRD_MSG_IOCTL) {
                unsigned int cap = r->msg.type == WUDFRD_MSG_READ ? asl->Parameters.Read.Length
                                                                  : asl->Parameters.DeviceIoControl.OutputBufferLength;
                unsigned int n = rp->data_len < cap ? rp->data_len : cap;
                if (n) memcpy(app->AssociatedIrp.SystemBuffer, rp + 1, n);
                if (info > n) info = n;
            } else if (r->msg.type == WUDFRD_MSG_WRITE) {
                if (info > r->msg.in_len) info = r->msg.in_len;
            } else {
                info = 0;
            }
            complete(app, rp->status, info);
            ExFreePoolWithTag(r, TAG);
        }
        return complete(irp, NT_STATUS_SUCCESS, 0);
    }
    case IOCTL_WUDFRD_IFACE: {
        WUDFRD_IFACE *f = buf;
        if (in_len < sizeof(*f)) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
        NT_GUID g;
        memcpy(&g, f->guid, sizeof(g));
        f->ref[63] = 0;
        unsigned short rl = 0;
        while (f->ref[rl]) rl++;
        NT_UNICODE_STRING ref = { (unsigned short)(rl * 2), (unsigned short)(rl * 2 + 2), f->ref }, link;
        NT_STATUS st = IoRegisterDeviceInterface(x->pdo, &g, rl ? &ref : 0, &link);
        if (!NT_SUCCESS_(st)) return complete(irp, st, 0);
        unsigned long long info = 0;
        if (f->op == WUDFRD_IFACE_REGISTER) {
            unsigned int n = link.Length + 2u;
            if (out_len < n) {
                RtlFreeUnicodeString(&link);
                return complete(irp, NT_STATUS_BUFFER_TOO_SMALL, 0);
            }
            memcpy(buf, link.Buffer, link.Length);
            ((unsigned short *)buf)[link.Length / 2] = 0;
            info = n;
        } else {
            int on = f->op == WUDFRD_IFACE_ENABLE;
            Iface keep;
            keep.guid = g;
            memset(keep.ref, 0, sizeof(keep.ref));
            memcpy(keep.ref, f->ref, rl * 2u);
            st = IoSetDeviceInterfaceState(&link, (unsigned char)on);
            unsigned char irql = KeAcquireSpinLockRaiseToDpc(&x->lock);
            int i = 0;
            while (i < x->n_on && (memcmp(&x->on[i], &keep, sizeof(keep)))) i++;
            if (on && i == x->n_on && x->n_on < MAX_IFACES && NT_SUCCESS_(st)) x->on[x->n_on++] = keep;
            if (!on && i < x->n_on) x->on[i] = x->on[--x->n_on];
            KeReleaseSpinLock(&x->lock, irql);
        }
        RtlFreeUnicodeString(&link);
        return complete(irp, st, info);
    }
    }
    return complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST, 0);
}

/* ---- programs' requests: to the host ---- */

static NT_STATUS to_host(Ext *x, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    unsigned int type = 0, data_len = 0;
    const void *data = 0;
    switch (sl->MajorFunction) {
    case NT_IRP_MJ_CREATE:
        type = WUDFRD_MSG_CREATE;
        data = sl->FileObject->FileName.Buffer;
        data_len = sl->FileObject->FileName.Buffer ? sl->FileObject->FileName.Length : 0;
        break;
    case NT_IRP_MJ_CLEANUP: type = WUDFRD_MSG_CLEANUP; break;
    case NT_IRP_MJ_CLOSE:   type = WUDFRD_MSG_CLOSE; break;
    case NT_IRP_MJ_READ:
        type = WUDFRD_MSG_READ;
        if (sl->Parameters.Read.Length > WUDFRD_MAX_DATA) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
        break;
    case NT_IRP_MJ_WRITE:
        type = WUDFRD_MSG_WRITE;
        data = irp->AssociatedIrp.SystemBuffer;
        data_len = sl->Parameters.Write.Length;
        if (data_len > WUDFRD_MAX_DATA) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
        break;
    case NT_IRP_MJ_DEVICE_CONTROL:
        type = WUDFRD_MSG_IOCTL;
        if ((sl->Parameters.DeviceIoControl.IoControlCode & 3) != NT_METHOD_BUFFERED)
            return complete(irp, NT_STATUS_NOT_SUPPORTED, 0);
        data = irp->AssociatedIrp.SystemBuffer;
        data_len = sl->Parameters.DeviceIoControl.InputBufferLength;
        if (data_len > WUDFRD_MAX_DATA || sl->Parameters.DeviceIoControl.OutputBufferLength > WUDFRD_MAX_DATA)
            return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
        break;
    default:
        return complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST, 0);
    }
    if (type == WUDFRD_MSG_CREATE && (!x->started || x->removed)) return complete(irp, STATUS_NO_SUCH_DEVICE, 0);
    Rec *r = rec_new(data_len);
    if (!r) return complete(irp, NT_STATUS_INSUFFICIENT_RESOURCES, 0);
    r->irp = irp;
    r->msg.type = type;
    r->msg.file = (unsigned long long)sl->FileObject;
    r->msg.pid = (unsigned int)IoGetRequestorProcessId(irp);
    r->msg.in_len = data_len;
    if (data_len) memcpy(&r->msg + 1, data, data_len);
    if (type == WUDFRD_MSG_READ) {
        r->msg.out_len = sl->Parameters.Read.Length;
        r->msg.offset = sl->Parameters.Read.ByteOffset;
    } else if (type == WUDFRD_MSG_WRITE) {
        r->msg.offset = sl->Parameters.Write.ByteOffset;
    } else if (type == WUDFRD_MSG_IOCTL) {
        r->msg.code = sl->Parameters.DeviceIoControl.IoControlCode;
        r->msg.out_len = sl->Parameters.DeviceIoControl.OutputBufferLength;
    } else if (type == WUDFRD_MSG_CREATE) {
        r->msg.options = sl->Parameters.Create.Options;
        r->msg.share = sl->Parameters.Create.ShareAccess;
        unsigned int *sec = sl->Parameters.Create.SecurityContext;   /* (SecurityQos, AccessState, DesiredAccess) */
        r->msg.access = sec ? *(unsigned int *)((char *)sec + 16) : 0;
    }
    NtIrpMarkPending(irp);
    if (!post(x, r)) {
        ExFreePoolWithTag(r, TAG);
        /* (no host: a create fails; cleanup and close of an open the host had succeed) */
        complete(irp, type == WUDFRD_MSG_CLEANUP || type == WUDFRD_MSG_CLOSE ? NT_STATUS_SUCCESS : STATUS_DEVICE_NOT_CONNECTED, 0);
    }
    return NT_STATUS_PENDING;
}

static NT_API NT_STATUS dispatch_io(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    NT_FILE_OBJECT *fo = sl->FileObject;
    if (sl->MajorFunction == NT_IRP_MJ_CREATE) {
        if (is_host_name(&fo->FileName)) {
            fo->FsContext = OPEN_HOST;
            return complete(irp, NT_STATUS_SUCCESS, 0);
        }
        fo->FsContext = OPEN_APP;
        return to_host(x, irp, sl);
    }
    if (fo && fo->FsContext == OPEN_HOST) {
        if (sl->MajorFunction == NT_IRP_MJ_DEVICE_CONTROL) return host_control(x, irp, sl);
        if (sl->MajorFunction == NT_IRP_MJ_CLEANUP) host_gone(x, fo);
        if (sl->MajorFunction == NT_IRP_MJ_CLEANUP || sl->MajorFunction == NT_IRP_MJ_CLOSE)
            return complete(irp, NT_STATUS_SUCCESS, 0);
        return complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST, 0);
    }
    return to_host(x, irp, sl);
}

/* ---- Plug and Play ---- */

static unsigned int resources_size(const NT_CM_RESOURCE_LIST *l)
{
    if (!l) return 0;
    const unsigned char *p = (const unsigned char *)l->List;
    for (unsigned int i = 0; i < l->Count; i++) {
        const NT_CM_FULL_RESOURCE_DESCRIPTOR *f = (const NT_CM_FULL_RESOURCE_DESCRIPTOR *)p;
        p += __builtin_offsetof(NT_CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList.PartialDescriptors) +
             f->PartialResourceList.Count * sizeof(NT_CM_PARTIAL_RESOURCE_DESCRIPTOR);
    }
    return (unsigned int)(p - (const unsigned char *)l);
}

static NT_CM_RESOURCE_LIST *resources_copy(const NT_CM_RESOURCE_LIST *l, unsigned int *len)
{
    *len = resources_size(l);
    if (!*len || *len > 4096) { *len = 0; return 0; }
    NT_CM_RESOURCE_LIST *c = ExAllocatePoolWithTag(NonPagedPoolNx, *len, TAG);
    if (c) memcpy(c, l, *len);
    else *len = 0;
    return c;
}

static void interfaces_off(Ext *x)
{
    IoSetDeviceInterfaceState(&x->host_link, 0);
    for (int i = 0; i < x->n_on; i++) {
        unsigned short rl = 0;
        while (x->on[i].ref[rl]) rl++;
        NT_UNICODE_STRING ref = { (unsigned short)(rl * 2), (unsigned short)(rl * 2 + 2), x->on[i].ref }, link;
        if (NT_SUCCESS_(IoRegisterDeviceInterface(x->pdo, &x->on[i].guid, rl ? &ref : 0, &link))) {
            IoSetDeviceInterfaceState(&link, 0);
            RtlFreeUnicodeString(&link);
        }
    }
    x->n_on = 0;
}

static void release_resources(Ext *x)
{
    if (x->raw) ExFreePoolWithTag(x->raw, TAG);
    if (x->xlat) ExFreePoolWithTag(x->xlat, TAG);
    x->raw = x->xlat = 0;
    x->raw_len = x->xlat_len = 0;
}

static NT_API NT_STATUS dispatch_pnp(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    NT_STATUS st;
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE:
        st = forward_and_wait(x, irp);
        if (NT_SUCCESS_(st)) {
            release_resources(x);
            x->raw = resources_copy(sl->Parameters.StartDevice.AllocatedResources, &x->raw_len);
            x->xlat = resources_copy(sl->Parameters.StartDevice.AllocatedResourcesTranslated, &x->xlat_len);
            x->started = 1;
            IoSetDeviceInterfaceState(&x->host_link, 1);
            DbgPrint("WUDFRd: started, waiting for the driver host");
            post_start(x);                       /* (when the host is already there) */
        }
        return complete(irp, st, 0);
    case NT_IRP_MN_QUERY_REMOVE_DEVICE:
        st = ask_host(x, WUDFRD_MSG_QUERY_REMOVE);
        if (!NT_SUCCESS_(st)) {
            DbgPrint("WUDFRd: the UMDF driver vetoed removal (%08x)", st);
            return complete(irp, st, 0);
        }
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_CANCEL_REMOVE_DEVICE:
        ask_host(x, WUDFRD_MSG_CANCEL_REMOVE);
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_SURPRISE_REMOVAL:
        x->removed = 1;
        ask_host(x, WUDFRD_MSG_SURPRISE_REMOVAL);
        interfaces_off(x);
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_REMOVE_DEVICE:
        x->removed = 1;
        ask_host(x, WUDFRD_MSG_REMOVE);
        interfaces_off(x);
        host_gone(x, 0);
        x->started = 0;
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        st = pass_down(x, irp);
        IoDetachDevice(x->lower);
        RtlFreeUnicodeString(&x->host_link);
        release_resources(x);
        DbgPrint("WUDFRd: removed");
        IoDeleteDevice(dev);
        return st;
    case NT_IRP_MN_QUERY_STOP_DEVICE:
        /* (the UMDF driver cannot give its resources back while running: no rebalance) */
        return complete(irp, NT_STATUS_UNSUCCESSFUL, 0);
    }
    return pass_down(x, irp);
}

static NT_API NT_STATUS dispatch_power(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    PoStartNextPowerIrp(irp);
    skip(irp);
    return PoCallDriver(x->lower, irp);
}

static NT_API NT_STATUS dispatch_other(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    return pass_down(dev->DeviceExtension, irp);
}

static NT_API NT_STATUS add_device(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *pdo)
{
    NT_DEVICE_OBJECT *dev;
    NT_STATUS st = IoCreateDevice(drv, sizeof(Ext), 0, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, 0, &dev);
    if (!NT_SUCCESS_(st)) return st;
    Ext *x = dev->DeviceExtension;
    memset(x, 0, sizeof(*x));
    x->self = dev;
    x->pdo = pdo;
    KeInitializeSpinLock(&x->lock);
    list_init(&x->queued);
    list_init(&x->sent);
    list_init(&x->gets);
    static unsigned short ref[] = WUDFRD_HOST_REF;
    NT_UNICODE_STRING r = { sizeof(ref) - 2, sizeof(ref), ref };
    st = IoRegisterDeviceInterface(pdo, &g_host_guid, &r, &x->host_link);
    if (!NT_SUCCESS_(st)) {
        IoDeleteDevice(dev);
        return st;
    }
    x->lower = IoAttachDeviceToDeviceStack(dev, pdo);
    if (!x->lower) {
        RtlFreeUnicodeString(&x->host_link);
        IoDeleteDevice(dev);
        return STATUS_NO_SUCH_DEVICE;
    }
    dev->Flags |= NT_DO_BUFFERED_IO | NT_DO_POWER_PAGABLE;
    dev->Flags &= ~(unsigned int)NT_DO_DEVICE_INITIALIZING;
    DbgPrint("WUDFRd: AddDevice");
    return NT_STATUS_SUCCESS;
}

static NT_API void unload(NT_DRIVER_OBJECT *drv)
{
    (void)drv;
    DbgPrint("WUDFRd: unloaded");
}

NT_API NT_STATUS DriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *path)
{
    (void)path;
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) drv->MajorFunction[i] = dispatch_other;
    drv->MajorFunction[NT_IRP_MJ_CREATE] = dispatch_io;
    drv->MajorFunction[NT_IRP_MJ_CLOSE] = dispatch_io;
    drv->MajorFunction[NT_IRP_MJ_CLEANUP] = dispatch_io;
    drv->MajorFunction[NT_IRP_MJ_READ] = dispatch_io;
    drv->MajorFunction[NT_IRP_MJ_WRITE] = dispatch_io;
    drv->MajorFunction[NT_IRP_MJ_DEVICE_CONTROL] = dispatch_io;
    drv->MajorFunction[NT_IRP_MJ_PNP] = dispatch_pnp;
    drv->MajorFunction[NT_IRP_MJ_POWER] = dispatch_power;
    drv->DriverExtension->AddDevice = add_device;
    drv->DriverUnload = unload;
    return NT_STATUS_SUCCESS;
}
