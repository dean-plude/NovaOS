/*
 * bthusb.c — NovaOS's USB Bluetooth transport (Windows' bthusb.sys)
 *
 * Windows runs a USB Bluetooth radio with bthusb.sys, which bth.inf
 * installs on USB\Class_E0&SubClass_01&Prot_01 and which a vendor's INF
 * names too: Intel's ibtusb.inf for the AX211's Bluetooth half says
 * "Include = bth.inf, Needs = BthUsb.NT..." and adds Intel's ibtusb.sys
 * below it as a lower filter (it loads the radio's firmware and passes
 * the rest down).  NovaOS's bth.inf (kernel/nt/bth.inf, in C:\Windows\INF)
 * has those sections and names this driver, built into the kernel as the
 * service "BTHUSB", a WDM function driver like any other: it reaches the
 * radio only through URBs down its stack (a vendor filter, then the USB
 * PDO, usbd.c), as the Bluetooth Core Specification's USB transport (Vol
 * 4, Part B) lays a radio out:
 *
 *   - HCI commands: class requests to the device on the control pipe
 *     (bmRequestType 0x20, bRequest 0);
 *   - HCI events: interrupt IN on interface 0, read a packet at a time and
 *     put back together by the event's length;
 *   - ACL data: bulk IN and OUT on interface 0, the IN side put back
 *     together by the ACL header's length;
 *   - SCO voice: interface 1's isochronous endpoints, at setting 0 (no
 *     bandwidth) until the host opens a voice link, then at the setting
 *     whose packets carry its samples (the specification's table: setting
 *     2, 17-byte packets every millisecond, for one link of 16-bit
 *     samples), SCO packets (a 3-byte header, then the samples) sent as a
 *     run of packets and the IN side's put back together by the header's
 *     length.  isochronous OUT and IN URBs (usbd.c) do it.
 *
 * The host stack above it is bthport.c, which this driver is the bus of:
 * the radio's device interface (GUID_BTHPORT_DEVICE_INTERFACE) and the
 * services' child nodes are this FDO's, as bthport.sys's are bthusb's.
 */
#include "ntapi.h"
#include "usbdi.h"
#include "bthport.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

NT_API NT_IRP *nt_IoAllocateIrp(char stack_size, UINT8 charge_quota);
NT_API void  nt_IoFreeIrp(NT_IRP *irp);

#define NT_IRP_MJ_INTERNAL_DEVICE_CONTROL 0x0F
#define FILE_DEVICE_BLUETOOTH 0x41
#define ST_DEVICE_NOT_CONNECTED ((NT_STATUS)0xC000009D)
#define ACL_MAX 1100                    /* an ACL packet (the controller's buffer size is less) */
#define SCO_ALTS 8                      /* interface 1's settings */
#define SCO_PACKETS 8                   /* isochronous packets an URB */
#define SCO_FIFO 4096                   /* bytes of SCO packets waiting to go out (a power of two) */

typedef struct {
    UINT32            magic;            /* (0: an FDO; bthport.c's service PDOs have theirs) */
    NT_DEVICE_OBJECT *self, *pdo, *next;
    UINT8             dev[18];
    UINT8            *cfg;
    UINT32            cfg_len;
    void             *cfg_handle;
    void             *evt, *acl_in, *acl_out;     /* the pipes */
    UINT16            evt_mps;
    BthRadio         *radio;
    NT_KEVENT         send_lock;
    NT_KEVENT         evt_done, acl_done;
    volatile bool     running, gone;
    bool              started;
    /* SCO: interface 1's settings and packet sizes (IN, OUT), the one it
     * is at, its pipes, and the SCO packets waiting to go out */
    struct { UINT8 alt; UINT16 in, out; } alts[SCO_ALTS];
    int               nalts;
    UINT8             sco_alt;
    void             *sco_in, *sco_out;
    UINT16            sco_in_mps, sco_out_mps;
    volatile bool     sco_running;
    NT_KEVENT         sco_lock, sco_in_done, sco_out_done;
    UINT8            *fifo;
    volatile UINT32   fifo_rd, fifo_wr;
    UINT32            fifo_dropped;
} BthUsb;

static BthUsb *bu(NT_DEVICE_OBJECT *fdo) { return fdo->DeviceExtension; }

