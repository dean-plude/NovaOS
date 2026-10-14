/*
 * usbvideo.c — NovaOS's USB Video Class driver (Windows' usbvideo.sys)
 *
 * Windows runs a USB camera no vendor driver claims with usbvideo.sys
 * (usbvideo.inf matches USB\Class_0E), an AVStream minidriver that Media
 * Foundation reaches through ks.sys.  NovaOS ships neither, so this is
 * its own, built into the kernel as the service "usbvideo" (ldr.c) and
 * written as a WDM function driver like any other: it reaches the camera
 * only through URBs to its USB PDO (usbd.c), and programs reach it only
 * through Media Foundation (userland/mf), which opens its device
 * interface, KSCATEGORY_VIDEO_CAMERA, and sends it the control codes of
 * userland/include/novacam.h.  The PnP manager installs it on a USB
 * device whose video interfaces no other driver took (pnp.c), as
 * usbvideo.inf would.
 *
 * USB Video Class 1.0-1.5 (usb.org's "Universal Serial Bus Device Class
 * Definition for Video Devices"):
 *   - start: the VideoControl interface's header (the UVC version), the
 *     VideoStreaming interface's input header (its endpoint), and each
 *     format (uncompressed: YUY2, NV12 and others by their GUID; MJPEG)
 *     with its frame sizes and intervals: the modes GET_INFO lists;
 *   - START: probe (SET_CUR, then GET_CUR VS_PROBE_CONTROL) and commit
 *     (SET_CUR VS_COMMIT_CONTROL) the chosen format, frame and interval;
 *     the device's answer gives the largest payload;
 *   - READ_FRAME: payloads from the streaming endpoint, each with its
 *     header (FID toggles per frame, EOF ends one, ERR spoils one), joined
 *     into one whole frame in the caller's buffer; an uncompressed frame
 *     of the wrong size is dropped;
 *   - STOP: the pipe is aborted and its halt cleared, as uvcvideo stops
 *     a bulk stream.
 *
 * Not done yet: isochronous streaming endpoints (usbd.c has no
 * isochronous URBs; most built-in laptop cameras stream that way, so
 * the T14's still needs them), camera controls (brightness, exposure:
 * the processing unit's requests), still images, and a camera that is
 * one function of a composite device (NovaOS's USB PDOs are whole
 * devices).
 */
#include "ntapi.h"
#include "usbdi.h"
#include "usbvideo.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../../userland/include/novacam.h"

NT_API void *nt_MmMapLockedPagesSpecifyCache(NT_MDL *m, char mode, int cache, void *requested, UINT32 bugcheck,
                                             UINT32 priority);
NT_API NT_IRP *nt_IoAllocateIrp(char stack_size, UINT8 charge_quota);
NT_API void  nt_IoFreeIrp(NT_IRP *irp);
NT_API UINT8 nt_IoCancelIrp(NT_IRP *irp);
NT_API NT_STATUS nt_IoRegisterDeviceInterface(NT_DEVICE_OBJECT *pdo, const NT_GUID *guid, NT_UNICODE_STRING *ref,
                                              NT_UNICODE_STRING *out);
NT_API NT_STATUS nt_IoSetDeviceInterfaceState(NT_UNICODE_STRING *name, UINT8 enable);

#define NT_IRP_MJ_INTERNAL_DEVICE_CONTROL 0x0F
#define NT_FILE_DEVICE_KS       0x2F
#define ST_DEVICE_NOT_CONNECTED ((NT_STATUS)0xC000009D)
#define ST_IO_TIMEOUT           ((NT_STATUS)0xC00000B5)
#define MAX_PAYLOAD             (512 * 1024)
#define FRAME_WAIT_MS           3000           /* a payload must come within this */

/* KSCATEGORY_VIDEO_CAMERA {E5323777-F976-4F5B-9B55-B94699C46E44} */
static const NT_GUID g_camera_iface = { 0xE5323777, 0xF976, 0x4F5B, { 0x9B, 0x55, 0xB9, 0x46, 0x99, 0xC4, 0x6E, 0x44 } };

