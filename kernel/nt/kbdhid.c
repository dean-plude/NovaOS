/*
 * kbdhid.c — the keyboard and mouse class drivers on HID collections
 * (Windows' kbdhid.sys and mouhid.sys)
 *
 * On Windows a USB keyboard or mouse is a HID device like any other:
 * hidusb.sys moves its reports, hidclass.sys makes a PDO for each of its
 * top-level collections, and keyboard.inf / msmouse.inf install kbdhid.sys
 * on a keyboard collection (HID_DEVICE_SYSTEM_KEYBOARD) and mouhid.sys on
 * a mouse (HID_DEVICE_SYSTEM_MOUSE).  Those open their collection, read
 * its reports and hand keystrokes and pointer motion on to win32k.  NovaOS
 * runs its own USB keyboards, mice, tablets and media keys the same way:
 * usb.c leaves them to Plug and Play, which starts HidUsb (hidusb.c) under
 * the HID class driver (hidclass.c), then one of these on each collection
 * (pnp.c: the inbox match keyboard.inf and msmouse.inf would make).
 *
 * Both services are this one driver, built into the kernel as hidusb.c
 * is: "kbdhid" takes keyboards and the consumer- and system-control
 * collections (media, browser, power and sleep keys), "mouhid" mice and
 * absolute pointers (QEMU's USB tablet).  On each device:
 *
 *   - start: the collection is opened (IRP_MJ_CREATE with a file object of
 *     the driver's own, as kbdhid.sys opens its collection), its report
 *     lengths and the device's report descriptor read
 *     (IOCTL_HID_NOVA_GET_REPORT_DESCRIPTOR) and given to the report parser
 *     usbhid.c, I2C-HID and these share (drivers/hid.h);
 *   - a thread keeps an IRP_MJ_READ outstanding and hands each report the
 *     collection gives to the parser, which posts the keys and pointer
 *     events (wm/input.h): the same events every other input device makes,
 *     so the desktop, Raw Input and DirectInput see them as before;
 *   - the keyboard LEDs (Caps, Num and Scroll Lock) follow the lock keys
 *     as output reports (IRP_MJ_WRITE on the collection);
 *   - a keyboard asks to wake the machine (IOCTL_HID_ENABLE_WAKE_ON_SX:
 *     on USB its remote wakeup is armed);
 *   - stop, surprise removal, removal: the read is cancelled, every key and
 *     button still down is let go, the collection closed.
 *
 * Every other request passes down to the collection unchanged, so programs
 * still open it (hidapi lists keyboards), as on Windows.
 */
#include "ntapi.h"
#include "hidclass.h"
#include "../drivers/hid.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

NT_API NT_IRP *nt_IoAllocateIrp(char stack_size, UINT8 charge_quota);
NT_API void  nt_IoFreeIrp(NT_IRP *irp);
NT_API UINT8 nt_IoCancelIrp(NT_IRP *irp);

#define NT_IRP_MJ_INTERNAL_DEVICE_CONTROL 0x0F
#define ST_DEVICE_NOT_CONNECTED ((NT_STATUS)0xC000009D)
#define MAX_DEV   16
#define READ_MAX  8                         /* reports a read takes at most */

/* A collection one of the two drivers runs.  (Never freed: the report
 * parser's device and the LED path may still name it after the removal,
 * as usbhid.c keeps its devices.) */
typedef struct {
    NT_DEVICE_OBJECT *fdo, *lower;
    bool              mouse;                /* mouhid's (else kbdhid's) */
    NT_FILE_OBJECT    fo;                   /* its open handle on the collection */
    bool              open;
    void             *hid;                  /* the report parser's device */
    bool              ids;                  /* its reports start with an ID byte */
    UINT16            vid, pid;
    UINT16            in_len, out_len;
    UINT8            *buf;                  /* READ_MAX input reports */
    volatile bool     stopping;
    bool              reader;
    NT_IRP           *read_irp;             /* the read at the collection */
    NT_KEVENT         reader_done;
    UINT32            reports;
    /* LEDs: the newest output report to send (the usb thread hands it
     * over, the LED thread sends it) */
    UINT8             led[16];
    int               led_len;
    volatile bool     led_pending, led_busy;
} Kdev;

