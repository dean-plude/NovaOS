/* Hyper-V's integration services (shutdown, time sync, heartbeat, key-value
 * exchange) on the
 * host (tests/tools/test_hv_util.py): the host's messages are fed to the
 * drivers, and their answers come back through a real VMBus ring */
#include "../../kernel/drivers/vmbus_ring.h"
#include "../../kernel/drivers/hv_util.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static VmRing wire;                         /* what the drivers sent */
static UINT64 now = 133000000000000000ull;  /* the NT system time */
static int    clock_sets, power_req = -1;
static bool   have_ref;
static UINT64 ref_now;

bool VmbusSend(VmbusChannel *ch, const void *data, UINT32 len, UINT64 trans_id, bool want_completion)
{
    (void)ch;
    return vm_ring_write(&wire, data, len, trans_id, want_completion ? VM_PKT_FLAG_COMPLETION_REQUESTED : 0, NULL);
}
bool VmbusReferenceTime(UINT64 *t) { if (have_ref) *t = ref_now; return have_ref; }
bool DesktopPowerRequest(int what) { power_req = what; return true; }
void UmClockSet(UINT64 t) { now = t; clock_sets++; }
UINT64 um_now_100ns(void) { return now; }
void kprintf(const char *f, ...) { va_list v; va_start(v, f); vprintf(f, v); va_end(v); }
void *kzalloc(size_t n) { return calloc(1, n); }
int ksnprintf(char *b, size_t n, const char *f, ...) { va_list v; va_start(v, f); int r = vsnprintf(b, n, f, v); va_end(v); return r; }
const char *NovaVersion(void) { return "0.1.0"; }
static bool net_ip;                         /* an IPv4 address from DHCP */
void NetGetStatus(NetStatus *st)
{
    memset(st, 0, sizeof(*st));
    st->configured = net_ip;
    st->ip = net_ip ? 0x0F02000A : 0;       /* 10.0.2.15 */
    st->ip6_count = 2;
    st->ip6[0][0] = 0xfe; st->ip6[0][1] = 0x80; st->ip6[0][15] = 1;
    st->ip6[1][0] = 0xfd; st->ip6[1][15] = 0x15;
}
void NetFormatIp(UINT32 ip, char *b, int cap)
{
    snprintf(b, cap, "%u.%u.%u.%u", ip & 0xFF, ip >> 8 & 0xFF, ip >> 16 & 0xFF, ip >> 24);
}
void NetFormatAddr(const NetIp *ip, char *b, int cap)
{
    snprintf(b, cap, "%x%02x::%x", ip->a[0], ip->a[1], ip->a[15]);  /* (enough to tell them apart) */
}

/* The ring pads a packet to 8 bytes: the length the driver reads, and so answers */
#define P8(n) (((n) + 7) & ~7u)

/* The driver's answer: its payload in @out, its length; 0 when none */
static UINT32 answer(UINT8 *out, UINT64 *trans)
{
    VmPacketDesc d; UINT32 len; bool sig;
    if (!vm_ring_read(&wire, &d, out, UTIL_BUF, &len, &sig)) return 0;
    assert(d.type == VM_PKT_DATA_INBAND && !(d.flags & VM_PKT_FLAG_COMPLETION_REQUESTED));
    *trans = d.trans_id;
    return len;
}

/* A host message of @type with @dsize bytes of data (zeroed) */
static UINT32 msg(UINT8 *b, UINT16 type, UINT32 dsize)
{
    memset(b, 0, IC_HDR + dsize);
    wr32(b, 0); wr32(b + 4, IC_HDR - 8 + dsize);
    wr16(b + 8, 3); wr16(b + 10, 0);
    wr16(b + 12, type);
    wr16(b + 18, (UINT16)dsize);
    b[25] = 1 | 2;                          /* transaction, request */
    return IC_HDR + dsize;
}

