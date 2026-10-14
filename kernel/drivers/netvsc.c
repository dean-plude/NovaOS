/*
 * netvsc.c — Hyper-V's synthetic network adapter
 *
 * A Hyper-V VM's network adapter is a VMBus channel speaking two
 * protocols, one inside the other (as FreeBSD's BSD-licensed hn(4) and
 * Linux's netvsc speak them):
 *
 *   NVSP, the channel's own messages:  INIT (a version: 5, 4, 2 or 1),
 *     NDIS_CONF (the MTU), NDIS_INIT (the NDIS version), RXBUF_CONN (the
 *     receive buffer, a GPADL), RNDIS (an RNDIS message rides with it) and
 *     RNDIS_ACK (the host is done with one)
 *   RNDIS, the USB network adapters' protocol:  INITIALIZE, QUERY (the
 *     MAC address, the link), SET (the packet filter), PACKET (a frame),
 *     INDICATE_STATUS (the link came or went), KEEPALIVE
 *
 * What the guest sends rides in GPA-direct packets: a page holding the
 * RNDIS message (and the frame) is listed in the packet, and the host
 * answers with an RNDIS_ACK completion once it has read it.  What the host
 * sends lands in the receive buffer: a transfer-page packet lists the
 * pieces of the buffer holding RNDIS messages (frames, answers, status),
 * and the guest gives them back with an RNDIS_ACK completion.
 *
 * The adapter is NetAdapter net.c runs (no send buffer, no offloads, one
 * channel).  Frames come in on the device poll thread and wait in a
 * queue for the net thread.
 */

#include "netvsc.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../mm/vmm.h"
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/apic.h"

/* NVSP */
#define NVS_INIT               1
#define NVS_INIT_RESP          2
#define NVS_NDIS_INIT          100
#define NVS_RXBUF_CONN         101
#define NVS_RXBUF_CONNRESP     102
#define NVS_RNDIS              107
#define NVS_RNDIS_ACK          108
#define NVS_NDIS_CONF          125
#define NVS_STATUS_OK          1
#define NVS_REQ                32        /* every request, padded */
#define NVS_RXBUF_SIG          0xCAFE
#define NVS_NDIS_CONF_VLAN     0x8
#define NVS_RNDIS_DATA         0
#define NVS_RNDIS_CONTROL      1
#define NVS_NO_CHIMNEY         0xFFFFFFFFu

/* RNDIS */
#define RNDIS_PACKET           0x00000001
#define RNDIS_INIT             0x00000002
#define RNDIS_QUERY            0x00000004
#define RNDIS_SET              0x00000005
#define RNDIS_INDICATE         0x00000007
#define RNDIS_KEEPALIVE        0x00000008
#define RNDIS_COMPLETION       0x80000000
#define RNDIS_PACKET_HDR       44
#define RNDIS_STATUS_MEDIA_CONNECT    0x4001000Bu
#define RNDIS_STATUS_MEDIA_DISCONNECT 0x4001000Cu
#define RNDIS_STATUS_NETWORK_CHANGE   0x40010018u
#define OID_GEN_MEDIA_CONNECT_STATUS  0x00010114
#define OID_GEN_CURRENT_PACKET_FILTER 0x0001010E
#define OID_802_3_PERMANENT_ADDRESS   0x01010101
#define FILTER_DIRECTED        0x1
#define FILTER_ALL_MULTICAST   0x4
#define FILTER_BROADCAST       0x8

#define REPLY_MS               5000
#define RXBUF_BYTES            (1u << 20)
#define TX_SLOTS               32        /* a page each */
#define RXQ                    128
#define FRAME_MAX              1536
#define ETH_MIN                60
#define PAGE                   4096u

/* Transaction IDs of what the guest sends */
#define TID_NVS                1
#define TID_CTRL               2
#define TID_KEEPALIVE          3
#define TID_TX                 0x100     /* + the slot */

typedef struct {
    VmbusChannel *ch;
    volatile int  sending;               /* one writer on the outbound ring */
    UINT32        nvs_version;
    UINT8        *rxbuf, *tx, *ctrl;     /* ctrl: requests, then the keepalive answer */
    volatile UINT8 tx_busy[TX_SLOTS];
    volatile bool ctrl_busy, keepalive_busy;
    volatile bool nvs_got;
    UINT8         nvs_reply[64];
    volatile UINT32 ctrl_wait;           /* the request ID an answer is waited for */
    volatile bool ctrl_got;
    UINT8         ctrl_reply[256];
    UINT32        next_rid;
    UINT8         mac[6];
    volatile bool link, up;
    volatile UINT32 rx_head, rx_tail;    /* the device poll thread adds, the net thread takes */
    UINT16        rx_len[RXQ];
    UINT8       (*rx_frame)[FRAME_MAX];
    UINT64        rx_dropped;
} Netvsc;