typedef struct {
    NT_DEVICE_OBJECT *self, *pdo, *next;
    NT_UNICODE_STRING iface;           /* the interface's symbolic link */
    UINT8          dev[18];
    UINT8         *cfg;
    UINT32         cfg_len;
    UINT16         uvc;                /* bcdUVC */
    UINT8          vc, vs;             /* the VideoControl and VideoStreaming interface numbers */
    UINT8          ep;                 /* the streaming endpoint */
    void          *pipe;
    bool           started, gone;
    NovaCamInfo    info;
    NT_KEVENT      lock;               /* (a synchronization event: one request at a time) */
    /* the stream */
    NT_FILE_OBJECT *owner;             /* the handle that started it (NULL: stopped) */
    const NovaCamMode *mode;
    UINT32         payload;            /* dwMaxPayloadTransferSize */
    UINT8         *buf;                /* one payload */
    UINT32         seq;
    UINT8          fid;                /* the last payload's frame ID, and whether it ended a frame */
    bool           eof;
} Uvc;

static Uvc *uvc(NT_DEVICE_OBJECT *fdo) { return fdo->DeviceExtension; }
static UINT16 vid(const Uvc *u) { return (UINT16)(u->dev[8] | u->dev[9] << 8); }
static UINT16 pid(const Uvc *u) { return (UINT16)(u->dev[10] | u->dev[11] << 8); }
static UINT32 le32(const UINT8 *p) { return (UINT32)(p[0] | p[1] << 8 | p[2] << 16 | (UINT32)p[3] << 24); }

static void lock(Uvc *u)   { nt_KeWaitForSingleObject(&u->lock, 0, 0, 0, NULL); }
static void unlock(Uvc *u) { nt_KeSetEvent(&u->lock, 0, 0); }

/* ---- URBs to the device's PDO ---- */

/* @urb down the stack; NT_STATUS_TIMEOUT if it took longer than @ms
 * (0: as long as it takes), when the pipe is aborted to get it back */
static NT_STATUS urb_wait(Uvc *u, void *urb, UINT32 ms)
{
    NT_IRP *irp = nt_IoAllocateIrp(u->next->StackSize, 0);
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
    nt_IofCallDriver(u->next, irp);
    bool late = false;
    if (ms) {
        INT64 t = -(INT64)ms * 10000;
        if (nt_KeWaitForSingleObject(&ev, 0, 0, 0, &t) == NT_STATUS_TIMEOUT) {
            late = true;
            nt_IoCancelIrp(irp);
        }
    }
    nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    nt_IoFreeIrp(irp);
    return late ? NT_STATUS_TIMEOUT : iosb.Status;
}

static int get_desc(Uvc *u, UINT8 type, UINT8 index, UINT16 lang, void *buf, UINT32 len)
{
    UrbXfer x;
    memset(&x, 0, sizeof(x));
    x.Hdr.Length = sizeof(x);
    x.Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    x.TransferBufferLength = len;
    x.TransferBuffer = buf;
    x.Setup[2] = index;
    x.Setup[3] = type;
    x.Setup[4] = (UINT8)lang;
    x.Setup[5] = (UINT8)(lang >> 8);
    return NT_SUCCESS(urb_wait(u, &x, 0)) ? (int)x.TransferBufferLength : -1;
}

/* A class request to the VideoStreaming interface (SET_CUR 0x01, GET_CUR 0x81) */
static NT_STATUS vs_req(Uvc *u, UINT8 req, UINT8 control, void *buf, UINT32 len)
{
    UrbXfer x;
    memset(&x, 0, sizeof(x));
    x.Hdr.Length = sizeof(x);
    x.Hdr.Function = URB_FUNCTION_CLASS_INTERFACE;
    x.TransferFlags = (req & 0x80 ? USBD_TRANSFER_DIRECTION_IN : 0) | USBD_SHORT_TRANSFER_OK;
    x.TransferBufferLength = len;
    x.TransferBuffer = buf;
    x.Setup[1] = req;
    x.Setup[3] = control;
    x.Setup[4] = u->vs;
    return urb_wait(u, &x, 2000);
}

static NT_STATUS pipe_req(Uvc *u, UINT16 fn)
{
    UrbPipe x;
    memset(&x, 0, sizeof(x));
    x.Hdr.Length = sizeof(x);
    x.Hdr.Function = fn;
    x.PipeHandle = u->pipe;
    return urb_wait(u, &x, 0);
}

/* ---- start: the descriptors ---- */

