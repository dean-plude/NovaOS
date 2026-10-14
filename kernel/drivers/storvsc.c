/*
 * storvsc.c — Hyper-V's synthetic SCSI controller
 *
 * A Generation 2 Hyper-V VM's disks and DVD drive sit on a synthetic SCSI
 * controller, a VMBus channel (vmbus.c).  Its protocol (as FreeBSD's
 * BSD-licensed hv_storvsc and Linux's storvsc speak it) wraps Windows'
 * SCSI request block in a 64-byte "vstor" packet:
 *
 *    0  operation (EXECUTE_SRB, BEGIN_INITIALIZATION, ...)
 *    4  flags (1: the host answers with a completion)
 *    8  status (0: done)
 *   12  the SRB: its length, SRB and SCSI status, path, target, LUN, CDB
 *       and sense lengths, the direction, the transfer length, the CDB (or,
 *       in the answer, the sense data) and the Windows 8 extension (SRB
 *       flags, time-out)
 *
 * Bringing it up: BEGIN_INITIALIZATION, QUERY_PROTOCOL_VERSION with the
 * newest version first (Windows 10's 6.2, Windows 8.1's 6.0, Windows 8's
 * 5.1: every host with Generation 2 VMs speaks one) until the host says
 * yes, QUERY_PROPERTIES (the largest transfer), END_INITIALIZATION.  Each
 * request goes with "completion wanted" and the host answers each with a
 * completion packet carrying the request's transaction ID.  Until a
 * version is agreed, the packets are sent without the Windows 8 extension
 * (16 bytes shorter), as Linux does.
 *
 * Then each target (0 and 1) and LUN (0-63) is asked INQUIRY: Hyper-V
 * puts the disk at "Location n" of the controller on LUN n.  A disk with
 * 512-byte blocks becomes a block device (hv0, hv1, ...), with READ
 * CAPACITY for its size; the DVD drive is left alone (NovaOS already
 * started from it).  A transfer is an EXECUTE_SRB packet sent GPA-direct:
 * the packet lists the pages of the controller's bounce buffer, which the
 * host reads or fills in place.  One request at a time per controller;
 * the caller waits for the answer by reading the channel itself
 * (VmbusPollChannel), so this works before interrupts are on (drive C: is
 * looked for at boot) and in any thread after.
 */

#include "storvsc.h"
#include "../fs/block.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../mm/vmm.h"
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/apic.h"

#define VSTOR_COMPLETE_IO          1
#define VSTOR_REMOVE_DEVICE        2
#define VSTOR_EXECUTE_SRB          3
#define VSTOR_BEGIN_INIT           7
#define VSTOR_END_INIT             8
#define VSTOR_QUERY_PROTOCOL       9
#define VSTOR_QUERY_PROPERTIES     10
#define VSTOR_ENUMERATE_BUS        11
#define VSTOR_FLAG_COMPLETION      1

#define SRB_WRITE                  0     /* data_in */
#define SRB_READ                   1
#define SRB_NO_DATA                2
#define SRB_FLAGS_QUEUE_ACTION     0x02
#define SRB_FLAGS_NO_SYNC          0x08
#define SRB_FLAGS_DATA_IN          0x40
#define SRB_FLAGS_DATA_OUT         0x80

#define SRB_STATUS_SUCCESS         0x01
#define SRB_STATUS_ERROR           0x04
#define SRB_STATUS_NO_DEVICE       0x08
#define SRB_STATUS_SELECTION_TIMEOUT 0x0A
#define SRB_STATUS_DATA_OVERRUN    0x12
#define SRB_STATUS_INVALID_LUN     0x20
#define SRB_STATUS_INVALID_TARGET  0x22
#define SRB_STATUS_MASK            0x3F

#define SENSE_LEN                  20
#define WIN8_EXT_LEN               16
#define XFER_PAGES                 32    /* the bounce buffer: 128 KiB */
#define TIMEOUT_MS                 30000
#define MAX_TARGETS                2
#define MAX_LUNS                   64