static Netvsc *g_nv;

static inline UINT32 rd32(const UINT8 *p) { UINT32 v; memcpy(&v, p, 4); return v; }
static inline void   wr32(UINT8 *p, UINT32 v) { memcpy(p, &v, 4); }

static void take(volatile int *f)
{
    while (__atomic_exchange_n(f, 1, __ATOMIC_ACQUIRE)) {
        if (interrupts_enabled()) sched_yield();
        else pause_cpu();
    }
}
static void drop(volatile int *f) { __atomic_store_n(f, 0, __ATOMIC_RELEASE); }

static bool send_inband(Netvsc *n, const UINT8 *m, UINT64 tid, bool completion)
{
    take(&n->sending);
    bool ok = VmbusSend(n->ch, m, NVS_REQ, tid, completion);
    drop(&n->sending);
    return ok;
}

/* An RNDIS message of @len bytes at @buf (a page of n->tx or n->ctrl) */
static bool send_rndis(Netvsc *n, UINT32 kind, const void *buf, UINT32 len, UINT64 tid)
{
    UINT8 m[NVS_REQ];
    memset(m, 0, sizeof(m));
    wr32(m, NVS_RNDIS);
    wr32(m + 4, kind);
    wr32(m + 8, NVS_NO_CHIMNEY);
    take(&n->sending);
    bool ok = VmbusSendGpa(n->ch, m, NVS_REQ, tid, buf, len);
    drop(&n->sending);
    return ok;
}

/* Give a transfer-page packet's pieces back */
static void ack(Netvsc *n, UINT64 tid)
{
    UINT8 m[NVS_REQ];
    memset(m, 0, sizeof(m));
    wr32(m, NVS_RNDIS_ACK);
    wr32(m + 4, NVS_STATUS_OK);
    for (int tries = 0; tries < 1000; tries++) {
        take(&n->sending);
        bool ok = VmbusSendCompletion(n->ch, m, NVS_REQ, tid);
        drop(&n->sending);
        if (ok) return;
        udelay(20);
    }
    kprintf("[NETVSC] The outbound ring stayed full: a receive was not given back\n");
}

/* Wait (polling the channel) until @flag is set */
static bool wait_for(Netvsc *n, volatile bool *flag)
{
    for (UINT32 t = 0; !__atomic_load_n(flag, __ATOMIC_ACQUIRE); t++) {
        VmbusPollChannel(n->ch);
        if (__atomic_load_n(flag, __ATOMIC_ACQUIRE)) break;
        if (t >= REPLY_MS * 100) return false;
        udelay(10);
    }
    return true;
}

/* An NVSP request the host answers in a completion packet (in n->nvs_reply) */
static bool nvs_transact(Netvsc *n, const UINT8 *req, UINT32 want)
{
    n->nvs_got = false;
    if (!send_inband(n, req, TID_NVS, true) || !wait_for(n, &n->nvs_got)) return false;
    return rd32(n->nvs_reply) == want;
}

/* Send the RNDIS request in n->ctrl (@len bytes; its request ID is set
 * here) and wait for the answer of type @want with status 0 (in
 * n->ctrl_reply) */
static bool rndis_request(Netvsc *n, UINT32 len, UINT32 want)
{
    for (UINT32 t = 0; __atomic_load_n(&n->ctrl_busy, __ATOMIC_ACQUIRE); t++) {   /* the host still reads the last one */
        VmbusPollChannel(n->ch);
        if (t >= REPLY_MS * 100) return false;
        udelay(10);
    }
    UINT32 rid = ++n->next_rid;
    wr32(n->ctrl + 8, rid);
    n->ctrl_got = false;
    __atomic_store_n(&n->ctrl_wait, rid, __ATOMIC_RELEASE);
    n->ctrl_busy = true;
    bool ok = send_rndis(n, NVS_RNDIS_CONTROL, n->ctrl, len, TID_CTRL);
    if (!ok) n->ctrl_busy = false;
    ok = ok && wait_for(n, &n->ctrl_got);
    __atomic_store_n(&n->ctrl_wait, 0, __ATOMIC_RELEASE);
    return ok && rd32(n->ctrl_reply) == want && rd32(n->ctrl_reply + 12) == 0;
}

