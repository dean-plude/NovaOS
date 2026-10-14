/* Hyper-V's synthetic SCSI controller on the host
 * (tests/tools/test_hv_storvsc.py): a fake host answers the driver's vstor
 * packets as Hyper-V would, with disks in memory behind it */
#include "../../kernel/drivers/vmbus_ring.h"
#include "../../kernel/drivers/storvsc.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- the kernel around the driver ---- */
static BlockDev *registered[8];
static int nregistered;
void BlockRegister(BlockDev *d) { registered[nregistered++] = d; }
void kprintf(const char *f, ...) { va_list v; va_start(v, f); vprintf(f, v); va_end(v); }
int ksnprintf(char *b, size_t n, const char *f, ...) { va_list v; va_start(v, f); int r = vsnprintf(b, n, f, v); va_end(v); return r; }
void *kzalloc(size_t n) { return calloc(1, n); }
void kfree(void *p) { free(p); }
void *kernel_alloc_pages(size_t n) { void *p = aligned_alloc(4096, n * 4096); memset(p, 0, n * 4096); return p; }
void udelay(UINT64 us) { (void)us; }
void sched_yield(void) {}
Thread *sched_current(void) { return NULL; }

/* ---- the fake host ---- */
#define DISK_SECTORS 2048
static UINT8  disk0[DISK_SECTORS * 512];
static Stor  *ctx;
static VstorPacket pending;                 /* the answer the next poll delivers */
static UINT64 pending_id;
static bool   have_pending, version_agreed, silent, attention = true;
static int    srbs, biggest_xfer, gpa_sends;

static void answer(const VstorPacket *in, UINT64 id, UINT32 status)
{
    pending = *in;
    pending.operation = VSTOR_COMPLETE_IO;
    pending.status = status;
    pending_id = id;
    have_pending = true;
}

static void srb_answer(VstorPacket *p, UINT64 id, UINT8 srb_status, UINT8 scsi_status, UINT8 key)
{
    p->srb.srb_status = srb_status;
    p->srb.scsi_status = scsi_status;
    if (scsi_status == 2) { memset(p->srb.cdb, 0, SENSE_LEN); p->srb.cdb[0] = 0x70; p->srb.cdb[2] = key; }
    answer(p, id, 0);
}

static UINT64 be(const UINT8 *b, int n) { UINT64 v = 0; for (int i = 0; i < n; i++) v = v << 8 | b[i]; return v; }
static void put_be(UINT8 *b, UINT64 v, int n) { for (int i = n - 1; i >= 0; i--, v >>= 8) b[i] = (UINT8)v; }

