/* Hyper-V's synthetic network adapter on the host
 * (tests/tools/test_hv_netvsc.py): a fake host answers the driver's NVSP
 * and RNDIS messages as Hyper-V would, and sends it frames */
#include "../../kernel/drivers/vmbus_ring.h"
#include "../../kernel/drivers/netvsc.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- the kernel around the driver ---- */
void kprintf(const char *f, ...) { va_list v; va_start(v, f); vprintf(f, v); va_end(v); }
void *kzalloc(size_t n) { return calloc(1, n); }
void *kernel_alloc_pages(size_t n) { void *p = aligned_alloc(4096, n * 4096); memset(p, 0, n * 4096); return p; }
void udelay(UINT64 us) { (void)us; }
void sched_yield(void) {}

/* ---- the fake host ---- */
#define GPADL 0x42
typedef struct { int kind; UINT64 tid; UINT8 data[64]; UINT32 len; VmbusXferRange r[8]; UINT32 n; } Event;
static Event   events[64];
static int     nevents;
static Netvsc *ctx;
static UINT32  accepts = 0x40000, rx_at;             /* where the next host message goes in the receive buffer */
static bool    silent, hold_tx, refuse_rxbuf;
static int     ndis_conf, ndis_init, ndis_minor, acks, keepalives, filter_sets;
static UINT64  last_ack;
static UINT8   frames[40][FRAME_MAX];
static UINT32  frame_len[40];
static int     nframes;
static UINT64  held[TX_SLOTS + 4];
static int     nheld;
static const UINT8 host_mac[6] = { 0x00, 0x15, 0x5d, 0x01, 0x02, 0x03 };
static bool    host_link = true;

static Event *event(int kind, UINT64 tid) { Event *e = &events[nevents++]; memset(e, 0, sizeof(*e)); e->kind = kind; e->tid = tid; return e; }
static void completion(UINT64 tid, const UINT8 *d, UINT32 len) { Event *e = event(1, tid); memcpy(e->data, d, len); e->len = len; }

/* Put RNDIS messages in the receive buffer and announce them in one transfer-page packet */
static void xfer(UINT64 tid, const UINT8 *const *msgs, const UINT32 *lens, UINT32 n)
{
    Event *e = event(2, tid);
    wr32(e->data, NVS_RNDIS);
    e->len = NVS_REQ;
    for (UINT32 i = 0; i < n; i++) {
        memcpy(ctx->rxbuf + rx_at, msgs[i], lens[i]);
        e->r[i].offset = rx_at;
        e->r[i].len = lens[i];
        rx_at = (rx_at + lens[i] + 63) & ~63u;
    }
    e->n = n;
}
static void xfer1(const UINT8 *m, UINT32 len) { xfer(9000 + nevents, &m, &len, 1); }

bool VmbusSend(VmbusChannel *ch, const void *data, UINT32 len, UINT64 tid, bool want_completion)
{
    (void)ch;
    const UINT8 *m = data;
    assert(len == NVS_REQ);
    if (silent) return true;
    UINT8 r[16] = { 0 };
    switch (rd32(m)) {
    case NVS_INIT:
        assert(want_completion && tid == TID_NVS && rd32(m + 4) == rd32(m + 8));
        wr32(r, NVS_INIT_RESP);
        wr32(r + 12, rd32(m + 4) == accepts ? NVS_STATUS_OK : 0);
        completion(tid, r, 16);
        break;
    case NVS_NDIS_CONF:
        assert(!want_completion && rd32(m + 4) == 1514 && m[12] == NVS_NDIS_CONF_VLAN);
        ndis_conf++;
        break;
    case NVS_NDIS_INIT:
        assert(!want_completion && rd32(m + 4) == 6);
        ndis_init++;
        ndis_minor = (int)rd32(m + 8);
        break;
    case NVS_RXBUF_CONN:
        assert(want_completion && rd32(m + 4) == GPADL && m[8] == 0xFE && m[9] == 0xCA);
        wr32(r, NVS_RXBUF_CONNRESP);
        wr32(r + 4, refuse_rxbuf ? 0 : NVS_STATUS_OK);
        completion(tid, r, 16);
        break;
    default:
        assert(!"unexpected NVSP message");
    }
    return true;
}

bool VmbusCreateGpadl(VmbusChannel *ch, void *buf, UINT32 len, UINT32 *gpadl)
{
    (void)ch;
    assert(buf == ctx->rxbuf && len == RXBUF_BYTES);
    *gpadl = GPADL;
    return true;
}

