/*
 * hv_util.c — Hyper-V's integration services: shutdown, time sync, heartbeat,
 * key-value exchange
 *
 * Each is a VMBus channel (vmbus.c) on which the host asks and the guest
 * answers.  The messages (as Linux's hv_utils and FreeBSD's hv_util speak
 * them) start with an 8-byte pipe header and a 20-byte IC header:
 *
 *    8  framework version (16-bit major, minor)
 *   12  message type: 0 negotiate, 1 heartbeat, 2 key-value exchange,
 *       3 shutdown, 4 time sync
 *   14  message version
 *   18  size of what follows the header
 *   20  status (0, or 0x80004005 for "failed")
 *   24  transaction, flags (1 transaction, 2 request, 4 response)
 *   28  the message's own data
 *
 * The guest answers every message by writing into the same buffer, setting
 * the flags to "transaction, response" and sending it back with the
 * packet's transaction ID.  The first message on each channel is NEGOTIATE:
 * the host lists the framework versions and the message versions it
 * speaks, and the guest answers with the one of each it picked.  Then:
 *
 *  - Heartbeat: a sequence number the guest answers incremented.  Hyper-V
 *    Manager shows "Heartbeat: OK" while the answers come.
 *  - Shutdown: the host's "Shut Down" (flags 0, 1) and, from version 3.1,
 *    "Restart" (4, 5).  The guest says yes, then shuts down or restarts as
 *    the Start menu does (DesktopPowerRequest: drive C: is saved first).
 *    Hibernate (8, 9) is refused.
 *  - Time sync: the host's clock (UTC, 100 ns since 1601), when it was read
 *    and flags: SYNC (at start and after a resume: the guest takes it) or
 *    SAMPLE (every few seconds: the guest corrects a clock that drifted).
 *    Version 4 adds the partition reference counter at the moment the host
 *    read its clock, so the time the message spent on the way is added.
 *    The time set is the NT system time, which Windows programs read.
 *  - Key-value exchange (KVP): the host reads and writes string pairs in
 *    five pools.  It enumerates the "auto" pool (2) by index to learn about
 *    the guest: its name, operating system and IP addresses, which Hyper-V
 *    Manager shows (Networking tab, Get-VMNetworkAdapter's IPAddresses).
 *    The other pools hold what the host sets (Add-VMKvp, WMI's
 *    Msvm_KvpExchangeComponent), kept in memory.  Linux and FreeBSD answer
 *    these in a user daemon; NovaOS answers in the kernel.  Each message
 *    has a 4-byte header (operation, pool) and then, for GET and SET, a
 *    value block (type, key size, value size, a 512-byte key and a
 *    2048-byte value, both UTF-16 with their NUL counted); ENUMERATE puts
 *    the index before the block, DELETE has only the key size and key.  An
 *    enumeration past the last item is answered 0x80070103 ("no more").
 */

#include "hv_util.h"
#include "../wm/desktop.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../mm/vmm.h"
#include "../net/net.h"
#include "../ke/version.h"

/* um_syscall.c: the NT system time (100 ns since 1601) from now on */
void   UmClockSet(UINT64 now_100ns);
UINT64 um_now_100ns(void);

#define IC_HDR             28
#define IC_NEGOTIATE       0
#define IC_HEARTBEAT       1
#define IC_KVP             2
#define IC_SHUTDOWN        3
#define IC_TIMESYNC        4
#define IC_FLAG_TRANSACTION 1
#define IC_FLAG_RESPONSE    4
#define IC_S_OK            0u
#define IC_E_FAIL          0x80004005u
#define IC_S_CONT          0x80070103u   /* KVP enumeration: no item at that index */

#define KVP_GET            0
#define KVP_SET            1
#define KVP_DELETE         2
#define KVP_ENUMERATE      3
#define KVP_POOLS          5
#define KVP_POOL_AUTO      2
#define KVP_KEY            512           /* bytes of UTF-16 */
#define KVP_VALUE          2048
#define KVP_BLOCK          (12 + KVP_KEY + KVP_VALUE)
#define KVP_ITEMS          16            /* the host's items NovaOS keeps */
#define REG_SZ             1

#define TS_SYNC            1
#define TS_SAMPLE          2
#define TS_STEP_100NS      10000000ull   /* a SAMPLE further off than 1 s moves the clock */