static NT_STATUS urb_sync(BthUsb *b, void *urb)
{
    NT_IRP *irp = nt_IoAllocateIrp(b->next->StackSize, 0);
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
    nt_IofCallDriver(b->next, irp);
    nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    nt_IoFreeIrp(irp);
    return iosb.Status;
}

static int get_desc(BthUsb *b, UINT8 type, void *buf, UINT32 len)
{
    UrbXfer u;
    memset(&u, 0, sizeof(u));
    u.Hdr.Length = sizeof(u);
    u.Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    u.TransferBufferLength = len;
    u.TransferBuffer = buf;
    u.Setup[3] = type;
    return NT_SUCCESS(urb_sync(b, &u)) ? (int)u.TransferBufferLength : -1;
}

/* A bulk or interrupt transfer on @pipe; the bytes moved, or -1 */
static int xfer(BthUsb *b, void *pipe, bool in, void *buf, UINT32 len)
{
    UrbXfer u;
    memset(&u, 0, sizeof(u));
    u.Hdr.Length = sizeof(u);
    u.Hdr.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    u.PipeHandle = pipe;
    u.TransferFlags = in ? USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK : 0;
    u.TransferBufferLength = len;
    u.TransferBuffer = buf;
    return NT_SUCCESS(urb_sync(b, &u)) ? (int)u.TransferBufferLength : -1;
}

static void abort_pipe(BthUsb *b, void *pipe)
{
    if (!pipe) return;
    UrbPipe u;
    memset(&u, 0, sizeof(u));
    u.Hdr.Length = sizeof(u);
    u.Hdr.Function = URB_FUNCTION_ABORT_PIPE;
    u.PipeHandle = pipe;
    urb_sync(b, &u);
}

/* ---- the transport bthport.c sends through ---- */

static NT_STATUS send_cmd(void *ctx, const UINT8 *pkt, UINT32 len)
{
    BthUsb *b = ctx;
    if (b->gone) return ST_DEVICE_NOT_CONNECTED;
    UrbXfer u;
    memset(&u, 0, sizeof(u));
    u.Hdr.Length = sizeof(u);
    u.Hdr.Function = URB_FUNCTION_CLASS_DEVICE;
    u.TransferBufferLength = len;
    u.TransferBuffer = (void *)pkt;
    nt_KeWaitForSingleObject(&b->send_lock, 0, 0, 0, NULL);
    NT_STATUS st = urb_sync(b, &u);
    nt_KeSetEvent(&b->send_lock, 0, 0);
    return st;
}

static NT_STATUS send_acl(void *ctx, const UINT8 *pkt, UINT32 len)
{
    BthUsb *b = ctx;
    if (b->gone) return ST_DEVICE_NOT_CONNECTED;
    return xfer(b, b->acl_out, false, (void *)pkt, len) == (int)len ? NT_STATUS_SUCCESS : ST_DEVICE_NOT_CONNECTED;
}

/* An SCO packet: into the queue the writer sends from (dropped while no
 * voice link runs, or when the queue is full) */
static NT_STATUS send_sco(void *ctx, const UINT8 *pkt, UINT32 len)
{
    BthUsb *b = ctx;
    if (b->gone || !b->sco_running) return ST_DEVICE_NOT_CONNECTED;
    UINT32 wr = b->fifo_wr;
    if (SCO_FIFO - (wr - __atomic_load_n(&b->fifo_rd, __ATOMIC_ACQUIRE)) < len) { b->fifo_dropped++; return NT_STATUS_SUCCESS; }
    for (UINT32 i = 0; i < len; i++) b->fifo[(wr + i) & (SCO_FIFO - 1)] = pkt[i];
    __atomic_store_n(&b->fifo_wr, wr + len, __ATOMIC_RELEASE);
    return NT_STATUS_SUCCESS;
}

static UrbIso *iso_urb(void *pipe, bool in, UINT8 *buf, UINT32 len, UINT32 mps)
{
    UINT32 n = (len + mps - 1) / mps, size = (UINT32)sizeof(UrbIso) + (SCO_PACKETS - 1) * (UINT32)sizeof(UsbdIsoPacket);
    UrbIso *u = kzalloc(size);
    if (!u) return NULL;
    u->Hdr.Length = (UINT16)size;
    u->Hdr.Function = URB_FUNCTION_ISOCH_TRANSFER;
    u->PipeHandle = pipe;
    u->TransferFlags = (in ? USBD_TRANSFER_DIRECTION_IN : 0) | USBD_START_ISO_TRANSFER_ASAP;
    u->TransferBufferLength = len;
    u->TransferBuffer = buf;
    u->NumberOfPackets = n;
    for (UINT32 k = 0; k < n; k++) u->IsoPacket[k].Offset = k * mps;
    return u;
}

