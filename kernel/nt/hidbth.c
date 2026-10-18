/*
 * hidbth.c — NovaOS's HID minidriver for Bluetooth devices (Windows'
 * hidbth.sys)
 *
 * Windows runs a paired Bluetooth mouse, keyboard or game pad with
 * hidbth.sys under hidclass.sys: hidbth.inf matches the HID service's
 * node bthport makes (BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}...),
 * and hidbth speaks the Bluetooth HID Profile (HIDP) on L2CAP's control
 * and interrupt channels.  NovaOS ships neither, so this is its own, built
 * into the kernel as the service "HidBth" and registered with the HID
 * class driver (hidclass.c) as hidusb.c is; the PnP manager installs it on
 * such a node (pnp.c) as hidbth.inf would.  It reaches the device through
 * bthport.c's functions for HID (bthport.h): Windows' hidbth sends BRBs
 * (IOCTL_INTERNAL_BTH_SUBMIT_BRB), which NovaOS does not have yet.
 *
 *   - start: the device's report descriptor and IDs, which pairing read
 *     from its SDP records (HIDDescriptorList, PnP Information); then a
 *     thread connects (ACL link, authentication, encryption, PSM 0x11
 *     then 0x13), and again whenever the link drops, every ten seconds
 *     while the device is away;
 *   - IOCTL_HID_READ_REPORT: the next DATA (Input) frame on the interrupt
 *     channel (0xA1, then the report);
 *   - IOCTL_HID_WRITE_REPORT and SET_OUTPUT_REPORT: DATA (Output) on the
 *     interrupt channel (0xA2); SET_FEATURE: SET_REPORT (Feature) on the
 *     control channel (0x53);
 *   - attributes and the product string from what SDP gave.
 *
 * Not done yet: GET_REPORT (the control channel's DATA answer), the
 * device connecting to us (virtual cable reconnection), suspend.
 */
#include "ntapi.h"
#include "hidclass.h"
#include "bthport.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

NT_API void nt_IoReleaseCancelSpinLock(UINT8 irql);

#define NT_IRP_MJ_INTERNAL_DEVICE_CONTROL 0x0F
#define ST_DEVICE_NOT_CONNECTED ((NT_STATUS)0xC000009D)
#define ST_INVALID_BUFFER_SIZE  ((NT_STATUS)0xC0000206)
#define MAX_READS 4

typedef struct {                       /* (the minidriver's device extension) */
    NT_DEVICE_OBJECT *fdo, *pdo;
    BthHidInfo     info;
    HidDescriptor  hid;
    KSpinLock      lock;
    NT_IRP        *reads[MAX_READS];   /* hidclass's waiting reads */
    volatile bool  stopping, linked;
    NT_KEVENT      thread_done;
    bool           thread;
} Hb;

static HidDeviceExtension *ext(NT_DEVICE_OBJECT *fdo) { return fdo->DeviceExtension; }
static Hb *hb(NT_DEVICE_OBJECT *fdo) { return ext(fdo)->MiniDeviceExtension; }

static void done(NT_IRP *irp, NT_STATUS st, NT_ULONG_PTR info)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    nt_IofCompleteRequest(irp, 0);
}

/* A waiting read out of the table (NULL: none, or it is being cancelled) */
static NT_IRP *take_read(Hb *h, NT_IRP *which)
{
    NT_IRP *irp = NULL;
    IrqState s = spin_lock_irqsave(&h->lock);
    for (int i = 0; i < MAX_READS && !irp; i++) {
        if (!h->reads[i] || (which && h->reads[i] != which)) continue;
        if (!which && !__atomic_exchange_n(&h->reads[i]->CancelRoutine, NULL, __ATOMIC_ACQ_REL)) continue;
        irp = h->reads[i];
        h->reads[i] = NULL;
    }
    spin_unlock_irqrestore(&h->lock, s);
    return irp;
}