static Kdev     *g_dev[MAX_DEV];
static KSpinLock g_lock = KSPINLOCK_INIT;   /* g_dev, read_irp and the LED fields */
static NT_KEVENT g_led_event;
static bool      g_led_thread;

static const char *tag(const Kdev *k) { return k->mouse ? "MOUHID" : "KBDHID"; }
static Kdev *kdev(NT_DEVICE_OBJECT *fdo) { return *(Kdev **)fdo->DeviceExtension; }

/* ---- requests to the collection ---- */

static NT_API NT_STATUS sync_done(NT_DEVICE_OBJECT *d, NT_IRP *irp, void *ctx)
{
    (void)d; (void)irp;
    nt_KeSetEvent(ctx, 0, 0);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;
}

/* An IRP the PnP manager sent, down to the collection and back: its status
 * (the IRP still ours to complete) */
static NT_STATUS forward_sync(Kdev *k, NT_IRP *irp)
{
    NT_KEVENT ev;
    nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    NT_IO_STACK_LOCATION *cur = NtIrpCurrentStack(irp), *next = NtIrpNextStack(irp);
    memcpy(next, cur, __builtin_offsetof(NT_IO_STACK_LOCATION, CompletionRoutine));
    next->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    next->CompletionRoutine = sync_done;
    next->Context = &ev;
    nt_IofCallDriver(k->lower, irp);
    nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    return irp->IoStatus.Status;
}

static NT_STATUS pass_down(Kdev *k, NT_IRP *irp)
{
    irp->CurrentLocation++;
    irp->Tail.Overlay.CurrentStackLocation++;
    return nt_IofCallDriver(k->lower, irp);
}

/* A request of the driver's own on its handle (@major; an I/O control
 * @code with @sys its system buffer of @in_len / @out_len bytes, or
 * @user its METHOD_NEITHER buffer; a read or write @sys of @out_len
 * bytes): waits for it; *@info the bytes it moved.  With @track, the IRP
 * is kept in read_irp while it runs (stop cancels it). */
static NT_STATUS request(Kdev *k, UINT8 major, UINT32 code, void *sys, UINT32 in_len, void *user, UINT32 out_len,
                         NT_ULONG_PTR *info, bool track)
{
    NT_IRP *irp = nt_IoAllocateIrp(k->lower->StackSize, 0);
    if (!irp) return NT_STATUS_INSUFFICIENT_RESOURCES;
    NT_KEVENT ev;
    NT_IO_STATUS_BLOCK iosb = { { 0 }, 0 };
    nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    irp->UserEvent = &ev;
    irp->UserIosb = &iosb;
    irp->RequestorMode = 0;
    irp->AssociatedIrp.SystemBuffer = sys;
    irp->UserBuffer = user;
    irp->IoStatus.Status = NT_STATUS_NOT_SUPPORTED;
    NT_IO_STACK_LOCATION *sl = NtIrpNextStack(irp);
    sl->MajorFunction = major;
    sl->FileObject = &k->fo;
    if (major == NT_IRP_MJ_READ || major == NT_IRP_MJ_WRITE) {
        sl->Parameters.Read.Length = out_len;
    } else if (major == NT_IRP_MJ_DEVICE_CONTROL || major == NT_IRP_MJ_INTERNAL_DEVICE_CONTROL) {
        sl->Parameters.DeviceIoControl.IoControlCode = code;
        sl->Parameters.DeviceIoControl.InputBufferLength = in_len;
        sl->Parameters.DeviceIoControl.OutputBufferLength = out_len;
    }
    if (track) {
        IrqState s = spin_lock_irqsave(&g_lock);
        bool stop = k->stopping;
        if (!stop) k->read_irp = irp;
        spin_unlock_irqrestore(&g_lock, s);
        if (stop) { nt_IoFreeIrp(irp); return NT_STATUS_CANCELLED; }
    }
    nt_IofCallDriver(k->lower, irp);
    nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    if (track) {
        IrqState s = spin_lock_irqsave(&g_lock);
        k->read_irp = NULL;
        spin_unlock_irqrestore(&g_lock, s);
    }
    nt_IoFreeIrp(irp);
    if (info) *info = iosb.Information;
    return iosb.Status;
}

