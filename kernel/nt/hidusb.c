/*
 * hidusb.c — NovaOS's HID minidriver for USB devices (Windows' hidusb.sys)
 *
 * Windows runs a USB HID device no vendor driver claims with hidusb.sys
 * (input.inf matches USB\Class_03) under hidclass.sys.  NovaOS ships
 * neither, so this is its own, built into the kernel as the service
 * "HidUsb" and written as a minidriver like any other: it registers with
 * the HID class driver (hidclass.c, HidRegisterMinidriver) and reaches the
 * device only through URBs to its USB PDO (usbd.c), as hidusb.sys does.
 *
 * NovaOS's own USB input drivers (usbhid.c for keyboards, mice and touch,
 * gamepad.c and xpad.c for game controllers) still take the devices they
 * know; this one runs the rest: vendor-defined HID devices (LED and RGB
 * controllers, macro pads, UPS units, programmers), and what usbhid.c
 * declines.  The PnP manager installs it on such a device (pnp.c) as
 * input.inf would.
 *
 *   - start: the device's descriptors, its HID interface (the first with
 *     class 3) selected with its interrupt IN pipe and, when it has one,
 *     interrupt OUT pipe; SET_IDLE(0); the report descriptor;
 *   - IOCTL_HID_READ_REPORT: an interrupt IN transfer, finished when the
 *     device sends a report;
 *   - IOCTL_HID_WRITE_REPORT: interrupt OUT, or SET_REPORT(Output) on the
 *     control pipe when the device has no OUT pipe;
 *   - GET_REPORT / SET_REPORT for input, output and feature reports;
 *   - strings (manufacturer, product, serial number, by index).
 *
 * Not done yet: a composite device's HID function (NovaOS's USB PDOs are
 * whole devices: one HID interface each), and selective suspend.
 */
#include "ntapi.h"
#include "hidclass.h"
#include "usbdi.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

NT_API void *nt_MmMapLockedPagesSpecifyCache(NT_MDL *m, char mode, int cache, void *requested, UINT32 bugcheck,
                                             UINT32 priority);
NT_API NT_IRP *nt_IoAllocateIrp(char stack_size, UINT8 charge_quota);
NT_API void  nt_IoFreeIrp(NT_IRP *irp);

#define NT_IRP_MJ_INTERNAL_DEVICE_CONTROL 0x0F
#define ST_DEVICE_NOT_CONNECTED ((NT_STATUS)0xC000009D)
#define ST_INVALID_BUFFER_SIZE  ((NT_STATUS)0xC0000206)

typedef struct {                       /* (the minidriver's device extension) */
    UINT8          dev[18];            /* the device descriptor */
    UINT8         *cfg;                /* the configuration descriptor */
    UINT32         cfg_len;
    UINT8          iface;              /* the HID interface's number */
    HidDescriptor  hid;
    UINT8         *rdesc;              /* the report descriptor */
    void          *pipe_in, *pipe_out; /* interrupt IN and OUT (NULL: none) */
    UINT32         in_mps;
} HuDev;

static HidDeviceExtension *ext(NT_DEVICE_OBJECT *fdo) { return fdo->DeviceExtension; }
static HuDev *hu(NT_DEVICE_OBJECT *fdo) { return ext(fdo)->MiniDeviceExtension; }

/* ---- URBs to the device's PDO ---- */

static NT_STATUS urb_sync(NT_DEVICE_OBJECT *fdo, void *urb)
{
    NT_DEVICE_OBJECT *next = ext(fdo)->NextDeviceObject;
    NT_IRP *irp = nt_IoAllocateIrp(next->StackSize, 0);
    if (!irp) return NT_STATUS_INSUFFICIENT_RESOURCES;
    NT_KEVENT ev;
    NT_IO_STATUS_BLOCK iosb = { { 0 }, 0 };
    nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    irp->UserEvent = &ev;
    irp->UserIosb = &iosb;
    NT_IO_STACK_LOCATION *sl = NtIrpNextStack(irp);
    sl->MajorFunction = NT_IRP_MJ_INTERNAL_DEVICE_CONTROL;
    sl->Parameters.Others.Argument1 = urb;
    sl->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    nt_IofCallDriver(next, irp);
    nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    nt_IoFreeIrp(irp);
    return iosb.Status;
}

/* A descriptor: from the device (@fn GET_DESCRIPTOR_FROM_DEVICE) or the
 * HID interface (GET_DESCRIPTOR_FROM_INTERFACE); the bytes, or -1 */