static void add_mode(Uvc *u, UINT32 fourcc, UINT8 bpp, UINT8 format, const UINT8 *f)
{
    if (u->info.nmodes >= NOVACAM_MAX_MODES || f[0] < 26) return;
    NovaCamMode *m = &u->info.modes[u->info.nmodes++];
    m->fourcc = fourcc;
    m->format = format;
    m->frame = f[3];
    m->width = (UINT16)(f[5] | f[6] << 8);
    m->height = (UINT16)(f[7] | f[8] << 8);
    m->max_bytes = le32(f + 17);
    m->interval = le32(f + 21);
    m->bpp = bpp;
    /* intervals: continuous (min, max, step) or a list */
    UINT8 n = f[25];
    UINT32 shortest = m->interval;
    if (!n && f[0] >= 38) shortest = le32(f + 26);
    for (UINT8 i = 0; i < n && 26 + 4 * (i + 1) <= f[0]; i++) {
        UINT32 v = le32(f + 26 + 4 * i);
        if (v && v < shortest) shortest = v;
    }
    m->min_interval = shortest;
    if (fourcc != NOVACAM_MJPG && bpp) {
        UINT32 bytes = (UINT32)m->width * m->height * bpp / 8;
        if (!m->max_bytes || m->max_bytes > bytes) m->max_bytes = bytes;   /* (UVC 1.5 leaves it 0) */
    }
}

static NT_STATUS parse(Uvc *u)
{
    UINT8 *c = u->cfg;
    int cur = -1;                              /* the interface the descriptors belong to: 1 VC, 2 VS */
    UINT8 format = 0, bpp = 0;
    UINT32 fourcc = 0;
    bool vs_found = false, ep_alt0 = false;
    for (UINT32 off = 0; off + 2 <= u->cfg_len && c[off] >= 2; off += c[off]) {
        UINT8 *d = &c[off];
        if (d[1] == 4 && d[0] >= 9) {                              /* INTERFACE */
            cur = -1;
            if (d[5] != 0x0E) continue;
            if (d[6] == 1 && !u->vc && d[3] == 0) { u->vc = d[2]; cur = 1; }
            else if (d[6] == 2 && (!vs_found || d[2] == u->vs)) {
                if (!vs_found) { u->vs = d[2]; vs_found = true; }
                cur = 2;
                if (d[3] == 0) ep_alt0 = d[4] > 0;
            }
        } else if (d[1] == 0x24 && cur == 1 && d[2] == 1 && d[0] >= 5) {   /* VC_HEADER */
            u->uvc = (UINT16)(d[3] | d[4] << 8);
        } else if (d[1] == 0x24 && cur == 2) {
            switch (d[2]) {
            case 0x01:                                                    /* VS_INPUT_HEADER */
                if (d[0] >= 7) u->ep = d[6];
                break;
            case 0x04:                                                    /* VS_FORMAT_UNCOMPRESSED */
                if (d[0] >= 27) { format = d[3]; fourcc = le32(d + 5); bpp = d[21]; } else format = 0;
                break;
            case 0x06:                                                    /* VS_FORMAT_MJPEG */
                format = d[3]; fourcc = NOVACAM_MJPG; bpp = 0;
                break;
            case 0x05:                                                    /* VS_FRAME_UNCOMPRESSED */
            case 0x07:                                                    /* VS_FRAME_MJPEG */
                if (format) add_mode(u, fourcc, bpp, format, d);
                break;
            case 0x10:                                                    /* VS_FORMAT_FRAME_BASED, ... */
            default:
                if (d[2] == 0x10 || d[2] == 0x0C) format = 0;
                break;
            }
        }
    }
    kprintf("[UVC] %04x:%04x: UVC %x.%02x, control interface %u, streaming interface %u, endpoint %02x, %u mode(s)\n",
            vid(u), pid(u), u->uvc >> 8, u->uvc & 0xFF, u->vc, u->vs, u->ep, u->info.nmodes);
    for (UINT32 i = 0; i < u->info.nmodes; i++) {
        const NovaCamMode *m = &u->info.modes[i];
        kprintf("[UVC] %04x:%04x: mode %u: %c%c%c%c %ux%u, %u.%u fps\n", vid(u), pid(u), i,
                m->fourcc & 0xFF, m->fourcc >> 8 & 0xFF, m->fourcc >> 16 & 0xFF, m->fourcc >> 24,
                m->width, m->height, m->interval ? 10000000 / m->interval : 0,
                m->interval ? 100000000 / m->interval % 10 : 0);
    }
    if (!vs_found || !u->ep || !u->info.nmodes) return NT_STATUS_UNSUCCESSFUL;
    if (!ep_alt0) {
        kprintf("[UVC] %04x:%04x: its stream is isochronous, which NovaOS does not support yet\n", vid(u), pid(u));
        return NT_STATUS_NOT_SUPPORTED;
    }
    return NT_STATUS_SUCCESS;
}