static NT_API void read_cancel(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    nt_IoReleaseCancelSpinLock(irp->CancelIrql);
    if (take_read(hb(dev), irp)) done(irp, NT_STATUS_CANCELLED, 0);
}

static void fail_reads(Hb *h, NT_STATUS st)
{
    for (NT_IRP *irp; (irp = take_read(h, NULL));) done(irp, st, 0);
}

/* A frame from the interrupt channel: an input report finishes a read */
static void input(void *ctx, const UINT8 *data, UINT32 len)
{
    Hb *h = ctx;
    if (len < 2 || data[0] != 0xA1) return;                     /* DATA, Input */
    NT_IRP *irp = take_read(h, NULL);
    if (!irp) return;                                           /* (nobody reading: dropped, as a full ring would) */
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    UINT32 cap = sl->Parameters.DeviceIoControl.OutputBufferLength, n = len - 1 < cap ? len - 1 : cap;
    memcpy(irp->UserBuffer, data + 1, n);
    done(irp, NT_STATUS_SUCCESS, n);
}

/* Connect, and again when the link drops */
static void link_thread(void *arg)
{
    Hb *h = arg;
    int wait = 0;
    while (!h->stopping) {
        bool up = BthHidConnected(h->pdo);
        if (up != h->linked) {
            h->linked = up;
            kprintf("[HIDBTH] %s: %s\n", h->info.name, up ? "connected" : "disconnected");
        }
        if (!up && wait <= 0) {
            if (NT_SUCCESS(BthHidConnect(h->pdo, input, h))) continue;
            wait = 100;                                         /* (ten seconds, then again) */
        }
        INT64 t = -10000LL * 100;
        nt_KeDelayExecutionThread(0, 0, &t);
        wait--;
    }
    nt_KeSetEvent(&h->thread_done, 0, 0);
}

static NT_API NT_STATUS sync_done(NT_DEVICE_OBJECT *d, NT_IRP *irp, void *ctx)
{
    (void)d; (void)irp;
    nt_KeSetEvent(ctx, 0, 0);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;
}

static NT_STATUS forward_sync(NT_DEVICE_OBJECT *fdo, NT_IRP *irp)
{
    NT_KEVENT ev;
    nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    NT_IO_STACK_LOCATION *cur = NtIrpCurrentStack(irp), *next = NtIrpNextStack(irp);
    memcpy(next, cur, __builtin_offsetof(NT_IO_STACK_LOCATION, CompletionRoutine));
    next->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    next->CompletionRoutine = sync_done;
    next->Context = &ev;
    nt_IofCallDriver(ext(fdo)->NextDeviceObject, irp);
    nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    return irp->IoStatus.Status;
}

static NT_STATUS pass_down(NT_DEVICE_OBJECT *fdo, NT_IRP *irp)
{
    irp->CurrentLocation++;
    irp->Tail.Overlay.CurrentStackLocation++;
    return nt_IofCallDriver(ext(fdo)->NextDeviceObject, irp);
}

static void stop(Hb *h)
{
    h->stopping = true;
    if (h->thread) nt_KeWaitForSingleObject(&h->thread_done, 0, 0, 0, NULL);
    h->thread = false;
    fail_reads(h, ST_DEVICE_NOT_CONNECTED);
    BthHidDisconnect(h->pdo);
}