static int get_desc(NT_DEVICE_OBJECT *fdo, UINT16 fn, UINT8 type, UINT8 index, UINT16 windex, void *buf, UINT32 len)
{
    UrbXfer u;
    memset(&u, 0, sizeof(u));
    u.Hdr.Length = sizeof(u);
    u.Hdr.Function = fn;
    u.TransferBufferLength = len;
    u.TransferBuffer = buf;
    u.Setup[2] = index;                          /* (_URB_CONTROL_DESCRIPTOR_REQUEST: Index, DescriptorType, LanguageId) */
    u.Setup[3] = type;
    u.Setup[4] = (UINT8)windex;
    u.Setup[5] = (UINT8)(windex >> 8);
    NT_STATUS st = urb_sync(fdo, &u);
    return NT_SUCCESS(st) ? (int)u.TransferBufferLength : -1;
}

/* A class request to the HID interface: GET_REPORT, SET_REPORT, SET_IDLE */
static NT_STATUS class_req(NT_DEVICE_OBJECT *fdo, bool in, UINT8 req, UINT16 value, void *buf, UINT32 len, UINT32 *moved)
{
    UrbXfer u;
    memset(&u, 0, sizeof(u));
    u.Hdr.Length = sizeof(u);
    u.Hdr.Function = URB_FUNCTION_CLASS_INTERFACE;
    u.TransferFlags = (in ? USBD_TRANSFER_DIRECTION_IN : 0) | USBD_SHORT_TRANSFER_OK;
    u.TransferBufferLength = len;
    u.TransferBuffer = buf;
    u.Setup[1] = req;
    u.Setup[2] = (UINT8)value;
    u.Setup[3] = (UINT8)(value >> 8);
    u.Setup[4] = hu(fdo)->iface;
    NT_STATUS st = urb_sync(fdo, &u);
    if (moved) *moved = u.TransferBufferLength;
    return st;
}

static NT_STATUS interrupt_out(NT_DEVICE_OBJECT *fdo, void *buf, UINT32 len)
{
    UrbXfer u;
    memset(&u, 0, sizeof(u));
    u.Hdr.Length = sizeof(u);
    u.Hdr.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    u.PipeHandle = hu(fdo)->pipe_out;
    u.TransferBufferLength = len;
    u.TransferBuffer = buf;
    return urb_sync(fdo, &u);
}

/* ---- start ---- */