static NT_STATUS configure(Uvc *u)
{
    UINT8 head[9];
    if (get_desc(u, 1, 0, 0, u->dev, sizeof(u->dev)) != sizeof(u->dev) ||
        get_desc(u, 2, 0, 0, head, sizeof(head)) != sizeof(head))
        return ST_DEVICE_NOT_CONNECTED;
    u->cfg_len = (UINT32)(head[2] | head[3] << 8);
    u->cfg = u->cfg_len >= 9 ? kmalloc(u->cfg_len) : NULL;
    if (!u->cfg) return NT_STATUS_INSUFFICIENT_RESOURCES;
    if (get_desc(u, 2, 0, 0, u->cfg, u->cfg_len) != (int)u->cfg_len) return ST_DEVICE_NOT_CONNECTED;
    u->info.vid = vid(u);
    u->info.pid = pid(u);
    NT_STATUS st = parse(u);
    if (!NT_SUCCESS(st)) return st;

    /* The name: the product string, else "USB Video Device" as Windows says */
    UINT8 s[256];
    int n = u->dev[15] ? get_desc(u, 3, u->dev[15], 0x0409, s, sizeof(s)) : -1;
    UINT32 k = 0;
    if (n >= 4 && s[1] == 3)
        for (UINT32 chars = (UINT32)((s[0] < n ? s[0] : n) - 2) / 2; k < chars && k < 63; k++)
            u->info.name[k] = (UINT16)(s[2 + 2 * k] | s[3 + 2 * k] << 8);
    if (!k)
        for (const char *t = "USB Video Device"; *t && k < 63; t++) u->info.name[k++] = (UINT16)*t;
    u->info.name[k] = 0;

    /* Both interfaces in their first settings: the stream's endpoint is there */
    UINT8 *vc = NULL, *vs = NULL;
    for (UINT32 off = 0; off + 2 <= u->cfg_len && u->cfg[off] >= 2; off += u->cfg[off]) {
        UINT8 *d = &u->cfg[off];
        if (d[1] == 4 && d[0] >= 9 && d[3] == 0 && d[2] == u->vc && !vc) vc = d;
        if (d[1] == 4 && d[0] >= 9 && d[3] == 0 && d[2] == u->vs && !vs) vs = d;
    }
    UsbdIfaceListEntry list[3] = { { vc, NULL }, { vs, NULL }, { NULL, NULL } };
    if (!vc) { list[0] = list[1]; list[1].InterfaceDescriptor = NULL; }
    UrbSelectConfig *sel = nt_USBD_CreateConfigurationRequestEx(u->cfg, list);
    if (!sel) return NT_STATUS_INSUFFICIENT_RESOURCES;
    st = urb_wait(u, sel, 0);
    if (NT_SUCCESS(st)) {
        UsbdIfaceInfo *i = list[vc ? 1 : 0].Interface;
        for (UINT32 k2 = 0; k2 < i->NumberOfPipes; k2++)
            if (i->Pipes[k2].EndpointAddress == u->ep) u->pipe = i->Pipes[k2].PipeHandle;
    }
    nt_ExFreePool(sel);
    if (!NT_SUCCESS(st)) return st;
    return u->pipe ? NT_STATUS_SUCCESS : NT_STATUS_UNSUCCESSFUL;
}

/* ---- the stream ---- */

static void stop(Uvc *u)
{
    if (!u->owner) return;
    if (!u->gone) {
        pipe_req(u, URB_FUNCTION_ABORT_PIPE);
        pipe_req(u, URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL);
    }
    kfree(u->buf);
    u->buf = NULL;
    u->owner = NULL;
    u->mode = NULL;
    kprintf("[UVC] %04x:%04x: stream stopped after %u frame(s)\n", vid(u), pid(u), u->seq);
}