static NT_API NT_STATUS hb_pnp(NT_DEVICE_OBJECT *fdo, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    Hb *h = hb(fdo);
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE: {
        NT_STATUS st = forward_sync(fdo, irp);
        h->fdo = fdo;
        h->pdo = ext(fdo)->PhysicalDeviceObject;
        if (NT_SUCCESS(st) && !BthHidGetInfo(h->pdo, &h->info)) st = NT_STATUS_UNSUCCESSFUL;
        if (NT_SUCCESS(st)) {
            h->hid.bLength = 9;
            h->hid.bDescriptorType = 0x21;
            h->hid.bcdHID = 0x0111;
            h->hid.bCountry = h->info.country;
            h->hid.bNumDescriptors = 1;
            h->hid.bReportType = 0x22;
            h->hid.wReportLength = h->info.rdesc_len;
            h->stopping = false;
            nt_KeInitializeEvent(&h->thread_done, NT_NotificationEvent, 0);
            h->thread = sched_create_thread_ex("hidbth", link_thread, h, PRIO_DEVICE_IO, 16 * 1024) != NULL;
            kprintf("[HIDBTH] %s (%04x:%04x): report descriptor %u bytes\n", h->info.name, h->info.vid, h->info.pid,
                    h->info.rdesc_len);
        }
        done(irp, st, 0);
        return st;
    }
    case NT_IRP_MN_SURPRISE_REMOVAL:
    case NT_IRP_MN_STOP_DEVICE:
        stop(h);
        break;
    case NT_IRP_MN_REMOVE_DEVICE:
        stop(h);
        break;
    }
    return pass_down(fdo, irp);
}

static NT_API NT_STATUS hb_pass(NT_DEVICE_OBJECT *fdo, NT_IRP *irp) { return pass_down(fdo, irp); }

/* A report to the device (its ID byte first, 0 when the device has none):
 * DATA on the interrupt channel, or SET_REPORT on the control one */
static NT_STATUS send_report(Hb *h, UINT8 header, bool intr, HidXferPacket *pk, NT_ULONG_PTR *info)
{
    if (!pk || !pk->reportBuffer || !pk->reportBufferLen || pk->reportBufferLen > 600) return NT_STATUS_INVALID_PARAMETER;
    UINT8 frame[601];
    UINT32 skip = pk->reportId ? 0 : 1;
    frame[0] = header;
    memcpy(frame + 1, pk->reportBuffer + skip, pk->reportBufferLen - skip);
    NT_STATUS st = BthHidSend(h->pdo, intr, frame, 1 + pk->reportBufferLen - skip);
    if (NT_SUCCESS(st)) *info = pk->reportBufferLen;
    return st;
}