static NT_STATUS configure(NT_DEVICE_OBJECT *fdo)
{
    HuDev *h = hu(fdo);
    UINT8 head[9];
    if (get_desc(fdo, URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE, 1, 0, 0, h->dev, sizeof(h->dev)) != sizeof(h->dev) ||
        get_desc(fdo, URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE, 2, 0, 0, head, sizeof(head)) != sizeof(head))
        return ST_DEVICE_NOT_CONNECTED;
    h->cfg_len = (UINT32)(head[2] | head[3] << 8);
    h->cfg = h->cfg_len >= 9 ? kmalloc(h->cfg_len) : NULL;
    if (!h->cfg) return NT_STATUS_INSUFFICIENT_RESOURCES;
    if (get_desc(fdo, URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE, 2, 0, 0, h->cfg, h->cfg_len) != (int)h->cfg_len)
        return ST_DEVICE_NOT_CONNECTED;

    /* The HID interface, its class descriptor and its interrupt endpoints */
    UINT8 *ifd = NULL, *hidd = NULL;
    for (UINT32 off = 0; off + 2 <= h->cfg_len && h->cfg[off] >= 2; off += h->cfg[off]) {
        UINT8 *d = &h->cfg[off];
        if (d[1] == 4 && d[0] >= 9) {
            if (ifd) break;                                       /* (past the HID interface) */
            if (d[5] == 3 && d[3] == 0) ifd = d;
        } else if (ifd && d[1] == 0x21 && d[0] >= 9) {
            hidd = d;
        }
    }
    if (!ifd || !hidd || hidd[6] != 0x22) {
        kprintf("[HIDUSB] %04x:%04x: no HID interface with a report descriptor\n",
                h->dev[8] | h->dev[9] << 8, h->dev[10] | h->dev[11] << 8);
        return NT_STATUS_UNSUCCESSFUL;
    }
    h->iface = ifd[2];
    h->hid.bLength = 9;
    h->hid.bDescriptorType = 0x21;
    h->hid.bcdHID = (UINT16)(hidd[2] | hidd[3] << 8);
    h->hid.bCountry = hidd[4];
    h->hid.bNumDescriptors = 1;
    h->hid.bReportType = 0x22;
    h->hid.wReportLength = (UINT16)(hidd[7] | hidd[8] << 8);

    UsbdIfaceListEntry list[2] = { { ifd, NULL }, { NULL, NULL } };
    UrbSelectConfig *sel = nt_USBD_CreateConfigurationRequestEx(h->cfg, list);
    if (!sel) return NT_STATUS_INSUFFICIENT_RESOURCES;
    NT_STATUS st = urb_sync(fdo, sel);
    if (NT_SUCCESS(st)) {
        UsbdIfaceInfo *i = list[0].Interface;
        for (UINT32 k = 0; k < i->NumberOfPipes; k++) {
            UsbdPipeInfo *p = &i->Pipes[k];
            if (p->PipeType != 3) continue;                       /* UsbdPipeTypeInterrupt */
            if ((p->EndpointAddress & 0x80) && !h->pipe_in) { h->pipe_in = p->PipeHandle; h->in_mps = p->MaximumPacketSize; }
            else if (!(p->EndpointAddress & 0x80) && !h->pipe_out) h->pipe_out = p->PipeHandle;
        }
    }
    nt_ExFreePool(sel);
    if (!NT_SUCCESS(st)) return st;
    if (!h->pipe_in) return NT_STATUS_UNSUCCESSFUL;

    class_req(fdo, false, 0x0A, 0, NULL, 0, NULL);               /* SET_IDLE(0): reports on change only (may stall) */
    h->rdesc = kmalloc(h->hid.wReportLength);
    if (!h->rdesc) return NT_STATUS_INSUFFICIENT_RESOURCES;
    int n = get_desc(fdo, URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE, 0x22, 0, h->iface, h->rdesc, h->hid.wReportLength);
    if (n <= 0) return ST_DEVICE_NOT_CONNECTED;
    h->hid.wReportLength = (UINT16)n;
    kprintf("[HIDUSB] %04x:%04x: interface %u, report descriptor %d bytes, interrupt IN%s\n",
            h->dev[8] | h->dev[9] << 8, h->dev[10] | h->dev[11] << 8, h->iface, n, h->pipe_out ? " and OUT" : "");
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS sync_done(NT_DEVICE_OBJECT *d, NT_IRP *irp, void *ctx)
{
    (void)d; (void)irp;
    nt_KeSetEvent(ctx, 0, 0);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;
}

/* Down the stack (to the USB PDO) and back: its status */
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

/* IoSkipCurrentIrpStackLocation and pass it down */
static NT_STATUS pass_down(NT_DEVICE_OBJECT *fdo, NT_IRP *irp)
{
    irp->CurrentLocation++;
    irp->Tail.Overlay.CurrentStackLocation++;
    return nt_IofCallDriver(ext(fdo)->NextDeviceObject, irp);
}

static void done(NT_IRP *irp, NT_STATUS st, NT_ULONG_PTR info)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    nt_IofCompleteRequest(irp, 0);
}

static NT_API NT_STATUS hu_pnp(NT_DEVICE_OBJECT *fdo, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    HuDev *h = hu(fdo);
    if (sl->MinorFunction == NT_IRP_MN_START_DEVICE) {
        NT_STATUS st = forward_sync(fdo, irp);
        if (NT_SUCCESS(st)) st = configure(fdo);
        done(irp, st, 0);
        return st;
    }
    if (sl->MinorFunction == NT_IRP_MN_REMOVE_DEVICE) {
        NT_STATUS st = pass_down(fdo, irp);
        kfree(h->cfg);
        kfree(h->rdesc);
        h->cfg = h->rdesc = NULL;
        h->pipe_in = h->pipe_out = NULL;
        return st;
    }
    return pass_down(fdo, irp);
}

static NT_API NT_STATUS hu_pass(NT_DEVICE_OBJECT *fdo, NT_IRP *irp) { return pass_down(fdo, irp); }

/* ---- hidclass's requests ---- */

static NT_API NT_STATUS read_done(NT_DEVICE_OBJECT *d, NT_IRP *irp, void *ctx)
{
    (void)d;
    UrbXfer *u = ctx;
    if (NT_SUCCESS(irp->IoStatus.Status)) irp->IoStatus.Information = u->TransferBufferLength;
    kfree(u);
    if (irp->PendingReturned) NtIrpMarkPending(irp);
    return NT_STATUS_SUCCESS;
}