/* SCO out: what the queue holds, in packets of the setting's size */
static void sco_writer(void *arg)
{
    BthUsb *b = arg;
    UINT32 mps = b->sco_out_mps, cap = SCO_PACKETS * mps;
    UINT8 *buf = kmalloc(cap);
    int errors = 0;
    while (buf && b->sco_running) {
        UINT32 rd = b->fifo_rd, have = __atomic_load_n(&b->fifo_wr, __ATOMIC_ACQUIRE) - rd;
        if (!have) { sched_sleep_tick(); continue; }
        UINT32 n = have < cap ? have : cap;
        for (UINT32 i = 0; i < n; i++) buf[i] = b->fifo[(rd + i) & (SCO_FIFO - 1)];
        __atomic_store_n(&b->fifo_rd, rd + n, __ATOMIC_RELEASE);
        UrbIso *u = iso_urb(b->sco_out, false, buf, n, mps);
        NT_STATUS st = u ? urb_sync(b, u) : NT_STATUS_INSUFFICIENT_RESOURCES;
        kfree(u);
        if (!NT_SUCCESS(st)) {
            if (!b->sco_running || ++errors > 20) break;
            INT64 t = -10000LL * 10;
            nt_KeDelayExecutionThread(0, 0, &t);
        } else {
            errors = 0;
        }
    }
    kfree(buf);
    nt_KeSetEvent(&b->sco_out_done, 0, 0);
}

/* SCO in: isochronous packets, joined into whole SCO packets */
static void sco_reader(void *arg)
{
    BthUsb *b = arg;
    UINT32 mps = b->sco_in_mps, cap = SCO_PACKETS * mps;
    UINT8 *buf = kmalloc(cap), *pkt = kmalloc(3 + 255 + cap);
    UINT32 have = 0;
    int errors = 0;
    while (buf && pkt && b->sco_running) {
        UrbIso *u = iso_urb(b->sco_in, true, buf, cap, mps);
        NT_STATUS st = u ? urb_sync(b, u) : NT_STATUS_INSUFFICIENT_RESOURCES;
        if (!NT_SUCCESS(st)) {
            kfree(u);
            if (!b->sco_running || ++errors > 20) break;
            INT64 t = -10000LL * 10;
            nt_KeDelayExecutionThread(0, 0, &t);
            continue;
        }
        errors = 0;
        for (UINT32 k = 0; k < u->NumberOfPackets; k++) {
            UsbdIsoPacket *p = &u->IsoPacket[k];
            if (p->Status || !p->Length || p->Length > mps) continue;
            memcpy(pkt + have, buf + p->Offset, p->Length);
            have += p->Length;
            while (have >= 3 && have >= 3u + pkt[2]) {
                UINT32 len = 3u + pkt[2];
                BthRadioSco(b->radio, pkt, len);
                have -= len;
                memmove(pkt, pkt + len, have);
            }
        }
        kfree(u);
    }
    kfree(buf);
    kfree(pkt);
    nt_KeSetEvent(&b->sco_in_done, 0, 0);
}

static void sco_stop(BthUsb *b)
{
    if (!b->sco_running) return;
    b->sco_running = false;
    if (!b->gone) {
        UrbPipe u;
        memset(&u, 0, sizeof(u));
        u.Hdr.Length = sizeof(u);
        u.Hdr.Function = URB_FUNCTION_ABORT_PIPE;
        u.PipeHandle = b->sco_in;
        urb_sync(b, &u);
        u.PipeHandle = b->sco_out;
        urb_sync(b, &u);
    }
    nt_KeWaitForSingleObject(&b->sco_in_done, 0, 0, 0, NULL);
    nt_KeWaitForSingleObject(&b->sco_out_done, 0, 0, 0, NULL);
    if (b->fifo_dropped) kprintf("[BTHUSB] %u byte(s) of voice dropped (the queue was full)\n", b->fifo_dropped);
    b->fifo_dropped = 0;
}