/* NEGOTIATE listing @fw framework and @mv message versions */
static UINT32 nego(UINT8 *b, const UINT32 *fw, int nfw, const UINT32 *mv, int nmv)
{
    UINT32 n = msg(b, IC_NEGOTIATE, 8 + 4 * (nfw + nmv));
    wr16(b + IC_HDR, (UINT16)nfw); wr16(b + IC_HDR + 2, (UINT16)nmv);
    UINT8 *v = b + IC_HDR + 8;
    for (int i = 0; i < nfw + nmv; i++) {
        UINT32 x = i < nfw ? fw[i] : mv[i - nfw];
        wr16(v + 4 * i, (UINT16)(x >> 16)); wr16(v + 4 * i + 2, (UINT16)x);
    }
    return n;
}

/* Feed @b to @drv's channel; the answer's two picked versions (0 when the
 * counts say none) */
static void negotiated(const VmbusDriver *drv, void *ctx, UINT8 *b, UINT32 n, UINT32 *fw, UINT32 *mv)
{
    static UINT8 a[UTIL_BUF]; UINT64 t;
    drv->packet(ctx, b, n, 77);
    UINT32 len = answer(a, &t);
    assert(len == P8(n) && t == 77 && a[25] == (IC_FLAG_TRANSACTION | IC_FLAG_RESPONSE) && rd16(a + 18) == 16);
    bool ok = rd16(a + IC_HDR) == 1 && rd16(a + IC_HDR + 2) == 1;
    assert(ok || (!rd16(a + IC_HDR) && !rd16(a + IC_HDR + 2)));
    UINT8 *v = a + IC_HDR + 8;
    *fw = ok ? V(rd16(v), rd16(v + 2)) : 0;
    *mv = ok ? V(rd16(v + 4), rd16(v + 6)) : 0;
}