#define V(major, minor)    ((UINT32)(major) << 16 | (minor))

#define UTIL_BUF           4096          /* vmbus.c's PKT_MAX */

enum { UTIL_SHUTDOWN, UTIL_TIMESYNC, UTIL_HEARTBEAT, UTIL_KVP };

typedef struct {
    bool   used;
    UINT8  pool;
    UINT8  block[KVP_BLOCK];             /* as on the wire: type, sizes, key, value */
} KvpItem;

typedef struct {
    VmbusChannel *ch;
    int           kind;
    const char   *name;
    UINT32        msgver;                /* negotiated; 0 before */
    bool          synced;                /* time sync: the first SYNC was logged */
    KvpItem      *items;                 /* KVP: the host's items */
    UINT8         buf[UTIL_BUF];
} HvUtil;

static UINT32 rd32(const UINT8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (UINT32)p[3] << 24; }
static UINT16 rd16(const UINT8 *p) { return (UINT16)(p[0] | p[1] << 8); }
static UINT64 rd64(const UINT8 *p) { return rd32(p) | (UINT64)rd32(p + 4) << 32; }
static void   wr16(UINT8 *p, UINT16 v) { p[0] = (UINT8)v; p[1] = (UINT8)(v >> 8); }
static void   wr32(UINT8 *p, UINT32 v) { for (int i = 0; i < 4; i++) p[i] = (UINT8)(v >> 8 * i); }
static void   wr64(UINT8 *p, UINT64 v) { wr32(p, (UINT32)v); wr32(p + 4, (UINT32)(v >> 32)); }

/* The versions NovaOS speaks, best first (Linux's lists) */
static const UINT32 g_fw[] = { V(3, 0), V(1, 0) };
static const UINT32 g_sd[] = { V(3, 1), V(3, 0), V(1, 0) };
static const UINT32 g_ts[] = { V(4, 0), V(3, 0), V(1, 0) };
static const UINT32 g_hb[] = { V(3, 0), V(1, 0) };
static const UINT32 g_kvp[] = { V(4, 0), V(3, 0) };

/* The first of @ours (best first) in the host's list @theirs; 0 when none */
static UINT32 pick(const UINT32 *ours, int n, const UINT8 *theirs, UINT32 count)
{
    for (int i = 0; i < n; i++)
        for (UINT32 j = 0; j < count; j++)
            if (V(rd16(theirs + 4 * j), rd16(theirs + 4 * j + 2)) == ours[i]) return ours[i];
    return 0;
}

/* NEGOTIATE (@d: the whole message, @len bytes): the host's versions in,
 * the picked ones out.  False when NovaOS speaks none of them */
static bool negotiate(HvUtil *u, UINT8 *d, UINT32 len)
{
    if (len < IC_HDR + 8) return false;
    UINT32 nfw = rd16(d + IC_HDR), nmsg = rd16(d + IC_HDR + 2);
    UINT8 *ver = d + IC_HDR + 8;
    if (len < IC_HDR + 8 + 4 * (nfw + nmsg) || nfw + nmsg < 2) return false;
    const UINT32 *ours = u->kind == UTIL_SHUTDOWN ? g_sd : u->kind == UTIL_TIMESYNC ? g_ts :
                         u->kind == UTIL_KVP ? g_kvp : g_hb;
    int n = u->kind == UTIL_SHUTDOWN || u->kind == UTIL_TIMESYNC ? 3 : 2;
    UINT64 ref;
    if (u->kind == UTIL_TIMESYNC && !VmbusReferenceTime(&ref)) { ours++; n--; }  /* 4.0 needs the counter */
    UINT32 fw = pick(g_fw, 2, ver, nfw), msg = pick(ours, n, ver + 4 * nfw, nmsg);
    wr16(d + 18, 16);                    /* the counts, the reserved word, two versions */
    wr16(d + IC_HDR, fw && msg);
    wr16(d + IC_HDR + 2, fw && msg);
    wr16(ver, (UINT16)(fw >> 16));  wr16(ver + 2, (UINT16)fw);
    wr16(ver + 4, (UINT16)(msg >> 16)); wr16(ver + 6, (UINT16)msg);
    if (!fw || !msg) return false;
    u->msgver = msg;
    kprintf("[HVUTIL] %s: framework %u.%u, messages %u.%u\n", u->name, fw >> 16, fw & 0xFFFF,
            msg >> 16, msg & 0xFFFF);
    return true;
}