/* Interface 1 at the setting that carries @links voice links of 16-bit
 * samples (0: back to setting 0), and the SCO reader and writer running
 * on it */
static NT_STATUS sco_links(void *ctx, int links)
{
    BthUsb *b = ctx;
    if (b->gone) return ST_DEVICE_NOT_CONNECTED;
    UINT8 alt = 0;
    if (links > 0) {
        /* the specification's table: one 16-bit link needs 17-byte packets, two 33, three 49 */
        UINT32 want = 16u * (UINT32)links + 1, bm = 0, gm = 0;
        int best = -1, big = -1;
        for (int i = 0; i < b->nalts; i++) {
            UINT32 m = b->alts[i].in < b->alts[i].out ? b->alts[i].in : b->alts[i].out;
            if (m >= want && (best < 0 || m < bm)) { best = i; bm = m; }
            if (m && (big < 0 || m > gm)) { big = i; gm = m; }
        }
        if (best < 0) best = big;                               /* (else the largest there is) */
        if (best < 0) return NT_STATUS_NOT_SUPPORTED;
        alt = b->alts[best].alt;
    }
    nt_KeWaitForSingleObject(&b->sco_lock, 0, 0, 0, NULL);
    NT_STATUS st = NT_STATUS_SUCCESS;
    if (alt != b->sco_alt) {
        sco_stop(b);
        struct { UrbSelectIface x; UsbdPipeInfo more[1]; } s;
        memset(&s, 0, sizeof(s));
        s.x.Hdr.Length = sizeof(s);
        s.x.Hdr.Function = URB_FUNCTION_SELECT_INTERFACE;
        s.x.ConfigurationHandle = b->cfg_handle;
        s.x.Interface.Length = (UINT16)(sizeof(UsbdIfaceInfo) + sizeof(UsbdPipeInfo));
        s.x.Interface.InterfaceNumber = 1;
        s.x.Interface.AlternateSetting = alt;
        st = urb_sync(b, &s.x);
        b->sco_in = b->sco_out = NULL;
        if (NT_SUCCESS(st)) {
            b->sco_alt = alt;
            for (UINT32 k = 0; k < s.x.Interface.NumberOfPipes && k < 2; k++) {
                UsbdPipeInfo *p = &s.x.Interface.Pipes[k];
                if (p->PipeType != 1) continue;
                if (p->EndpointAddress & 0x80) { b->sco_in = p->PipeHandle; b->sco_in_mps = p->MaximumPacketSize; }
                else { b->sco_out = p->PipeHandle; b->sco_out_mps = p->MaximumPacketSize; }
            }
            if (alt && (!b->sco_in || !b->sco_out || !b->sco_in_mps || !b->sco_out_mps)) st = NT_STATUS_UNSUCCESSFUL;
        }
        if (NT_SUCCESS(st) && alt) {
            b->fifo_rd = b->fifo_wr = 0;
            b->sco_running = true;
            nt_KeClearEvent(&b->sco_in_done);
            nt_KeClearEvent(&b->sco_out_done);
            if (!sched_create_thread_ex("bth-sco-in", sco_reader, b, PRIO_DEVICE_IO, 16 * 1024)) nt_KeSetEvent(&b->sco_in_done, 0, 0);
            if (!sched_create_thread_ex("bth-sco-out", sco_writer, b, PRIO_DEVICE_IO, 16 * 1024)) nt_KeSetEvent(&b->sco_out_done, 0, 0);
            kprintf("[BTHUSB] voice: interface 1 setting %u, %u-byte isochronous packets each way\n", alt, b->sco_out_mps);
        } else if (!NT_SUCCESS(st)) {
            kprintf("[BTHUSB] interface 1 setting %u refused (%08x)\n", alt, (UINT32)st);
        }
    }
    nt_KeSetEvent(&b->sco_lock, 0, 0);
    return st;
}

static const BthTransport g_transport = { send_cmd, send_acl, sco_links, send_sco };