static void control(const UINT8 *m, UINT32 len)
{
    UINT8 r[64];
    memset(r, 0, sizeof(r));
    UINT32 type = rd32(m), rid = rd32(m + 8);
    assert(rd32(m + 4) == len);
    wr32(r, type | RNDIS_COMPLETION);
    wr32(r + 8, rid);
    switch (type) {
    case RNDIS_INIT:
        assert(len == 24 && rd32(m + 12) == 1 && rd32(m + 16) == 0);
        wr32(r + 4, 52);
        wr32(r + 16, 1);
        xfer1(r, 52);
        break;
    case RNDIS_QUERY: {
        assert(len == 28 && rd32(m + 20) == 20);
        UINT32 oid = rd32(m + 12);
        wr32(r + 16, oid == OID_802_3_PERMANENT_ADDRESS ? 6 : 4);
        wr32(r + 20, 16);
        if (oid == OID_802_3_PERMANENT_ADDRESS) memcpy(r + 24, host_mac, 6);
        else { assert(oid == OID_GEN_MEDIA_CONNECT_STATUS); wr32(r + 24, host_link ? 0 : 1); }
        wr32(r + 4, 32);
        xfer1(r, 32);
        break;
    }
    case RNDIS_SET:
        assert(len == 32 && rd32(m + 12) == OID_GEN_CURRENT_PACKET_FILTER && rd32(m + 16) == 4);
        assert(rd32(m + 28) == (FILTER_DIRECTED | FILTER_ALL_MULTICAST | FILTER_BROADCAST));
        filter_sets++;
        wr32(r + 4, 16);
        xfer1(r, 16);
        break;
    case RNDIS_KEEPALIVE | RNDIS_COMPLETION:
        assert(len == 16 && !rd32(m + 12));
        keepalives++;
        break;
    default:
        assert(!"unexpected RNDIS message");
    }
}

bool VmbusSendGpa(VmbusChannel *ch, const void *data, UINT32 len, UINT64 tid, const void *buf, UINT32 buf_len)
{
    (void)ch;
    const UINT8 *m = data, *b = buf;
    assert(len == NVS_REQ && rd32(m) == NVS_RNDIS && rd32(m + 8) == NVS_NO_CHIMNEY && buf_len <= PAGE);
    assert((UINT64)(uintptr_t)b % PAGE + buf_len <= PAGE);          /* in one page */
    UINT8 a[16] = { 0 };
    wr32(a, NVS_RNDIS_ACK);
    wr32(a + 4, NVS_STATUS_OK);
    if (rd32(m + 4) == NVS_RNDIS_CONTROL) {
        assert(tid == TID_CTRL || tid == TID_KEEPALIVE);
        if (!silent) control(b, buf_len);
        completion(tid, a, 16);
        return true;
    }
    assert(rd32(m + 4) == NVS_RNDIS_DATA && tid >= TID_TX && tid < TID_TX + TX_SLOTS);
    assert(rd32(b) == RNDIS_PACKET && rd32(b + 4) == buf_len && rd32(b + 8) == 36);
    UINT32 flen = rd32(b + 12);
    assert(flen + RNDIS_PACKET_HDR == buf_len && flen >= ETH_MIN);
    memcpy(frames[nframes], b + RNDIS_PACKET_HDR, flen);
    frame_len[nframes++] = flen;
    if (hold_tx) held[nheld++] = tid;
    else completion(tid, a, 16);
    return true;
}

bool VmbusSendCompletion(VmbusChannel *ch, const void *data, UINT32 len, UINT64 tid)
{
    (void)ch;
    assert(len == NVS_REQ && rd32(data) == NVS_RNDIS_ACK && rd32((const UINT8 *)data + 4) == NVS_STATUS_OK);
    acks++;
    last_ack = tid;
    return true;
}

void VmbusPollChannel(VmbusChannel *ch)
{
    (void)ch;
    for (int i = 0; i < nevents; i++) {           /* (an event may add more) */
        Event e = events[i];
        if (e.kind == 1) HvNetworkDriver.completion(ctx, e.data, e.len, e.tid);
        else HvNetworkDriver.xfer(ctx, e.r, e.n, e.data, e.len, e.tid);
    }
    nevents = 0;
}