/* SHUTDOWN: what the host asks for (POWER_NONE: refused) */
static int shutdown_msg(HvUtil *u, UINT8 *d, UINT32 len)
{
    if (len < IC_HDR + 12) { wr32(d + 20, IC_E_FAIL); return POWER_NONE; }
    UINT32 flags = rd32(d + IC_HDR + 8);
    int what = (flags == 0 || flags == 1) ? POWER_SHUTDOWN :
               (flags == 4 || flags == 5) ? POWER_RESTART : POWER_NONE;
    wr32(d + 20, what == POWER_NONE ? IC_E_FAIL : IC_S_OK);
    kprintf("[HVUTIL] %s: the host asks to %s (flags %u)\n", u->name,
            what == POWER_SHUTDOWN ? "shut down" : what == POWER_RESTART ? "restart" : "hibernate: refused", flags);
    return what;
}

static void timesync_msg(HvUtil *u, const UINT8 *d, UINT32 len)
{
    bool v4 = u->msgver >= V(4, 0);
    if (len < IC_HDR + (v4 ? 17u : 25u)) return;
    UINT64 host = rd64(d + IC_HDR);
    UINT8 flags = d[IC_HDR + (v4 ? 16 : 24)];
    UINT64 ref, now_ref;
    if (v4 && (ref = rd64(d + IC_HDR + 8)) && VmbusReferenceTime(&now_ref) && now_ref > ref)
        host += now_ref - ref;           /* (the counter counts 100 ns too) */
    if (!(flags & (TS_SYNC | TS_SAMPLE))) return;
    INT64 off = (INT64)(host - um_now_100ns());
    UINT64 mag = off < 0 ? (UINT64)-off : (UINT64)off;
    if (!(flags & TS_SYNC) && mag < TS_STEP_100NS) return;
    UmClockSet(host);
    if (!u->synced || mag >= TS_STEP_100NS)
        kprintf("[HVUTIL] %s: clock set from the host's (moved %s%llu.%03llu s)\n", u->name,
                off < 0 ? "-" : "+", (unsigned long long)(mag / 10000000),
                (unsigned long long)(mag / 10000 % 1000));
    u->synced = true;
}

/* ---- key-value exchange ---- */
/* @s (ASCII) as UTF-16 with its NUL into @out (@cap bytes): the bytes written */
static UINT32 utf16(UINT8 *out, UINT32 cap, const char *s)
{
    UINT32 n = 0;
    while (s[n] && 2 * (n + 2) <= cap) { out[2 * n] = (UINT8)s[n]; out[2 * n + 1] = 0; n++; }
    out[2 * n] = out[2 * n + 1] = 0;
    return 2 * (n + 1);
}

/* The guest's own items (the auto pool), in the order Linux's daemon gives them */
static const char *const g_auto_keys[] = {
    "FullyQualifiedDomainName", "IntegrationServicesVersion", "NetworkAddressIPv4", "NetworkAddressIPv6",
    "OSBuildNumber", "OSName", "OSMajorVersion", "OSMinorVersion", "OSVersion", "ProcessorArchitecture",
};

/* The value of auto item @i into @v (KVP_VALUE bytes of text) */
static void auto_value(UINT32 i, char *v, int cap)
{
    const char *ver = NovaVersion();
    NetStatus st;
    int n = 0;
    v[0] = 0;
    switch (i) {
    case 0: ksnprintf(v, cap, "nova-pc"); break;           /* (the name DHCP gives: net.c) */
    case 1: case 4: case 8: ksnprintf(v, cap, "%s", ver); break;
    case 2:
        NetGetStatus(&st);
        if (st.configured && st.ip) NetFormatIp(st.ip, v, cap);
        break;
    case 3:
        NetGetStatus(&st);
        for (int k = 0; k < st.ip6_count && n + 48 < cap; k++) {
            NetIp a = { true, { 0 } };
            memcpy(a.a, st.ip6[k], 16);
            if (n) v[n++] = ';';
            NetFormatAddr(&a, v + n, cap - n);
            n += (int)strlen(v + n);
        }
        break;
    case 5: ksnprintf(v, cap, "NovaOS"); break;
    case 6: case 7: {                                       /* "0.1.0": 0, then 1 */
        const char *p = ver;
        if (i == 7) { while (*p && *p != '.') p++; if (*p) p++; }
        while (*p >= '0' && *p <= '9' && n < cap - 1) v[n++] = *p++;
        v[n] = 0;
        break;
    }
    case 9: ksnprintf(v, cap, "x86_64"); break;
    }
}

