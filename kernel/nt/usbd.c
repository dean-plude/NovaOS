/*
 * usbd.c — the USB bus interface Windows USB client drivers talk to
 *
 * On Windows a USB device's function driver (a USB serial converter's,
 * a vendor device's) never touches the host controller: the hub driver
 * (usbhub3.sys over the controller's ucx01000.sys) gives each device a
 * physical device object, and the client sends it USB Request Blocks
 * (URBs) in IRP_MJ_INTERNAL_DEVICE_CONTROL requests
 * (IOCTL_INTERNAL_USB_SUBMIT_URB).  NovaOS ships none of Microsoft's USB
 * stack: this file is that hub driver's side, written over NovaOS's own
 * USB core (drivers/usb.c), which has already enumerated the device,
 * read its descriptors and set its configuration.
 *
 *   - a PDO per USB device (\Device\USBPDO-n) whose node in the device
 *     tree a Windows driver is installed on (pnp.c's NtPnpStart makes it
 *     and hands it to the driver's AddDevice);
 *   - for a composite device, a PDO per function instead (its
 *     USB\VID_v&PID_p&MI_nn node), as Windows' usbccgp.sys gives one:
 *     the function's driver sees a configuration descriptor holding only
 *     its own interfaces (and their interface association), selects only
 *     those, and has its own pipes, while the device's other functions
 *     run under other drivers (NovaOS's or Windows');
 *   - the URBs a client sends: select configuration and interface
 *     (pipe handles for its endpoints), control transfers (standard,
 *     class and vendor requests, descriptors, status, features), bulk and
 *     interrupt transfers, abort and reset of a pipe, the frame number;
 *   - the hub's other requests: port status, port reset and cycle, the
 *     selective-suspend idle notification (held until the client cancels
 *     it: NovaOS does not suspend a device), and IRP_MN_QUERY_INTERFACE
 *     for USB_BUS_INTERFACE_USBDI (versions 0-3);
 *   - usbd.sys's exports (USBD_CreateConfigurationRequestEx,
 *     USBD_ParseConfigurationDescriptorEx, USBD_GetUSBDIVersion, ...).
 *
 * Transfers: a client may submit at DISPATCH_LEVEL and expects the IRP
 * back later, so each pipe (and the default pipe) has a queue and a
 * thread that carries its requests out one after the other on the USB
 * core (which waits for each) and completes them at DISPATCH_LEVEL, as
 * the hub's completion DPC would.  An IN transfer the device has no data
 * for waits on its pipe (others are not held up); aborting the pipe or
 * cancelling the IRP ends it.  Isochronous transfers are not supported
 * yet (no client needs them); interrupt pipes run as bulk ones do (on
 * xHCI, which treats both alike).  When the device is unplugged its
 * requests fail with USBD_STATUS_DEVICE_GONE and the PnP manager is told
 * (surprise removal).
 */
#include "ntapi.h"
#include "usbdi.h"
#include "../drivers/usb_hc.h"
#include "../io/pnp.h"
#include "../ke/irql.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

NT_API void *nt_MmMapLockedPagesSpecifyCache(NT_MDL *m, char mode, int cache, void *requested, UINT32 bugcheck,
                                             UINT32 priority);
NT_API UINT8 nt_IoCancelIrp(NT_IRP *irp);
NT_API void  nt_IoAcquireCancelSpinLock(UINT8 *irql);
NT_API void  nt_IoReleaseCancelSpinLock(UINT8 irql);
NT_API void  usbd_cancel_idle(NT_DEVICE_OBJECT *dev, NT_IRP *irp);

#define NT_IRP_MJ_INTERNAL_DEVICE_CONTROL 0x0F
#define USBD_STACK            (24 * 1024)
#define PIPE_BUF              (64 * 1024)          /* each pipe's DMA buffer: a transfer moves this much at a time */
#define TAG_USBD              0x64627355u          /* 'Usbd' */

#define ST_DEVICE_NOT_CONNECTED ((NT_STATUS)0xC000009D)
#define ST_CANCELLED            ((NT_STATUS)0xC0000120)
#define ST_IO_DEVICE_ERROR      ((NT_STATUS)0xC0000185)
#define ST_NO_SUCH_DEVICE       ((NT_STATUS)0xC000000E)

/* ---- the PDOs ---- */

typedef struct UsbdDev UsbdDev;

typedef struct {
    UsbdDev       *dev;
    UsbPipe       *pipe;               /* NULL: the default pipe (endpoint 0) */
    UINT8          addr, type, interval;
    UINT16         mps;
    KSpinLock      lock;
    NT_LIST_ENTRY  queue;              /* IRPs waiting (Tail.Overlay.ListEntry) */
    NT_IRP        *active;             /* the one being carried out */
    NT_KEVENT      wake, idle;         /* work for the thread; nothing active */
    volatile bool  abort;              /* ABORT_PIPE: the active transfer gives up */
    bool           thread;
} UsbdPipe;

struct UsbdDev {
    NT_DEVICE_OBJECT *pdo;
    UsbDev           *usb;
    UINT32            node;
    UsbdPipe          ep0;
    UsbdPipe         *pipes[32];       /* by the core's pipe index */
    UINT8             config;          /* bConfigurationValue selected (0: none) */
    UINT8             alt[32];         /* each interface's alternate setting */
    UINT32            ifaces;          /* a composite device's function: its interfaces (bit n: interface n); 0: the whole device */
    NT_IRP           *idle;            /* the held idle notification */
    volatile bool     gone;
};

static NT_DRIVER_OBJECT g_usbd;        /* the "hub driver": owns the PDOs */
static UsbdDev         *g_devs[32];
static KSpinLock        g_lock = KSPINLOCK_INIT;

static UsbdDev *dev_of(NT_DEVICE_OBJECT *d)
{
    return d && d->DriverObject == &g_usbd ? (UsbdDev *)d->DeviceExtension : NULL;
}

static void list_init(NT_LIST_ENTRY *h) { h->Flink = h->Blink = h; }
static void list_del(NT_LIST_ENTRY *e) { e->Blink->Flink = e->Flink; e->Flink->Blink = e->Blink; list_init(e); }
static void list_add_tail(NT_LIST_ENTRY *h, NT_LIST_ENTRY *e)
{
    e->Flink = h;
    e->Blink = h->Blink;
    h->Blink->Flink = e;
    h->Blink = e;
}

/* Finish @irp as the hub would: URB status, IRP status, at DISPATCH_LEVEL */
static void complete(NT_IRP *irp, NT_STATUS st)
{
    irp->IoStatus.Status = st;
    UINT8 old = KeIrql();
    if (old < DISPATCH_LEVEL) KeIrqlSet(DISPATCH_LEVEL);
    nt_IofCompleteRequest(irp, 0);
    if (old < DISPATCH_LEVEL) KeIrqlSet(old);
}

static NT_STATUS urb_status(UrbHeader *u, UINT32 usbd)
{
    u->Status = usbd;
    switch (usbd) {
    case USBD_STATUS_SUCCESS:          return NT_STATUS_SUCCESS;
    case USBD_STATUS_CANCELED:         return ST_CANCELLED;
    case USBD_STATUS_DEVICE_GONE:      return ST_DEVICE_NOT_CONNECTED;
    case USBD_STATUS_INVALID_URB_FUNCTION:
    case USBD_STATUS_INVALID_PARAMETER:
    case USBD_STATUS_INVALID_PIPE_HANDLE: return NT_STATUS_INVALID_PARAMETER;
    case USBD_STATUS_NOT_SUPPORTED:    return NT_STATUS_NOT_SUPPORTED;
    default:                           return ST_IO_DEVICE_ERROR;
    }
}