typedef struct __attribute__((packed)) {
    UINT16 length;
    UINT8  srb_status, scsi_status;
    UINT8  port, path_id, target_id, lun;
    UINT8  cdb_length, sense_length, data_in, reserved;
    UINT32 transfer_length;
    UINT8  cdb[SENSE_LEN];               /* the CDB out, the sense data back */
    UINT16 w8_reserved;                  /* the Windows 8 extension */
    UINT8  queue_tag, queue_action;
    UINT32 srb_flags, timeout, queue_sort_key;
} VmScsiRequest;

typedef struct __attribute__((packed)) {
    UINT32 operation, flags, status;
    union {
        VmScsiRequest srb;
        struct { UINT16 major_minor, revision; } version;
        struct { UINT32 reserved; UINT16 max_channels, reserved1; UINT32 flags, max_transfer; } props;
        UINT8 raw[52];
    };
} VstorPacket;

_Static_assert(sizeof(VmScsiRequest) == 52, "vmscsi_request");
_Static_assert(sizeof(VstorPacket) == 64, "vstor_packet");

typedef struct {
    VmbusChannel *ch;
    int           number;                /* controller n of this VM */
    volatile int  busy;                  /* one request at a time */
    UINT32        pkt_len;               /* 48 until a version with the extension is agreed */
    UINT32        max_transfer;
    UINT64        next_id;
    volatile UINT64 wait_id;             /* the request waiting for its answer */
    volatile bool done;
    bool          dead;                  /* a request got no answer: no more are sent */
    VstorPacket   reply;
    UINT8        *bounce;                /* XFER_PAGES pages */
} Stor;

typedef struct {
    BlockDev dev;
    Stor    *s;
    UINT8    target, lun;
} StorDisk;

static int g_controllers, g_disks;

static void take(volatile int *f)
{
    while (__atomic_exchange_n(f, 1, __ATOMIC_ACQUIRE)) {
        if (interrupts_enabled()) sched_yield();
        else pause_cpu();
    }
}
static void drop(volatile int *f) { __atomic_store_n(f, 0, __ATOMIC_RELEASE); }

/* An answer from the host: the reply to the waiting request when its
 * transaction ID matches (completion packets, and in-band ones too, as
 * Linux takes them) */
static bool take_reply(Stor *s, const UINT8 *d, UINT32 len, UINT64 trans_id)
{
    if (!trans_id || trans_id != s->wait_id || s->done) return false;
    memset(&s->reply, 0, sizeof(s->reply));
    memcpy(&s->reply, d, len < sizeof(s->reply) ? len : sizeof(s->reply));
    __atomic_store_n(&s->done, true, __ATOMIC_RELEASE);
    return true;
}

static void stor_completion(void *ctx, const UINT8 *d, UINT32 len, UINT64 trans_id)
{
    take_reply(ctx, d, len, trans_id);
}

static void stor_packet(void *ctx, const UINT8 *d, UINT32 len, UINT64 trans_id)
{
    Stor *s = ctx;
    if (!s || take_reply(s, d, len, trans_id) || len < 4) return;
    UINT32 op = d[0] | d[1] << 8 | d[2] << 16 | (UINT32)d[3] << 24;
    if (op == VSTOR_ENUMERATE_BUS || op == VSTOR_REMOVE_DEVICE)
        kprintf("[STORVSC] controller %d: the host %s (disks added or removed while running are not picked up)\n",
                s->number, op == VSTOR_ENUMERATE_BUS ? "asks for a rescan" : "removed a device");
}

/* Send @p (@buf: the bounce buffer's first @buf_len bytes go with it, or
 * none) and wait for the answer in s->reply.  False when it could not be
 * sent or no answer came */
static bool request(Stor *s, VstorPacket *p, UINT32 buf_len)
{
    if (s->dead) return false;
    UINT64 id = ++s->next_id;
    s->done = false;
    __atomic_store_n(&s->wait_id, id, __ATOMIC_RELEASE);
    p->flags = VSTOR_FLAG_COMPLETION;
    bool sent = buf_len ? VmbusSendGpa(s->ch, p, s->pkt_len, id, s->bounce, buf_len)
                        : VmbusSend(s->ch, p, s->pkt_len, id, true);
    for (UINT32 t = 0; sent && !__atomic_load_n(&s->done, __ATOMIC_ACQUIRE); t++) {
        VmbusPollChannel(s->ch);
        if (__atomic_load_n(&s->done, __ATOMIC_ACQUIRE)) break;
        if (t >= TIMEOUT_MS * 100) {
            kprintf("[STORVSC] controller %d: no answer in %u s: given up\n", s->number, TIMEOUT_MS / 1000);
            s->dead = true;
            sent = false;
            break;
        }
        udelay(10);
        if (interrupts_enabled() && sched_current() && !(t & 127)) sched_yield();
    }
    __atomic_store_n(&s->wait_id, 0, __ATOMIC_RELEASE);
    return sent && s->done;
}