static NT_STATUS start(Uvc *u, NT_FILE_OBJECT *fo, const NovaCamStart *s)
{
    if (u->owner && u->owner != fo) return NT_STATUS_DEVICE_BUSY;
    if (s->mode >= u->info.nmodes) return NT_STATUS_INVALID_PARAMETER;
    stop(u);
    const NovaCamMode *m = &u->info.modes[s->mode];
    UINT32 len = u->uvc >= 0x0150 ? 48 : u->uvc >= 0x0110 ? 34 : 26;
    UINT8 probe[48];
    memset(probe, 0, sizeof(probe));
    UINT32 interval = s->interval ? s->interval : m->interval;
    probe[0] = 1;                                              /* bmHint: keep dwFrameInterval */
    probe[2] = m->format;
    probe[3] = m->frame;
    memcpy(probe + 4, &interval, 4);
    NT_STATUS st = vs_req(u, 0x01, 1, probe, len);             /* SET_CUR PROBE */
    if (NT_SUCCESS(st)) st = vs_req(u, 0x81, 1, probe, len);   /* GET_CUR PROBE: what the device will do */
    if (NT_SUCCESS(st)) st = vs_req(u, 0x01, 2, probe, len);   /* SET_CUR COMMIT */
    if (!NT_SUCCESS(st)) {
        kprintf("[UVC] %04x:%04x: probe and commit failed (%08x)\n", vid(u), pid(u), (UINT32)st);
        return st;
    }
    UINT32 payload = le32(probe + 22);
    if (!payload || payload > MAX_PAYLOAD) payload = payload ? MAX_PAYLOAD : 64 * 1024;
    u->buf = kmalloc(payload);
    if (!u->buf) return NT_STATUS_INSUFFICIENT_RESOURCES;
    u->payload = payload;
    u->owner = fo;
    u->mode = m;
    u->seq = 0;
    u->fid = 0xFF;
    u->eof = true;
    kprintf("[UVC] %04x:%04x: streaming mode %u (%ux%u), interval %u, frames up to %u bytes, payloads up to %u\n",
            vid(u), pid(u), s->mode, m->width, m->height, le32(probe + 4), le32(probe + 18), payload);
    return NT_STATUS_SUCCESS;
}

/* One payload from the streaming endpoint into u->buf: its size, or -1
 * (with *@st why) */
static int payload(Uvc *u, NT_STATUS *st)
{
    UrbXfer x;
    memset(&x, 0, sizeof(x));
    x.Hdr.Length = sizeof(x);
    x.Hdr.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    x.PipeHandle = u->pipe;
    x.TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
    x.TransferBufferLength = u->payload;
    x.TransferBuffer = u->buf;
    *st = urb_wait(u, &x, FRAME_WAIT_MS);
    if (*st == NT_STATUS_TIMEOUT) *st = ST_IO_TIMEOUT;
    return NT_SUCCESS(*st) ? (int)x.TransferBufferLength : -1;
}

/* The next whole frame into @dst (@cap bytes): its size, or -1 (*@st) */
static int read_frame(Uvc *u, UINT8 *dst, UINT32 cap, NT_STATUS *st)
{
    UINT32 have = 0;
    bool in_frame = false, spoiled = false;
    for (int tries = 0; tries < 4096; tries++) {
        int n = payload(u, st);
        if (n < 0) return -1;
        if (n < 2 || u->buf[0] < 2 || u->buf[0] > n) continue;      /* (no header: not a payload) */
        UINT8 hl = u->buf[0], info = u->buf[1], fid = info & 1;
        bool eof = info & 2, err = info & 0x40;
        bool new_frame = u->eof || fid != u->fid;
        u->fid = fid;
        u->eof = eof;
        if (new_frame) {                                         /* (one cut short is dropped) */
            have = 0;
            in_frame = true;
            spoiled = false;
        }
        if (!in_frame) continue;
        if (err) spoiled = true;
        UINT32 data = (UINT32)n - hl;
        if (have + data > cap) spoiled = true;
        else memcpy(dst + have, u->buf + hl, data), have += data;
        bool full = u->mode->fourcc != NOVACAM_MJPG && have >= u->mode->max_bytes;
        if (eof || full) {
            if (full) u->eof = true;
            if (!spoiled && (u->mode->fourcc == NOVACAM_MJPG ? have > 0 : have == u->mode->max_bytes)) return (int)have;
            in_frame = false;                                      /* (a bad frame: wait for the next) */
        }
    }
    *st = ST_IO_TIMEOUT;
    return -1;
}