/* The URB's transfer buffer in kernel space (its MDL mapped if it has one) */
static UINT8 *xfer_buf(UrbXfer *u)
{
    if (!u->TransferBufferLength) return NULL;
    if (u->TransferBufferMDL) return nt_MmMapLockedPagesSpecifyCache(u->TransferBufferMDL, 0, 1, NULL, 0, 16);
    return u->TransferBuffer;
}

/* ---- carrying a URB out (a pipe's thread, or at PASSIVE_LEVEL in the dispatch routine) ---- */

/* A control transfer on endpoint 0: the bytes moved, or a USBD status */
static UINT32 control(UsbdDev *v, UINT8 type, UINT8 req, UINT16 value, UINT16 index, UINT8 *buf, UINT32 len,
                      UINT32 *moved)
{
    *moved = 0;
    if (v->gone || v->usb->gone) return USBD_STATUS_DEVICE_GONE;
    if (len > USB_CTL_BUF) return USBD_STATUS_INVALID_PARAMETER;
    int n = UsbControl(v->usb, type, req, value, index, (UINT16)len, buf);
    if (n < 0) return v->usb->gone ? USBD_STATUS_DEVICE_GONE : USBD_STATUS_STALL_PID;
    *moved = (UINT32)n;
    return USBD_STATUS_SUCCESS;
}

/* The request type's recipient for a function: 0 device, 1 interface, 2 endpoint, 3 other */
static UINT8 recipient(UINT16 fn)
{
    switch (fn) {
    case URB_FUNCTION_SET_FEATURE_TO_INTERFACE: case URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE:
    case URB_FUNCTION_GET_STATUS_FROM_INTERFACE: case URB_FUNCTION_VENDOR_INTERFACE:
    case URB_FUNCTION_CLASS_INTERFACE: case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
    case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE: case URB_FUNCTION_GET_INTERFACE:
        return 1;
    case URB_FUNCTION_SET_FEATURE_TO_ENDPOINT: case URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT:
    case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT: case URB_FUNCTION_VENDOR_ENDPOINT:
    case URB_FUNCTION_CLASS_ENDPOINT: case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
    case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
        return 2;
    case URB_FUNCTION_CLASS_OTHER: case URB_FUNCTION_VENDOR_OTHER: case URB_FUNCTION_GET_STATUS_FROM_OTHER:
    case URB_FUNCTION_CLEAR_FEATURE_TO_OTHER: case URB_FUNCTION_SET_FEATURE_TO_OTHER:
        return 3;
    }
    return 0;
}

/* A composite device's function: the configuration descriptor as its
 * driver sees it (usbccgp.sys's), the header with only the function's
 * interfaces counted, then each of its interfaces (every alternate
 * setting) with the descriptors after it, and an interface association
 * that starts at one of them.  At most @cap bytes into @out (which may
 * be NULL); returns how many. */
static UINT32 function_config(UsbdDev *v, UINT8 *out, UINT32 cap)
{
    UsbDev *d = v->usb;
    UINT8 *t = kmalloc((size_t)d->cfg_len);
    if (!t || d->cfg_len < 9) { kfree(t); return 0; }
    memcpy(t, d->cfg, 9);
    UINT32 n = 9;
    UINT8 count = 0;
    bool keep = false;
    for (int off = d->cfg[0]; off + 2 <= d->cfg_len && d->cfg[off] >= 2 && off + d->cfg[off] <= d->cfg_len;
         off += d->cfg[off]) {
        const UINT8 *x = d->cfg + off;
        bool take;
        if (x[1] == USB_DT_INTERFACE && x[0] >= 9) {
            keep = x[2] < 32 && (v->ifaces >> x[2] & 1);
            if (keep && x[3] == 0) count++;
            take = keep;
        } else if (x[1] == 0x0B && x[0] >= 8) {                  /* INTERFACE_ASSOCIATION */
            take = x[2] < 32 && (v->ifaces >> x[2] & 1);
            keep = false;
        } else {
            take = keep;
        }
        if (take) { memcpy(t + n, x, x[0]); n += x[0]; }
    }
    t[2] = (UINT8)n;
    t[3] = (UINT8)(n >> 8);
    t[4] = count;
    UINT32 m = n < cap ? n : cap;
    if (out) memcpy(out, t, m);
    kfree(t);
    return m;
}

/* Every control-pipe URB: its SETUP packet from its fields, then the transfer */
static UINT32 run_control(UsbdDev *v, UrbXfer *u)
{
    UINT16 fn = u->Hdr.Function;
    UINT8 *s = u->Setup, type, req;
    UINT16 value = 0, index = 0;
    bool in;
    switch (fn) {
    case URB_FUNCTION_CONTROL_TRANSFER:
    case URB_FUNCTION_CONTROL_TRANSFER_EX:
        type = s[0]; req = s[1];
        value = (UINT16)(s[2] | s[3] << 8);
        index = (UINT16)(s[4] | s[5] << 8);
        in = type & 0x80;
        break;
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE: case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT: case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
    case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE: case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
        /* _URB_CONTROL_DESCRIPTOR_REQUEST: Index at 130, DescriptorType 131, LanguageId 132 */
        in = fn == URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE || fn == URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE ||
             fn == URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT;
        type = (UINT8)((in ? 0x80 : 0) | recipient(fn));
        req = in ? 6 : 7;
        value = (UINT16)(s[3] << 8 | s[2]);
        index = (UINT16)(s[4] | s[5] << 8);
        break;
    case URB_FUNCTION_SET_FEATURE_TO_DEVICE: case URB_FUNCTION_SET_FEATURE_TO_INTERFACE:
    case URB_FUNCTION_SET_FEATURE_TO_ENDPOINT: case URB_FUNCTION_SET_FEATURE_TO_OTHER:
    case URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE: case URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE:
    case URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT: case URB_FUNCTION_CLEAR_FEATURE_TO_OTHER: {
        /* _URB_CONTROL_FEATURE_REQUEST: FeatureSelector at 130, Index 132 */
        bool set = fn == URB_FUNCTION_SET_FEATURE_TO_DEVICE || fn == URB_FUNCTION_SET_FEATURE_TO_INTERFACE ||
                   fn == URB_FUNCTION_SET_FEATURE_TO_ENDPOINT || fn == URB_FUNCTION_SET_FEATURE_TO_OTHER;
        UINT32 moved;
        return control(v, recipient(fn), set ? 3 : 1, (UINT16)(s[2] | s[3] << 8), (UINT16)(s[4] | s[5] << 8),
                       NULL, 0, &moved);
    }
    case URB_FUNCTION_GET_STATUS_FROM_DEVICE: case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
    case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT: case URB_FUNCTION_GET_STATUS_FROM_OTHER:
        type = (UINT8)(0x80 | recipient(fn)); req = 0;          /* (_URB_CONTROL_GET_STATUS_REQUEST: Index at 132) */
        index = (UINT16)(s[4] | s[5] << 8);
        in = true;
        break;
    case URB_FUNCTION_GET_CONFIGURATION:
        type = 0x80; req = 8; in = true;
        break;
    case URB_FUNCTION_GET_INTERFACE:
        type = 0x81; req = 10; index = (UINT16)(s[4] | s[5] << 8); in = true;
        break;
    case URB_FUNCTION_VENDOR_DEVICE: case URB_FUNCTION_VENDOR_INTERFACE: case URB_FUNCTION_VENDOR_ENDPOINT:
    case URB_FUNCTION_VENDOR_OTHER: case URB_FUNCTION_CLASS_DEVICE: case URB_FUNCTION_CLASS_INTERFACE:
    case URB_FUNCTION_CLASS_ENDPOINT: case URB_FUNCTION_CLASS_OTHER: {
        /* _URB_CONTROL_VENDOR_OR_CLASS_REQUEST: RequestTypeReservedBits at 128,
         * Request 129, Value 130, Index 132; direction from TransferFlags */
        bool vendor = fn == URB_FUNCTION_VENDOR_DEVICE || fn == URB_FUNCTION_VENDOR_INTERFACE ||
                      fn == URB_FUNCTION_VENDOR_ENDPOINT || fn == URB_FUNCTION_VENDOR_OTHER;
        in = u->TransferFlags & USBD_TRANSFER_DIRECTION_IN;
        type = (UINT8)((in ? 0x80 : 0) | (vendor ? 0x40 : 0x20) | recipient(fn) | (s[0] & 0x1F & ~3));
        req = s[1];
        value = (UINT16)(s[2] | s[3] << 8);
        index = (UINT16)(s[4] | s[5] << 8);
        break;
    }
    default:
        return USBD_STATUS_INVALID_URB_FUNCTION;
    }
    UINT8 *buf = xfer_buf(u);
    UINT32 len = buf ? u->TransferBufferLength : 0, moved = 0;
    if (v->ifaces && type == 0x80 && req == 6 && value >> 8 == USB_DT_CONFIG) {   /* (a function: its own part) */
        if (v->gone || v->usb->gone) return USBD_STATUS_DEVICE_GONE;
        u->TransferBufferLength = function_config(v, buf, len);
        return USBD_STATUS_SUCCESS;
    }
    UINT32 st = control(v, type, req, value, index, buf, len, &moved);
    u->TransferBufferLength = in ? moved : (st == USBD_STATUS_SUCCESS ? len : 0);
    return st;
}