/* The host's item of @pool with key @key (@key_size bytes); NULL when none */
static KvpItem *kvp_find(HvUtil *u, UINT8 pool, const UINT8 *key, UINT32 key_size)
{
    for (int i = 0; i < KVP_ITEMS; i++) {
        KvpItem *it = &u->items[i];
        if (it->used && it->pool == pool && rd32(it->block + 4) == key_size && !memcmp(it->block + 12, key, key_size))
            return it;
    }
    return NULL;
}

/* A KVP message: its status (the answer is written in place) */
static UINT32 kvp_msg(HvUtil *u, UINT8 *d, UINT32 len)
{
    if (len < IC_HDR + 4 || !u->items) return IC_E_FAIL;
    UINT8 op = d[IC_HDR], pool = d[IC_HDR + 1];
    if (pool >= KVP_POOLS) return IC_E_FAIL;
    if (op == KVP_ENUMERATE) {
        if (len < IC_HDR + 8 + KVP_BLOCK) return IC_E_FAIL;
        UINT32 index = rd32(d + IC_HDR + 4);
        UINT8 *b = d + IC_HDR + 8;
        if (pool == KVP_POOL_AUTO) {
            if (index >= sizeof(g_auto_keys) / sizeof(g_auto_keys[0])) return IC_S_CONT;
            char v[KVP_VALUE / 2];
            auto_value(index, v, sizeof(v));
            memset(b, 0, KVP_BLOCK);
            wr32(b, REG_SZ);
            wr32(b + 4, utf16(b + 12, KVP_KEY, g_auto_keys[index]));
            wr32(b + 8, utf16(b + 12 + KVP_KEY, KVP_VALUE, v));
            return IC_S_OK;
        }
        for (int i = 0; i < KVP_ITEMS; i++)
            if (u->items[i].used && u->items[i].pool == pool && !index--) {
                memcpy(b, u->items[i].block, KVP_BLOCK);
                return IC_S_OK;
            }
        return IC_S_CONT;
    }
    if (op == KVP_DELETE) {
        if (len < IC_HDR + 8 + KVP_KEY) return IC_E_FAIL;
        UINT32 ks = rd32(d + IC_HDR + 4);
        KvpItem *it = ks <= KVP_KEY ? kvp_find(u, pool, d + IC_HDR + 8, ks) : NULL;
        if (!it) return IC_E_FAIL;
        it->used = false;
        return IC_S_OK;
    }
    if (op != KVP_GET && op != KVP_SET) return IC_E_FAIL;  /* (GET_IP_INFO, SET_IP_INFO: Hyper-V Replica's) */
    if (len < IC_HDR + 4 + KVP_BLOCK) return IC_E_FAIL;
    UINT8 *b = d + IC_HDR + 4;
    UINT32 ks = rd32(b + 4), vs = rd32(b + 8);
    if (!ks || ks > KVP_KEY || vs > KVP_VALUE) return IC_E_FAIL;
    KvpItem *it = kvp_find(u, pool, b + 12, ks);
    if (op == KVP_GET) {
        if (!it) return IC_E_FAIL;
        memcpy(b, it->block, KVP_BLOCK);
        return IC_S_OK;
    }
    if (pool == KVP_POOL_AUTO) return IC_E_FAIL;            /* (the guest's own) */
    for (int i = 0; !it && i < KVP_ITEMS; i++)
        if (!u->items[i].used) it = &u->items[i];
    if (!it) return IC_E_FAIL;
    it->used = true;
    it->pool = pool;
    memcpy(it->block, b, KVP_BLOCK);
    return IC_S_OK;
}