/* ---- the checks ---- */
static void fresh(UINT32 version)
{
    g_nv = NULL;
    accepts = version;
    silent = hold_tx = refuse_rxbuf = false;
    nevents = acks = ndis_conf = ndis_init = filter_sets = nframes = nheld = 0;
    rx_at = 0;
    ctx = HvNetworkDriver.opened(NULL);
}

static void ring_checks(void)
{
    static UINT8 mem[4 * 4096];
    VmRing r;
    vm_ring_init(&r, mem, 4);
    UINT8 head[8 + 16] = { 0xFE, 0xCA, 0, 0, 2, 0, 0, 0, 100, 0, 0, 0, 0, 1, 0, 0, 50, 0, 0, 0, 0, 2, 0, 0 };
    UINT8 payload[32] = { 107 }, out[64], h[64];
    bool sig;
    assert(vm_ring_write_typed(&r, VM_PKT_XFER_PAGES, head, sizeof(head), payload, 32, 77, 0, &sig));
    VmPacketDesc d; UINT32 len, hlen;
    assert(vm_ring_read_head(&r, &d, h, sizeof(h), &hlen, out, sizeof(out), &len, &sig));
    assert(d.type == VM_PKT_XFER_PAGES && d.trans_id == 77 && hlen == sizeof(head) && !memcmp(h, head, hlen));
    assert(len == 32 && out[0] == 107);
    const VmbusXferRange *x = (const VmbusXferRange *)(h + 8);
    assert(rd32(h + 4) == 2 && x[0].len == 100 && x[0].offset == 256 && x[1].len == 50 && x[1].offset == 512);
    puts("transfer-page packet passed");
}

static void bringup_checks(void)
{
    fresh(0x40000);                                  /* version 5 refused, 4 taken */
    HvNetworkDriver.started(ctx);
    assert(g_nv == ctx && ctx->up && ctx->nvs_version == 0x40000);
    assert(ndis_conf == 1 && ndis_init == 1 && ndis_minor == 1 && filter_sets == 1);
    assert(acks == 4);                               /* each answer's piece given back */
    const NetAdapter *a = NetvscAdapter();
    assert(a && !strcmp(a->name(), "Hyper-V network adapter") && a->link_up());
    UINT8 mac[6];
    a->mac(mac);
    assert(!memcmp(mac, host_mac, 6));
    puts("bring-up passed");

    fresh(0x50000);                                  /* NVSP 5: NDIS 6.30 */
    HvNetworkDriver.started(ctx);
    assert(ctx->up && ctx->nvs_version == 0x50000 && ndis_minor == 30);
    fresh(0x00002);                                  /* NVSP 1: no NDIS_CONF */
    host_link = false;
    HvNetworkDriver.started(ctx);
    assert(ctx->up && ctx->nvs_version == 0x00002 && !ndis_conf && !NetvscAdapter()->link_up());
    host_link = true;
    puts("older hosts passed");
}