/* ---- input ---- */

static void reader(void *arg)
{
    Kdev *k = arg;
    int errors = 0;
    while (!k->stopping) {
        NT_ULONG_PTR n = 0;
        NT_STATUS st = request(k, NT_IRP_MJ_READ, 0, k->buf, 0, NULL, (UINT32)k->in_len * READ_MAX, &n, true);
        if (NT_SUCCESS(st)) {
            errors = 0;
            UINT64 now = sched_ticks();
            for (NT_ULONG_PTR at = 0; at + k->in_len <= n; at += k->in_len) {
                if (!k->reports++) kprintf("[%s] %04x:%04x: first input report\n", tag(k), k->vid, k->pid);
                /* (hidclass's reports start with their ID byte, 0 when the
                 * device has none; the parser wants them as the device sends them) */
                const UINT8 *r = k->buf + at;
                HidInputAt(k->hid, k->ids ? r : r + 1, k->ids ? k->in_len : k->in_len - 1, now);
            }
        } else if (st == ST_DEVICE_NOT_CONNECTED || st == NT_STATUS_CANCELLED) {
            break;                                           /* (gone, or being stopped) */
        } else {
            INT64 wait = -10000LL * (++errors > 10 ? 1000 : 50);
            nt_KeDelayExecutionThread(0, 0, &wait);
        }
    }
    nt_KeSetEvent(&k->reader_done, 0, 0);
}

/* ---- the keyboard LEDs ---- */

/* The parser's output report (the usb thread): to the LED thread */
static void led_out(void *ctx, const UINT8 *report, int len)
{
    Kdev *k = ctx;
    if (!k->out_len || k->stopping) return;
    IrqState s = spin_lock_irqsave(&g_lock);
    memset(k->led, 0, sizeof(k->led));
    k->led_len = k->out_len < sizeof(k->led) ? k->out_len : (int)sizeof(k->led);
    memcpy(k->led, report, (size_t)(len < k->led_len ? len : k->led_len));
    k->led_pending = true;
    spin_unlock_irqrestore(&g_lock, s);
    nt_KeSetEvent(&g_led_event, 0, 0);
}

static void led_thread(void *arg)
{
    (void)arg;
    for (;;) {
        nt_KeWaitForSingleObject(&g_led_event, 0, 0, 0, NULL);
        for (int i = 0; i < MAX_DEV; i++) {
            UINT8 rep[16];
            int len = 0;
            IrqState s = spin_lock_irqsave(&g_lock);
            Kdev *k = g_dev[i];
            if (k && k->led_pending && !k->stopping) {
                memcpy(rep, k->led, sizeof(rep));
                len = k->led_len;
                k->led_pending = false;
                k->led_busy = true;
            }
            spin_unlock_irqrestore(&g_lock, s);
            if (!len) continue;
            NT_STATUS st = request(k, NT_IRP_MJ_WRITE, 0, rep, 0, NULL, (UINT32)len, NULL, false);
            if (!NT_SUCCESS(st)) kprintf("[KBDHID] %04x:%04x: keyboard LEDs not set (0x%08x)\n", k->vid, k->pid, (UINT32)st);
            s = spin_lock_irqsave(&g_lock);
            k->led_busy = false;
            spin_unlock_irqrestore(&g_lock, s);
        }
    }
}

/* ---- starting and stopping ---- */