/* QUERY @oid: the answer's data and its length (0: none) */
static UINT32 rndis_query(Netvsc *n, UINT32 oid, const UINT8 **data)
{
    memset(n->ctrl, 0, 28);
    wr32(n->ctrl, RNDIS_QUERY);
    wr32(n->ctrl + 4, 28);
    wr32(n->ctrl + 12, oid);
    wr32(n->ctrl + 20, 20);               /* (where data would go, from the request ID) */
    if (!rndis_request(n, 28, RNDIS_QUERY | RNDIS_COMPLETION)) return 0;
    UINT32 len = rd32(n->ctrl_reply + 16), off = rd32(n->ctrl_reply + 20);
    if (8 + off + len > sizeof(n->ctrl_reply) || 8 + off + len > rd32(n->ctrl_reply + 4)) return 0;
    *data = n->ctrl_reply + 8 + off;
    return len;
}

static bool rndis_set32(Netvsc *n, UINT32 oid, UINT32 value)
{
    memset(n->ctrl, 0, 32);
    wr32(n->ctrl, RNDIS_SET);
    wr32(n->ctrl + 4, 32);
    wr32(n->ctrl + 12, oid);
    wr32(n->ctrl + 16, 4);
    wr32(n->ctrl + 20, 20);
    wr32(n->ctrl + 28, value);
    return rndis_request(n, 32, RNDIS_SET | RNDIS_COMPLETION);
}

static void set_link(Netvsc *n, bool up)
{
    if (up != n->link) kprintf("[NETVSC] Link %s\n", up ? "up" : "down");
    __atomic_store_n(&n->link, up, __ATOMIC_RELEASE);
}

/* One RNDIS message from the host, @len bytes of the receive buffer */
static void rndis_in(Netvsc *n, const UINT8 *m, UINT32 len)
{
    if (len < 12) return;
    UINT32 type = rd32(m), mlen = rd32(m + 4);
    if (mlen > len || mlen < 12) return;
    switch (type) {
    case RNDIS_PACKET: {
        if (mlen < RNDIS_PACKET_HDR) return;
        UINT32 off = rd32(m + 8), flen = rd32(m + 12);
        if (8 + (UINT64)off + flen > mlen || flen < 14 || flen > FRAME_MAX) { n->rx_dropped++; return; }
        UINT32 head = n->rx_head;
        if (head - __atomic_load_n(&n->rx_tail, __ATOMIC_ACQUIRE) >= RXQ) { n->rx_dropped++; return; }
        memcpy(n->rx_frame[head % RXQ], m + 8 + off, flen);
        n->rx_len[head % RXQ] = (UINT16)flen;
        __atomic_store_n(&n->rx_head, head + 1, __ATOMIC_RELEASE);
        return;
    }
    case RNDIS_INDICATE: {
        UINT32 st = rd32(m + 8);
        if (st == RNDIS_STATUS_MEDIA_CONNECT || st == RNDIS_STATUS_NETWORK_CHANGE) set_link(n, true);
        else if (st == RNDIS_STATUS_MEDIA_DISCONNECT) set_link(n, false);
        return;
    }
    case RNDIS_KEEPALIVE: {                     /* answered from the second control page */
        if (n->keepalive_busy) return;
        UINT8 *a = n->ctrl + PAGE;
        wr32(a, RNDIS_KEEPALIVE | RNDIS_COMPLETION);
        wr32(a + 4, 16);
        wr32(a + 8, rd32(m + 8));
        wr32(a + 12, 0);
        n->keepalive_busy = true;
        if (!send_rndis(n, NVS_RNDIS_CONTROL, a, 16, TID_KEEPALIVE)) n->keepalive_busy = false;
        return;
    }
    }
    if ((type & RNDIS_COMPLETION) && rd32(m + 8) == __atomic_load_n(&n->ctrl_wait, __ATOMIC_ACQUIRE) &&
        !n->ctrl_got) {
        memset(n->ctrl_reply, 0, sizeof(n->ctrl_reply));
        memcpy(n->ctrl_reply, m, mlen < sizeof(n->ctrl_reply) ? mlen : sizeof(n->ctrl_reply));
        __atomic_store_n(&n->ctrl_got, true, __ATOMIC_RELEASE);
    }
}

static void nv_xfer(void *ctx, const VmbusXferRange *r, UINT32 count, const UINT8 *data, UINT32 len, UINT64 tid)
{
    Netvsc *n = ctx;
    if (len >= 4 && rd32(data) == NVS_RNDIS)
        for (UINT32 i = 0; i < count; i++)
            if ((UINT64)r[i].offset + r[i].len <= RXBUF_BYTES) rndis_in(n, n->rxbuf + r[i].offset, r[i].len);
    ack(n, tid);
}