/* A bulk or interrupt transfer on @p, PIPE_BUF at a time; an IN one ends
 * at a short packet.  It waits as long as the device takes, a second at a
 * time, until the pipe is aborted or the IRP cancelled. */
static UINT32 run_bulk(UsbdPipe *p, UrbXfer *u, NT_IRP *irp)
{
    UsbdDev *v = p->dev;
    bool in = p->addr & 0x80;
    UINT8 *buf = xfer_buf(u);
    UINT32 len = u->TransferBufferLength, done = 0;
    if (len && !buf) return USBD_STATUS_INVALID_PARAMETER;
    UINT32 st = USBD_STATUS_SUCCESS;
    while (done < len || (!len && !done)) {
        UINT32 chunk = len - done > PIPE_BUF ? PIPE_BUF : len - done;
        if (!chunk) break;                                       /* (a zero-length transfer: nothing to move) */
        bool stalled = false;
        int n;
        for (;;) {
            if (v->gone || v->usb->gone) { st = USBD_STATUS_DEVICE_GONE; goto out; }
            if (p->abort || irp->Cancel) { st = USBD_STATUS_CANCELED; goto out; }
            n = UsbBulkPoll(p->pipe, buf + done, chunk, 1000, &stalled);
            if (n != USB_TIMED_OUT) break;
        }
        if (n < 0) { st = stalled ? USBD_STATUS_STALL_PID : v->usb->gone ? USBD_STATUS_DEVICE_GONE : USBD_STATUS_DEV_NOT_RESPONDING; break; }
        done += (UINT32)n;
        if (in && (UINT32)n < chunk) break;                      /* a short packet: the device has no more */
    }
out:
    u->TransferBufferLength = done;
    return st;
}

static UINT32 run(UsbdPipe *p, NT_IRP *irp)
{
    UrbXfer *u = NtIrpCurrentStack(irp)->Parameters.Others.Argument1;
    if (u->Hdr.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER) return run_bulk(p, u, irp);
    return run_control(p->dev, u);
}

/* ---- the pipes' queues ---- */

static NT_API void cancel_queued(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    (void)dev;
    UsbdPipe *p = irp->Tail.Overlay.DriverContext[0];
    nt_IoReleaseCancelSpinLock(irp->CancelIrql);
    IrqState s = spin_lock_irqsave(&p->lock);
    bool queued = irp->Tail.Overlay.ListEntry.Flink != &irp->Tail.Overlay.ListEntry;
    if (queued) list_del(&irp->Tail.Overlay.ListEntry);
    spin_unlock_irqrestore(&p->lock, s);
    if (!queued) return;                                         /* (the thread took it: it sees Cancel) */
    urb_status(NtIrpCurrentStack(irp)->Parameters.Others.Argument1, USBD_STATUS_CANCELED);
    irp->IoStatus.Information = 0;
    complete(irp, ST_CANCELLED);
}

static void pipe_thread(void *arg)
{
    UsbdPipe *p = arg;
    for (;;) {
        nt_KeWaitForSingleObject(&p->wake, 0, 0, 0, NULL);
        for (;;) {
            IrqState s = spin_lock_irqsave(&p->lock);
            if (p->queue.Flink == &p->queue) {
                p->active = NULL;
                nt_KeSetEvent(&p->idle, 0, 0);
                spin_unlock_irqrestore(&p->lock, s);
                break;
            }
            NT_IRP *irp = (NT_IRP *)((UINT8 *)p->queue.Flink - __builtin_offsetof(NT_IRP, Tail.Overlay.ListEntry));
            list_del(&irp->Tail.Overlay.ListEntry);
            p->active = irp;
            spin_unlock_irqrestore(&p->lock, s);
            /* no longer cancelable by the routine: the transfer watches Cancel */
            void *old = __atomic_exchange_n(&irp->CancelRoutine, NULL, __ATOMIC_ACQ_REL);
            if (!old && irp->Cancel) {                           /* (its cancel routine already ran) */
                s = spin_lock_irqsave(&p->lock);
                p->active = NULL;
                spin_unlock_irqrestore(&p->lock, s);
                continue;
            }
            UrbHeader *u = NtIrpCurrentStack(irp)->Parameters.Others.Argument1;
            UINT32 st = p->abort ? USBD_STATUS_CANCELED : run(p, irp);
            irp->IoStatus.Information = 0;
            complete(irp, urb_status(u, st));
        }
    }
}

static void pipe_init(UsbdPipe *p, UsbdDev *v)
{
    p->dev = v;
    p->lock = (KSpinLock)KSPINLOCK_INIT;
    list_init(&p->queue);
    nt_KeInitializeEvent(&p->wake, NT_SynchronizationEvent, 0);
    nt_KeInitializeEvent(&p->idle, NT_NotificationEvent, 1);
}