static NT_STATUS start(Kdev *k)
{
    memset(&k->fo, 0, sizeof(k->fo));
    k->fo.Type = 5;
    k->fo.Size = (short)sizeof(k->fo);
    k->fo.DeviceObject = k->lower;
    NT_STATUS st = request(k, NT_IRP_MJ_CREATE, 0, NULL, 0, NULL, 0, NULL, false);
    if (!NT_SUCCESS(st)) { kprintf("[%s] the collection did not open (0x%08x)\n", tag(k), (UINT32)st); return st; }
    k->open = true;

    HidCollectionInformation ci;
    memset(&ci, 0, sizeof(ci));
    st = request(k, NT_IRP_MJ_DEVICE_CONTROL, IOCTL_HID_GET_COLLECTION_INFORMATION, &ci, 0, NULL, sizeof(ci), NULL, false);
    if (NT_SUCCESS(st)) { k->vid = ci.VendorID; k->pid = ci.ProductID; }

    UINT32 cap = 4096;
    HidNovaReportDescriptor *rd = kmalloc(cap);
    NT_ULONG_PTR n = 0;
    st = rd ? request(k, NT_IRP_MJ_INTERNAL_DEVICE_CONTROL, IOCTL_HID_NOVA_GET_REPORT_DESCRIPTOR, NULL, 0, rd, cap, &n, false)
            : NT_STATUS_INSUFFICIENT_RESOURCES;
    if (!NT_SUCCESS(st) || n < sizeof(*rd) || !rd->InputLength) {
        kprintf("[%s] %04x:%04x: no report descriptor (0x%08x)\n", tag(k), k->vid, k->pid, (UINT32)st);
        kfree(rd);
        return NT_SUCCESS(st) ? NT_STATUS_UNSUCCESSFUL : st;
    }
    k->in_len = rd->InputLength;
    k->out_len = rd->OutputLength;
    const char *kind = NULL;
    k->hid = HidAttach(rd->Descriptor, (int)rd->DescriptorLength, &kind);
    kfree(rd);
    if (!k->hid) {
        kprintf("[%s] %04x:%04x: nothing in its reports NovaOS uses\n", tag(k), k->vid, k->pid);
        return NT_STATUS_UNSUCCESSFUL;
    }
    k->ids = HidReportIds(k->hid);
    k->buf = kmalloc((size_t)k->in_len * READ_MAX);
    if (!k->buf) return NT_STATUS_INSUFFICIENT_RESOURCES;

    /* A keyboard: its LEDs, and it wakes the machine, as Windows lets a
     * keyboard do by default */
    bool wake = false;
    if (!k->mouse && HidIsKeyboard(k->hid)) {
        UINT8 on = 1;
        wake = NT_SUCCESS(request(k, NT_IRP_MJ_DEVICE_CONTROL, IOCTL_HID_ENABLE_WAKE_ON_SX, &on, 1, NULL, 0, NULL, false));
        if (k->out_len) {
            IrqState s = spin_lock_irqsave(&g_lock);
            bool first = !g_led_thread;
            g_led_thread = true;
            spin_unlock_irqrestore(&g_lock, s);
            if (first) {
                nt_KeInitializeEvent(&g_led_event, NT_SynchronizationEvent, 0);
                sched_create_thread_ex("kbdhid-leds", led_thread, NULL, PRIO_DEVICE_IO, 16 * 1024);
            }
            HidSetOutput(k->hid, led_out, k);
        }
    }
    HidStart(k->hid);
    nt_KeInitializeEvent(&k->reader_done, NT_NotificationEvent, 0);
    k->stopping = false;
    k->reader = sched_create_thread_ex(k->mouse ? "mouhid" : "kbdhid", reader, k, PRIO_DEVICE_IO, 16 * 1024) != NULL;
    if (!k->reader) { HidStop(k->hid); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    kprintf("[%s] %04x:%04x: %s, input reports %u bytes%s\n", tag(k), k->vid, k->pid, kind ? kind : "input device",
            k->in_len, wake ? ", wakes the machine" : "");
    return NT_STATUS_SUCCESS;
}

/* The device stops (or went): no more reads, every key and button let go,
 * the collection closed */
static void stop(Kdev *k)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    k->stopping = true;
    if (k->read_irp) nt_IoCancelIrp(k->read_irp);
    spin_unlock_irqrestore(&g_lock, s);
    if (k->reader) {
        nt_KeWaitForSingleObject(&k->reader_done, 0, 0, 0, NULL);
        k->reader = false;
    }
    for (int i = 0; i < 200; i++) {                          /* (an LED report on its way finishes) */
        s = spin_lock_irqsave(&g_lock);
        bool busy = k->led_busy;
        spin_unlock_irqrestore(&g_lock, s);
        if (!busy) break;
        INT64 wait = -10000LL * 10;
        nt_KeDelayExecutionThread(0, 0, &wait);
    }
    if (k->hid) {
        HidStop(k->hid);
        kprintf("[%s] %04x:%04x: removed\n", tag(k), k->vid, k->pid);
        k->hid = NULL;
    }
    if (k->open) {
        request(k, NT_IRP_MJ_CLEANUP, 0, NULL, 0, NULL, 0, NULL, false);
        request(k, NT_IRP_MJ_CLOSE, 0, NULL, 0, NULL, 0, NULL, false);
        k->open = false;
    }
}