/* A vstor operation with no data: its status (0 done), -1 when no answer */
static INT64 simple(Stor *s, UINT32 op, UINT16 version)
{
    VstorPacket p;
    memset(&p, 0, sizeof(p));
    p.operation = op;
    if (op == VSTOR_QUERY_PROTOCOL) p.version.major_minor = version;
    if (!request(s, &p, 0)) return -1;
    return s->reply.operation == VSTOR_COMPLETE_IO ? (INT64)s->reply.status : -1;
}

/* One SCSI command to @target/@lun: @cdb, @len bytes of data in the bounce
 * buffer going @dir (SRB_READ, SRB_WRITE, SRB_NO_DATA).  0 done, 1 check
 * condition (*sense_key set), -2 no such device, -1 any other failure */
static int srb(Stor *s, UINT8 target, UINT8 lun, const UINT8 *cdb, UINT8 cdb_len, int dir,
               UINT32 len, UINT8 *sense_key)
{
    VstorPacket p;
    memset(&p, 0, sizeof(p));
    p.operation = VSTOR_EXECUTE_SRB;
    VmScsiRequest *r = &p.srb;
    r->length = sizeof(VmScsiRequest);
    r->target_id = target;
    r->lun = lun;
    r->cdb_length = cdb_len;
    r->sense_length = SENSE_LEN;
    r->data_in = (UINT8)dir;
    r->transfer_length = dir == SRB_NO_DATA ? 0 : len;
    memcpy(r->cdb, cdb, cdb_len);
    r->srb_flags = SRB_FLAGS_QUEUE_ACTION | SRB_FLAGS_NO_SYNC |
                   (dir == SRB_READ ? SRB_FLAGS_DATA_IN : dir == SRB_WRITE ? SRB_FLAGS_DATA_OUT : 0);
    r->timeout = 60;
    if (!request(s, &p, dir == SRB_NO_DATA ? 0 : len)) return -1;
    if (s->reply.operation != VSTOR_COMPLETE_IO) return -1;
    const VmScsiRequest *a = &s->reply.srb;
    UINT8 st = a->srb_status & SRB_STATUS_MASK;
    if (st == SRB_STATUS_INVALID_LUN || st == SRB_STATUS_SELECTION_TIMEOUT ||
        st == SRB_STATUS_NO_DEVICE || st == SRB_STATUS_INVALID_TARGET)
        return -2;
    if (a->scsi_status == 2) {                         /* CHECK CONDITION */
        if (sense_key) *sense_key = a->cdb[2] & 0xF;
        return 1;
    }
    if (a->scsi_status) return -1;
    return (st == SRB_STATUS_SUCCESS || st == SRB_STATUS_DATA_OVERRUN) ? 0 : -1;
}

/* Read or write @count sectors at @lba through the bounce buffer, retrying
 * after a check condition (a unit attention after a reset) */
static bool rw(StorDisk *k, bool write, UINT64 lba, UINT32 count)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        UINT8 cdb[16];
        memset(cdb, 0, sizeof(cdb));
        UINT8 cl;
        if (lba + count > 0xFFFFFFFFull) {               /* READ(16) / WRITE(16) */
            cdb[0] = write ? 0x8A : 0x88;
            for (int i = 0; i < 8; i++) cdb[2 + i] = (UINT8)(lba >> (56 - 8 * i));
            for (int i = 0; i < 4; i++) cdb[10 + i] = (UINT8)(count >> (24 - 8 * i));
            cl = 16;
        } else {                                         /* READ(10) / WRITE(10) */
            cdb[0] = write ? 0x2A : 0x28;
            for (int i = 0; i < 4; i++) cdb[2 + i] = (UINT8)(lba >> (24 - 8 * i));
            cdb[7] = (UINT8)(count >> 8);
            cdb[8] = (UINT8)count;
            cl = 10;
        }
        int st = srb(k->s, k->target, k->lun, cdb, cl, write ? SRB_WRITE : SRB_READ, count * BLOCK_SECTOR, NULL);
        if (st == 0) return true;
        if (st != 1) return false;
    }
    return false;
}