static void guid_checks(void)
{
    static const struct { const VmbusDriver *d; const char *g; } k[] = {
        { &HvShutdownDriver,  "0e0b6031-5213-4934-818b-38d90ced39db" },
        { &HvTimeSyncDriver,  "9527e630-d0ae-497b-adce-e80ab0175caf" },
        { &HvHeartbeatDriver, "57164f39-9115-4e78-ab55-382f3bd5422d" },
        { &HvKvpDriver,       "a9a0f4e7-5a45-4d96-b827-8a841e8c03e6" },
    };
    for (int i = 0; i < 4; i++) {
        const UINT8 *g = k[i].d->type;
        char s[40];
        snprintf(s, sizeof(s), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
                 g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
        assert(!strcmp(s, k[i].g));
    }
    puts("device types passed");
}

static void heartbeat_checks(void)
{
    static UINT8 b[UTIL_BUF], a[UTIL_BUF];
    UINT64 t; UINT32 fw, mv;
    void *ctx = HvHeartbeatDriver.opened(NULL);
    UINT32 n = msg(b, IC_HEARTBEAT, 40);
    HvHeartbeatDriver.packet(ctx, b, n, 5);
    assert(!answer(a, &t));                                 /* nothing before NEGOTIATE */

    const UINT32 hfw[] = { V(1, 0), V(3, 0) }, hmv[] = { V(1, 0), V(3, 0) };
    negotiated(&HvHeartbeatDriver, ctx, b, nego(b, hfw, 2, hmv, 2), &fw, &mv);
    assert(fw == V(3, 0) && mv == V(3, 0));

    n = msg(b, IC_HEARTBEAT, 40);
    wr64(b + IC_HDR, 41);
    HvHeartbeatDriver.packet(ctx, b, n, 9);
    assert(answer(a, &t) == P8(n) && t == 9 && rd64(a + IC_HDR) == 42 && !rd32(a + 20) && a[25] == 5);

    void *old = HvHeartbeatDriver.opened(NULL);             /* a host with nothing in common */
    const UINT32 ofw[] = { V(2, 0) };
    negotiated(&HvHeartbeatDriver, old, b, nego(b, ofw, 1, hmv, 2), &fw, &mv);
    assert(!fw && !mv);
    n = msg(b, IC_HEARTBEAT, 40);
    HvHeartbeatDriver.packet(old, b, n, 10);
    assert(!answer(a, &t));
    puts("heartbeat passed");
}

static void shutdown_checks(void)
{
    static UINT8 b[UTIL_BUF], a[UTIL_BUF];
    UINT64 t; UINT32 fw, mv;
    void *ctx = HvShutdownDriver.opened(NULL);
    const UINT32 hfw[] = { V(3, 0) }, hmv[] = { V(1, 0), V(3, 0), V(3, 1), V(3, 2) };
    negotiated(&HvShutdownDriver, ctx, b, nego(b, hfw, 1, hmv, 4), &fw, &mv);
    assert(fw == V(3, 0) && mv == V(3, 1));

    static const struct { UINT32 flags; int power; UINT32 status; } c[] = {
        { 0, POWER_SHUTDOWN, IC_S_OK }, { 1, POWER_SHUTDOWN, IC_S_OK },
        { 4, POWER_RESTART, IC_S_OK },  { 5, POWER_RESTART, IC_S_OK },
        { 8, -1, IC_E_FAIL },           { 9, -1, IC_E_FAIL },
    };
    for (int i = 0; i < 6; i++) {
        UINT32 n = msg(b, IC_SHUTDOWN, 12 + 2048);           /* reason, timeout, flags, message */
        wr32(b + IC_HDR + 8, c[i].flags);
        power_req = -1;
        HvShutdownDriver.packet(ctx, b, n, 100 + i);
        assert(answer(a, &t) == P8(n) && t == (UINT64)(100 + i) && a[25] == 5);
        assert(rd32(a + 20) == c[i].status && power_req == c[i].power);
    }
    puts("shutdown passed");
}

static void timesync_checks(void)
{
    static UINT8 b[UTIL_BUF], a[UTIL_BUF];
    UINT64 t; UINT32 fw, mv;
    const UINT32 hfw[] = { V(1, 0), V(3, 0) }, hmv[] = { V(1, 0), V(3, 0), V(4, 0) };

    have_ref = false;                                       /* no reference counter: 3.0 */
    void *ctx = HvTimeSyncDriver.opened(NULL);
    negotiated(&HvTimeSyncDriver, ctx, b, nego(b, hfw, 2, hmv, 3), &fw, &mv);
    assert(fw == V(3, 0) && mv == V(3, 0));
    UINT64 host = now + 3600ull * 10000000;                 /* an hour ahead */
    UINT32 n = msg(b, IC_TIMESYNC, 25);
    wr64(b + IC_HDR, host); b[IC_HDR + 24] = TS_SYNC;
    clock_sets = 0;
    HvTimeSyncDriver.packet(ctx, b, n, 1);
    assert(answer(a, &t) == P8(n) && a[25] == 5 && clock_sets == 1 && now == host);

    wr64(b + IC_HDR, now + 5000000); b[IC_HDR + 24] = TS_SAMPLE;   /* half a second: left alone */
    HvTimeSyncDriver.packet(ctx, b, n, 2);
    assert(answer(a, &t) == P8(n) && clock_sets == 1 && now == host);
    wr64(b + IC_HDR, now - 20000000);                       /* two seconds behind: corrected */
    HvTimeSyncDriver.packet(ctx, b, n, 3);
    assert(answer(a, &t) == P8(n) && clock_sets == 2 && now == host - 20000000);
    b[IC_HDR + 24] = 0;                                     /* a probe: answered, nothing set */
    wr64(b + IC_HDR, 1);
    HvTimeSyncDriver.packet(ctx, b, n, 4);
    assert(answer(a, &t) == P8(n) && clock_sets == 2);

    have_ref = true; ref_now = 1500;                        /* version 4: the delay is added */
    ctx = HvTimeSyncDriver.opened(NULL);
    negotiated(&HvTimeSyncDriver, ctx, b, nego(b, hfw, 2, hmv, 3), &fw, &mv);
    assert(mv == V(4, 0));
    n = msg(b, IC_TIMESYNC, 24);
    host = now + 10ull * 10000000;
    wr64(b + IC_HDR, host); wr64(b + IC_HDR + 8, 1000); b[IC_HDR + 16] = TS_SYNC;
    HvTimeSyncDriver.packet(ctx, b, n, 5);
    assert(answer(a, &t) == P8(n) && clock_sets == 3 && now == host + 500);
    puts("time sync passed");
}

/* UTF-16 @p (@size bytes, its NUL counted) as ASCII */
static const char *ascii(const UINT8 *p, UINT32 size)
{
    static char s[1100];
    UINT32 n = size / 2;
    assert(size >= 2 && !(size & 1) && !p[2 * n - 2] && !p[2 * n - 1] && n < sizeof(s));
    for (UINT32 i = 0; i < n; i++) s[i] = (char)p[2 * i];
    return s;
}

/* A KVP message: @op on @pool, its body (zeroed) ready at the result + IC_HDR + 4 */
static UINT32 kvp(UINT8 *b, UINT8 op, UINT8 pool)
{
    UINT32 n = msg(b, IC_KVP, 4 + (op == KVP_ENUMERATE ? 4 : 0) + KVP_BLOCK);
    b[IC_HDR] = op;
    b[IC_HDR + 1] = pool;
    return n;
}

/* Put @key and @value (ASCII) into the value block @v */
static void block(UINT8 *v, const char *key, const char *value)
{
    wr32(v, REG_SZ);
    wr32(v + 4, utf16(v + 12, KVP_KEY, key));
    wr32(v + 8, utf16(v + 12 + KVP_KEY, KVP_VALUE, value));
}

/* Send @n bytes of @b; the answer's status (and its body in @a) */
static UINT32 kvp_send(void *ctx, UINT8 *b, UINT32 n, UINT8 *a)
{
    UINT64 t;
    HvKvpDriver.packet(ctx, b, n, 31);
    assert(answer(a, &t) == P8(n) && t == 31 && a[25] == 5);
    return rd32(a + 20);
}

static void kvp_checks(void)
{
    static UINT8 b[UTIL_BUF], a[UTIL_BUF];
    UINT32 fw, mv;
    void *ctx = HvKvpDriver.opened(NULL);
    const UINT32 hfw[] = { V(1, 0), V(3, 0) }, hmv[] = { V(3, 0), V(4, 0), V(5, 0) };
    negotiated(&HvKvpDriver, ctx, b, nego(b, hfw, 2, hmv, 3), &fw, &mv);
    assert(fw == V(3, 0) && mv == V(4, 0));

    /* the auto pool, as Hyper-V Manager enumerates it */
    static const char *const want[][2] = {
        { "FullyQualifiedDomainName", "nova-pc" }, { "IntegrationServicesVersion", "0.1.0" },
        { "NetworkAddressIPv4", "" }, { "NetworkAddressIPv6", "fe80::1;fd00::15" }, { "OSBuildNumber", "0.1.0" },
        { "OSName", "NovaOS" }, { "OSMajorVersion", "0" }, { "OSMinorVersion", "1" }, { "OSVersion", "0.1.0" },
        { "ProcessorArchitecture", "x86_64" },
    };
    for (UINT32 i = 0; i <= 10; i++) {
        UINT32 n = kvp(b, KVP_ENUMERATE, KVP_POOL_AUTO);
        wr32(b + IC_HDR + 4, i);
        UINT32 st = kvp_send(ctx, b, n, a);
        if (i == 10) { assert(st == IC_S_CONT); break; }
        const UINT8 *v = a + IC_HDR + 8;
        assert(!st && rd32(v) == REG_SZ);
        assert(!strcmp(ascii(v + 12, rd32(v + 4)), want[i][0]));
        assert(!strcmp(ascii(v + 12 + KVP_KEY, rd32(v + 8)), want[i][1]));
    }
    net_ip = true;                                          /* DHCP gave an address since */
    UINT32 n = kvp(b, KVP_ENUMERATE, KVP_POOL_AUTO);
    wr32(b + IC_HDR + 4, 2);
    assert(!kvp_send(ctx, b, n, a) && !strcmp(ascii(a + IC_HDR + 8 + 12 + KVP_KEY, rd32(a + IC_HDR + 16)), "10.0.2.15"));
    puts("key-value exchange: the guest's items passed");

    /* the host's own items (pool 0): set, read, replaced, listed, deleted */
    n = kvp(b, KVP_SET, 0);
    block(b + IC_HDR + 4, "Site", "Lab 4");
    assert(!kvp_send(ctx, b, n, a));
    n = kvp(b, KVP_SET, 0);
    block(b + IC_HDR + 4, "Owner", "Dean");
    assert(!kvp_send(ctx, b, n, a));
    n = kvp(b, KVP_SET, 0);
    block(b + IC_HDR + 4, "Site", "Lab 5");
    assert(!kvp_send(ctx, b, n, a));
    n = kvp(b, KVP_GET, 0);
    block(b + IC_HDR + 4, "Site", "");
    assert(!kvp_send(ctx, b, n, a) && !strcmp(ascii(a + IC_HDR + 4 + 12 + KVP_KEY, rd32(a + IC_HDR + 12)), "Lab 5"));
    n = kvp(b, KVP_GET, 1);                                 /* the same key in another pool */
    block(b + IC_HDR + 4, "Site", "");
    assert(kvp_send(ctx, b, n, a) == IC_E_FAIL);
    int items = 0;
    for (UINT32 i = 0;; i++, items++) {
        n = kvp(b, KVP_ENUMERATE, 0);
        wr32(b + IC_HDR + 4, i);
        if (kvp_send(ctx, b, n, a) == IC_S_CONT) break;
    }
    assert(items == 2);
    n = kvp(b, KVP_DELETE, 0);
    wr32(b + IC_HDR + 4, utf16(b + IC_HDR + 8, KVP_KEY, "Owner"));
    assert(!kvp_send(ctx, b, n, a));
    assert(kvp_send(ctx, b, n, a) == IC_E_FAIL);            /* (gone already) */
    n = kvp(b, KVP_SET, KVP_POOL_AUTO);                     /* the guest's own pool is read-only */
    block(b + IC_HDR + 4, "OSName", "Windows");
    assert(kvp_send(ctx, b, n, a) == IC_E_FAIL);
    for (int i = 0; i < KVP_ITEMS; i++) {                   /* (with "Site" kept: the 16th is refused) */
        char key[8];
        snprintf(key, sizeof(key), "k%d", i);
        n = kvp(b, KVP_SET, 1);
        block(b + IC_HDR + 4, key, "v");
        assert(kvp_send(ctx, b, n, a) == (i < KVP_ITEMS - 1 ? IC_S_OK : IC_E_FAIL));
    }
    n = kvp(b, 4, 0);                                       /* GET_IP_INFO: refused */
    assert(kvp_send(ctx, b, n, a) == IC_E_FAIL);
    n = kvp(b, KVP_SET, 7);                                 /* no such pool */
    assert(kvp_send(ctx, b, n, a) == IC_E_FAIL);
    puts("key-value exchange: the host's items passed");
}

int main(void)
{
    static UINT8 ring[8 * VMBUS_PAGE];
    vm_ring_init(&wire, ring, 8);
    guid_checks();
    heartbeat_checks();
    shutdown_checks();
    timesync_checks();
    kvp_checks();
    puts("hv_util: all passed");
    return 0;
}