static void forget(Kdev *k)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < MAX_DEV; i++) if (g_dev[i] == k) g_dev[i] = NULL;
    spin_unlock_irqrestore(&g_lock, s);
}

/* ---- the driver ---- */

static NT_API NT_STATUS dispatch(NT_DEVICE_OBJECT *fdo, NT_IRP *irp)
{
    Kdev *k = kdev(fdo);
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    if (sl->MajorFunction != NT_IRP_MJ_PNP) return pass_down(k, irp);
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE: {
        NT_STATUS st = forward_sync(k, irp);
        if (NT_SUCCESS(st)) {
            st = start(k);
            if (!NT_SUCCESS(st)) stop(k);
        }
        irp->IoStatus.Status = st;
        nt_IofCompleteRequest(irp, 0);
        return st;
    }
    case NT_IRP_MN_STOP_DEVICE:
    case NT_IRP_MN_SURPRISE_REMOVAL:
        stop(k);
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_REMOVE_DEVICE: {
        stop(k);
        forget(k);
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        NT_STATUS st = pass_down(k, irp);
        nt_IoDetachDevice(k->lower);
        nt_IoDeleteDevice(fdo);
        return st;
    }
    }
    return pass_down(k, irp);
}

static NT_STATUS add_device(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *pdo, bool mouse)
{
    Kdev *k = kzalloc(sizeof(*k));
    if (!k) return NT_STATUS_INSUFFICIENT_RESOURCES;
    int slot = -1;
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < MAX_DEV && slot < 0; i++) if (!g_dev[i]) { slot = i; g_dev[i] = k; }
    spin_unlock_irqrestore(&g_lock, s);
    if (slot < 0) { kfree(k); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    NT_DEVICE_OBJECT *fdo;
    NT_STATUS st = nt_IoCreateDevice(drv, sizeof(Kdev *), NULL, mouse ? 0x0F /* FILE_DEVICE_MOUSE */ : 0x0B /* KEYBOARD */,
                                     0x100 /* AUTOGENERATED_NAME */, 0, &fdo);
    if (!NT_SUCCESS(st)) { forget(k); kfree(k); return st; }
    *(Kdev **)fdo->DeviceExtension = k;
    k->fdo = fdo;
    k->mouse = mouse;
    k->lower = nt_IoAttachDeviceToDeviceStack(fdo, pdo);
    if (!k->lower) { nt_IoDeleteDevice(fdo); forget(k); kfree(k); return NT_STATUS_UNSUCCESSFUL; }
    fdo->StackSize = (char)(k->lower->StackSize + 1);
    fdo->Flags = (fdo->Flags & ~NT_DO_DEVICE_INITIALIZING) | (k->lower->Flags & NT_DO_BUFFERED_IO) | NT_DO_POWER_PAGABLE;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS kbd_add(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *pdo) { return add_device(drv, pdo, false); }
static NT_API NT_STATUS mou_add(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *pdo) { return add_device(drv, pdo, true); }

NTSTATUS NT_API KbdHidDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg)
{
    (void)reg;
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) drv->MajorFunction[i] = dispatch;
    drv->DriverExtension->AddDevice = kbd_add;
    return NT_STATUS_SUCCESS;
}

NTSTATUS NT_API MouHidDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg)
{
    (void)reg;
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) drv->MajorFunction[i] = dispatch;
    drv->DriverExtension->AddDevice = mou_add;
    return NT_STATUS_SUCCESS;
}