static bool stor_xfer(BlockDev *bd, bool write, UINT64 lba, UINT32 count, UINT8 *p)
{
    StorDisk *k = bd->ctx;
    Stor *s = k->s;
    UINT32 per = s->max_transfer / BLOCK_SECTOR;
    bool ok = true;
    while (count && ok) {
        UINT32 n = count < per ? count : per;
        take(&s->busy);
        if (write) memcpy(s->bounce, p, (size_t)n * BLOCK_SECTOR);
        ok = rw(k, write, lba, n);
        if (ok && !write) memcpy(p, s->bounce, (size_t)n * BLOCK_SECTOR);
        drop(&s->busy);
        lba += n; count -= n; p += (size_t)n * BLOCK_SECTOR;
    }
    return ok;
}

static bool stor_read(BlockDev *bd, UINT64 lba, UINT32 count, void *buf)
{
    return stor_xfer(bd, false, lba, count, buf);
}

static bool stor_write(BlockDev *bd, UINT64 lba, UINT32 count, const void *buf)
{
    return stor_xfer(bd, true, lba, count, (UINT8 *)buf);
}

static bool stor_flush(BlockDev *bd)
{
    StorDisk *k = bd->ctx;
    take(&k->s->busy);
    UINT8 cdb[10] = { 0x35 };                          /* SYNCHRONIZE CACHE(10) */
    int st = srb(k->s, k->target, k->lun, cdb, 10, SRB_NO_DATA, 0, NULL);
    drop(&k->s->busy);
    return st >= 0;
}

static UINT32 be32(const UINT8 *b) { return (UINT32)b[0] << 24 | (UINT32)b[1] << 16 | (UINT32)b[2] << 8 | b[3]; }

/* The device at @target/@lun: a disk becomes a block device */
static void probe(Stor *s, UINT8 target, UINT8 lun)
{
    UINT8 *b = s->bounce;
    UINT8 inq[6] = { 0x12, 0, 0, 0, 36, 0 };
    memset(b, 0, 36);
    if (srb(s, target, lun, inq, 6, SRB_READ, 36, NULL) != 0 || (b[0] & 0xE0)) return;  /* nothing there */
    UINT8 type = b[0] & 0x1F;
    char model[41];
    memset(model, 0, sizeof(model));
    memcpy(model, b + 8, 8);
    model[8] = ' ';
    memcpy(model + 9, b + 16, 16);
    for (int i = 24; i >= 0 && (model[i] == ' ' || !model[i]); i--) model[i] = 0;
    if (type != 0) {
        kprintf("[STORVSC] controller %d, location %u: %s (%s): not used\n", s->number, lun, model,
                type == 5 ? "DVD drive; NovaOS started from it already" : "not a disk");
        return;
    }

    UINT8 tur[6] = { 0 }, key = 0;                     /* (clears a unit attention) */
    srb(s, target, lun, tur, 6, SRB_NO_DATA, 0, &key);
    UINT8 rc10[10] = { 0x25 };
    UINT64 blocks = 0;
    UINT32 bsize = 0;
    if (srb(s, target, lun, rc10, 10, SRB_READ, 8, NULL) == 0) {
        UINT32 last = be32(b);
        bsize = be32(b + 4);
        blocks = (UINT64)last + 1;
        if (last == 0xFFFFFFFFu) {
            UINT8 rc16[16] = { 0x9E, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0 };
            if (srb(s, target, lun, rc16, 16, SRB_READ, 32, NULL) == 0) {
                blocks = ((UINT64)be32(b) << 32 | be32(b + 4)) + 1;
                bsize = be32(b + 8);
            }
        }
    }
    if (!blocks || bsize != BLOCK_SECTOR) {
        kprintf("[STORVSC] controller %d, location %u: %s: %s\n", s->number, lun, model,
                blocks ? "its blocks are not 512 bytes (a VHDX made with 4 KiB logical sectors): not used"
                       : "no capacity reported: not used");
        return;
    }
    StorDisk *k = kzalloc(sizeof(StorDisk));
    if (!k) return;
    k->s = s;
    k->target = target;
    k->lun = lun;
    ksnprintf(k->dev.name, sizeof(k->dev.name), "hv%d", g_disks++);
    strncpy(k->dev.model, model, sizeof(k->dev.model) - 1);
    k->dev.sectors = blocks;
    k->dev.read = stor_read;
    k->dev.write = stor_write;
    k->dev.flush = stor_flush;
    k->dev.ctx = k;
    kprintf("[STORVSC] controller %d, location %u: %s \"%s\", %llu MiB\n", s->number, lun, k->dev.name,
            model, (unsigned long long)(blocks * BLOCK_SECTOR >> 20));
    BlockRegister(&k->dev);
}