static void execute(VstorPacket p, UINT64 id, UINT8 *buf, UINT32 buf_len)
{
    VmScsiRequest *r = &p.srb;
    srbs++;
    assert(r->length == sizeof(VmScsiRequest) && r->sense_length == SENSE_LEN && r->timeout == 60);
    assert((r->data_in == SRB_NO_DATA) == !buf);
    if (buf) {
        assert(buf_len == r->transfer_length);
        assert((r->srb_flags & SRB_FLAGS_DATA_IN) == (r->data_in == SRB_READ ? SRB_FLAGS_DATA_IN : 0));
        assert((r->srb_flags & SRB_FLAGS_DATA_OUT) == (r->data_in == SRB_WRITE ? SRB_FLAGS_DATA_OUT : 0));
    }
    if (r->target_id) { srb_answer(&p, id, SRB_STATUS_INVALID_TARGET, 0, 0); return; }
    UINT8 lun = r->lun, op = r->cdb[0];
    if (lun > 3) {                                          /* (Hyper-V answers an empty LUN either way) */
        if (lun & 1) { srb_answer(&p, id, SRB_STATUS_INVALID_LUN, 0, 0); return; }
        if (op == 0x12) { memset(buf, 0, buf_len); buf[0] = 0x7F; }
        srb_answer(&p, id, SRB_STATUS_SUCCESS, 0, 0);
        return;
    }
    switch (op) {
    case 0x12:                                              /* INQUIRY */
        memset(buf, 0, buf_len);
        buf[0] = lun == 1 ? 5 : 0;
        memcpy(buf + 8, "Msft    ", 8);
        memcpy(buf + 16, lun == 1 ? "Virtual DVD-ROM " : "Virtual Disk    ", 16);
        break;
    case 0x00:                                              /* TEST UNIT READY: one unit attention */
        if (lun == 0 && attention) { attention = false; srb_answer(&p, id, SRB_STATUS_ERROR | 0x80, 2, 6); return; }
        break;
    case 0x25:                                              /* READ CAPACITY(10) */
        put_be(buf, lun == 3 ? 0xFFFFFFFFu : lun == 2 ? 255 : DISK_SECTORS - 1, 4);
        put_be(buf + 4, lun == 2 ? 4096 : 512, 4);
        break;
    case 0x9E:                                              /* READ CAPACITY(16) */
        assert(lun == 3 && r->cdb[1] == 0x10);
        memset(buf, 0, buf_len);
        put_be(buf, (1ull << 33) - 1, 8);
        put_be(buf + 8, 512, 4);
        break;
    case 0x28: case 0x2A: case 0x88: case 0x8A: {           /* READ / WRITE */
        assert(lun == 0);
        bool sixteen = op >= 0x88, write = op == 0x2A || op == 0x8A;
        UINT64 lba = sixteen ? be(r->cdb + 2, 8) : be(r->cdb + 2, 4);
        UINT32 n = (UINT32)(sixteen ? be(r->cdb + 10, 4) : be(r->cdb + 7, 2));
        assert(!sixteen && r->cdb_length == 10 && n * 512 == buf_len && lba + n <= DISK_SECTORS);
        assert(r->data_in == (write ? SRB_WRITE : SRB_READ));
        if ((int)buf_len > biggest_xfer) biggest_xfer = (int)buf_len;
        if (write) memcpy(disk0 + lba * 512, buf, buf_len);
        else memcpy(buf, disk0 + lba * 512, buf_len);
        break;
    }
    case 0x35:                                              /* SYNCHRONIZE CACHE */
        break;
    default:
        assert(!"unexpected CDB");
    }
    srb_answer(&p, id, SRB_STATUS_SUCCESS, 0, 0);
}

static void host(const void *data, UINT32 len, UINT64 id, UINT8 *buf, UINT32 buf_len)
{
    VstorPacket p;
    memset(&p, 0, sizeof(p));
    assert(len == (version_agreed ? 64u : 48u));
    memcpy(&p, data, len);
    assert(p.flags == VSTOR_FLAG_COMPLETION && id);
    assert(!have_pending);
    switch (p.operation) {
    case VSTOR_BEGIN_INIT: case VSTOR_END_INIT: answer(&p, id, 0); break;
    case VSTOR_QUERY_PROTOCOL:
        assert(p.version.revision == 0);
        if (p.version.major_minor == 0x600) version_agreed = true;
        answer(&p, id, p.version.major_minor == 0x600 ? 0 : 0xC0000001u);
        break;
    case VSTOR_QUERY_PROPERTIES:
        p.props.max_transfer = 65536;
        answer(&p, id, 0);
        break;
    case VSTOR_EXECUTE_SRB: execute(p, id, buf, buf_len); break;
    default: assert(!"unexpected operation");
    }
}

bool VmbusSend(VmbusChannel *ch, const void *data, UINT32 len, UINT64 trans_id, bool want_completion)
{
    (void)ch;
    assert(want_completion);
    host(data, len, trans_id, NULL, 0);
    return true;
}
bool VmbusSendGpa(VmbusChannel *ch, const void *data, UINT32 len, UINT64 trans_id, const void *buf, UINT32 buf_len)
{
    (void)ch;
    gpa_sends++;
    assert(buf == ctx->bounce && buf_len <= XFER_PAGES * 4096u);
    host(data, len, trans_id, (UINT8 *)buf, buf_len);
    return true;
}
void VmbusPollChannel(VmbusChannel *ch)
{
    (void)ch;
    if (!have_pending || silent) return;
    have_pending = false;
    HvStorageDriver.completion(ctx, (const UINT8 *)&pending, sizeof(pending), pending_id);
}