/* Queue @irp on @p for its thread: STATUS_PENDING */
static NT_STATUS enqueue(UsbdPipe *p, NT_IRP *irp)
{
    if (!p->thread) {
        if (!sched_create_thread_ex("usbd-pipe", pipe_thread, p, 9, USBD_STACK)) return NT_STATUS_INSUFFICIENT_RESOURCES;
        p->thread = true;
    }
    irp->Tail.Overlay.DriverContext[0] = p;
    NtIrpMarkPending(irp);
    IrqState s = spin_lock_irqsave(&p->lock);
    list_add_tail(&p->queue, &irp->Tail.Overlay.ListEntry);
    p->idle.Header.SignalState = 0;                              /* (busy until the queue drains) */
    spin_unlock_irqrestore(&p->lock, s);
    __atomic_store_n(&irp->CancelRoutine, (void *)cancel_queued, __ATOMIC_RELEASE);
    if (irp->Cancel && __atomic_exchange_n(&irp->CancelRoutine, NULL, __ATOMIC_ACQ_REL)) {
        UINT8 irql;                                              /* (cancelled before the routine was set) */
        nt_IoAcquireCancelSpinLock(&irql);
        irp->CancelIrql = irql;
        cancel_queued(NULL, irp);
    }
    nt_KeSetEvent(&p->wake, 0, 0);
    return NT_STATUS_PENDING;
}

/* ABORT_PIPE: everything queued on @p is cancelled and the active transfer gives up */
static void pipe_abort(UsbdPipe *p)
{
    p->abort = true;
    for (;;) {
        IrqState s = spin_lock_irqsave(&p->lock);
        NT_IRP *irp = NULL;
        if (p->queue.Flink != &p->queue) {
            irp = (NT_IRP *)((UINT8 *)p->queue.Flink - __builtin_offsetof(NT_IRP, Tail.Overlay.ListEntry));
            list_del(&irp->Tail.Overlay.ListEntry);
        }
        spin_unlock_irqrestore(&p->lock, s);
        if (!irp) break;
        if (!__atomic_exchange_n(&irp->CancelRoutine, NULL, __ATOMIC_ACQ_REL) && irp->Cancel) continue;
        urb_status(NtIrpCurrentStack(irp)->Parameters.Others.Argument1, USBD_STATUS_CANCELED);
        irp->IoStatus.Information = 0;
        complete(irp, ST_CANCELLED);
    }
    if (KeIrql() == PASSIVE_LEVEL) nt_KeWaitForSingleObject(&p->idle, 0, 0, 0, NULL);
    p->abort = false;
}

/* ---- configurations and interfaces ---- */

/* The interface descriptor @num/@alt in the configuration; its offset, or -1 */
static int find_iface(UsbDev *d, int num, int alt)
{
    for (int off = 0; off + 2 <= d->cfg_len && d->cfg[off] >= 2; off += d->cfg[off]) {
        const UINT8 *x = d->cfg + off;
        if (x[1] == USB_DT_INTERFACE && x[0] >= 9 && x[2] == num && x[3] == alt) return off;
    }
    return -1;
}

/* Fill @info (its InterfaceNumber and AlternateSetting chosen) with the
 * interface's facts and a pipe handle for each endpoint */
static UINT32 fill_iface(UsbdDev *v, UsbdIfaceInfo *info)
{
    UsbDev *d = v->usb;
    int off = find_iface(d, info->InterfaceNumber, info->AlternateSetting);
    if (off < 0) return USBD_STATUS_INVALID_PARAMETER;
    const UINT8 *x = d->cfg + off;
    info->Class = x[5];
    info->SubClass = x[6];
    info->Protocol = x[7];
    info->InterfaceHandle = (void *)(uintptr_t)(0x100 | info->InterfaceNumber);
    UINT32 want = x[4], k = 0;
    UINT32 room = info->Length >= 24 ? (info->Length - 24) / sizeof(UsbdPipeInfo) : 0;
    for (int o = off + x[0]; o + 2 <= d->cfg_len && d->cfg[o] >= 2 && d->cfg[o + 1] != USB_DT_INTERFACE; o += d->cfg[o]) {
        const UINT8 *e = d->cfg + o;
        if (e[1] != USB_DT_ENDPOINT || e[0] < 7 || k >= want || k >= room) continue;
        UINT8 idx = (UINT8)((e[2] & 0xF) * 2 + ((e[2] & 0x80) ? 1 : 0));
        UsbPipe *up = UsbOpenPipe(d, e, PIPE_BUF);
        if (!up) return USBD_STATUS_NOT_SUPPORTED;
        up->patient = true;
        up->keep = up->xfer == 3;                                /* interrupt: as on Windows, a transfer stays queued */
        UsbdPipe *p = v->pipes[idx];
        if (!p) {
            p = kzalloc(sizeof(*p));
            if (!p) return USBD_STATUS_INVALID_PARAMETER;
            pipe_init(p, v);
            v->pipes[idx] = p;
        }
        p->pipe = up;
        p->addr = e[2];
        p->type = e[3] & 3;
        p->mps = (UINT16)(e[4] | e[5] << 8);
        p->interval = e[6];
        UsbdPipeInfo *pi = &info->Pipes[k++];
        pi->MaximumPacketSize = (UINT16)(p->mps & 0x7FF);
        pi->EndpointAddress = p->addr;
        pi->Interval = p->interval;
        pi->PipeType = p->type;
        pi->PipeHandle = p;
        if (!pi->MaximumTransferSize || pi->MaximumTransferSize > 0x400000) pi->MaximumTransferSize = 0x400000;
    }
    info->NumberOfPipes = k;
    return USBD_STATUS_SUCCESS;
}

/* Whether interface @num is the PDO's to select (a function's own, or any of a whole device) */
static bool mine(UsbdDev *v, UINT8 num)
{
    return !v->ifaces || (num < 32 && (v->ifaces >> num & 1));
}

static UINT32 select_config(UsbdDev *v, UrbSelectConfig *u)
{
    UsbDev *d = v->usb;
    if (!u->ConfigurationDescriptor) {                            /* deconfigure: the core keeps its own */
        v->config = 0;
        return USBD_STATUS_SUCCESS;
    }
    v->config = d->cfg[5];
    u->ConfigurationHandle = v;
    UINT8 *end = (UINT8 *)u + u->Hdr.Length;
    for (UsbdIfaceInfo *i = &u->Interface; (UINT8 *)i + 24 <= end && i->Length >= 24;
         i = (UsbdIfaceInfo *)((UINT8 *)i + i->Length)) {
        if (!mine(v, i->InterfaceNumber)) return USBD_STATUS_INVALID_PARAMETER;   /* (another function's) */
        if (i->AlternateSetting != v->alt[i->InterfaceNumber & 31]) {
            if (!UsbSetInterface(d, i->InterfaceNumber, i->AlternateSetting)) return USBD_STATUS_STALL_PID;
            v->alt[i->InterfaceNumber & 31] = i->AlternateSetting;
        }
        UINT32 st = fill_iface(v, i);
        if (st) return st;
        kprintf("[USBD] %s: interface %u.%u selected, %u pipe(s)\n", d->name, i->InterfaceNumber,
                i->AlternateSetting, i->NumberOfPipes);
    }
    return USBD_STATUS_SUCCESS;
}

