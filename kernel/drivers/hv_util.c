/*
 * hv_util.c — Hyper-V's integration services: shutdown, time sync, heartbeat
 *
 * Each is a VMBus channel (vmbus.c) on which the host asks and the guest
 * answers.  The messages (as Linux's hv_utils and FreeBSD's hv_util speak
 * them) start with an 8-byte pipe header and a 20-byte IC header:
 *
 *    8  framework version (16-bit major, minor)
 *   12  message type: 0 negotiate, 1 heartbeat, 3 shutdown, 4 time sync
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
 */

#include "hv_util.h"
#include "../wm/desktop.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../mm/vmm.h"

/* um_syscall.c: the NT system time (100 ns since 1601) from now on */
void   UmClockSet(UINT64 now_100ns);
UINT64 um_now_100ns(void);

#define IC_HDR             28
#define IC_NEGOTIATE       0
#define IC_HEARTBEAT       1
#define IC_SHUTDOWN        3
#define IC_TIMESYNC        4
#define IC_FLAG_TRANSACTION 1
#define IC_FLAG_RESPONSE    4
#define IC_S_OK            0u
#define IC_E_FAIL          0x80004005u

#define TS_SYNC            1
#define TS_SAMPLE          2
#define TS_STEP_100NS      10000000ull   /* a SAMPLE further off than 1 s moves the clock */

#define V(major, minor)    ((UINT32)(major) << 16 | (minor))

#define UTIL_BUF           4096          /* vmbus.c's PKT_MAX */

enum { UTIL_SHUTDOWN, UTIL_TIMESYNC, UTIL_HEARTBEAT };

typedef struct {
    VmbusChannel *ch;
    int           kind;
    const char   *name;
    UINT32        msgver;                /* negotiated; 0 before */
    bool          synced;                /* time sync: the first SYNC was logged */
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
    const UINT32 *ours = u->kind == UTIL_SHUTDOWN ? g_sd : u->kind == UTIL_TIMESYNC ? g_ts : g_hb;
    int n = u->kind == UTIL_SHUTDOWN ? 3 : u->kind == UTIL_TIMESYNC ? 3 : 2;
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

/* {0e0b6031-5213-4934-818b-38d90ced39db} */
const VmbusDriver HvShutdownDriver = {
    { 0x31, 0x60, 0x0b, 0x0e, 0x13, 0x52, 0x34, 0x49, 0x81, 0x8b, 0x38, 0xd9, 0x0c, 0xed, 0x39, 0xdb },
    "Hyper-V shutdown", shutdown_opened, util_packet, NULL, NULL,
};
/* {9527e630-d0ae-497b-adce-e80ab0175caf} */
const VmbusDriver HvTimeSyncDriver = {
    { 0x30, 0xe6, 0x27, 0x95, 0xae, 0xd0, 0x7b, 0x49, 0xad, 0xce, 0xe8, 0x0a, 0xb0, 0x17, 0x5c, 0xaf },
    "Hyper-V time sync", timesync_opened, util_packet, NULL, NULL,
};
/* {57164f39-9115-4e78-ab55-382f3bd5422d} */
const VmbusDriver HvHeartbeatDriver = {
    { 0x39, 0x4f, 0x16, 0x57, 0x15, 0x91, 0x78, 0x4e, 0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d },
    "Hyper-V heartbeat", heartbeat_opened, util_packet, NULL, NULL,
};