static void nv_completion(void *ctx, const UINT8 *d, UINT32 len, UINT64 tid)
{
    Netvsc *n = ctx;
    if (tid == TID_NVS) {
        memset(n->nvs_reply, 0, sizeof(n->nvs_reply));
        memcpy(n->nvs_reply, d, len < sizeof(n->nvs_reply) ? len : sizeof(n->nvs_reply));
        __atomic_store_n(&n->nvs_got, true, __ATOMIC_RELEASE);
    } else if (tid == TID_CTRL) {
        __atomic_store_n(&n->ctrl_busy, false, __ATOMIC_RELEASE);
    } else if (tid == TID_KEEPALIVE) {
        __atomic_store_n(&n->keepalive_busy, false, __ATOMIC_RELEASE);
    } else if (tid >= TID_TX && tid < TID_TX + TX_SLOTS) {
        __atomic_store_n(&n->tx_busy[tid - TID_TX], 0, __ATOMIC_RELEASE);
    }
}

static void nv_packet(void *ctx, const UINT8 *d, UINT32 len, UINT64 tid)
{
    (void)ctx; (void)d; (void)len; (void)tid;    /* (the host's notes: the send table, a VF) */
}

/* ---- the NetAdapter ---- */
static const char *nv_name(void) { return "Hyper-V network adapter"; }
static void nv_mac(UINT8 mac[6]) { memcpy(mac, g_nv->mac, 6); }
static bool nv_link_up(void) { return __atomic_load_n(&g_nv->link, __ATOMIC_ACQUIRE); }