static UINT32 select_iface(UsbdDev *v, UrbSelectIface *u)
{
    UsbdIfaceInfo *i = &u->Interface;
    if (!mine(v, i->InterfaceNumber)) return USBD_STATUS_INVALID_PARAMETER;
    if (!UsbSetInterface(v->usb, i->InterfaceNumber, i->AlternateSetting)) return USBD_STATUS_STALL_PID;
    v->alt[i->InterfaceNumber & 31] = i->AlternateSetting;
    return fill_iface(v, i);
}

static UsbdPipe *pipe_of(UsbdDev *v, void *handle)
{
    for (int i = 0; i < 32; i++) if (v->pipes[i] && v->pipes[i] == handle) return v->pipes[i];
    return NULL;
}

/* ---- the PDO's requests ---- */

static NT_STATUS submit_urb(UsbdDev *v, NT_IRP *irp, UrbHeader *u)
{
    if (!u) return NT_STATUS_INVALID_PARAMETER;
    if (v->gone || v->usb->gone) return urb_status(u, USBD_STATUS_DEVICE_GONE);
    bool passive = KeIrql() == PASSIVE_LEVEL;
    switch (u->Function) {
    case URB_FUNCTION_SELECT_CONFIGURATION:
        return urb_status(u, select_config(v, (UrbSelectConfig *)u));
    case URB_FUNCTION_SELECT_INTERFACE:
        return urb_status(u, select_iface(v, (UrbSelectIface *)u));
    case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
        ((UrbFrame *)u)->FrameNumber = (UINT32)(sched_ticks() * 10);   /* (milliseconds: a full-speed frame) */
        return urb_status(u, USBD_STATUS_SUCCESS);
    case URB_FUNCTION_ABORT_PIPE: {
        UsbdPipe *p = pipe_of(v, ((UrbPipe *)u)->PipeHandle);
        if (!p) return urb_status(u, USBD_STATUS_INVALID_PIPE_HANDLE);
        pipe_abort(p);
        return urb_status(u, USBD_STATUS_SUCCESS);
    }
    case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
    case URB_FUNCTION_SYNC_RESET_PIPE:
    case URB_FUNCTION_SYNC_CLEAR_STALL: {
        UsbdPipe *p = pipe_of(v, ((UrbPipe *)u)->PipeHandle);
        if (!p) return urb_status(u, USBD_STATUS_INVALID_PIPE_HANDLE);
        if (!passive) return enqueue(&v->ep0, irp);              /* (it is PASSIVE_LEVEL only on Windows too) */
        return urb_status(u, UsbPipeReset(p->pipe) ? USBD_STATUS_SUCCESS : USBD_STATUS_STALL_PID);
    }
    case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER: {
        UrbXfer *x = (UrbXfer *)u;
        UsbdPipe *p = pipe_of(v, x->PipeHandle);
        if (!p || !p->pipe) return urb_status(u, USBD_STATUS_INVALID_PIPE_HANDLE);
        if (p->type == 1) return urb_status(u, USBD_STATUS_NOT_SUPPORTED);
        u->Status = USBD_STATUS_PENDING;
        return enqueue(p, irp);
    }
    case URB_FUNCTION_ISOCH_TRANSFER:
        kprintf("[USBD] %s: isochronous transfers are not supported yet\n", v->usb->name);
        return urb_status(u, USBD_STATUS_NOT_SUPPORTED);
    case URB_FUNCTION_CONTROL_TRANSFER:
    case URB_FUNCTION_CONTROL_TRANSFER_EX: {
        UrbXfer *x = (UrbXfer *)u;
        if (x->PipeHandle && !(x->TransferFlags & USBD_DEFAULT_PIPE_TRANSFER) && pipe_of(v, x->PipeHandle))
            return urb_status(u, USBD_STATUS_NOT_SUPPORTED);     /* (control endpoints besides 0: none in the core) */
    }
        /* fall through */
    default:
        if (u->Function > URB_FUNCTION_CONTROL_TRANSFER_EX) return urb_status(u, USBD_STATUS_INVALID_URB_FUNCTION);
        u->Status = USBD_STATUS_PENDING;
        return enqueue(&v->ep0, irp);
    }
}

static NT_STATUS internal_ioctl(UsbdDev *v, NT_IRP *irp, NT_IO_STACK_LOCATION *sl, bool *pending)
{
    UINT32 code = sl->Parameters.DeviceIoControl.IoControlCode;
    switch (code) {
    case IOCTL_INTERNAL_USB_SUBMIT_URB: {
        NT_STATUS st = submit_urb(v, irp, sl->Parameters.Others.Argument1);
        *pending = st == NT_STATUS_PENDING;
        return st;
    }
    case IOCTL_INTERNAL_USB_GET_PORT_STATUS: {
        UINT32 *out = sl->Parameters.Others.Argument1;
        if (out) *out = v->usb->gone ? 0 : USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
        return NT_STATUS_SUCCESS;
    }
    case IOCTL_INTERNAL_USB_RESET_PORT:
    case IOCTL_INTERNAL_USB_CYCLE_PORT:
    case IOCTL_INTERNAL_USB_ENABLE_PORT:
        /* The port stays as it is: the core enumerated the device and the
         * pipes are fresh (a reset would make it address 0 again under the
         * core's feet).  What a client wants from a reset, its endpoints
         * back to their first state, it gets by resetting its pipes. */
        if (v->usb->gone) return ST_DEVICE_NOT_CONNECTED;
        for (int i = 0; i < 32; i++) if (v->pipes[i] && v->pipes[i]->pipe) UsbPipeReset(v->pipes[i]->pipe);
        return NT_STATUS_SUCCESS;
    case IOCTL_INTERNAL_USB_GET_HUB_COUNT: {
        UINT32 *out = sl->Parameters.Others.Argument1;
        if (out) *out = (UINT32)v->usb->depth + 1;
        return NT_STATUS_SUCCESS;
    }
    case IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION: {
        /* Selective suspend: the hub calls the client back when it may
         * power the device down.  NovaOS does not suspend devices, so
         * the callback never comes; the IRP waits until the client
         * cancels it (when it wants the device fully on again). */
        IrqState s = spin_lock_irqsave(&g_lock);
        bool busy = v->idle != NULL;
        if (!busy) v->idle = irp;
        spin_unlock_irqrestore(&g_lock, s);
        if (busy) return NT_STATUS_DEVICE_BUSY;
        irp->Tail.Overlay.DriverContext[0] = v;
        NtIrpMarkPending(irp);
        __atomic_store_n(&irp->CancelRoutine, (void *)usbd_cancel_idle, __ATOMIC_RELEASE);
        if (irp->Cancel && __atomic_exchange_n(&irp->CancelRoutine, NULL, __ATOMIC_ACQ_REL)) {
            UINT8 irql;
            nt_IoAcquireCancelSpinLock(&irql);
            irp->CancelIrql = irql;
            usbd_cancel_idle(NULL, irp);
        }
        *pending = true;
        return NT_STATUS_PENDING;
    }
    }
    return NT_STATUS_NOT_SUPPORTED;
}