static void traffic_checks(void)
{
    fresh(0x50000);
    HvNetworkDriver.started(ctx);
    const NetAdapter *a = NetvscAdapter();

    UINT8 f[100];                                     /* sending: a short frame is padded */
    for (int i = 0; i < 100; i++) f[i] = (UINT8)i;
    assert(a->transmit(f, 42) && nframes == 1 && frame_len[0] == ETH_MIN && !memcmp(frames[0], f, 42));
    assert(!frames[0][42] && !frames[0][59]);
    assert(a->transmit(f, 100) && frame_len[1] == 100 && !memcmp(frames[1], f, 100));
    VmbusPollChannel(NULL);
    assert(!a->transmit(f, 0) && !a->transmit(f, 2000));

    hold_tx = true;                                   /* the host keeps every page: 32 in flight */
    nframes = 0;
    for (int i = 0; i < TX_SLOTS; i++) assert(a->transmit(f, 64));
    assert(!a->transmit(f, 64) && nframes == TX_SLOTS);
    UINT8 ackm[16] = { 0 };
    wr32(ackm, NVS_RNDIS_ACK);
    wr32(ackm + 4, NVS_STATUS_OK);
    completion(held[5], ackm, 16);                    /* one comes back */
    assert(a->transmit(f, 64) && nframes == TX_SLOTS + 1);
    hold_tx = false;
    for (int i = 0; i < nheld; i++) if (held[i] != held[5]) HvNetworkDriver.completion(ctx, ackm, 16, held[i]);
    puts("sending passed");

    UINT8 p1[RNDIS_PACKET_HDR + 80], p2[RNDIS_PACKET_HDR + 8 + 1500];   /* receiving: two frames in one packet */
    memset(p1, 0, sizeof(p1)); memset(p2, 0, sizeof(p2));
    wr32(p1, RNDIS_PACKET); wr32(p1 + 4, sizeof(p1)); wr32(p1 + 8, 36); wr32(p1 + 12, 80);
    for (int i = 0; i < 80; i++) p1[RNDIS_PACKET_HDR + i] = (UINT8)(0xA0 + i);
    wr32(p2, RNDIS_PACKET); wr32(p2 + 4, sizeof(p2)); wr32(p2 + 8, 36 + 8); wr32(p2 + 12, 1500);   /* (after a per-packet info) */
    p2[RNDIS_PACKET_HDR + 8] = 0x55;
    const UINT8 *ms[2] = { p1, p2 };
    UINT32 ls[2] = { sizeof(p1), sizeof(p2) };
    int before = acks;
    xfer(31337, ms, ls, 2);
    VmbusPollChannel(NULL);
    assert(acks == before + 1 && last_ack == 31337);
    UINT8 buf[FRAME_MAX];
    assert(a->receive(buf, sizeof(buf)) == 80 && buf[0] == 0xA0 && buf[79] == 0xA0 + 79);
    assert(a->receive(buf, sizeof(buf)) == 1500 && buf[0] == 0x55);
    assert(!a->receive(buf, sizeof(buf)));

    wr32(p1 + 12, 2000);                              /* a frame that overruns its message: dropped, still given back */
    xfer1(p1, sizeof(p1));
    Event *e = &events[nevents - 1];
    e->r[1].offset = RXBUF_BYTES - 4; e->r[1].len = 64; e->n = 2;   /* ... and a range outside the buffer */
    before = acks;
    VmbusPollChannel(NULL);
    assert(acks == before + 1 && !a->receive(buf, sizeof(buf)) && ctx->rx_dropped == 1);
    wr32(p1 + 12, 80);

    for (int i = 0; i < RXQ + 5; i++) { rx_at = 0; xfer1(p1, sizeof(p1)); VmbusPollChannel(NULL); }   /* a full queue */
    int got = 0;
    while (a->receive(buf, sizeof(buf))) got++;
    assert(got == RXQ && ctx->rx_dropped == 6);
    puts("receiving passed");

    UINT8 st[20] = { 0 };                             /* the link */
    wr32(st, RNDIS_INDICATE); wr32(st + 4, 20); wr32(st + 8, RNDIS_STATUS_MEDIA_DISCONNECT);
    xfer1(st, 20);
    VmbusPollChannel(NULL);
    assert(!a->link_up());
    wr32(st + 8, RNDIS_STATUS_MEDIA_CONNECT);
    xfer1(st, 20);
    VmbusPollChannel(NULL);
    assert(a->link_up());
    UINT8 ka[12] = { 0 };                             /* the host's keepalive, answered */
    wr32(ka, RNDIS_KEEPALIVE); wr32(ka + 4, 12); wr32(ka + 8, 99);
    keepalives = 0;
    xfer1(ka, 12);
    VmbusPollChannel(NULL);
    assert(keepalives == 1 && rd32(ctx->ctrl + PAGE + 8) == 99);
    VmbusPollChannel(NULL);
    assert(!ctx->keepalive_busy);
    puts("link and keepalive passed");
}

static void refusal_checks(void)
{
    fresh(0x12345);                                   /* no version in common */
    HvNetworkDriver.started(ctx);
    assert(!ctx->up && !NetvscAdapter());
    fresh(0x50000);                                   /* a host that never answers */
    silent = true;
    HvNetworkDriver.started(ctx);
    assert(!ctx->up && !NetvscAdapter());
    fresh(0x50000);                                   /* the receive buffer refused */
    refuse_rxbuf = true;
    HvNetworkDriver.started(ctx);
    assert(!ctx->up && !NetvscAdapter() && !filter_sets);
    puts("refusals passed");
}

int main(void)
{
    /* {f8615163-df3e-46c5-913f-f2d2f965ed0e} */
    const UINT8 *g = HvNetworkDriver.type;
    char s[40];
    snprintf(s, sizeof(s), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    assert(!strcmp(s, "f8615163-df3e-46c5-913f-f2d2f965ed0e"));
    ring_checks();
    bringup_checks();
    traffic_checks();
    refusal_checks();
    puts("hv_netvsc: all passed");
    return 0;
}