/* HCI events: interrupt IN packets, joined into whole events */
static void evt_reader(void *arg)
{
    BthUsb *b = arg;
    UINT8 *pkt = kmalloc(b->evt_mps), *ev = kmalloc(2 + 255 + 64);
    UINT32 have = 0;
    int errors = 0;
    while (pkt && ev && b->running) {
        int n = xfer(b, b->evt, true, pkt, b->evt_mps);
        if (n < 0) {
            if (!b->running || ++errors > 20) break;
            INT64 t = -10000LL * 50;
            nt_KeDelayExecutionThread(0, 0, &t);
            continue;
        }
        errors = 0;
        if (have + (UINT32)n > 2 + 255 + 64) have = 0;          /* (lost track: start again) */
        memcpy(ev + have, pkt, (UINT32)n);
        have += (UINT32)n;
        while (have >= 2 && have >= 2u + ev[1]) {
            UINT32 len = 2u + ev[1];
            BthRadioEvent(b->radio, ev, len);
            have -= len;
            memmove(ev, ev + len, have);
        }
    }
    kfree(pkt);
    kfree(ev);
    nt_KeSetEvent(&b->evt_done, 0, 0);
}

/* ACL data: bulk IN transfers, joined into whole packets */
static void acl_reader(void *arg)
{
    BthUsb *b = arg;
    UINT8 *buf = kmalloc(ACL_MAX), *acl = kmalloc(2 * ACL_MAX);
    UINT32 have = 0;
    int errors = 0;
    while (buf && acl && b->running) {
        int n = xfer(b, b->acl_in, true, buf, ACL_MAX);
        if (n < 0) {
            if (!b->running || ++errors > 20) break;
            INT64 t = -10000LL * 50;
            nt_KeDelayExecutionThread(0, 0, &t);
            continue;
        }
        errors = 0;
        if (have + (UINT32)n > 2 * ACL_MAX) have = 0;
        memcpy(acl + have, buf, (UINT32)n);
        have += (UINT32)n;
        while (have >= 4) {
            UINT32 len = 4u + (UINT32)(acl[2] | acl[3] << 8);
            if (len > ACL_MAX) { have = 0; break; }
            if (have < len) break;
            BthRadioAcl(b->radio, acl, len);
            have -= len;
            memmove(acl, acl + len, have);
        }
    }
    kfree(buf);
    kfree(acl);
    nt_KeSetEvent(&b->acl_done, 0, 0);
}

/* ---- start and stop ---- */

static NT_STATUS configure(BthUsb *b)
{
    UINT8 head[9];
    if (get_desc(b, 1, b->dev, sizeof(b->dev)) != sizeof(b->dev) || get_desc(b, 2, head, sizeof(head)) != sizeof(head))
        return ST_DEVICE_NOT_CONNECTED;
    b->cfg_len = (UINT32)(head[2] | head[3] << 8);
    b->cfg = b->cfg_len >= 9 ? kmalloc(b->cfg_len) : NULL;
    if (!b->cfg) return NT_STATUS_INSUFFICIENT_RESOURCES;
    if (get_desc(b, 2, b->cfg, b->cfg_len) != (int)b->cfg_len) return ST_DEVICE_NOT_CONNECTED;
    /* Interfaces 0 (HCI) and 1 (SCO), each at its first setting */
    UsbdIfaceListEntry list[3] = { { NULL, NULL }, { NULL, NULL }, { NULL, NULL } };
    int k = 0;
    for (UINT32 off = 0; off + 2 <= b->cfg_len && b->cfg[off] >= 2 && k < 2; off += b->cfg[off]) {
        UINT8 *d = &b->cfg[off];
        if (d[1] == 4 && d[0] >= 9 && d[3] == 0 && (d[2] == 0 || d[2] == 1)) list[k++].InterfaceDescriptor = d;
    }
    if (!k) return NT_STATUS_UNSUCCESSFUL;
    /* interface 1's settings: each one's isochronous packet sizes */
    b->nalts = 0;
    for (UINT32 off = 0, cur = 0xFF; off + 2 <= b->cfg_len && b->cfg[off] >= 2; off += b->cfg[off]) {
        UINT8 *d = &b->cfg[off];
        if (d[1] == 4 && d[0] >= 9) {
            cur = d[2] == 1 && b->nalts < SCO_ALTS ? (UINT32)b->nalts++ : 0xFF;
            if (cur != 0xFF) { b->alts[cur].alt = d[3]; b->alts[cur].in = b->alts[cur].out = 0; }
        } else if (d[1] == 5 && d[0] >= 7 && cur != 0xFF && (d[3] & 3) == 1) {
            UINT16 m = (UINT16)((d[4] | d[5] << 8) & 0x7FF);
            if (d[2] & 0x80) b->alts[cur].in = m; else b->alts[cur].out = m;
        }
    }
    UrbSelectConfig *sel = nt_USBD_CreateConfigurationRequestEx(b->cfg, list);
    if (!sel) return NT_STATUS_INSUFFICIENT_RESOURCES;
    NT_STATUS st = urb_sync(b, sel);
    b->cfg_handle = sel->ConfigurationHandle;
    b->sco_alt = 0;
    for (int i = 0; NT_SUCCESS(st) && i < k; i++) {
        UsbdIfaceInfo *f = list[i].Interface;
        if (f->InterfaceNumber != 0) continue;
        for (UINT32 j = 0; j < f->NumberOfPipes; j++) {
            UsbdPipeInfo *p = &f->Pipes[j];
            if (p->PipeType == 3 && (p->EndpointAddress & 0x80)) { b->evt = p->PipeHandle; b->evt_mps = p->MaximumPacketSize; }
            else if (p->PipeType == 2 && (p->EndpointAddress & 0x80)) b->acl_in = p->PipeHandle;
            else if (p->PipeType == 2) b->acl_out = p->PipeHandle;
        }
    }
    nt_ExFreePool(sel);
    if (!NT_SUCCESS(st)) return st;
    if (!b->evt || !b->acl_in || !b->acl_out || !b->evt_mps) {
        kprintf("[BTHUSB] %04x:%04x: not a Bluetooth radio's endpoints\n", b->dev[8] | b->dev[9] << 8, b->dev[10] | b->dev[11] << 8);
        return NT_STATUS_UNSUCCESSFUL;
    }
    kprintf("[BTHUSB] %04x:%04x: HCI events on interrupt IN (%u bytes), ACL on bulk IN and OUT, %d interface(s)\n",
            b->dev[8] | b->dev[9] << 8, b->dev[10] | b->dev[11] << 8, b->evt_mps, k);
    return NT_STATUS_SUCCESS;
}