NT_API void usbd_cancel_idle(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    (void)dev;
    UsbdDev *v = irp->Tail.Overlay.DriverContext[0];
    nt_IoReleaseCancelSpinLock(irp->CancelIrql);
    IrqState s = spin_lock_irqsave(&g_lock);
    if (v->idle == irp) v->idle = NULL;
    spin_unlock_irqrestore(&g_lock, s);
    irp->IoStatus.Information = 0;
    complete(irp, ST_CANCELLED);
}

/* ---- USB_BUS_INTERFACE_USBDI_V0..V3 ---- */

static const NT_GUID g_usbdi = { 0xB1A96A13, 0x3DE0, 0x4574, { 0x9B, 0x01, 0xC0, 0x8F, 0xEA, 0xB3, 0x18, 0xD6 } };

typedef struct { UINT32 USBDI_Version, Supported_USB_Version; } UsbdVersion;

typedef struct {
    NT_INTERFACE Header;               /* (Size, Version, BusContext, InterfaceReference, InterfaceDereference) */
    void *GetUSBDIVersion, *QueryBusTime, *SubmitIsoOutUrb, *QueryBusInformation;   /* V0 */
    void *IsDeviceHighSpeed;                                                         /* V1 */
    void *EnumLogEntry;                                                              /* V2 */
    void *QueryBusTimeEx, *QueryControllerType;                                      /* V3 */
} UsbdiInterface;

static NT_API void bi_ref(void *ctx) { (void)ctx; }
static NT_API void bi_version(void *ctx, UsbdVersion *v, UINT32 *caps)
{
    (void)ctx;
    if (v) { v->USBDI_Version = 0x600; v->Supported_USB_Version = 0x200; }
    if (caps) *caps = 0;
}
static NT_API NT_STATUS bi_bus_time(void *ctx, UINT32 *frame)
{
    (void)ctx;
    if (frame) *frame = (UINT32)(sched_ticks() * 10);
    return NT_STATUS_SUCCESS;
}
static NT_API NT_STATUS bi_bus_time_ex(void *ctx, UINT32 *uframe)
{
    (void)ctx;
    if (uframe) *uframe = (UINT32)(sched_ticks() * 80);
    return NT_STATUS_SUCCESS;
}
static NT_API NT_STATUS bi_iso_out(void *ctx, void *urb) { (void)ctx; (void)urb; return NT_STATUS_NOT_SUPPORTED; }
static NT_API NT_STATUS bi_bus_info(void *ctx, UINT32 level, void *info, UINT32 *len, UINT32 *actual)
{
    (void)ctx;
    /* USB_BUS_INFORMATION_LEVEL_0: TotalBandwidth, ConsumedBandwidth (bits/ms) */
    if (level != 0) return NT_STATUS_NOT_SUPPORTED;
    if (actual) *actual = 8;
    if (!len || *len < 8 || !info) { if (len) *len = 8; return NT_STATUS_BUFFER_TOO_SMALL; }
    ((UINT32 *)info)[0] = 12000;
    ((UINT32 *)info)[1] = 0;
    *len = 8;
    return NT_STATUS_SUCCESS;
}
static NT_API UINT8 bi_high_speed(void *ctx)
{
    UsbdDev *v = ctx;
    return v->usb->speed == USB_SPEED_HIGH || v->usb->speed == USB_SPEED_SUPER;
}
static NT_API NT_STATUS bi_log(void *ctx, UINT32 a, UINT32 b, UINT32 c, UINT32 d)
{
    (void)ctx; (void)a; (void)b; (void)c; (void)d;
    return NT_STATUS_SUCCESS;
}
static NT_API NT_STATUS bi_controller(void *ctx, UINT32 *flags, UINT16 *ven, UINT16 *dev, UINT8 *cls, UINT8 *sub,
                                      UINT8 *rev, UINT8 *progif)
{
    UsbHc *hc = ((UsbdDev *)ctx)->usb->hc;
    if (flags) *flags = 0;
    if (ven) *ven = hc->pci.vendor;
    if (dev) *dev = hc->pci.device;
    if (cls) *cls = hc->pci.class_code;
    if (sub) *sub = hc->pci.subclass;
    if (rev) *rev = 0;                                           /* (PciDevice has no revision ID) */
    if (progif) *progif = hc->pci.prog_if;
    return NT_STATUS_SUCCESS;
}

static NT_STATUS query_interface(UsbdDev *v, NT_IO_STACK_LOCATION *sl)
{
    const NT_GUID *g = sl->Parameters.QueryInterface.InterfaceType;
    UsbdiInterface *b = (UsbdiInterface *)sl->Parameters.QueryInterface.Interface;
    unsigned ver = sl->Parameters.QueryInterface.Version;
    if (!g || memcmp(g, &g_usbdi, sizeof(*g)) || !b) return NT_STATUS_NOT_SUPPORTED;
    static const unsigned short sizes[4] = { 0x40, 0x48, 0x50, 0x60 };
    if (ver > 3) ver = 3;
    if (sl->Parameters.QueryInterface.Size < sizes[ver]) return NT_STATUS_INVALID_PARAMETER;
    b->Header.Size = sizes[ver];
    b->Header.Version = (unsigned short)ver;
    b->Header.Context = v;
    b->Header.InterfaceReference = bi_ref;
    b->Header.InterfaceDereference = bi_ref;
    b->GetUSBDIVersion = (void *)bi_version;
    b->QueryBusTime = (void *)bi_bus_time;
    b->SubmitIsoOutUrb = (void *)bi_iso_out;
    b->QueryBusInformation = (void *)bi_bus_info;
    if (ver >= 1) b->IsDeviceHighSpeed = (void *)bi_high_speed;
    if (ver >= 2) b->EnumLogEntry = (void *)bi_log;
    if (ver >= 3) { b->QueryBusTimeEx = (void *)bi_bus_time_ex; b->QueryControllerType = (void *)bi_controller; }
    return NT_STATUS_SUCCESS;
}

_Static_assert(sizeof(UsbdiInterface) == 0x60, "USB_BUS_INTERFACE_USBDI_V3");

/* ---- PnP ---- */

static NT_WCHAR *pool_wide(const char *s, size_t n)
{
    NT_WCHAR *w = nt_ExAllocatePoolWithTag(1, 2 * (n + 1), TAG_USBD);
    if (!w) return NULL;
    for (size_t i = 0; i < n; i++) w[i] = (UINT8)s[i];
    w[n] = 0;
    return w;
}

static size_t multi_len(const char *m)
{
    size_t n = 0;
    while (m[n]) n += strlen(m + n) + 1;
    return n + 1;
}

/* Every request still waiting on the device ends (cancelled) */
static void flush(UsbdDev *v)
{
    pipe_abort(&v->ep0);
    for (int i = 0; i < 32; i++) if (v->pipes[i]) pipe_abort(v->pipes[i]);
    IrqState s = spin_lock_irqsave(&g_lock);
    NT_IRP *idle = v->idle;
    spin_unlock_irqrestore(&g_lock, s);
    if (idle) nt_IoCancelIrp(idle);
}