/* ---- the checks ---- */
static void ring_gpa_checks(void)
{
    static UINT8 mem[4 * 4096];
    VmRing r;
    vm_ring_init(&r, mem, 4);
    VmGpaRange g = { 0, 1, 5000, 100, { 7, 8 } };
    UINT8 payload[64], out[256];
    for (int i = 0; i < 64; i++) payload[i] = (UINT8)i;
    bool sig;
    assert(vm_ring_write_typed(&r, VM_PKT_GPA_DIRECT, &g, vm_gpa_len(2), payload, 64, 9,
                               VM_PKT_FLAG_COMPLETION_REQUESTED, &sig));
    VmPacketDesc d; UINT32 len;
    assert(vm_ring_read(&r, &d, out, sizeof(out), &len, &sig));
    assert(d.type == VM_PKT_GPA_DIRECT && d.offset8 == (16 + 32) / 8 && d.len8 == (16 + 32 + 64) / 8);
    assert(len == 64 && !memcmp(out, payload, 64) && d.trans_id == 9);
    UINT8 raw[48];
    vm_ring_copy_out(&r, 0, raw, 48);                       /* the page list right after the descriptor */
    VmGpaRange *back = (VmGpaRange *)(raw + 16);
    assert(back->range_count == 1 && back->len == 5000 && back->offset == 100 && back->pfn[0] == 7 && back->pfn[1] == 8);
    puts("GPA-direct packet passed");
}

int main(void)
{
    ring_gpa_checks();

    /* {ba6163d9-04a1-4d29-b605-72e2ffb1dc7f} */
    const UINT8 *g = HvStorageDriver.type;
    char s[40];
    snprintf(s, sizeof(s), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    assert(!strcmp(s, "ba6163d9-04a1-4d29-b605-72e2ffb1dc7f"));

    ctx = HvStorageDriver.opened(NULL);
    HvStorageDriver.started(ctx);
    assert(version_agreed && ctx->max_transfer == 65536 && !ctx->dead);
    assert(nregistered == 2);                               /* the 512-byte disk and the 8 TiB one */
    assert(!strcmp(registered[0]->name, "hv0") && registered[0]->sectors == DISK_SECTORS);
    assert(!strcmp(registered[0]->model, "Msft     Virtual Disk"));
    assert(registered[1]->sectors == 1ull << 33);
    puts("bring-up and scan passed");

    BlockDev *d = registered[0];
    static UINT8 data[300 * 512], back[300 * 512];
    for (size_t i = 0; i < sizeof(data); i++) data[i] = (UINT8)(i * 31 + 7);
    int before = gpa_sends;
    assert(d->write(d, 1000, 300, data));
    assert(gpa_sends - before == 3 && biggest_xfer == 65536);   /* 128 + 128 + 44 sectors */
    assert(!memcmp(disk0 + 1000 * 512, data, sizeof(data)));
    assert(d->read(d, 1000, 300, back) && !memcmp(back, data, sizeof(data)));
    assert(d->flush(d));
    puts("read, write and flush passed");

    UINT8 bus[64] = { VSTOR_ENUMERATE_BUS };                /* the host's own packets: logged */
    HvStorageDriver.packet(ctx, bus, sizeof(bus), 0);

    silent = true;                                          /* a host that stops answering */
    assert(!d->read(d, 0, 1, back) && ctx->dead);
    have_pending = false;
    int n = srbs;
    assert(!d->read(d, 0, 1, back) && srbs == n);           /* nothing more is sent */
    puts("a silent host passed");
    puts("hv_storvsc: all passed");
    return 0;
}