static void stop(BthUsb *b)
{
    if (!b->started) return;
    b->started = false;
    BthRadioStop(b->radio);
    sco_stop(b);
    b->running = false;
    if (!b->gone) { abort_pipe(b, b->evt); abort_pipe(b, b->acl_in); }
    nt_KeWaitForSingleObject(&b->evt_done, 0, 0, 0, NULL);
    nt_KeWaitForSingleObject(&b->acl_done, 0, 0, 0, NULL);
}

static NT_API NT_STATUS sync_done(NT_DEVICE_OBJECT *d, NT_IRP *irp, void *ctx)
{
    (void)d; (void)irp;
    nt_KeSetEvent(ctx, 0, 0);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;
}

static NT_STATUS forward_sync(BthUsb *b, NT_IRP *irp)
{
    NT_KEVENT ev;
    nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    NT_IO_STACK_LOCATION *cur = NtIrpCurrentStack(irp), *next = NtIrpNextStack(irp);
    memcpy(next, cur, __builtin_offsetof(NT_IO_STACK_LOCATION, CompletionRoutine));
    next->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    next->CompletionRoutine = sync_done;
    next->Context = &ev;
    nt_IofCallDriver(b->next, irp);
    nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    return irp->IoStatus.Status;
}

static NT_STATUS pass_down(BthUsb *b, NT_IRP *irp)
{
    irp->CurrentLocation++;
    irp->Tail.Overlay.CurrentStackLocation++;
    return nt_IofCallDriver(b->next, irp);
}

static void done(NT_IRP *irp, NT_STATUS st, NT_ULONG_PTR info)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    nt_IofCompleteRequest(irp, 0);
}