static NT_STATUS pdo_pnp(UsbdDev *v, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    NT_STATUS st = irp->IoStatus.Status;
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE:
        v->gone = v->usb->gone;
        st = v->gone ? ST_NO_SUCH_DEVICE : NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_REMOVE_DEVICE:
    case NT_IRP_MN_SURPRISE_REMOVAL:
    case NT_IRP_MN_STOP_DEVICE:
        flush(v);
        v->config = 0;
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_QUERY_REMOVE_DEVICE:
    case NT_IRP_MN_CANCEL_REMOVE_DEVICE:
    case NT_IRP_MN_QUERY_STOP_DEVICE:
    case NT_IRP_MN_CANCEL_STOP_DEVICE:
    case NT_IRP_MN_QUERY_PNP_DEVICE_STATE:
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_QUERY_CAPABILITIES: {
        NT_DEVICE_CAPABILITIES *c = sl->Parameters.Others.Argument1;
        if (!c || c->Size < sizeof(*c)) { st = NT_STATUS_INVALID_PARAMETER; break; }
        c->Flags |= 0x10 | 0x200;                         /* Removable, SurpriseRemovalOK */
        c->Address = v->usb->parent ? v->usb->parent_port : v->usb->root_port;
        c->UINumber = c->Address;
        c->DeviceState[1] = 1;                            /* PowerSystemWorking: D0 */
        for (int i = 2; i < 7; i++) c->DeviceState[i] = 4;
        c->SystemWake = c->DeviceWake = 0;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_ID: {
        PnpNode *n = kmalloc(sizeof(PnpNode));
        if (!n || !PnpGet(v->node, n)) { kfree(n); break; }
        int type = (int)(UINT64)sl->Parameters.Others.Argument1;
        const char *s = NULL;
        size_t len = 0;
        if (type == 0) { s = n->hwids + strlen(n->hwids) + 1; if (!*s) s = n->hwids; len = strlen(s); }  /* the ID without REV */
        else if (type == 1) { s = n->hwids; len = multi_len(n->hwids); }
        else if (type == 2) { s = n->compat; len = multi_len(n->compat); }
        else if (type == 3) {
            s = n->instance;
            for (const char *c = n->instance; *c; c++) if (*c == '\\') s = c + 1;
            len = strlen(s);
        }
        if (s) {
            NT_WCHAR *w = pool_wide(s, len);
            if (w) { irp->IoStatus.Information = (NT_ULONG_PTR)w; st = NT_STATUS_SUCCESS; }
            else st = NT_STATUS_INSUFFICIENT_RESOURCES;
        }
        kfree(n);
        break;
    }
    case NT_IRP_MN_QUERY_DEVICE_TEXT: {
        PnpNode *n = kmalloc(sizeof(PnpNode));
        int type = (int)(UINT64)sl->Parameters.Others.Argument1;
        if (n && PnpGet(v->node, n) && type <= 1) {
            const char *s = type ? n->location : n->friendly[0] ? n->friendly : n->desc;
            NT_WCHAR *w = pool_wide(s, strlen(s));
            if (w) { irp->IoStatus.Information = (NT_ULONG_PTR)w; st = NT_STATUS_SUCCESS; }
            else st = NT_STATUS_INSUFFICIENT_RESOURCES;
        }
        kfree(n);
        break;
    }
    case NT_IRP_MN_QUERY_INTERFACE: {
        NT_STATUS q = query_interface(v, sl);
        if (q != NT_STATUS_NOT_SUPPORTED) st = q;
        break;
    }
    case NT_IRP_MN_QUERY_BUS_INFORMATION: {
        static const NT_GUID usb = { 0x9D7DEBBC, 0xC85D, 0x11D1, { 0x9E, 0xB4, 0x00, 0x60, 0x08, 0xC3, 0xA1, 0x9A } };
        struct { NT_GUID BusTypeGuid; int LegacyBusType; unsigned int BusNumber; } *b =
            nt_ExAllocatePoolWithTag(1, sizeof(*b), TAG_USBD);
        if (!b) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
        b->BusTypeGuid = usb;                             /* GUID_BUS_TYPE_USB */
        b->LegacyBusType = -1;                            /* PNPBus... InterfaceTypeUndefined */
        b->BusNumber = (unsigned)v->usb->hc->num;
        irp->IoStatus.Information = (NT_ULONG_PTR)b;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_DEVICE_RELATIONS:
        if ((int)(UINT64)sl->Parameters.Others.Argument1 == 4) {        /* TargetDeviceRelation */
            struct { unsigned int Count; NT_DEVICE_OBJECT *Objects[1]; } *r =
                nt_ExAllocatePoolWithTag(1, sizeof(*r), TAG_USBD);
            if (!r) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
            r->Count = 1;
            r->Objects[0] = v->pdo;
            irp->IoStatus.Information = (NT_ULONG_PTR)r;
            st = NT_STATUS_SUCCESS;
        }
        break;
    }
    return st;
}

/* IRP_MN_WAIT_WAKE: the device may wake the machine (its remote wakeup
 * armed, SET_FEATURE(DEVICE_REMOTE_WAKEUP), when its configuration says
 * it can).  NovaOS completes it at once: the wake itself is the
 * controller's (xhci.c), and the device stays armed. */
static NT_STATUS pdo_power(UsbdDev *v, NT_IO_STACK_LOCATION *sl)
{
    if (sl->MinorFunction != 0x00) return NT_STATUS_SUCCESS;  /* (D0 always: NovaOS does not suspend USB devices) */
    UsbDev *d = v->usb;
    if (v->gone || d->gone) return ST_NO_SUCH_DEVICE;
    if (!d->cfg || d->cfg_len < 9 || !(d->cfg[7] & 0x20)) return NT_STATUS_NOT_SUPPORTED;
    if (UsbControl(d, 0x00, 3, 1, 0, 0, NULL) < 0) return ST_IO_DEVICE_ERROR;
    kprintf("[USBD] %s: remote wakeup armed\n", d->name);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS pdo_dispatch(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    UsbdDev *v = dev_of(dev);
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    NT_STATUS st;
    bool pending = false;
    switch (sl->MajorFunction) {
    case NT_IRP_MJ_INTERNAL_DEVICE_CONTROL: st = internal_ioctl(v, irp, sl, &pending); break;
    case NT_IRP_MJ_PNP:            st = pdo_pnp(v, irp, sl); break;
    case NT_IRP_MJ_POWER:          st = pdo_power(v, sl); break;
    case NT_IRP_MJ_SYSTEM_CONTROL: st = irp->IoStatus.Status; break;
    default:                       st = NT_STATUS_INVALID_DEVICE_REQUEST; break;
    }
    if (pending) return NT_STATUS_PENDING;
    irp->IoStatus.Status = st;
    nt_IofCompleteRequest(irp, 0);
    return st;
}

/* The device left (the usb thread, from its gone() callbacks) */
static void usb_gone(void *inst)
{
    UsbdDev *v = inst;
    v->gone = true;
    kprintf("[USBD] %s: unplugged\n", v->usb->name);
    NtPnpSurpriseRemoval(v->node);
}

NT_DEVICE_OBJECT *NtUsbPdo(UINT32 node)
{
    UINT32 ifaces = 0;
    UsbDev *d = UsbFnByPnp(node, &ifaces);
    if (!d || d->gone) return NULL;
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < 32; i++)
        if (g_devs[i] && g_devs[i]->usb == d && g_devs[i]->node == node) {
            spin_unlock_irqrestore(&g_lock, s);
            return g_devs[i]->pdo;
        }
    int slot = -1;
    for (int i = 0; i < 32 && slot < 0; i++) if (!g_devs[i]) slot = i;
    spin_unlock_irqrestore(&g_lock, s);
    if (slot < 0) return NULL;
    if (!g_usbd.Type) {
        g_usbd.Type = 4;
        g_usbd.Size = (short)sizeof(g_usbd);
        for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) g_usbd.MajorFunction[i] = pdo_dispatch;
    }
    static int seq;
    char name[48];
    ksnprintf(name, sizeof(name), "\\Device\\USBPDO-%d", seq++);
    NT_WCHAR w[48];
    size_t n = strlen(name);
    for (size_t i = 0; i <= n; i++) w[i] = (UINT8)name[i];
    NT_UNICODE_STRING u = { (unsigned short)(2 * n), (unsigned short)(2 * n + 2), w };
    NT_DEVICE_OBJECT *pdo;
    if (!NT_SUCCESS(nt_IoCreateDevice(&g_usbd, sizeof(UsbdDev), &u, 0x22 /* FILE_DEVICE_USB */, 0x100 /* AUTOGENERATED_NAME */,
                                      0, &pdo)))
        return NULL;
    pdo->Flags = (pdo->Flags & ~NT_DO_DEVICE_INITIALIZING) | NT_DO_BUS_ENUMERATED_DEVICE | NT_DO_POWER_PAGABLE;
    UsbdDev *v = pdo->DeviceExtension;
    v->pdo = pdo;
    v->usb = d;
    v->node = node;
    v->ifaces = ifaces;
    pipe_init(&v->ep0, v);
    g_devs[slot] = v;
    UsbBind(d, v, usb_gone);
    if (ifaces) kprintf("[USBD] %s: %s for %04x:%04x function (interfaces 0x%x)\n", d->name, name, d->vid, d->pid, ifaces);
    else        kprintf("[USBD] %s: %s for %04x:%04x\n", d->name, name, d->vid, d->pid);
    return pdo;
}