static void *stor_opened(VmbusChannel *ch)
{
    Stor *s = kzalloc(sizeof(Stor));
    if (!s) return NULL;
    s->bounce = kernel_alloc_pages(XFER_PAGES);
    if (!s->bounce) { kfree(s); return NULL; }
    s->ch = ch;
    s->number = g_controllers++;
    s->pkt_len = sizeof(VstorPacket) - WIN8_EXT_LEN;
    return s;
}

static void stor_started(void *ctx)
{
    static const UINT16 versions[] = { 0x602, 0x600, 0x501 };   /* Windows 10, 8.1, 8 */
    Stor *s = ctx;
    INT64 st = simple(s, VSTOR_BEGIN_INIT, 0);
    if (st) {
        kprintf("[STORVSC] controller %d: %s\n", s->number,
                st < 0 ? "no answer to BEGIN_INITIALIZATION" : "BEGIN_INITIALIZATION refused");
        return;
    }
    UINT16 v = 0;
    for (UINT32 i = 0; i < sizeof(versions) / sizeof(versions[0]) && !v; i++)
        if (simple(s, VSTOR_QUERY_PROTOCOL, versions[i]) == 0) v = versions[i];
    if (!v) {
        kprintf("[STORVSC] controller %d: the host took no protocol version NovaOS speaks (6.2, 6.0, 5.1)\n",
                s->number);
        return;
    }
    s->pkt_len = sizeof(VstorPacket);                  /* (5.1 and later: the Windows 8 extension) */
    VstorPacket p;
    memset(&p, 0, sizeof(p));
    p.operation = VSTOR_QUERY_PROPERTIES;
    UINT32 max = XFER_PAGES * 4096u;
    if (request(s, &p, 0) && s->reply.status == 0 && s->reply.props.max_transfer >= BLOCK_SECTOR &&
        s->reply.props.max_transfer < max)
        max = s->reply.props.max_transfer & ~(BLOCK_SECTOR - 1);
    s->max_transfer = max;
    if (simple(s, VSTOR_END_INIT, 0)) {
        kprintf("[STORVSC] controller %d: END_INITIALIZATION refused\n", s->number);
        return;
    }
    kprintf("[STORVSC] controller %d: protocol %u.%u, transfers up to %u KiB\n", s->number,
            v >> 8, v & 0xFF, max >> 10);
    int before = g_disks;
    for (UINT8 t = 0; t < MAX_TARGETS; t++)
        for (UINT8 l = 0; l < MAX_LUNS && !s->dead; l++) probe(s, t, l);
    if (g_disks == before) kprintf("[STORVSC] controller %d: no disks\n", s->number);
}

/* {ba6163d9-04a1-4d29-b605-72e2ffb1dc7f} */
/* The controller was taken away: its disks answer no more */
static void stor_removed(void *ctx)
{
    Stor *s = ctx;
    s->dead = true;
}

const VmbusDriver HvStorageDriver = {
    { 0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d, 0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f },
    "Hyper-V SCSI", stor_opened, stor_packet, stor_completion, stor_started, NULL, NULL, stor_removed,
};