static void util_packet(void *ctx, const UINT8 *data, UINT32 len, UINT64 trans_id)
{
    HvUtil *u = ctx;
    if (!u || len < IC_HDR || len > UTIL_BUF) return;
    UINT8 *d = u->buf;
    memcpy(d, data, len);
    UINT16 type = rd16(d + 12);
    int power = POWER_NONE;
    if (type == IC_NEGOTIATE) {
        if (!negotiate(u, d, len)) kprintf("[HVUTIL] %s: no version in common with the host\n", u->name);
    } else if (!u->msgver) {
        return;                          /* (nothing before NEGOTIATE) */
    } else if (type == IC_HEARTBEAT && u->kind == UTIL_HEARTBEAT && len >= IC_HDR + 8) {
        wr64(d + IC_HDR, rd64(d + IC_HDR) + 1);
    } else if (type == IC_SHUTDOWN && u->kind == UTIL_SHUTDOWN) {
        power = shutdown_msg(u, d, len);
    } else if (type == IC_TIMESYNC && u->kind == UTIL_TIMESYNC) {
        timesync_msg(u, d, len);
    } else if (type == IC_KVP && u->kind == UTIL_KVP) {
        wr32(d + 20, kvp_msg(u, d, len));
    } else {
        wr32(d + 20, IC_E_FAIL);
    }
    d[25] = IC_FLAG_TRANSACTION | IC_FLAG_RESPONSE;
    if (!VmbusSend(u->ch, d, len, trans_id, false))
        kprintf("[HVUTIL] %s: cannot answer the host (ring full)\n", u->name);
    if (power != POWER_NONE) DesktopPowerRequest(power);   /* (answered first, as Linux does) */
}

static void *util_open(VmbusChannel *ch, int kind, const char *name)
{
    HvUtil *u = kzalloc(sizeof(HvUtil));
    if (!u) return NULL;
    u->ch = ch;
    u->kind = kind;
    u->name = name;
    return u;
}

static void *shutdown_opened(VmbusChannel *ch)  { return util_open(ch, UTIL_SHUTDOWN, "shutdown"); }
static void *timesync_opened(VmbusChannel *ch)  { return util_open(ch, UTIL_TIMESYNC, "time sync"); }
static void *heartbeat_opened(VmbusChannel *ch) { return util_open(ch, UTIL_HEARTBEAT, "heartbeat"); }
static void *kvp_opened(VmbusChannel *ch)
{
    HvUtil *u = util_open(ch, UTIL_KVP, "key-value exchange");
    if (u && !(u->items = kzalloc(KVP_ITEMS * sizeof(KvpItem)))) u = NULL;
    return u;
}

/* {0e0b6031-5213-4934-818b-38d90ced39db} */
const VmbusDriver HvShutdownDriver = {
    { 0x31, 0x60, 0x0b, 0x0e, 0x13, 0x52, 0x34, 0x49, 0x81, 0x8b, 0x38, 0xd9, 0x0c, 0xed, 0x39, 0xdb },
    "Hyper-V shutdown", shutdown_opened, util_packet, NULL, NULL, NULL, NULL, NULL,
};
/* {9527e630-d0ae-497b-adce-e80ab0175caf} */
const VmbusDriver HvTimeSyncDriver = {
    { 0x30, 0xe6, 0x27, 0x95, 0xae, 0xd0, 0x7b, 0x49, 0xad, 0xce, 0xe8, 0x0a, 0xb0, 0x17, 0x5c, 0xaf },
    "Hyper-V time sync", timesync_opened, util_packet, NULL, NULL, NULL, NULL, NULL,
};
/* {57164f39-9115-4e78-ab55-382f3bd5422d} */
const VmbusDriver HvHeartbeatDriver = {
    { 0x39, 0x4f, 0x16, 0x57, 0x15, 0x91, 0x78, 0x4e, 0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d },
    "Hyper-V heartbeat", heartbeat_opened, util_packet, NULL, NULL, NULL, NULL, NULL,
};
/* {a9a0f4e7-5a45-4d96-b827-8a841e8c03e6} */
const VmbusDriver HvKvpDriver = {
    { 0xe7, 0xf4, 0xa0, 0xa9, 0x45, 0x5a, 0x96, 0x4d, 0xb8, 0x27, 0x8a, 0x84, 0x1e, 0x8c, 0x03, 0xe6 },
    "Hyper-V key-value exchange", kvp_opened, util_packet, NULL, NULL, NULL, NULL, NULL,
};