static int free_slot(Netvsc *n)
{
    for (int i = 0; i < TX_SLOTS; i++) {
        UINT8 zero = 0;
        if (__atomic_compare_exchange_n(&n->tx_busy[i], &zero, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return i;
    }
    return -1;
}

static bool nv_transmit(const void *frame, UINT16 len)
{
    Netvsc *n = g_nv;
    if (!len || len > FRAME_MAX - RNDIS_PACKET_HDR) return false;
    int s = free_slot(n);
    if (s < 0) {                                 /* the host may have answered already */
        VmbusPollChannel(n->ch);
        if ((s = free_slot(n)) < 0) return false;
    }
    UINT32 flen = len < ETH_MIN ? ETH_MIN : len;
    UINT8 *p = n->tx + (UINT32)s * PAGE;
    memset(p, 0, RNDIS_PACKET_HDR);
    wr32(p, RNDIS_PACKET);
    wr32(p + 4, RNDIS_PACKET_HDR + flen);
    wr32(p + 8, RNDIS_PACKET_HDR - 8);
    wr32(p + 12, flen);
    memcpy(p + RNDIS_PACKET_HDR, frame, len);
    memset(p + RNDIS_PACKET_HDR + len, 0, flen - len);
    if (!send_rndis(n, NVS_RNDIS_DATA, p, RNDIS_PACKET_HDR + flen, TID_TX + (UINT32)s)) {
        __atomic_store_n(&n->tx_busy[s], 0, __ATOMIC_RELEASE);
        return false;
    }
    return true;
}

static int nv_receive(void *buf, int cap)
{
    Netvsc *n = g_nv;
    UINT32 tail = n->rx_tail;
    if (tail == __atomic_load_n(&n->rx_head, __ATOMIC_ACQUIRE)) return 0;
    int len = n->rx_len[tail % RXQ];
    if (len > cap) len = cap;
    memcpy(buf, n->rx_frame[tail % RXQ], (size_t)len);
    __atomic_store_n(&n->rx_tail, tail + 1, __ATOMIC_RELEASE);
    return len;
}

static const NetAdapter g_adapter = { nv_name, nv_mac, nv_link_up, nv_transmit, nv_receive };

const NetAdapter *NetvscAdapter(void) { return g_nv ? &g_adapter : NULL; }

/* ---- bring-up ---- */
static void *nv_opened(VmbusChannel *ch)
{
    Netvsc *n = kzalloc(sizeof(Netvsc));
    if (!n) return NULL;
    n->ch = ch;
    return n;
}

static void nv_started(void *ctx)
{
    static const UINT32 versions[] = { 0x50000, 0x40000, 0x30002, 0x00002 };
    Netvsc *n = ctx;
    if (g_nv) return;                            /* (one adapter) */
    n->rxbuf = kernel_alloc_pages(RXBUF_BYTES / PAGE);
    n->tx = kernel_alloc_pages(TX_SLOTS);
    n->ctrl = kernel_alloc_pages(2);
    n->rx_frame = kzalloc(RXQ * FRAME_MAX);
    if (!n->rxbuf || !n->tx || !n->ctrl || !n->rx_frame) {
        kprintf("[NETVSC] Out of memory\n");
        return;
    }
    memset(n->rxbuf, 0, RXBUF_BYTES);

    UINT8 req[NVS_REQ];
    for (UINT32 i = 0; i < sizeof(versions) / sizeof(versions[0]) && !n->nvs_version; i++) {
        memset(req, 0, sizeof(req));
        wr32(req, NVS_INIT);
        wr32(req + 4, versions[i]);
        wr32(req + 8, versions[i]);
        if (!nvs_transact(n, req, NVS_INIT_RESP)) {
            kprintf("[NETVSC] No answer to INIT\n");
            return;
        }
        if (rd32(n->nvs_reply + 12) == NVS_STATUS_OK) n->nvs_version = versions[i];
    }
    if (!n->nvs_version) {
        kprintf("[NETVSC] The host took no NVSP version NovaOS speaks (5, 4, 2, 1)\n");
        return;
    }

    if (n->nvs_version >= 0x30002) {             /* the MTU (with the Ethernet header) */
        memset(req, 0, sizeof(req));
        wr32(req, NVS_NDIS_CONF);
        wr32(req + 4, 1514);
        req[12] = NVS_NDIS_CONF_VLAN;
        send_inband(n, req, 0, false);
    }
    memset(req, 0, sizeof(req));                 /* NDIS 6.30 from NVSP 5, else 6.1 */
    wr32(req, NVS_NDIS_INIT);
    wr32(req + 4, 6);
    wr32(req + 8, n->nvs_version >= 0x50000 ? 30 : 1);
    send_inband(n, req, 0, false);

    UINT32 gpadl;
    if (!VmbusCreateGpadl(n->ch, n->rxbuf, RXBUF_BYTES, &gpadl)) return;
    memset(req, 0, sizeof(req));
    wr32(req, NVS_RXBUF_CONN);
    wr32(req + 4, gpadl);
    req[8] = NVS_RXBUF_SIG & 0xFF;
    req[9] = NVS_RXBUF_SIG >> 8;
    if (!nvs_transact(n, req, NVS_RXBUF_CONNRESP) || rd32(n->nvs_reply + 4) != NVS_STATUS_OK) {
        kprintf("[NETVSC] The host did not take the receive buffer\n");
        return;
    }

    memset(n->ctrl, 0, 24);                      /* RNDIS 1.0, messages up to 16 KiB */
    wr32(n->ctrl, RNDIS_INIT);
    wr32(n->ctrl + 4, 24);
    wr32(n->ctrl + 12, 1);
    wr32(n->ctrl + 20, 0x4000);
    if (!rndis_request(n, 24, RNDIS_INIT | RNDIS_COMPLETION)) {
        kprintf("[NETVSC] RNDIS did not start\n");
        return;
    }
    const UINT8 *d;
    if (rndis_query(n, OID_802_3_PERMANENT_ADDRESS, &d) < 6) {
        kprintf("[NETVSC] No MAC address\n");
        return;
    }
    memcpy(n->mac, d, 6);
    UINT32 dl = rndis_query(n, OID_GEN_MEDIA_CONNECT_STATUS, &d);
    n->link = dl >= 4 && rd32(d) == 0;           /* (0: connected) */
    if (!rndis_set32(n, OID_GEN_CURRENT_PACKET_FILTER, FILTER_DIRECTED | FILTER_ALL_MULTICAST | FILTER_BROADCAST)) {
        kprintf("[NETVSC] The packet filter was refused\n");
        return;
    }
    n->up = true;
    g_nv = n;
    UINT32 v = n->nvs_version == 0x00002 ? 1 : n->nvs_version == 0x30002 ? 2 : n->nvs_version >> 16;
    kprintf("[NETVSC] NVSP %u, MAC %02x:%02x:%02x:%02x:%02x:%02x, link %s\n", v, n->mac[0], n->mac[1], n->mac[2], n->mac[3],
            n->mac[4], n->mac[5], n->link ? "up" : "down");
}

/* {f8615163-df3e-46c5-913f-f2d2f965ed0e} */
const VmbusDriver HvNetworkDriver = {
    { 0x63, 0x51, 0x61, 0xf8, 0x3e, 0xdf, 0xc5, 0x46, 0x91, 0x3f, 0xf2, 0xd2, 0xf9, 0x65, 0xed, 0x0e },
    "Hyper-V network", nv_opened, nv_packet, nv_completion, nv_started, NULL, nv_xfer,
};