/* ---- requests ---- */

static void done(NT_IRP *irp, NT_STATUS st, NT_ULONG_PTR info)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    nt_IofCompleteRequest(irp, 0);
}

static NT_STATUS ioctl(Uvc *u, NT_IRP *irp, NT_IO_STACK_LOCATION *sl, NT_ULONG_PTR *info)
{
    UINT32 code = sl->Parameters.DeviceIoControl.IoControlCode;
    UINT32 in_len = sl->Parameters.DeviceIoControl.InputBufferLength;
    UINT32 out_len = sl->Parameters.DeviceIoControl.OutputBufferLength;
    void *sys = irp->AssociatedIrp.SystemBuffer;
    if (u->gone || !u->started) return ST_DEVICE_NOT_CONNECTED;
    switch (code) {
    case IOCTL_NOVACAM_GET_INFO:
        if (!sys || out_len < sizeof(NovaCamInfo)) return NT_STATUS_BUFFER_TOO_SMALL;
        memcpy(sys, &u->info, sizeof(NovaCamInfo));
        *info = sizeof(NovaCamInfo);
        return NT_STATUS_SUCCESS;
    case IOCTL_NOVACAM_START:
        if (!sys || in_len < sizeof(NovaCamStart)) return NT_STATUS_INVALID_PARAMETER;
        return start(u, sl->FileObject, sys);
    case IOCTL_NOVACAM_STOP:
        if (u->owner && u->owner != sl->FileObject) return NT_STATUS_DEVICE_BUSY;
        stop(u);
        return NT_STATUS_SUCCESS;
    case IOCTL_NOVACAM_READ_FRAME: {
        if (u->owner != sl->FileObject || !u->mode) return NT_STATUS_INVALID_DEVICE_REQUEST;
        UINT8 *out = irp->MdlAddress ? nt_MmMapLockedPagesSpecifyCache(irp->MdlAddress, 0, 1, NULL, 0, 16) : NULL;
        if (!out || out_len < sizeof(NovaCamFrame) + u->mode->max_bytes) return NT_STATUS_BUFFER_TOO_SMALL;
        NT_STATUS st;
        int n = read_frame(u, out + sizeof(NovaCamFrame), out_len - (UINT32)sizeof(NovaCamFrame), &st);
        if (n < 0) {
            if (u->seq == 0 || st != ST_IO_TIMEOUT)
                kprintf("[UVC] %04x:%04x: no frame (%08x)\n", vid(u), pid(u), (UINT32)st);
            return st;
        }
        NovaCamFrame *h = (NovaCamFrame *)out;
        h->bytes = (UINT32)n;
        h->seq = ++u->seq;
        h->reserved = 0;
        if (u->seq == 1) kprintf("[UVC] %04x:%04x: first frame, %d bytes\n", vid(u), pid(u), n);
        *info = sizeof(NovaCamFrame) + (UINT32)n;
        return NT_STATUS_SUCCESS;
    }
    }
    return NT_STATUS_INVALID_DEVICE_REQUEST;
}

static NT_API NT_STATUS sync_done(NT_DEVICE_OBJECT *d, NT_IRP *irp, void *ctx)
{
    (void)d; (void)irp;
    nt_KeSetEvent(ctx, 0, 0);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;
}

static NT_STATUS forward_sync(Uvc *u, NT_IRP *irp)
{
    NT_KEVENT ev;
    nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    NT_IO_STACK_LOCATION *cur = NtIrpCurrentStack(irp), *next = NtIrpNextStack(irp);
    memcpy(next, cur, __builtin_offsetof(NT_IO_STACK_LOCATION, CompletionRoutine));
    next->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    next->CompletionRoutine = sync_done;
    next->Context = &ev;
    nt_IofCallDriver(u->next, irp);
    nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    return irp->IoStatus.Status;
}