/* IOCTL_HID_READ_REPORT: an interrupt IN transfer into the request's buffer */
static NT_STATUS read_report(NT_DEVICE_OBJECT *fdo, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    HuDev *h = hu(fdo);
    UrbXfer *u = kzalloc(sizeof(*u));
    if (!u) { done(irp, NT_STATUS_INSUFFICIENT_RESOURCES, 0); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    u->Hdr.Length = sizeof(*u);
    u->Hdr.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    u->PipeHandle = h->pipe_in;
    u->TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
    u->TransferBufferLength = sl->Parameters.DeviceIoControl.OutputBufferLength;
    u->TransferBuffer = irp->UserBuffer;
    NT_IO_STACK_LOCATION *next = NtIrpNextStack(irp);
    memset(next, 0, sizeof(*next));
    next->MajorFunction = NT_IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.Others.Argument1 = u;
    next->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    next->CompletionRoutine = read_done;
    next->Context = u;
    next->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    return nt_IofCallDriver(ext(fdo)->NextDeviceObject, irp);
}

/* A report by GET_REPORT (@type 1 input, 2 output, 3 feature) into
 * @pk, its ID byte first: the bytes, with that byte */
static NT_STATUS get_report(NT_DEVICE_OBJECT *fdo, int type, HidXferPacket *pk, NT_ULONG_PTR *info)
{
    if (!pk || !pk->reportBuffer || !pk->reportBufferLen) return NT_STATUS_INVALID_PARAMETER;
    UINT8 *buf = pk->reportBuffer + (pk->reportId ? 0 : 1);       /* (no ID: the device sends the report alone) */
    UINT32 len = pk->reportBufferLen - (pk->reportId ? 0 : 1), moved = 0;
    NT_STATUS st = class_req(fdo, true, 0x01, (UINT16)(type << 8 | pk->reportId), buf, len, &moved);
    if (!NT_SUCCESS(st)) return st;
    if (!pk->reportId) pk->reportBuffer[0] = 0;
    *info = moved + (pk->reportId ? 0 : 1);
    return NT_STATUS_SUCCESS;
}

/* A report by SET_REPORT, or by the interrupt OUT pipe (@out) */
static NT_STATUS set_report(NT_DEVICE_OBJECT *fdo, int type, HidXferPacket *pk, bool out, NT_ULONG_PTR *info)
{
    if (!pk || !pk->reportBuffer || !pk->reportBufferLen) return NT_STATUS_INVALID_PARAMETER;
    UINT8 *buf = pk->reportBuffer + (pk->reportId ? 0 : 1);
    UINT32 len = pk->reportBufferLen - (pk->reportId ? 0 : 1);
    NT_STATUS st = out && hu(fdo)->pipe_out ? interrupt_out(fdo, buf, len)
                                            : class_req(fdo, false, 0x09, (UINT16)(type << 8 | pk->reportId), buf, len, NULL);
    if (NT_SUCCESS(st)) *info = pk->reportBufferLen;
    return st;
}

/* String descriptor @index as UTF-16 with a NUL, into @out (@cap bytes) */
static NT_STATUS get_string(NT_DEVICE_OBJECT *fdo, UINT8 index, UINT16 lang, UINT8 *out, UINT32 cap, NT_ULONG_PTR *info)
{
    if (!index) return NT_STATUS_INVALID_DEVICE_REQUEST;
    if (!out || cap < 2) return ST_INVALID_BUFFER_SIZE;
    UINT8 s[256];
    int n = get_desc(fdo, URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE, 3, index, lang ? lang : 0x0409, s, sizeof(s));
    if (n < 2 || s[1] != 3) return NT_STATUS_INVALID_DEVICE_REQUEST;
    UINT32 chars = (UINT32)((s[0] < n ? s[0] : n) - 2) / 2, k = 0;
    for (; k < chars && 2 * (k + 1) < cap; k++) ((NT_WCHAR *)out)[k] = (NT_WCHAR)(s[2 + 2 * k] | s[3 + 2 * k] << 8);
    ((NT_WCHAR *)out)[k] = 0;
    *info = 2 * (k + 1);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS hu_ioctl(NT_DEVICE_OBJECT *fdo, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    HuDev *h = hu(fdo);
    UINT32 code = sl->Parameters.DeviceIoControl.IoControlCode;
    UINT32 out_len = sl->Parameters.DeviceIoControl.OutputBufferLength;
    UINT8 *out = irp->UserBuffer;
    NT_ULONG_PTR info = 0;
    NT_STATUS st;
    if (!h->rdesc && code != IOCTL_HID_DEACTIVATE_DEVICE) { done(irp, ST_DEVICE_NOT_CONNECTED, 0); return ST_DEVICE_NOT_CONNECTED; }
    switch (code) {
    case IOCTL_HID_READ_REPORT:
        return read_report(fdo, irp, sl);
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        if (!out || out_len < sizeof(h->hid)) { st = ST_INVALID_BUFFER_SIZE; break; }
        memcpy(out, &h->hid, sizeof(h->hid));
        info = sizeof(h->hid);
        st = NT_STATUS_SUCCESS;
        break;
    case IOCTL_HID_GET_REPORT_DESCRIPTOR:
        if (!out || out_len < h->hid.wReportLength) { st = ST_INVALID_BUFFER_SIZE; break; }
        memcpy(out, h->rdesc, h->hid.wReportLength);
        info = h->hid.wReportLength;
        st = NT_STATUS_SUCCESS;
        break;
    case IOCTL_HID_GET_DEVICE_ATTRIBUTES: {
        if (!out || out_len < sizeof(HidDeviceAttributes)) { st = ST_INVALID_BUFFER_SIZE; break; }
        HidDeviceAttributes *a = (HidDeviceAttributes *)out;
        memset(a, 0, sizeof(*a));
        a->Size = sizeof(*a);
        a->VendorID = (UINT16)(h->dev[8] | h->dev[9] << 8);
        a->ProductID = (UINT16)(h->dev[10] | h->dev[11] << 8);
        a->VersionNumber = (UINT16)(h->dev[12] | h->dev[13] << 8);
        info = sizeof(*a);
        st = NT_STATUS_SUCCESS;
        break;
    }
    case IOCTL_HID_WRITE_REPORT:
        st = set_report(fdo, 2, irp->UserBuffer, true, &info);
        break;
    case IOCTL_HID_SET_OUTPUT_REPORT: st = set_report(fdo, 2, irp->UserBuffer, false, &info); break;
    case IOCTL_HID_SET_FEATURE:       st = set_report(fdo, 3, irp->UserBuffer, false, &info); break;
    case IOCTL_HID_GET_INPUT_REPORT:  st = get_report(fdo, 1, irp->UserBuffer, &info); break;
    case IOCTL_HID_GET_OUTPUT_REPORT: st = get_report(fdo, 2, irp->UserBuffer, &info); break;
    case IOCTL_HID_GET_FEATURE:       st = get_report(fdo, 3, irp->UserBuffer, &info); break;
    case IOCTL_HID_GET_STRING: {
        UINT32 v = (UINT32)(NT_ULONG_PTR)sl->Parameters.DeviceIoControl.Type3InputBuffer;
        UINT16 id = (UINT16)v;
        UINT8 index = id == HID_STRING_ID_IMANUFACTURER ? h->dev[14] : id == HID_STRING_ID_IPRODUCT ? h->dev[15] :
                      id == HID_STRING_ID_ISERIALNUMBER ? h->dev[16] : 0;
        st = get_string(fdo, index, (UINT16)(v >> 16), out, out_len, &info);
        break;
    }
    case IOCTL_HID_GET_INDEXED_STRING: {
        UINT32 v = (UINT32)(NT_ULONG_PTR)sl->Parameters.DeviceIoControl.Type3InputBuffer;
        UINT8 *buf = irp->MdlAddress ? nt_MmMapLockedPagesSpecifyCache(irp->MdlAddress, 0, 1, NULL, 0, 16) : out;
        st = get_string(fdo, (UINT8)v, (UINT16)(v >> 16), buf, out_len, &info);
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

static NT_API NT_STATUS hu_add_device(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *fdo)
{
    (void)drv;
    memset(hu(fdo), 0, sizeof(HuDev));
    fdo->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    return NT_STATUS_SUCCESS;
}

NTSTATUS NT_API HidUsbDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg)
{
    drv->MajorFunction[NT_IRP_MJ_INTERNAL_DEVICE_CONTROL] = hu_ioctl;
    drv->MajorFunction[NT_IRP_MJ_PNP] = hu_pnp;
    drv->MajorFunction[NT_IRP_MJ_POWER] = hu_pass;
    drv->MajorFunction[NT_IRP_MJ_SYSTEM_CONTROL] = hu_pass;
    drv->DriverExtension->AddDevice = hu_add_device;
    HidMinidriverRegistration r;
    memset(&r, 0, sizeof(r));
    r.Revision = HID_REVISION;
    r.DriverObject = drv;
    r.RegistryPath = reg;
    r.DeviceExtensionSize = sizeof(HuDev);
    r.DevicesArePolled = 0;
    return nt_HidRegisterMinidriver(&r);
}