static NT_API NT_STATUS hb_ioctl(NT_DEVICE_OBJECT *fdo, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    Hb *h = hb(fdo);
    UINT32 code = sl->Parameters.DeviceIoControl.IoControlCode;
    UINT32 out_len = sl->Parameters.DeviceIoControl.OutputBufferLength;
    UINT8 *out = irp->UserBuffer;
    NT_ULONG_PTR info = 0;
    NT_STATUS st;
    if (!h->info.rdesc && code != IOCTL_HID_DEACTIVATE_DEVICE) { done(irp, ST_DEVICE_NOT_CONNECTED, 0); return ST_DEVICE_NOT_CONNECTED; }
    switch (code) {
    case IOCTL_HID_READ_REPORT: {
        if (h->stopping) { st = ST_DEVICE_NOT_CONNECTED; break; }
        NtIrpMarkPending(irp);
        IrqState s = spin_lock_irqsave(&h->lock);
        int slot = -1;
        for (int i = 0; i < MAX_READS && slot < 0; i++) if (!h->reads[i]) slot = i;
        if (slot >= 0) {
            h->reads[slot] = irp;
            __atomic_store_n(&irp->CancelRoutine, (void *)read_cancel, __ATOMIC_RELEASE);
            if (irp->Cancel && __atomic_exchange_n(&irp->CancelRoutine, NULL, __ATOMIC_ACQ_REL)) {
                h->reads[slot] = NULL;                          /* (cancelled before it waited) */
                slot = -2;
            }
        }
        spin_unlock_irqrestore(&h->lock, s);
        if (slot == -1) { done(irp, NT_STATUS_DEVICE_BUSY, 0); return NT_STATUS_PENDING; }
        if (slot == -2) { done(irp, NT_STATUS_CANCELLED, 0); return NT_STATUS_PENDING; }
        return NT_STATUS_PENDING;
    }
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        if (!out || out_len < sizeof(h->hid)) { st = ST_INVALID_BUFFER_SIZE; break; }
        memcpy(out, &h->hid, sizeof(h->hid));
        info = sizeof(h->hid);
        st = NT_STATUS_SUCCESS;
        break;
    case IOCTL_HID_GET_REPORT_DESCRIPTOR:
        if (!out || out_len < h->info.rdesc_len) { st = ST_INVALID_BUFFER_SIZE; break; }
        memcpy(out, h->info.rdesc, h->info.rdesc_len);
        info = h->info.rdesc_len;
        st = NT_STATUS_SUCCESS;
        break;
    case IOCTL_HID_GET_DEVICE_ATTRIBUTES: {
        if (!out || out_len < sizeof(HidDeviceAttributes)) { st = ST_INVALID_BUFFER_SIZE; break; }
        HidDeviceAttributes *a = (HidDeviceAttributes *)out;
        memset(a, 0, sizeof(*a));
        a->Size = sizeof(*a);
        a->VendorID = h->info.vid;
        a->ProductID = h->info.pid;
        a->VersionNumber = h->info.version;
        info = sizeof(*a);
        st = NT_STATUS_SUCCESS;
        break;
    }
    case IOCTL_HID_WRITE_REPORT:
    case IOCTL_HID_SET_OUTPUT_REPORT:
        st = send_report(h, 0xA2, true, irp->UserBuffer, &info);   /* DATA, Output */
        break;
    case IOCTL_HID_SET_FEATURE:
        st = send_report(h, 0x53, false, irp->UserBuffer, &info);  /* SET_REPORT, Feature */
        break;
    case IOCTL_HID_GET_STRING: {
        UINT32 v = (UINT32)(NT_ULONG_PTR)sl->Parameters.DeviceIoControl.Type3InputBuffer;
        if ((UINT16)v != HID_STRING_ID_IPRODUCT || !h->info.name[0]) { st = NT_STATUS_INVALID_DEVICE_REQUEST; break; }
        if (!out || out_len < 2) { st = ST_INVALID_BUFFER_SIZE; break; }
        UINT32 k = 0;
        for (; h->info.name[k] && 2 * (k + 1) < out_len; k++) ((NT_WCHAR *)out)[k] = (UINT8)h->info.name[k];
        ((NT_WCHAR *)out)[k] = 0;
        info = 2 * (k + 1);
        st = NT_STATUS_SUCCESS;
        break;
    }
    case IOCTL_HID_ACTIVATE_DEVICE:
    case IOCTL_HID_DEACTIVATE_DEVICE:
        st = NT_STATUS_SUCCESS;
        break;
    default:
        st = NT_STATUS_NOT_SUPPORTED;
        break;
    }
    done(irp, st, info);
    return st;
}

static NT_API NT_STATUS hb_add_device(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *fdo)
{
    (void)drv;
    Hb *h = hb(fdo);
    memset(h, 0, sizeof(*h));
    h->lock = (KSpinLock)KSPINLOCK_INIT;   /* (memset already zeroed it) */
    fdo->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    return NT_STATUS_SUCCESS;
}

NTSTATUS NT_API HidBthDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg)
{
    drv->MajorFunction[NT_IRP_MJ_INTERNAL_DEVICE_CONTROL] = hb_ioctl;
    drv->MajorFunction[NT_IRP_MJ_PNP] = hb_pnp;
    drv->MajorFunction[NT_IRP_MJ_POWER] = hb_pass;
    drv->MajorFunction[NT_IRP_MJ_SYSTEM_CONTROL] = hb_pass;
    drv->DriverExtension->AddDevice = hb_add_device;
    HidMinidriverRegistration r;
    memset(&r, 0, sizeof(r));
    r.Revision = HID_REVISION;
    r.DriverObject = drv;
    r.RegistryPath = reg;
    r.DeviceExtensionSize = sizeof(Hb);
    r.DevicesArePolled = 0;
    return nt_HidRegisterMinidriver(&r);
}