static NT_STATUS pass_down(Uvc *u, NT_IRP *irp)
{
    irp->CurrentLocation++;
    irp->Tail.Overlay.CurrentStackLocation++;
    return nt_IofCallDriver(u->next, irp);
}

static void interface_off(Uvc *u)
{
    if (u->iface.Buffer && u->started) nt_IoSetDeviceInterfaceState(&u->iface, 0);
    u->started = false;
}

static NT_STATUS pnp(Uvc *u, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE: {
        NT_STATUS st = forward_sync(u, irp);
        if (NT_SUCCESS(st)) st = configure(u);
        if (NT_SUCCESS(st) && u->iface.Buffer) st = nt_IoSetDeviceInterfaceState(&u->iface, 1);
        if (NT_SUCCESS(st)) {
            u->started = true;
            kprintf("[UVC] %04x:%04x: started as a camera\n", vid(u), pid(u));
        }
        done(irp, st, 0);
        return st;
    }
    case NT_IRP_MN_SURPRISE_REMOVAL:
        lock(u);
        u->gone = true;
        interface_off(u);
        unlock(u);
        break;
    case NT_IRP_MN_REMOVE_DEVICE: {
        lock(u);
        u->gone = true;
        interface_off(u);
        stop(u);
        unlock(u);
        NT_STATUS st = pass_down(u, irp);
        nt_IoDetachDevice(u->next);
        kfree(u->cfg);
        if (u->iface.Buffer) nt_ExFreePool(u->iface.Buffer);
        nt_IoDeleteDevice(u->self);
        return st;
    }
    }
    return pass_down(u, irp);
}

static NT_API NT_STATUS dispatch(NT_DEVICE_OBJECT *fdo, NT_IRP *irp)
{
    Uvc *u = uvc(fdo);
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    NT_STATUS st;
    NT_ULONG_PTR info = 0;
    switch (sl->MajorFunction) {
    case NT_IRP_MJ_PNP:
        return pnp(u, irp, sl);
    case NT_IRP_MJ_POWER:
    case NT_IRP_MJ_SYSTEM_CONTROL:
        return pass_down(u, irp);
    case NT_IRP_MJ_CREATE:
        st = u->started && !u->gone ? NT_STATUS_SUCCESS : ST_DEVICE_NOT_CONNECTED;
        break;
    case NT_IRP_MJ_CLEANUP:
        lock(u);
        if (u->owner == sl->FileObject) stop(u);
        unlock(u);
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MJ_CLOSE:
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MJ_DEVICE_CONTROL:
        lock(u);
        st = ioctl(u, irp, sl, &info);
        unlock(u);
        break;
    default:
        st = NT_STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    done(irp, st, info);
    return st;
}

static NT_API NT_STATUS add_device(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *pdo)
{
    NT_DEVICE_OBJECT *fdo;
    NT_STATUS st = nt_IoCreateDevice(drv, sizeof(Uvc), NULL, NT_FILE_DEVICE_KS, 0x100 /* AUTOGENERATED_NAME */, 0, &fdo);
    if (!NT_SUCCESS(st)) return st;
    Uvc *u = uvc(fdo);
    memset(u, 0, sizeof(*u));
    u->self = fdo;
    u->pdo = pdo;
    nt_KeInitializeEvent(&u->lock, NT_SynchronizationEvent, 1);
    u->next = nt_IoAttachDeviceToDeviceStack(fdo, pdo);
    if (!u->next) { nt_IoDeleteDevice(fdo); return NT_STATUS_UNSUCCESSFUL; }
    st = nt_IoRegisterDeviceInterface(pdo, &g_camera_iface, NULL, &u->iface);
    if (!NT_SUCCESS(st)) u->iface.Buffer = NULL;
    fdo->StackSize = (char)(u->next->StackSize + 1);
    fdo->Flags = (fdo->Flags & ~NT_DO_DEVICE_INITIALIZING) | NT_DO_BUFFERED_IO | NT_DO_POWER_PAGABLE;
    return NT_STATUS_SUCCESS;
}

NTSTATUS NT_API UsbVideoDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg)
{
    (void)reg;
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) drv->MajorFunction[i] = dispatch;
    drv->DriverExtension->AddDevice = add_device;
    return NT_STATUS_SUCCESS;
}
