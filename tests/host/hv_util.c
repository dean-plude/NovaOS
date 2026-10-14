/* Hyper-V's integration services (shutdown, time sync, heartbeat) on the
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
    assert(len == n && t == 77 && a[25] == (IC_FLAG_TRANSACTION | IC_FLAG_RESPONSE) && rd16(a + 18) == 16);
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
    };
    for (int i = 0; i < 3; i++) {
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
    assert(answer(a, &t) == n && t == 9 && rd64(a + IC_HDR) == 42 && !rd32(a + 20) && a[25] == 5);

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
        assert(answer(a, &t) == n && t == (UINT64)(100 + i) && a[25] == 5);
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
    assert(answer(a, &t) == n && a[25] == 5 && clock_sets == 1 && now == host);

    wr64(b + IC_HDR, now + 5000000); b[IC_HDR + 24] = TS_SAMPLE;   /* half a second: left alone */
    HvTimeSyncDriver.packet(ctx, b, n, 2);
    assert(answer(a, &t) == n && clock_sets == 1 && now == host);
    wr64(b + IC_HDR, now - 20000000);                       /* two seconds behind: corrected */
    HvTimeSyncDriver.packet(ctx, b, n, 3);
    assert(answer(a, &t) == n && clock_sets == 2 && now == host - 20000000);
    b[IC_HDR + 24] = 0;                                     /* a probe: answered, nothing set */
    wr64(b + IC_HDR, 1);
    HvTimeSyncDriver.packet(ctx, b, n, 4);
    assert(answer(a, &t) == n && clock_sets == 2);

    have_ref = true; ref_now = 1500;                        /* version 4: the delay is added */
    ctx = HvTimeSyncDriver.opened(NULL);
    negotiated(&HvTimeSyncDriver, ctx, b, nego(b, hfw, 2, hmv, 3), &fw, &mv);
    assert(mv == V(4, 0));
    n = msg(b, IC_TIMESYNC, 24);
    host = now + 10ull * 10000000;
    wr64(b + IC_HDR, host); wr64(b + IC_HDR + 8, 1000); b[IC_HDR + 16] = TS_SYNC;
    HvTimeSyncDriver.packet(ctx, b, n, 5);
    assert(answer(a, &t) == n && clock_sets == 3 && now == host + 500);
    puts("time sync passed");
}

int main(void)
{
    static UINT8 ring[8 * VMBUS_PAGE];
    vm_ring_init(&wire, ring, 8);
    guid_checks();
    heartbeat_checks();
    shutdown_checks();
    timesync_checks();
    puts("hv_util: all passed");
    return 0;
}