static NT_STATUS pnp(BthUsb *b, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE: {
        NT_STATUS st = forward_sync(b, irp);
        if (NT_SUCCESS(st)) st = configure(b);
        if (NT_SUCCESS(st)) {
            b->running = true;
            nt_KeClearEvent(&b->evt_done);
            nt_KeClearEvent(&b->acl_done);
            if (!sched_create_thread_ex("bth-evt", evt_reader, b, PRIO_DEVICE_IO, 16 * 1024)) nt_KeSetEvent(&b->evt_done, 0, 0);
            if (!sched_create_thread_ex("bth-acl", acl_reader, b, PRIO_DEVICE_IO, 16 * 1024)) nt_KeSetEvent(&b->acl_done, 0, 0);
            b->started = true;
            BthRadioStart(b->radio);
        }
        done(irp, st, 0);
        return st;
    }
    case NT_IRP_MN_QUERY_DEVICE_RELATIONS:
        if ((int)(UINT64)sl->Parameters.Others.Argument1 == 0 && b->radio) {   /* BusRelations: the services' nodes */
            NT_ULONG_PTR info = 0;
            NT_STATUS st = BthRadioRelations(b->radio, &info);
            if (!NT_SUCCESS(st)) { done(irp, st, 0); return st; }
            irp->IoStatus.Status = NT_STATUS_SUCCESS;
            irp->IoStatus.Information = info;
        }
        break;
    case NT_IRP_MN_SURPRISE_REMOVAL:
        b->gone = true;
        stop(b);
        break;
    case NT_IRP_MN_REMOVE_DEVICE: {
        stop(b);
        NT_STATUS st = pass_down(b, irp);
        nt_IoDetachDevice(b->next);
        BthRadioFree(b->radio);
        kfree(b->cfg);
        kfree(b->fifo);
        nt_IoDeleteDevice(b->self);
        return st;
    }
    }
    return pass_down(b, irp);
}

static NT_API NT_STATUS dispatch(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    if (BthIsServicePdo(dev)) return BthServiceDispatch(dev, irp);
    BthUsb *b = bu(dev);
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    NT_STATUS st;
    NT_ULONG_PTR info = 0;
    switch (sl->MajorFunction) {
    case NT_IRP_MJ_PNP:
        return pnp(b, irp, sl);
    case NT_IRP_MJ_POWER:
    case NT_IRP_MJ_SYSTEM_CONTROL:
        return pass_down(b, irp);
    case NT_IRP_MJ_CREATE:
        st = b->started && !b->gone ? NT_STATUS_SUCCESS : ST_DEVICE_NOT_CONNECTED;
        break;
    case NT_IRP_MJ_CLEANUP:
    case NT_IRP_MJ_CLOSE:
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MJ_DEVICE_CONTROL:
        st = BthRadioIoctl(b->radio, irp, sl, &info);
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
    NT_STATUS st = nt_IoCreateDevice(drv, sizeof(BthUsb), NULL, FILE_DEVICE_BLUETOOTH, 0x100 /* AUTOGENERATED_NAME */, 0, &fdo);
    if (!NT_SUCCESS(st)) return st;
    BthUsb *b = bu(fdo);
    memset(b, 0, sizeof(*b));
    b->self = fdo;
    b->pdo = pdo;
    nt_KeInitializeEvent(&b->send_lock, NT_SynchronizationEvent, 1);
    nt_KeInitializeEvent(&b->evt_done, NT_NotificationEvent, 1);
    nt_KeInitializeEvent(&b->acl_done, NT_NotificationEvent, 1);
    nt_KeInitializeEvent(&b->sco_lock, NT_SynchronizationEvent, 1);
    nt_KeInitializeEvent(&b->sco_in_done, NT_NotificationEvent, 1);
    nt_KeInitializeEvent(&b->sco_out_done, NT_NotificationEvent, 1);
    b->fifo = kmalloc(SCO_FIFO);
    if (!b->fifo) { nt_IoDeleteDevice(fdo); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    b->next = nt_IoAttachDeviceToDeviceStack(fdo, pdo);
    if (!b->next) { nt_IoDeleteDevice(fdo); return NT_STATUS_UNSUCCESSFUL; }
    b->radio = BthRadioCreate(fdo, pdo, &g_transport, b);
    if (!b->radio) { nt_IoDetachDevice(b->next); nt_IoDeleteDevice(fdo); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    fdo->StackSize = (char)(b->next->StackSize + 1);
    fdo->Flags = (fdo->Flags & ~NT_DO_DEVICE_INITIALIZING) | NT_DO_BUFFERED_IO | NT_DO_POWER_PAGABLE;
    return NT_STATUS_SUCCESS;
}

NTSTATUS NT_API BthUsbDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg)
{
    (void)reg;
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) drv->MajorFunction[i] = dispatch;
    drv->DriverExtension->AddDevice = add_device;
    return NT_STATUS_SUCCESS;
}