bool NtUsbPdoProperty(NT_DEVICE_OBJECT *pdo, int prop, UINT32 *num)
{
    UsbdDev *v = dev_of(pdo);
    if (!v) return false;
    if (prop == 16) *num = v->usb->parent ? v->usb->parent_port : v->usb->root_port;   /* DevicePropertyAddress */
    else if (prop == 14) *num = (UINT32)v->usb->hc->num;                               /* BusNumber */
    else return false;
    return true;
}

/* =======================================================================
 * usbd.sys's exports
 * ======================================================================= */

NT_API void nt_USBD_GetUSBDIVersion(UsbdVersion *v) { bi_version(NULL, v, NULL); }

/* The next interface descriptor from @start that matches (-1: any) */
NT_API UINT8 *nt_USBD_ParseConfigurationDescriptorEx(UINT8 *cfg, UINT8 *start, int num, int alt, int cls, int sub,
                                                     int proto)
{
    if (!cfg) return NULL;
    UINT16 total = (UINT16)(cfg[2] | cfg[3] << 8);
    UINT8 *end = cfg + total;
    for (UINT8 *x = start ? start : cfg; x + 2 <= end && x[0] >= 2; x += x[0]) {
        if (x[1] != USB_DT_INTERFACE || x[0] < 9) continue;
        if ((num == -1 || x[2] == num) && (alt == -1 || x[3] == alt) && (cls == -1 || x[5] == cls) &&
            (sub == -1 || x[6] == sub) && (proto == -1 || x[7] == proto))
            return x;
    }
    return NULL;
}

NT_API UINT8 *nt_USBD_ParseConfigurationDescriptor(UINT8 *cfg, UINT8 num, UINT8 alt)
{
    return nt_USBD_ParseConfigurationDescriptorEx(cfg, cfg, num, alt, -1, -1, -1);
}

/* The first descriptor of @type at or after @start; NULL when none */
NT_API UINT8 *nt_USBD_ParseDescriptors(UINT8 *buf, UINT32 len, UINT8 *start, int type)
{
    for (UINT8 *x = start; x + 2 <= buf + len && x[0] >= 2; x += x[0])
        if (x[1] == type) return x;
    return NULL;
}

/* A SELECT_CONFIGURATION URB for the interfaces of @list (ended by an
 * entry with no descriptor), each entry's Interface pointed into it */
NT_API UrbSelectConfig *nt_USBD_CreateConfigurationRequestEx(UINT8 *cfg, UsbdIfaceListEntry *list)
{
    size_t size = __builtin_offsetof(UrbSelectConfig, Interface);
    for (UsbdIfaceListEntry *e = list; e->InterfaceDescriptor; e++) {
        UINT8 eps = e->InterfaceDescriptor[4];
        size += 24 + sizeof(UsbdPipeInfo) * (eps ? eps : 1);
    }
    UrbSelectConfig *u = nt_ExAllocatePoolWithTag(0, size, TAG_USBD);
    if (!u) return NULL;
    memset(u, 0, size);
    u->Hdr.Length = (UINT16)size;
    u->Hdr.Function = URB_FUNCTION_SELECT_CONFIGURATION;
    u->ConfigurationDescriptor = cfg;
    UsbdIfaceInfo *i = &u->Interface;
    for (UsbdIfaceListEntry *e = list; e->InterfaceDescriptor; e++) {
        UINT8 eps = e->InterfaceDescriptor[4];
        i->Length = (UINT16)(24 + sizeof(UsbdPipeInfo) * (eps ? eps : 1));
        i->InterfaceNumber = e->InterfaceDescriptor[2];
        i->AlternateSetting = e->InterfaceDescriptor[3];
        i->NumberOfPipes = eps;
        for (UINT8 k = 0; k < eps; k++) i->Pipes[k].MaximumTransferSize = 0x10000;
        e->Interface = i;
        i = (UsbdIfaceInfo *)((UINT8 *)i + i->Length);
    }
    return u;
}

/* The older form: one interface, as a list */
NT_API UrbSelectConfig *nt_USBD_CreateConfigurationRequest(UINT8 *cfg, UINT16 *size)
{
    UsbdIfaceListEntry list[2] = { { nt_USBD_ParseConfigurationDescriptorEx(cfg, cfg, -1, -1, -1, -1, -1), NULL },
                                   { NULL, NULL } };
    UrbSelectConfig *u = list[0].InterfaceDescriptor ? nt_USBD_CreateConfigurationRequestEx(cfg, list) : NULL;
    if (size) *size = u ? u->Hdr.Length : 0;
    return u;
}

NT_API UINT32 nt_USBD_GetInterfaceLength(UINT8 *iface, UINT8 *end)
{
    UINT32 len = iface[0];
    for (UINT8 *x = iface + iface[0]; x + 2 <= end && x[0] >= 2 && x[1] != USB_DT_INTERFACE; x += x[0]) len += x[0];
    return len;
}

NT_API NT_STATUS nt_USBD_ValidateConfigurationDescriptor(UINT8 *cfg, UINT32 len, UINT16 level, UINT8 **offset,
                                                         UINT32 tag)
{
    (void)level; (void)tag;
    if (offset) *offset = NULL;
    if (!cfg || len < 9 || cfg[1] != USB_DT_CONFIG || (UINT32)(cfg[2] | cfg[3] << 8) > len) {
        if (offset) *offset = cfg;
        return NT_STATUS_INVALID_PARAMETER;
    }
    return NT_STATUS_SUCCESS;
}
