/*
 * bthport.c — NovaOS's Bluetooth host (the role Windows' bthport.sys plays)
 *
 * On Windows a Bluetooth radio's driver stack is a transport at the bottom
 * (bthusb.sys for a USB radio, with a vendor's filter such as Intel's
 * ibtusb.sys under it) and bthport.sys, the host stack, which the
 * transport links in: HCI, the Link Manager's pairing questions, L2CAP,
 * SDP, and a child device node per service a paired device offers
 * (BTHENUM\{uuid}...), which Plug and Play gives its profile driver
 * (hidbth.sys for HID, ...).  NovaOS ships none of them, so this is its
 * own, written from the Bluetooth Core Specification 5.3: the transport
 * (bthusb.c) hands it HCI events and ACL packets and sends what it asks.
 * BlueZ (GPL) stays out; FreeBSD's netgraph stack (BSD) was looked at but
 * is tied to netgraph's node graph, so the protocol code here is new.
 *
 *   - bring-up (Vol 4, Part E): Reset, Read Local Version, Supported
 *     Features, Buffer Size and BD_ADDR, Set Event Mask, the local name
 *     and class of device ("computer, laptop"), Simple Pairing on,
 *     inquiry with RSSI, page scan on (connectable);
 *   - inquiry (IOCTL_NOVABTH_INQUIRY): Inquiry Result (standard, with
 *     RSSI, extended with a name in its EIR) and each new device's name
 *     by Remote Name Request;
 *   - pairing (IOCTL_NOVABTH_PAIR): Create Connection, then
 *     Authentication Requested; the controller's questions are answered
 *     as Windows answers them with no user present: Link Key Request
 *     with the stored key or a negative reply, IO Capability Request as
 *     NoInputNoOutput with dedicated bonding (so Secure Simple Pairing is
 *     "Just Works" and User Confirmation Request is accepted), PIN Code
 *     Request with 0000 for a legacy device; Link Key Notification's key
 *     is kept where Windows keeps it, HKLM\SYSTEM\CurrentControlSet\
 *     Services\BTHPORT\Parameters\Keys\<radio>\<device>; then
 *     encryption, and the device's SDP records;
 *   - L2CAP (Vol 3, Part A) basic mode: connection, configuration (MTU),
 *     disconnection, information requests answered; segments of ACL
 *     packets put back together, ours split to the controller's buffer
 *     size and sent as its Number Of Completed Packets allows;
 *   - SDP (Vol 3, Part B) client: ServiceSearchAttribute for every
 *     record, with continuation; each record's service class, and for
 *     HID its report descriptor (HIDDescriptorList) and for PnP
 *     Information its vendor and product, cached under
 *     ...\BTHPORT\Parameters\Devices\<device>;
 *   - services (IOCTL_NOVABTH_SET_SERVICE): a service switched on gets a
 *     child PDO, as bthenum gives one; HID's (BTHENUM\{00001124-...})
 *     gets NovaOS's hidbth (hidbth.c), which connects through
 *     BthHidConnect when it starts.
 *
 * Not done yet: devices that connect to us (a mouse reconnecting after
 * sleep: Connection Request is refused), LE, SCO audio, RFCOMM, BRBs for
 * third-party profile drivers (IOCTL_INTERNAL_BTH_SUBMIT_BRB).
 */
#include "ntapi.h"
#include "bthport.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../um/um.h"
#include "../../userland/include/novabth.h"

NT_API NT_STATUS nt_IoRegisterDeviceInterface(NT_DEVICE_OBJECT *pdo, const NT_GUID *guid, NT_UNICODE_STRING *ref,
                                              NT_UNICODE_STRING *out);
NT_API NT_STATUS nt_IoSetDeviceInterfaceState(NT_UNICODE_STRING *name, UINT8 enable);
NT_API void nt_IoInvalidateDeviceRelations(NT_DEVICE_OBJECT *pdo, int type);
NT_API void nt_PoStartNextPowerIrp(NT_IRP *irp);

#define BTH_KEY       "Machine\\SYSTEM\\CurrentControlSet\\Services\\BTHPORT\\Parameters"
#define MAX_DEVS      16
#define MAX_CHANS     16
#define MAX_SVCS      NOVABTH_MAX_SERVICES
#define ACL_BUF       2048             /* an L2CAP frame put back together */
#define OUR_MTU       1013
#define SDP_BUF       4096
#define SVC_MAGIC     0x48544231u      /* 'BTH1': a service PDO's extension */
#define UUID_HID      0x1124
#define UUID_PNP      0x1200
#define FILE_DEVICE_BLUETOOTH 0x41
#define ST_DEVICE_NOT_CONNECTED ((NT_STATUS)0xC000009D)
#define ST_IO_TIMEOUT           ((NT_STATUS)0xC00000B5)
#define ST_NOT_FOUND            ((NT_STATUS)0xC0000225)
#define ST_ACCESS_DENIED        ((NT_STATUS)0xC0000022)

typedef struct BthChan BthChan;
typedef struct BthDev BthDev;

struct BthDev {
    bool     used;
    UINT8    addr[6];                  /* (as HCI sends it: least significant byte first) */
    UINT32   cod;
    char     name[NOVABTH_MAX_NAME];
    INT8     rssi;
    UINT8    psrm;
    UINT16   clock;                    /* (bit 15: valid) */
    bool     named, found, paired;
    UINT8    key[16];
    /* the connection: volatile, the event thread sets them */
    volatile int conn;                 /* 0 none, 1 connecting, 2 up */
    volatile UINT8 conn_status;
    UINT16   handle;
    volatile int auth, enc, name_req;  /* 0 not asked, 1 asked, 2 answered */
    volatile UINT8 auth_status, enc_status;
    UINT8    rx[ACL_BUF];              /* an L2CAP frame being put together */
    UINT32   rx_len;
    UINT8    ident;
    /* from its SDP records */
    UINT16   svc[MAX_SVCS];
    int      nsvc;
    UINT16   vid, pid, version;
    UINT8    country;
    UINT8   *rdesc;
    UINT16   rdesc_len;
    bool     hid_on;
    NT_DEVICE_OBJECT *hid_pdo;
};

struct BthChan {
    bool     used;
    BthDev  *dev;
    UINT16   psm, lcid, rcid, rmtu;
    UINT8    ident;                    /* our request's */
    volatile int state;                /* 0 closed, 1 connecting, 2 configuring, 3 open */
    volatile bool cfg_in, cfg_out;
    volatile UINT16 result;
    BthHidInput rx;                    /* (frames: the profile's) */
    void    *ctx;
    UINT8   *sdp;                      /* (SDP: the response being waited for) */
    volatile UINT32 sdp_len;
};

struct BthRadio {
    NT_DEVICE_OBJECT *fdo, *pdo;
    BthTransport t;
    void    *ctx;
    NT_UNICODE_STRING iface;
    NT_KEVENT cmd_lock, lock;          /* (synchronization events: one command at a time; the tables) */
    volatile bool up, gone;
    /* the command being waited for */
    volatile UINT16 cmd_op;
    volatile int cmd_state;            /* 1 sent, 2 answered */
    UINT8    cmd_ret[256];
    volatile UINT32 cmd_len;
    /* the controller */
    UINT8    addr[6];
    char     name[64];
    UINT8    hci_ver, lmp_ver;
    UINT16   hci_rev, mfg, lmp_sub;
    UINT8    features[8];
    UINT16   acl_mtu;
    volatile int acl_free;
    UINT8    scan;
    volatile bool inquiring;
    BthDev   dev[MAX_DEVS];
    BthChan  chan[MAX_CHANS];
    UINT16   next_cid;
};

typedef struct {                       /* a service PDO's extension */
    UINT32    magic;
    BthRadio *r;
    BthDev   *dev;
    UINT16    uuid;
    bool      listed;                  /* (in the bus's relations) */
    bool      gone;                    /* (surprise removal: the radio or the bus went) */
    BthChan  *ctrl, *intr;
    BthHidInput input;
    void     *ctx;
} SvcPdo;

static const NT_GUID g_radio_iface = NOVABTH_RADIO_GUID_INIT;

static void lock(BthRadio *r)   { nt_KeWaitForSingleObject(&r->lock, 0, 0, 0, NULL); }
static void unlock(BthRadio *r) { nt_KeSetEvent(&r->lock, 0, 0); }

static void sleep_ms(UINT32 ms)
{
    INT64 t = -(INT64)ms * 10000;
    nt_KeDelayExecutionThread(0, 0, &t);
}

/* Wait (10 ms steps) until @cond, at most @ms; whether it came */
#define WAIT_FOR(r, cond, ms) ({ int _n = (int)((ms) / 10); while (!(cond) && _n-- > 0 && !(r)->gone) sleep_ms(10); (bool)(cond); })

static UINT16 le16(const UINT8 *p) { return (UINT16)(p[0] | p[1] << 8); }
static UINT16 be16(const UINT8 *p) { return (UINT16)(p[0] << 8 | p[1]); }
static UINT32 be32(const UINT8 *p) { return (UINT32)p[0] << 24 | (UINT32)p[1] << 16 | (UINT32)p[2] << 8 | p[3]; }
static void put16(UINT8 *p, UINT16 v) { p[0] = (UINT8)v; p[1] = (UINT8)(v >> 8); }

static UINT64 addr_u64(const UINT8 *a)
{
    UINT64 v = 0;
    for (int i = 5; i >= 0; i--) v = v << 8 | a[i];
    return v;
}

static void addr_from(UINT64 v, UINT8 *a) { for (int i = 0; i < 6; i++) a[i] = (UINT8)(v >> (8 * i)); }

/* "4e4f56410001", as Windows names a device's registry values and keys */
static void addr_hex(const UINT8 *a, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++) { out[2 * i] = hex[a[5 - i] >> 4]; out[2 * i + 1] = hex[a[5 - i] & 15]; }
    out[12] = 0;
}

/* "4E:4F:56:41:00:01", for the log */
static const char *addr_text(const UINT8 *a, char *buf)
{
    ksnprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
    return buf;
}

/* ---- HCI ---- */

static NT_STATUS send_cmd(BthRadio *r, UINT16 op, const void *p, UINT8 n)
{
    UINT8 pkt[3 + 255];
    put16(pkt, op);
    pkt[2] = n;
    if (n) memcpy(pkt + 3, p, n);
    if (r->gone) return ST_DEVICE_NOT_CONNECTED;
    return r->t.send_cmd(r->ctx, pkt, 3u + n);
}

/* Command @op, waiting for its Command Complete (its return parameters
 * into @ret, their status the result) or Command Status: the HCI status
 * (0 success), or -1 if the controller did not answer */
static int cmd(BthRadio *r, UINT16 op, const void *p, UINT8 n, UINT8 *ret, UINT32 cap)
{
    nt_KeWaitForSingleObject(&r->cmd_lock, 0, 0, 0, NULL);
    r->cmd_op = op;
    r->cmd_len = 0;
    __atomic_store_n(&r->cmd_state, 1, __ATOMIC_RELEASE);
    int st = -1;
    if (NT_SUCCESS(send_cmd(r, op, p, n)) && WAIT_FOR(r, __atomic_load_n(&r->cmd_state, __ATOMIC_ACQUIRE) == 2, 3000)) {
        st = r->cmd_len ? r->cmd_ret[0] : 0;
        if (ret) memcpy(ret, r->cmd_ret, r->cmd_len < cap ? r->cmd_len : cap);
    } else {
        kprintf("[BTH] command %04x: no answer\n", op);
    }
    r->cmd_state = 0;
    r->cmd_op = 0;
    nt_KeSetEvent(&r->cmd_lock, 0, 0);
    return st;
}

static BthDev *dev_find(BthRadio *r, const UINT8 *addr)
{
    for (int i = 0; i < MAX_DEVS; i++) if (r->dev[i].used && !memcmp(r->dev[i].addr, addr, 6)) return &r->dev[i];
    return NULL;
}

static BthDev *dev_by_handle(BthRadio *r, UINT16 h)
{
    for (int i = 0; i < MAX_DEVS; i++) if (r->dev[i].used && r->dev[i].conn == 2 && r->dev[i].handle == h) return &r->dev[i];
    return NULL;
}

/* @addr's entry, made if new (NULL: the table is full) */
static BthDev *dev_get(BthRadio *r, const UINT8 *addr)
{
    BthDev *d = dev_find(r, addr);
    if (d) return d;
    for (int i = 0; i < MAX_DEVS; i++)
        if (!r->dev[i].used) {
            d = &r->dev[i];
            memset(d, 0, sizeof(*d));
            memcpy(d->addr, addr, 6);
            d->used = true;
            return d;
        }
    return NULL;
}

static void dev_key_path(BthRadio *r, char *out, int cap)
{
    char a[13];
    addr_hex(r->addr, a);
    ksnprintf(out, cap, BTH_KEY "\\Keys\\%s", a);
}

static void dev_path(const BthDev *d, char *out, int cap)
{
    char a[13];
    addr_hex(d->addr, a);
    ksnprintf(out, cap, BTH_KEY "\\Devices\\%s", a);
}

/* What is known of @d, into the registry (BTHPORT\Parameters) */
static void dev_save(BthRadio *r, BthDev *d)
{
    char path[160], a[13];
    dev_path(d, path, sizeof(path));
    um_registry_create_key(path);
    um_registry_set_sz(path, "Name", d->name);
    um_registry_set_dword(path, "COD", d->cod);
    if (d->nsvc) um_registry_set_bin(path, "CachedServices", d->svc, (UINT32)(2 * d->nsvc));
    if (d->rdesc_len) um_registry_set_bin(path, "HidDescriptor", d->rdesc, d->rdesc_len);
    um_registry_set_dword(path, "VID", d->vid);
    um_registry_set_dword(path, "PID", d->pid);
    um_registry_set_dword(path, "Version", d->version);
    um_registry_set_dword(path, "HidEnabled", d->hid_on);
    if (d->paired) {
        dev_key_path(r, path, sizeof(path));
        addr_hex(d->addr, a);
        um_registry_create_key(path);
        um_registry_set_bin(path, a, d->key, 16);
    }
}

/* The devices paired with this radio before (its Keys key, and each
 * one's Devices key) */
static void devs_load(BthRadio *r)
{
    char path[160], kpath[160];
    dev_key_path(r, kpath, sizeof(kpath));
    for (UINT32 i = 0; ; i++) {
        UINT16 wname[32];
        int nlen = 0;
        UINT32 type = 0;
        UINT8 key[16];
        int n = um_registry_enum_value(kpath, i, wname, 32, &nlen, &type, key, sizeof(key));
        if (n < 0) break;
        if (n != 16 || nlen != 12) continue;
        UINT8 addr[6];
        bool ok = true;
        for (int k = 0; k < 6 && ok; k++) {
            int v = 0;
            for (int j = 0; j < 2; j++) {
                UINT16 c = wname[2 * k + j];
                int x = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (x < 0) ok = false;
                v = v << 4 | (x & 15);
            }
            addr[5 - k] = (UINT8)v;
        }
        if (!ok) continue;
        BthDev *d = dev_get(r, addr);
        if (!d) break;
        d->paired = true;
        memcpy(d->key, key, 16);
        dev_path(d, path, sizeof(path));
        if (um_registry_get_sz(path, "Name", d->name, sizeof(d->name)) && d->name[0]) d->named = true;
        UINT32 v = 0;
        if (um_registry_get_dword(path, "COD", &v)) d->cod = v;
        if (um_registry_get_dword(path, "VID", &v)) d->vid = (UINT16)v;
        if (um_registry_get_dword(path, "PID", &v)) d->pid = (UINT16)v;
        if (um_registry_get_dword(path, "Version", &v)) d->version = (UINT16)v;
        if (um_registry_get_dword(path, "HidEnabled", &v)) d->hid_on = v != 0;
        int sn = um_registry_get_bin(path, "CachedServices", d->svc, sizeof(d->svc));
        d->nsvc = sn > 0 ? sn / 2 : 0;
        UINT8 *buf = kmalloc(1024);
        int rn = buf ? um_registry_get_bin(path, "HidDescriptor", buf, 1024) : -1;
        if (rn > 0) { d->rdesc = buf; d->rdesc_len = (UINT16)rn; } else kfree(buf);
        kprintf("[BTH] paired before: %s \"%s\"%s\n", addr_text(d->addr, path), d->name, d->hid_on ? ", HID on" : "");
    }
}

/* ---- L2CAP ---- */

/* ACL data to @d: @frame (an L2CAP frame, header included) in packets of
 * the controller's size, each when it has a buffer free */
static NT_STATUS acl_send(BthRadio *r, BthDev *d, const UINT8 *frame, UINT32 len)
{
    UINT8 *pkt = kmalloc(4u + r->acl_mtu);
    if (!pkt) return NT_STATUS_INSUFFICIENT_RESOURCES;
    NT_STATUS st = NT_STATUS_SUCCESS;
    for (UINT32 off = 0; off < len && NT_SUCCESS(st); ) {
        UINT32 n = len - off < r->acl_mtu ? len - off : r->acl_mtu;
        if (!WAIT_FOR(r, __atomic_load_n(&r->acl_free, __ATOMIC_ACQUIRE) > 0, 2000)) { st = ST_IO_TIMEOUT; break; }
        __atomic_fetch_sub(&r->acl_free, 1, __ATOMIC_ACQ_REL);
        put16(pkt, (UINT16)(d->handle | (off ? 0x1000 : 0x2000)));   /* (packet boundary: first, or continuing) */
        put16(pkt + 2, (UINT16)n);
        memcpy(pkt + 4, frame + off, n);
        st = d->conn == 2 && !r->gone ? r->t.send_acl(r->ctx, pkt, 4 + n) : ST_DEVICE_NOT_CONNECTED;
        off += n;
    }
    kfree(pkt);
    return st;
}

static NT_STATUS l2_frame(BthRadio *r, BthDev *d, UINT16 cid, const UINT8 *data, UINT32 len)
{
    UINT8 *f = kmalloc(4 + len);
    if (!f) return NT_STATUS_INSUFFICIENT_RESOURCES;
    put16(f, (UINT16)len);
    put16(f + 2, cid);
    memcpy(f + 4, data, len);
    NT_STATUS st = acl_send(r, d, f, 4 + len);
    kfree(f);
    return st;
}

static NT_STATUS sig_send(BthRadio *r, BthDev *d, UINT8 code, UINT8 ident, const UINT8 *body, UINT16 len)
{
    UINT8 b[64];
    b[0] = code;
    b[1] = ident;
    put16(b + 2, len);
    memcpy(b + 4, body, len);
    return l2_frame(r, d, 0x0001, b, 4u + len);
}

static BthChan *chan_by_lcid(BthRadio *r, BthDev *d, UINT16 cid)
{
    for (int i = 0; i < MAX_CHANS; i++)
        if (r->chan[i].used && r->chan[i].dev == d && r->chan[i].lcid == cid) return &r->chan[i];
    return NULL;
}

static BthChan *chan_by_ident(BthRadio *r, BthDev *d, UINT8 ident)
{
    for (int i = 0; i < MAX_CHANS; i++)
        if (r->chan[i].used && r->chan[i].dev == d && r->chan[i].ident == ident) return &r->chan[i];
    return NULL;
}

static void chan_free(BthRadio *r, BthChan *c)
{
    lock(r);
    c->state = 0;
    c->used = false;
    unlock(r);
}

/* A request from the device on the signalling channel, or its answer
 * to ours */
static void l2_signal(BthRadio *r, BthDev *d, UINT8 code, UINT8 ident, const UINT8 *p, UINT16 len)
{
    UINT8 b[16];
    char t[18];
    BthChan *c;
    switch (code) {
    case 0x01:                                                  /* Command Reject */
        if ((c = chan_by_ident(r, d, ident))) { c->result = 0xFFFF; c->state = 0; }
        break;
    case 0x02:                                                  /* Connection Request: none taken yet */
        if (len < 4) break;
        put16(b, 0);
        put16(b + 2, le16(p + 2));
        put16(b + 4, 0x0002);                                   /* (PSM not supported) */
        put16(b + 6, 0);
        sig_send(r, d, 0x03, ident, b, 8);
        kprintf("[BTH] %s asked for PSM %04x: refused\n", addr_text(d->addr, t), le16(p));
        break;
    case 0x03:                                                  /* Connection Response */
        if (len < 8 || !(c = chan_by_lcid(r, d, le16(p + 2)))) break;
        c->result = le16(p + 4);
        if (c->result == 1) break;                              /* (pending) */
        if (c->result) { c->state = 0; break; }
        c->rcid = le16(p);
        c->state = 2;
        c->ident = (UINT8)(++d->ident ? d->ident : ++d->ident);
        put16(b, c->rcid);
        put16(b + 2, 0);
        b[4] = 0x01; b[5] = 2;                                  /* (MTU) */
        put16(b + 6, OUR_MTU);
        sig_send(r, d, 0x04, c->ident, b, 8);
        break;
    case 0x04: {                                                /* Configuration Request: what it asks is taken */
        if (len < 4) break;
        c = chan_by_lcid(r, d, le16(p));
        for (UINT16 o = 4; c && o + 2 <= len; o += (UINT16)(2 + p[o + 1]))
            if ((p[o] & 0x7F) == 0x01 && p[o + 1] == 2 && o + 4 <= len) c->rmtu = le16(p + o + 2);
        put16(b, c ? c->rcid : 0);
        put16(b + 2, 0);
        put16(b + 4, c ? 0 : 0x0002);
        sig_send(r, d, 0x05, ident, b, 6);
        if (c) c->cfg_in = true;
        if (c && c->cfg_out) c->state = 3;
        break;
    }
    case 0x05:                                                  /* Configuration Response */
        if (len < 6 || !(c = chan_by_lcid(r, d, le16(p)))) break;
        if (le16(p + 4)) { c->result = le16(p + 4); c->state = 0; break; }
        c->cfg_out = true;
        if (c->cfg_in) c->state = 3;
        break;
    case 0x06:                                                  /* Disconnection Request */
        if (len < 4) break;
        c = chan_by_lcid(r, d, le16(p));
        memcpy(b, p, 4);
        sig_send(r, d, 0x07, ident, b, 4);
        if (c) c->state = 0;
        break;
    case 0x07:                                                  /* Disconnection Response */
        if (len >= 4 && (c = chan_by_lcid(r, d, le16(p + 2)))) c->state = 0;
        break;
    case 0x0A: {                                                /* Information Request */
        UINT16 type = len >= 2 ? le16(p) : 0;
        put16(b, type);
        if (type == 2) { put16(b + 2, 0); memset(b + 4, 0, 4); sig_send(r, d, 0x0B, ident, b, 8); }
        else if (type == 3) { put16(b + 2, 0); memset(b + 4, 0, 8); b[4] = 0x02; sig_send(r, d, 0x0B, ident, b, 12); }
        else { put16(b + 2, 1); sig_send(r, d, 0x0B, ident, b, 4); }
        break;
    }
    }
}

static void l2_input(BthRadio *r, BthDev *d, UINT16 cid, const UINT8 *p, UINT16 len)
{
    if (cid == 0x0001) {
        for (UINT32 o = 0; o + 4 <= len; ) {
            UINT16 n = le16(p + o + 2);
            if (o + 4 + n > len) break;
            l2_signal(r, d, p[o], p[o + 1], p + o + 4, n);
            o += 4u + n;
        }
        return;
    }
    BthChan *c = chan_by_lcid(r, d, cid);
    if (!c || c->state != 3) return;
    if (c->sdp) {
        UINT32 n = len < SDP_BUF ? len : SDP_BUF;
        memcpy(c->sdp, p, n);
        __atomic_store_n(&c->sdp_len, n, __ATOMIC_RELEASE);
    } else if (c->rx) {
        c->rx(c->ctx, p, len);
    }
}

/* Channel @psm to @d (its ACL link up): the request, then both sides'
 * configuration; NULL if it did not open */
static BthChan *l2_open(BthRadio *r, BthDev *d, UINT16 psm)
{
    BthChan *c = NULL;
    lock(r);
    for (int i = 0; i < MAX_CHANS && !c; i++)
        if (!r->chan[i].used) {
            c = &r->chan[i];
            memset(c, 0, sizeof(*c));
            c->used = true;
            c->dev = d;
            c->psm = psm;
            c->lcid = r->next_cid++;
            if (r->next_cid < 0x40) r->next_cid = 0x40;
            c->rmtu = 672;
            c->state = 1;
            c->ident = (UINT8)(++d->ident ? d->ident : ++d->ident);
        }
    unlock(r);
    if (!c) return NULL;
    UINT8 b[4];
    put16(b, psm);
    put16(b + 2, c->lcid);
    char t[18];
    if (NT_SUCCESS(sig_send(r, d, 0x02, c->ident, b, 4)) && WAIT_FOR(r, c->state == 3 || c->state == 0, 5000) && c->state == 3)
        return c;
    kprintf("[BTH] %s: L2CAP PSM %04x did not open (result %04x)\n", addr_text(d->addr, t), psm, c->result);
    chan_free(r, c);
    return NULL;
}

static void l2_close(BthRadio *r, BthChan *c)
{
    if (!c) return;
    BthDev *d = c->dev;
    if (c->state && d->conn == 2) {
        UINT8 b[4];
        put16(b, c->rcid);
        put16(b + 2, c->lcid);
        c->ident = (UINT8)(++d->ident ? d->ident : ++d->ident);
        if (NT_SUCCESS(sig_send(r, d, 0x06, c->ident, b, 4))) WAIT_FOR(r, c->state == 0, 1000);
    }
    chan_free(r, c);
}

static NT_STATUS l2_send(BthRadio *r, BthChan *c, const UINT8 *data, UINT32 len)
{
    if (!c || c->state != 3) return ST_DEVICE_NOT_CONNECTED;
    if (len > c->rmtu) return NT_STATUS_INVALID_PARAMETER;
    return l2_frame(r, c->dev, c->rcid, data, len);
}

/* ---- events ---- */

static void name_from_eir(BthDev *d, const UINT8 *eir, UINT32 len)
{
    for (UINT32 o = 0; o + 1 < len && eir[o]; o += 1u + eir[o]) {
        UINT8 n = eir[o], type = eir[o + 1];
        if (o + 1 + n > len || n < 1) break;
        if (type == 0x09 || (type == 0x08 && !d->named)) {      /* (complete, else shortened, local name) */
            UINT32 k = n - 1u < NOVABTH_MAX_NAME - 1 ? n - 1u : NOVABTH_MAX_NAME - 1;
            memcpy(d->name, eir + o + 2, k);
            d->name[k] = 0;
            d->named = true;
        }
    }
}

static void found(BthRadio *r, const UINT8 *addr, UINT8 psrm, const UINT8 *cod, UINT16 clock, INT8 rssi, const UINT8 *eir,
                  UINT32 eir_len)
{
    lock(r);
    BthDev *d = dev_get(r, addr);
    if (d) {
        d->psrm = psrm;
        d->cod = (UINT32)(cod[0] | cod[1] << 8 | cod[2] << 16);
        d->clock = (UINT16)(clock | 0x8000);
        d->rssi = rssi;
        if (eir) name_from_eir(d, eir, eir_len);
        if (!d->found) {
            char t[18];
            kprintf("[BTH] found %s, class %06x, RSSI %d\n", addr_text(addr, t), d->cod, rssi);
        }
        d->found = true;
    }
    unlock(r);
}

static void on_event(BthRadio *r, UINT8 code, const UINT8 *p, UINT32 n)
{
    BthDev *d;
    char t[18];
    UINT8 b[32];
    switch (code) {
    case 0x0E:                                                  /* Command Complete */
        if (n >= 3 && le16(p + 1) == r->cmd_op && r->cmd_state == 1) {
            UINT32 k = n - 3 < sizeof(r->cmd_ret) ? n - 3 : sizeof(r->cmd_ret);
            memcpy(r->cmd_ret, p + 3, k);
            r->cmd_len = k;
            __atomic_store_n(&r->cmd_state, 2, __ATOMIC_RELEASE);
        }
        break;
    case 0x0F:                                                  /* Command Status */
        if (n >= 4 && le16(p + 2) == r->cmd_op && r->cmd_state == 1) {
            r->cmd_ret[0] = p[0];
            r->cmd_len = 1;
            __atomic_store_n(&r->cmd_state, 2, __ATOMIC_RELEASE);
        }
        break;
    case 0x01:                                                  /* Inquiry Complete */
        r->inquiring = false;
        break;
    case 0x02: {                                                /* Inquiry Result (per field arrays) */
        UINT32 k = n ? p[0] : 0;
        if (n < 1 + 14 * k) break;
        for (UINT32 i = 0; i < k; i++)
            found(r, p + 1 + 6 * i, p[1 + 6 * k + i], p + 1 + 9 * k + 3 * i, le16(p + 1 + 12 * k + 2 * i), 0, NULL, 0);
        break;
    }
    case 0x22: {                                                /* Inquiry Result with RSSI */
        UINT32 k = n ? p[0] : 0;
        if (n < 1 + 14 * k) break;
        for (UINT32 i = 0; i < k; i++)
            found(r, p + 1 + 6 * i, p[1 + 6 * k + i], p + 1 + 8 * k + 3 * i, le16(p + 1 + 11 * k + 2 * i),
                  (INT8)p[1 + 13 * k + i], NULL, 0);
        break;
    }
    case 0x2F:                                                  /* Extended Inquiry Result */
        if (n >= 15) found(r, p + 1, p[7], p + 9, le16(p + 12), (INT8)p[14], p + 15, n - 15);
        break;
    case 0x07:                                                  /* Remote Name Request Complete */
        if (n < 7 || !(d = dev_find(r, p + 1))) break;
        if (!p[0]) {
            UINT32 k = n - 7 < NOVABTH_MAX_NAME - 1 ? n - 7 : NOVABTH_MAX_NAME - 1;
            memcpy(d->name, p + 7, k);
            d->name[k] = 0;
            d->named = true;
            kprintf("[BTH] %s is \"%s\"\n", addr_text(d->addr, t), d->name);
        }
        d->name_req = 2;
        break;
    case 0x03:                                                  /* Connection Complete */
        if (n < 11 || !(d = dev_find(r, p + 3)) || d->conn != 1) break;
        d->conn_status = p[0];
        d->handle = le16(p + 1) & 0x0FFF;
        d->rx_len = 0;
        d->conn = p[0] ? 0 : 2;
        break;
    case 0x05:                                                  /* Disconnection Complete */
        if (n < 4 || p[0]) break;
        for (int i = 0; i < MAX_DEVS; i++) {
            d = &r->dev[i];
            if (!d->used || d->conn != 2 || d->handle != (le16(p + 1) & 0x0FFF)) continue;
            d->conn = 0;
            d->auth = d->enc = 0;
            for (int c = 0; c < MAX_CHANS; c++) if (r->chan[c].used && r->chan[c].dev == d) r->chan[c].state = 0;
            kprintf("[BTH] %s disconnected (reason %02x)\n", addr_text(d->addr, t), p[3]);
        }
        break;
    case 0x06:                                                  /* Authentication Complete */
        if (n >= 3 && (d = dev_by_handle(r, le16(p + 1) & 0x0FFF))) { d->auth_status = p[0]; d->auth = 2; }
        break;
    case 0x08:                                                  /* Encryption Change */
        if (n >= 4 && (d = dev_by_handle(r, le16(p + 1) & 0x0FFF))) { d->enc_status = p[0] ? p[0] : p[3] ? 0 : 0xFF; d->enc = 2; }
        break;
    case 0x13: {                                                /* Number Of Completed Packets */
        UINT32 k = n ? p[0] : 0;
        for (UINT32 i = 0; i < k && 1 + 4 * i + 4 <= n; i++) __atomic_fetch_add(&r->acl_free, le16(p + 1 + 4 * i + 2), __ATOMIC_ACQ_REL);
        break;
    }
    case 0x04:                                                  /* Connection Request: one of ours may reconnect later */
        if (n < 10) break;
        memcpy(b, p, 6);
        b[6] = 0x0F;                                            /* (rejected: unacceptable address) */
        send_cmd(r, 0x040A, b, 7);
        kprintf("[BTH] %s wants to connect: refused (not done yet)\n", addr_text(p, t));
        break;
    case 0x17:                                                  /* Link Key Request */
        if (n < 6) break;
        d = dev_find(r, p);
        memcpy(b, p, 6);
        if (d && d->paired) { memcpy(b + 6, d->key, 16); send_cmd(r, 0x040B, b, 22); }
        else send_cmd(r, 0x040C, b, 6);
        break;
    case 0x18:                                                  /* Link Key Notification: paired */
        if (n < 23 || !(d = dev_find(r, p))) break;
        memcpy(d->key, p + 6, 16);
        d->paired = true;
        kprintf("[BTH] paired with %s (key type %u)\n", addr_text(d->addr, t), p[22]);
        break;
    case 0x16:                                                  /* PIN Code Request: 0000, as for most headsets */
        if (n < 6) break;
        memcpy(b, p, 6);
        b[6] = 4;
        memset(b + 7, 0, 16);
        memcpy(b + 7, "0000", 4);
        send_cmd(r, 0x040D, b, 23);
        kprintf("[BTH] %s asked for a PIN: 0000\n", addr_text(p, t));
        break;
    case 0x31:                                                  /* IO Capability Request: no display, no keyboard */
        if (n < 6) break;
        memcpy(b, p, 6);
        b[6] = 0x03;                                            /* NoInputNoOutput */
        b[7] = 0x00;                                            /* (no OOB data) */
        b[8] = 0x02;                                            /* dedicated bonding, no MITM: Just Works */
        send_cmd(r, 0x042B, b, 9);
        break;
    case 0x33:                                                  /* User Confirmation Request: Just Works, accepted */
        if (n < 6) break;
        memcpy(b, p, 6);
        send_cmd(r, 0x042C, b, 6);
        break;
    case 0x36:                                                  /* Simple Pairing Complete */
        if (n >= 7 && p[0]) kprintf("[BTH] %s: Simple Pairing failed (%02x)\n", addr_text(p + 1, t), p[0]);
        break;
    }
}

void BthRadioEvent(BthRadio *r, const UINT8 *evt, UINT32 len)
{
    if (r && len >= 2 && 2u + evt[1] <= len) on_event(r, evt[0], evt + 2, evt[1]);
}

void BthRadioAcl(BthRadio *r, const UINT8 *pkt, UINT32 len)
{
    if (!r || len < 4) return;
    UINT16 hf = le16(pkt), n = le16(pkt + 2);
    if (4u + n > len) return;
    BthDev *d = dev_by_handle(r, hf & 0x0FFF);
    if (!d) return;
    if (((hf >> 12) & 3) != 0x1) d->rx_len = 0;                 /* (a first segment) */
    if (d->rx_len + n > ACL_BUF) { d->rx_len = 0; return; }
    memcpy(d->rx + d->rx_len, pkt + 4, n);
    d->rx_len += n;
    while (d->rx_len >= 4 && d->rx_len >= 4u + le16(d->rx)) {
        UINT16 flen = le16(d->rx);
        l2_input(r, d, le16(d->rx + 2), d->rx + 4, flen);
        d->rx_len -= 4u + flen;
        memmove(d->rx, d->rx + 4 + flen, d->rx_len);
    }
}

/* ---- links ---- */

static NT_STATUS connect(BthRadio *r, BthDev *d)
{
    char t[18];
    if (d->conn == 2) return NT_STATUS_SUCCESS;
    UINT8 p[13];
    memcpy(p, d->addr, 6);
    put16(p + 6, 0xCC18);                                       /* DM1 DH1 DM3 DH3 DM5 DH5 */
    p[8] = d->psrm;
    p[9] = 0;
    put16(p + 10, d->clock);
    p[12] = 1;                                                  /* (role switch allowed) */
    d->conn = 1;
    int st = cmd(r, 0x0405, p, 13, NULL, 0);
    if (st) { d->conn = 0; kprintf("[BTH] %s: Create Connection failed (%02x)\n", addr_text(d->addr, t), st & 0xFF); return ST_DEVICE_NOT_CONNECTED; }
    if (!WAIT_FOR(r, d->conn != 1, 15000) || d->conn != 2) {
        kprintf("[BTH] %s: no connection (%02x)\n", addr_text(d->addr, t), d->conn_status);
        d->conn = 0;
        return ST_DEVICE_NOT_CONNECTED;
    }
    kprintf("[BTH] %s connected, handle %03x\n", addr_text(d->addr, t), d->handle);
    return NT_STATUS_SUCCESS;
}

static void disconnect(BthRadio *r, BthDev *d)
{
    if (d->conn != 2) return;
    UINT8 p[3];
    put16(p, d->handle);
    p[2] = 0x13;                                                /* (remote user terminated) */
    if (!cmd(r, 0x0406, p, 3, NULL, 0)) WAIT_FOR(r, d->conn != 2, 3000);
}

/* Authentication (pairing, when there is no key yet), then encryption */
static NT_STATUS secure(BthRadio *r, BthDev *d)
{
    char t[18];
    UINT8 p[3];
    put16(p, d->handle);
    d->auth = 1;
    int st = cmd(r, 0x0411, p, 2, NULL, 0);
    if (st || !WAIT_FOR(r, d->auth == 2, 30000) || d->auth_status) {
        kprintf("[BTH] %s: authentication failed (%02x)\n", addr_text(d->addr, t), st ? st & 0xFF : d->auth_status);
        return ST_ACCESS_DENIED;
    }
    p[2] = 1;
    d->enc = 1;
    st = cmd(r, 0x0413, p, 3, NULL, 0);
    if (st || !WAIT_FOR(r, d->enc == 2, 10000) || d->enc_status) {
        kprintf("[BTH] %s: encryption failed (%02x)\n", addr_text(d->addr, t), st ? st & 0xFF : d->enc_status);
        return ST_ACCESS_DENIED;
    }
    kprintf("[BTH] %s authenticated and encrypted\n", addr_text(d->addr, t));
    return NT_STATUS_SUCCESS;
}

/* ---- SDP ---- */

/* The data element at @p (@n bytes): its type, its data and their
 * length; the whole element's length, 0 if malformed */
static UINT32 de(const UINT8 *p, UINT32 n, UINT8 *type, const UINT8 **data, UINT32 *dlen)
{
    if (n < 1) return 0;
    UINT8 t = p[0] >> 3, si = p[0] & 7;
    UINT32 hdr = 1, len;
    if (si < 5) len = t == 0 ? 0 : (1u << si);
    else {
        UINT32 k = si == 5 ? 1 : si == 6 ? 2 : 4;
        if (n < 1 + k) return 0;
        len = k == 1 ? p[1] : k == 2 ? be16(p + 1) : be32(p + 1);
        hdr += k;
    }
    if (len > n - hdr) return 0;
    *type = t;
    *data = p + hdr;
    *dlen = len;
    return hdr + len;
}

static UINT32 de_uint(const UINT8 *p, UINT32 n)
{
    UINT32 v = 0;
    for (UINT32 i = 0; i < n && i < 4; i++) v = v << 8 | p[i];
    return v;
}

/* One service record (a sequence of attribute ID, value) into @d */
static void sdp_record(BthDev *d, const UINT8 *p, UINT32 n)
{
    UINT16 svc = 0;
    const UINT8 *hid = NULL;
    UINT32 hid_len = 0;
    UINT16 vid = 0, pid = 0, ver = 0;
    UINT8 country = 0;
    for (UINT32 o = 0; o < n; ) {
        UINT8 t, vt;
        const UINT8 *id, *v;
        UINT32 il, vl, a = de(p + o, n - o, &t, &id, &il);
        if (!a || t != 1 || il != 2) return;
        UINT16 attr = be16(id);
        o += a;
        UINT32 b = de(p + o, n - o, &vt, &v, &vl);
        if (!b) return;
        o += b;
        if (attr == 0x0001 && vt == 6) {                        /* ServiceClassIDList: its first class */
            UINT8 ut;
            const UINT8 *u;
            UINT32 ul;
            if (de(v, vl, &ut, &u, &ul) && ut == 3 && ul == 2) svc = be16(u);
            else if (ut == 3 && ul == 16 && !u[0] && !u[1]) svc = be16(u + 2);
        } else if (attr == 0x0206 && vt == 6) {                 /* HIDDescriptorList: { { 0x22, descriptor } } */
            UINT8 st, ct, rt;
            const UINT8 *s, *c, *rd;
            UINT32 sl, cl, rl, k;
            if (de(v, vl, &st, &s, &sl) && st == 6 && (k = de(s, sl, &ct, &c, &cl)) && ct == 1 && c[0] == 0x22 &&
                de(s + k, sl - k, &rt, &rd, &rl) && rt == 4) { hid = rd; hid_len = rl; }
        } else if (attr == 0x0203 && vt == 1) {                 /* HIDCountryCode, or (PnP) Version */
            country = (UINT8)de_uint(v, vl);
            ver = (UINT16)de_uint(v, vl);
        } else if (attr == 0x0201 && vt == 1) {
            vid = (UINT16)de_uint(v, vl);
        } else if (attr == 0x0202 && vt == 1) {
            pid = (UINT16)de_uint(v, vl);
        }
    }
    if (!svc) return;
    bool have = false;
    for (int i = 0; i < d->nsvc; i++) have |= d->svc[i] == svc;
    if (!have && d->nsvc < MAX_SVCS) d->svc[d->nsvc++] = svc;
    if (svc == UUID_HID && hid && hid_len && hid_len <= 1024) {
        UINT8 *copy = kmalloc(hid_len);
        if (copy) {
            memcpy(copy, hid, hid_len);
            kfree(d->rdesc);
            d->rdesc = copy;
            d->rdesc_len = (UINT16)hid_len;
            d->country = country;
        }
    } else if (svc == UUID_PNP) {
        d->vid = vid;
        d->pid = pid;
        d->version = ver;
    }
}

/* Every SDP record @d has (its ACL link up) */
static NT_STATUS sdp_query(BthRadio *r, BthDev *d)
{
    char t[18];
    BthChan *c = l2_open(r, d, 0x0001);
    if (!c) return ST_DEVICE_NOT_CONNECTED;
    UINT8 *resp = kmalloc(SDP_BUF), *all = kmalloc(SDP_BUF);
    NT_STATUS st = resp && all ? NT_STATUS_SUCCESS : NT_STATUS_INSUFFICIENT_RESOURCES;
    UINT32 total = 0;
    UINT8 cont[17] = { 0 };
    c->sdp = resp;
    for (UINT16 tid = 1; NT_SUCCESS(st) && tid < 32; tid++) {
        static const UINT8 pattern[] = { 0x35, 0x03, 0x19, 0x01, 0x00 };        /* (records with L2CAP: all) */
        static const UINT8 attrs[] = { 0x35, 0x05, 0x0A, 0x00, 0x00, 0xFF, 0xFF };
        UINT8 req[64];
        UINT32 n = 5;
        memcpy(req + n, pattern, sizeof(pattern)); n += sizeof(pattern);
        req[n++] = (UINT8)((OUR_MTU - 16) >> 8);                /* MaximumAttributeByteCount: what our MTU holds */
        req[n++] = (UINT8)(OUR_MTU - 16);
        memcpy(req + n, attrs, sizeof(attrs)); n += sizeof(attrs);
        memcpy(req + n, cont, 1u + cont[0]); n += 1u + cont[0];
        req[0] = 0x06;
        req[1] = (UINT8)(tid >> 8); req[2] = (UINT8)tid;
        req[3] = (UINT8)((n - 5) >> 8); req[4] = (UINT8)(n - 5);
        c->sdp_len = 0;
        st = l2_send(r, c, req, n);
        if (!NT_SUCCESS(st)) break;
        if (!WAIT_FOR(r, __atomic_load_n(&c->sdp_len, __ATOMIC_ACQUIRE) > 0 || c->state != 3, 5000) || !c->sdp_len) {
            st = ST_IO_TIMEOUT;
            break;
        }
        UINT32 got = c->sdp_len;
        if (got < 7 || resp[0] != 0x07 || be16(resp + 1) != tid) { st = NT_STATUS_UNSUCCESSFUL; break; }
        UINT16 bytes = be16(resp + 5);
        if (7u + bytes + 1 > got || total + bytes > SDP_BUF) { st = NT_STATUS_UNSUCCESSFUL; break; }
        memcpy(all + total, resp + 7, bytes);
        total += bytes;
        UINT8 cl = resp[7 + bytes];
        if (cl > 16 || 7u + bytes + 1 + cl > got) { st = NT_STATUS_UNSUCCESSFUL; break; }
        cont[0] = cl;
        memcpy(cont + 1, resp + 8 + bytes, cl);
        if (!cl) break;
    }
    c->sdp = NULL;
    l2_close(r, c);
    if (NT_SUCCESS(st)) {                                       /* AttributeLists: a sequence of records */
        UINT8 ty;
        const UINT8 *recs;
        UINT32 rl;
        if (!de(all, total, &ty, &recs, &rl) || ty != 6) st = NT_STATUS_UNSUCCESSFUL;
        d->nsvc = 0;
        for (UINT32 o = 0; NT_SUCCESS(st) && o < rl; ) {
            const UINT8 *rec;
            UINT32 len, k = de(recs + o, rl - o, &ty, &rec, &len);
            if (!k) break;
            if (ty == 6) sdp_record(d, rec, len);
            o += k;
        }
        kprintf("[BTH] %s: %d service(s) by SDP", addr_text(d->addr, t), d->nsvc);
        for (int i = 0; i < d->nsvc; i++) kprintf(" %04x", d->svc[i]);
        if (d->rdesc_len) kprintf(", HID report descriptor %u bytes", d->rdesc_len);
        if (d->vid || d->pid) kprintf(", device %04x:%04x", d->vid, d->pid);
        kprintf("\n");
    } else {
        kprintf("[BTH] %s: SDP failed (%08x)\n", addr_text(d->addr, t), (UINT32)st);
    }
    kfree(resp);
    kfree(all);
    return st;
}

/* ---- the services' nodes ---- */

static SvcPdo *svc(NT_DEVICE_OBJECT *pdo) { return pdo ? pdo->DeviceExtension : NULL; }

bool BthIsServicePdo(NT_DEVICE_OBJECT *d)
{
    return d && d->DeviceExtension && svc(d)->magic == SVC_MAGIC;
}

/* HID's node for @d, made (and the bus told) when the service is on */
static void hid_node(BthRadio *r, BthDev *d)
{
    char t[18];
    if (d->hid_on && !d->hid_pdo) {
        NT_DEVICE_OBJECT *pdo;
        if (!NT_SUCCESS(nt_IoCreateDevice(r->fdo->DriverObject, sizeof(SvcPdo), NULL, FILE_DEVICE_BLUETOOTH,
                                          0x100 /* AUTOGENERATED_NAME */, 0, &pdo)))
            return;
        SvcPdo *s = svc(pdo);
        memset(s, 0, sizeof(*s));
        s->magic = SVC_MAGIC;
        s->r = r;
        s->dev = d;
        s->uuid = UUID_HID;
        s->listed = true;
        pdo->Flags &= ~NT_DO_DEVICE_INITIALIZING;
        d->hid_pdo = pdo;
        kprintf("[BTH] %s: HID service on\n", addr_text(d->addr, t));
    } else if (!d->hid_on && d->hid_pdo) {
        svc(d->hid_pdo)->listed = false;                        /* (its remove deletes it) */
        d->hid_pdo = NULL;
        kprintf("[BTH] %s: HID service off\n", addr_text(d->addr, t));
    } else {
        return;
    }
    nt_IoInvalidateDeviceRelations(r->pdo, 0);
}

NT_STATUS BthRadioRelations(BthRadio *r, NT_ULONG_PTR *info)
{
    int n = 0;
    for (int i = 0; i < MAX_DEVS; i++) if (r->dev[i].used && r->dev[i].hid_pdo && !r->gone) n++;
    struct { UINT32 Count; NT_DEVICE_OBJECT *Objects[MAX_DEVS]; } *rel =
        nt_ExAllocatePoolWithTag(1, sizeof(UINT64) + sizeof(void *) * (n ? (UINT64)n : 1), 0x20485442);
    if (!rel) return NT_STATUS_INSUFFICIENT_RESOURCES;
    rel->Count = 0;
    for (int i = 0; i < MAX_DEVS && !r->gone; i++) if (r->dev[i].used && r->dev[i].hid_pdo) rel->Objects[rel->Count++] = r->dev[i].hid_pdo;
    *info = (NT_ULONG_PTR)rel;
    return NT_STATUS_SUCCESS;
}

static NT_WCHAR *pool_wide(const char *s, UINT32 len)       /* (@len: with any embedded NULs) */
{
    NT_WCHAR *w = nt_ExAllocatePoolWithTag(1, 2 * ((UINT64)len + 2), 0x20485442);
    if (!w) return NULL;
    for (UINT32 i = 0; i < len; i++) w[i] = (UINT8)s[i];
    w[len] = w[len + 1] = 0;
    return w;
}

static void done(NT_IRP *irp, NT_STATUS st, NT_ULONG_PTR info)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    nt_IofCompleteRequest(irp, 0);
}

#define HID_UUID_TEXT "{00001124-0000-1000-8000-00805f9b34fb}"

NT_STATUS BthServiceDispatch(NT_DEVICE_OBJECT *pdo, NT_IRP *irp)
{
    SvcPdo *s = svc(pdo);
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    NT_STATUS st = irp->IoStatus.Status;
    NT_ULONG_PTR info = irp->IoStatus.Information;
    if (sl->MajorFunction == NT_IRP_MJ_POWER) {
        nt_PoStartNextPowerIrp(irp);
        done(irp, NT_STATUS_SUCCESS, 0);
        return NT_STATUS_SUCCESS;
    }
    if (sl->MajorFunction == NT_IRP_MJ_CREATE || sl->MajorFunction == NT_IRP_MJ_CLOSE || sl->MajorFunction == NT_IRP_MJ_CLEANUP) {
        done(irp, NT_STATUS_SUCCESS, 0);
        return NT_STATUS_SUCCESS;
    }
    if (sl->MajorFunction != NT_IRP_MJ_PNP) {
        done(irp, NT_STATUS_INVALID_DEVICE_REQUEST, 0);
        return NT_STATUS_INVALID_DEVICE_REQUEST;
    }
    BthDev *d = s->dev;
    char buf[200], a[13];
    addr_hex(d->addr, a);
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE:
    case NT_IRP_MN_QUERY_REMOVE_DEVICE:
    case NT_IRP_MN_CANCEL_REMOVE_DEVICE:
    case NT_IRP_MN_QUERY_STOP_DEVICE:
    case NT_IRP_MN_CANCEL_STOP_DEVICE:
    case NT_IRP_MN_STOP_DEVICE:
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_SURPRISE_REMOVAL:
        s->gone = true;
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_REMOVE_DEVICE:
        done(irp, NT_STATUS_SUCCESS, 0);
        if (!s->listed || s->gone || s->r->gone) {              /* (the service went off, or the radio is going) */
            if (d->hid_pdo == pdo) d->hid_pdo = NULL;
            s->magic = 0;
            nt_IoDeleteDevice(pdo);
        }
        return NT_STATUS_SUCCESS;
    case NT_IRP_MN_QUERY_CAPABILITIES: {
        NT_DEVICE_CAPABILITIES *c = sl->Parameters.Others.Argument1;
        if (!c || c->Size < sizeof(*c)) { st = NT_STATUS_INVALID_PARAMETER; break; }
        c->Flags |= 0x10 | 0x80;                                /* Removable, SurpriseRemovalOK */
        c->Address = 0xFFFFFFFFu;
        c->UINumber = 0xFFFFFFFFu;
        c->DeviceState[1] = 1;
        for (int i = 2; i < 7; i++) c->DeviceState[i] = 4;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_ID: {
        int type = (int)(UINT64)sl->Parameters.Others.Argument1;
        char id[96];
        if (d->vid || d->pid) ksnprintf(id, sizeof(id), "BTHENUM\\" HID_UUID_TEXT "_VID&0002%04x_PID&%04x", d->vid, d->pid);
        else ksnprintf(id, sizeof(id), "BTHENUM\\" HID_UUID_TEXT "_LOCALMFG&%04x", s->r->mfg);
        UINT32 n;
        if (type == 0) n = (UINT32)ksnprintf(buf, sizeof(buf), "%s", id);
        else if (type == 1) {
            n = (UINT32)ksnprintf(buf, sizeof(buf), "%s", id) + 1;
            n += (UINT32)ksnprintf(buf + n, sizeof(buf) - n, "BTHENUM\\" HID_UUID_TEXT) + 1;
        } else if (type == 2) n = (UINT32)ksnprintf(buf, sizeof(buf), "BTHENUM\\" HID_UUID_TEXT) + 1;
        else if (type == 3) n = (UINT32)ksnprintf(buf, sizeof(buf), "%s_C00000000", a);
        else break;
        NT_WCHAR *w = pool_wide(buf, n);
        if (!w) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
        info = (NT_ULONG_PTR)w;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_DEVICE_TEXT: {
        int type = (int)(UINT64)sl->Parameters.Others.Argument1;
        if (type > 1) break;
        UINT32 n = (UINT32)ksnprintf(buf, sizeof(buf), "%s", type ? "Bluetooth" : d->named ? d->name : "Bluetooth HID Device");
        NT_WCHAR *w = pool_wide(buf, n);
        if (!w) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
        info = (NT_ULONG_PTR)w;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_DEVICE_RELATIONS:
        if ((int)(UINT64)sl->Parameters.Others.Argument1 == 4) {   /* TargetDeviceRelation */
            struct { UINT32 Count; NT_DEVICE_OBJECT *Objects[1]; } *rel = nt_ExAllocatePoolWithTag(1, 16, 0x20485442);
            if (!rel) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
            rel->Count = 1;
            rel->Objects[0] = pdo;
            info = (NT_ULONG_PTR)rel;
            st = NT_STATUS_SUCCESS;
        }
        break;
    }
    done(irp, st, info);
    return st;
}

/* ---- HID's driver ---- */

static SvcPdo *hid_svc(NT_DEVICE_OBJECT *pdo)
{
    return BthIsServicePdo(pdo) && svc(pdo)->uuid == UUID_HID ? svc(pdo) : NULL;
}

bool BthHidGetInfo(NT_DEVICE_OBJECT *pdo, BthHidInfo *out)
{
    SvcPdo *s = hid_svc(pdo);
    if (!s || !s->dev->rdesc_len) return false;
    memset(out, 0, sizeof(*out));
    out->vid = s->dev->vid;
    out->pid = s->dev->pid;
    out->version = s->dev->version;
    out->country = s->dev->country;
    out->rdesc = s->dev->rdesc;
    out->rdesc_len = s->dev->rdesc_len;
    strncpy(out->name, s->dev->name, sizeof(out->name) - 1);
    return true;
}

NT_STATUS BthHidConnect(NT_DEVICE_OBJECT *pdo, BthHidInput input, void *ctx)
{
    SvcPdo *s = hid_svc(pdo);
    if (!s) return NT_STATUS_INVALID_PARAMETER;
    BthRadio *r = s->r;
    BthDev *d = s->dev;
    char t[18];
    if (!r->up || r->gone) return ST_DEVICE_NOT_CONNECTED;
    if (s->intr && s->intr->state == 3) return NT_STATUS_SUCCESS;
    BthHidDisconnect(pdo);
    s->input = input;
    s->ctx = ctx;
    NT_STATUS st = connect(r, d);
    if (NT_SUCCESS(st)) st = secure(r, d);
    if (NT_SUCCESS(st) && !(s->ctrl = l2_open(r, d, 0x0011))) st = ST_DEVICE_NOT_CONNECTED;
    if (NT_SUCCESS(st)) {
        s->intr = l2_open(r, d, 0x0013);
        if (s->intr) { s->intr->ctx = ctx; s->intr->rx = input; }
        else st = ST_DEVICE_NOT_CONNECTED;
    }
    if (!NT_SUCCESS(st)) {
        BthHidDisconnect(pdo);
        disconnect(r, d);
        return st;
    }
    kprintf("[BTH] %s: HID control and interrupt channels open\n", addr_text(d->addr, t));
    return NT_STATUS_SUCCESS;
}

NT_STATUS BthHidSend(NT_DEVICE_OBJECT *pdo, bool intr, const UINT8 *data, UINT32 len)
{
    SvcPdo *s = hid_svc(pdo);
    if (!s) return NT_STATUS_INVALID_PARAMETER;
    return l2_send(s->r, intr ? s->intr : s->ctrl, data, len);
}

void BthHidDisconnect(NT_DEVICE_OBJECT *pdo)
{
    SvcPdo *s = hid_svc(pdo);
    if (!s) return;
    if (s->intr) { s->intr->rx = NULL; l2_close(s->r, s->intr); s->intr = NULL; }
    if (s->ctrl) { l2_close(s->r, s->ctrl); s->ctrl = NULL; }
}

bool BthHidConnected(NT_DEVICE_OBJECT *pdo)
{
    SvcPdo *s = hid_svc(pdo);
    return s && s->intr && s->intr->state == 3 && s->ctrl && s->ctrl->state == 3 && !s->r->gone;
}

/* ---- the radio ---- */

static void radio_up(void *arg)
{
    BthRadio *r = arg;
    UINT8 ret[256], p[248];
    char t[18];
    int st = cmd(r, 0x0C03, NULL, 0, NULL, 0);                  /* Reset */
    if (st) { kprintf("[BTH] the radio did not reset (%d)\n", st); return; }
    if (!cmd(r, 0x1001, NULL, 0, ret, sizeof(ret))) {            /* Read Local Version Information */
        r->hci_ver = ret[1]; r->hci_rev = le16(ret + 2); r->lmp_ver = ret[4]; r->mfg = le16(ret + 5); r->lmp_sub = le16(ret + 7);
    }
    if (!cmd(r, 0x1003, NULL, 0, ret, sizeof(ret))) memcpy(r->features, ret + 1, 8);
    r->acl_mtu = 27;
    r->acl_free = 1;
    if (!cmd(r, 0x1005, NULL, 0, ret, sizeof(ret))) {            /* Read Buffer Size */
        r->acl_mtu = le16(ret + 1) ? le16(ret + 1) : 27;
        r->acl_free = le16(ret + 4) ? le16(ret + 4) : 1;
    }
    if (cmd(r, 0x1009, NULL, 0, ret, sizeof(ret))) { kprintf("[BTH] no BD_ADDR\n"); return; }
    memcpy(r->addr, ret + 1, 6);
    static const UINT8 mask[8] = { 0xFF, 0xFF, 0xFB, 0xFF, 0x07, 0xF8, 0xBF, 0x3D };
    cmd(r, 0x0C01, mask, 8, NULL, 0);                           /* Set Event Mask */
    memset(p, 0, sizeof(p));
    um_registry_get_sz("Machine\\SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ComputerName", "ComputerName", (char *)p, 64);
    if (!p[0]) strcpy((char *)p, "NOVAOS-PC");
    strncpy(r->name, (const char *)p, sizeof(r->name) - 1);
    cmd(r, 0x0C13, p, 248, NULL, 0);                            /* Write Local Name */
    static const UINT8 cod[3] = { 0x0C, 0x01, 0x1C };            /* computer, laptop; networking, transfer, telephony */
    cmd(r, 0x0C24, cod, 3, NULL, 0);
    p[0] = 1;
    if (r->features[6] & 0x08) cmd(r, 0x0C56, p, 1, NULL, 0);    /* Write Simple Pairing Mode: on */
    p[0] = 1;
    cmd(r, 0x0C45, p, 1, NULL, 0);                              /* Write Inquiry Mode: with RSSI */
    r->scan = 0x02;
    cmd(r, 0x0C1A, &r->scan, 1, NULL, 0);                       /* Write Scan Enable: page scan */
    lock(r);
    devs_load(r);
    unlock(r);
    r->up = true;
    kprintf("[BTH] radio %s up: HCI %u.%u, LMP %u, manufacturer %04x, ACL %u x %d, \"%s\"\n",
            addr_text(r->addr, t), r->hci_ver, r->hci_rev, r->lmp_ver, r->mfg, r->acl_mtu, r->acl_free, r->name);
    if (r->iface.Buffer) nt_IoSetDeviceInterfaceState(&r->iface, 1);
    for (int i = 0; i < MAX_DEVS; i++) if (r->dev[i].used) hid_node(r, &r->dev[i]);
}

BthRadio *BthRadioCreate(NT_DEVICE_OBJECT *fdo, NT_DEVICE_OBJECT *pdo, const BthTransport *t, void *ctx)
{
    BthRadio *r = kzalloc(sizeof(*r));
    if (!r) return NULL;
    r->fdo = fdo;
    r->pdo = pdo;
    r->t = *t;
    r->ctx = ctx;
    r->next_cid = 0x40;
    nt_KeInitializeEvent(&r->cmd_lock, NT_SynchronizationEvent, 1);
    nt_KeInitializeEvent(&r->lock, NT_SynchronizationEvent, 1);
    if (!NT_SUCCESS(nt_IoRegisterDeviceInterface(pdo, &g_radio_iface, NULL, &r->iface))) r->iface.Buffer = NULL;
    return r;
}

void BthRadioStart(BthRadio *r)
{
    if (r && !sched_create_thread_ex("bth-up", radio_up, r, PRIO_DEVICE_IO, 16 * 1024)) kprintf("[BTH] no thread\n");
}

void BthRadioStop(BthRadio *r)
{
    if (!r) return;
    r->gone = true;
    r->up = false;
    if (r->iface.Buffer) nt_IoSetDeviceInterfaceState(&r->iface, 0);
    for (int i = 0; i < MAX_DEVS; i++) r->dev[i].conn = 0;
}

void BthRadioFree(BthRadio *r)
{
    if (!r) return;
    for (int i = 0; i < MAX_DEVS; i++) kfree(r->dev[i].rdesc);   /* (the services' PDOs went first, as children do) */
    if (r->iface.Buffer) nt_ExFreePool(r->iface.Buffer);
    kfree(r);
}

/* ---- the radio's control codes ---- */

static void dev_info(const BthDev *d, NovaBthDeviceInfo *o)
{
    memset(o, 0, sizeof(*o));
    o->flags = NOVABTH_BDIF_ADDRESS | NOVABTH_BDIF_COD | (d->named ? NOVABTH_BDIF_NAME : 0) |
               (d->paired ? NOVABTH_BDIF_PAIRED | NOVABTH_BDIF_PERSONAL : 0) | (d->conn == 2 ? NOVABTH_BDIF_CONNECTED : 0);
    o->address = addr_u64(d->addr);
    o->classOfDevice = d->cod;
    strncpy(o->name, d->name, sizeof(o->name) - 1);
}

static NT_STATUS inquiry(BthRadio *r, UINT32 length, UINT32 *count)
{
    if (length < 1 || length > 48) length = 8;
    lock(r);
    for (int i = 0; i < MAX_DEVS; i++) r->dev[i].found = false;
    unlock(r);
    static const UINT8 giac[3] = { 0x33, 0x8B, 0x9E };
    UINT8 p[5];
    memcpy(p, giac, 3);
    p[3] = (UINT8)length;
    p[4] = 0;                                                   /* (unlimited responses) */
    r->inquiring = true;
    int st = cmd(r, 0x0401, p, 5, NULL, 0);
    if (st) { r->inquiring = false; return NT_STATUS_UNSUCCESSFUL; }
    WAIT_FOR(r, !r->inquiring, length * 1280 + 2000);
    if (r->inquiring) { cmd(r, 0x0402, NULL, 0, NULL, 0); r->inquiring = false; }
    UINT32 n = 0;
    for (int i = 0; i < MAX_DEVS; i++) {
        BthDev *d = &r->dev[i];
        if (!d->used || !d->found) continue;
        n++;
        if (d->named) continue;
        UINT8 q[10];
        memcpy(q, d->addr, 6);
        q[6] = d->psrm;
        q[7] = 0;
        put16(q + 8, d->clock);
        d->name_req = 1;
        if (!cmd(r, 0x0419, q, 10, NULL, 0)) WAIT_FOR(r, d->name_req == 2, 10000);
    }
    char t[18];
    kprintf("[BTH] radio %s: inquiry found %u device(s)\n", addr_text(r->addr, t), n);
    *count = n;
    return NT_STATUS_SUCCESS;
}

static NT_STATUS pair(BthRadio *r, BthDev *d)
{
    NT_STATUS st = connect(r, d);
    if (NT_SUCCESS(st)) st = secure(r, d);
    if (NT_SUCCESS(st) && !d->paired) st = ST_ACCESS_DENIED;
    if (NT_SUCCESS(st)) sdp_query(r, d);
    if (NT_SUCCESS(st)) {
        lock(r);
        dev_save(r, d);
        unlock(r);
    }
    if (!d->hid_pdo) disconnect(r, d);                          /* (HID keeps it: its driver's channels) */
    return st;
}

static NT_STATUS forget(BthRadio *r, BthDev *d)
{
    char path[160], a[13];
    if (d->hid_pdo) {
        BthHidDisconnect(d->hid_pdo);
        d->hid_on = false;
        hid_node(r, d);
    }
    disconnect(r, d);
    dev_key_path(r, path, sizeof(path));
    addr_hex(d->addr, a);
    um_registry_delete_value(path, a);
    dev_path(d, path, sizeof(path));
    um_registry_delete_tree(path);
    kprintf("[BTH] %s forgotten\n", addr_text(d->addr, path));
    lock(r);
    kfree(d->rdesc);
    memset(d, 0, sizeof(*d));
    unlock(r);
    return NT_STATUS_SUCCESS;
}

NT_STATUS BthRadioIoctl(BthRadio *r, NT_IRP *irp, NT_IO_STACK_LOCATION *sl, NT_ULONG_PTR *info)
{
    UINT32 code = sl->Parameters.DeviceIoControl.IoControlCode;
    UINT32 in_len = sl->Parameters.DeviceIoControl.InputBufferLength;
    UINT32 out_len = sl->Parameters.DeviceIoControl.OutputBufferLength;
    UINT8 *sys = irp->AssociatedIrp.SystemBuffer;
    if (!r || !r->up || r->gone) return ST_DEVICE_NOT_CONNECTED;
    BthDev *d = NULL;
    if ((code == IOCTL_NOVABTH_PAIR || code == IOCTL_NOVABTH_REMOVE || code == IOCTL_NOVABTH_SERVICES ||
         code == IOCTL_BTH_DISCONNECT_DEVICE || code == IOCTL_NOVABTH_SET_SERVICE)) {
        if (!sys || in_len < 8) return NT_STATUS_INVALID_PARAMETER;
        UINT8 a[6];
        UINT64 v;
        memcpy(&v, sys, 8);
        addr_from(v, a);
        if (!(d = dev_find(r, a))) return ST_NOT_FOUND;
    }
    switch (code) {
    case IOCTL_BTH_GET_LOCAL_INFO: {
        if (!sys || out_len < sizeof(NovaBthLocalRadioInfo)) return NT_STATUS_BUFFER_TOO_SMALL;
        NovaBthLocalRadioInfo *o = (NovaBthLocalRadioInfo *)sys;
        memset(o, 0, sizeof(*o));
        o->localInfo.flags = NOVABTH_BDIF_ADDRESS | NOVABTH_BDIF_COD | NOVABTH_BDIF_NAME;
        o->localInfo.address = addr_u64(r->addr);
        o->localInfo.classOfDevice = 0x1C010C;
        strncpy(o->localInfo.name, r->name, sizeof(o->localInfo.name) - 1);
        o->flags = (r->scan & 1 ? NOVABTH_RADIO_DISCOVERABLE : 0) | (r->scan & 2 ? NOVABTH_RADIO_CONNECTABLE : 0);
        o->hciVersion = r->hci_ver;
        o->hciRevision = r->hci_rev;
        UINT64 f = 0;
        for (int i = 7; i >= 0; i--) f = f << 8 | r->features[i];
        o->radioInfo.lmpSupportedFeatures = f;
        o->radioInfo.mfg = r->mfg;
        o->radioInfo.lmpSubversion = r->lmp_sub;
        o->radioInfo.lmpVersion = r->lmp_ver;
        *info = sizeof(*o);
        return NT_STATUS_SUCCESS;
    }
    case IOCTL_BTH_GET_DEVICE_INFO: {
        if (!sys || out_len < sizeof(UINT32)) return NT_STATUS_BUFFER_TOO_SMALL;
        NovaBthDeviceInfoList *o = (NovaBthDeviceInfoList *)sys;
        UINT32 room = out_len >= 8 ? (out_len - 8) / (UINT32)sizeof(NovaBthDeviceInfo) : 0, n = 0, k = 0;
        lock(r);
        for (int i = 0; i < MAX_DEVS; i++) {
            BthDev *e = &r->dev[i];
            if (!e->used || !(e->paired || e->found)) continue;
            if (k < room) dev_info(e, &o->deviceList[k++]);
            n++;
        }
        unlock(r);
        o->numOfDevices = n;
        *info = k ? 8 + k * sizeof(NovaBthDeviceInfo) : sizeof(UINT32);
        return k < n ? NT_STATUS_BUFFER_OVERFLOW : NT_STATUS_SUCCESS;
    }
    case IOCTL_BTH_DISCONNECT_DEVICE:
        if (d->hid_pdo) BthHidDisconnect(d->hid_pdo);
        disconnect(r, d);
        return NT_STATUS_SUCCESS;
    case IOCTL_NOVABTH_INQUIRY: {
        if (!sys || out_len < sizeof(UINT32)) return NT_STATUS_BUFFER_TOO_SMALL;
        UINT32 n = 0, len = in_len >= sizeof(NovaBthInquiry) ? ((NovaBthInquiry *)sys)->length : 8;
        NT_STATUS st = inquiry(r, len, &n);
        *(UINT32 *)sys = n;
        *info = sizeof(UINT32);
        return st;
    }
    case IOCTL_NOVABTH_PAIR:
        return pair(r, d);
    case IOCTL_NOVABTH_SERVICES: {
        if (out_len < sizeof(NovaBthServices)) return NT_STATUS_BUFFER_TOO_SMALL;
        NovaBthServices *o = (NovaBthServices *)sys;
        memset(o, 0, sizeof(*o));
        for (int i = 0; i < d->nsvc; i++) {
            o->uuid[i] = d->svc[i];
            o->enabled[i] = d->svc[i] == UUID_HID && d->hid_on;
        }
        o->count = (UINT32)d->nsvc;
        *info = sizeof(*o);
        return NT_STATUS_SUCCESS;
    }
    case IOCTL_NOVABTH_SET_SERVICE: {
        if (in_len < sizeof(NovaBthSetService)) return NT_STATUS_INVALID_PARAMETER;
        NovaBthSetService *q = (NovaBthSetService *)sys;
        bool has = false;
        for (int i = 0; i < d->nsvc; i++) has |= d->svc[i] == q->uuid;
        if (!has || !d->paired) return ST_NOT_FOUND;
        if (q->uuid != UUID_HID) return NT_STATUS_NOT_SUPPORTED;   /* (no driver for the others yet) */
        if (!d->rdesc_len) return NT_STATUS_UNSUCCESSFUL;
        if (!q->enable && d->hid_pdo) BthHidDisconnect(d->hid_pdo);
        d->hid_on = q->enable != 0;
        lock(r);
        dev_save(r, d);
        unlock(r);
        hid_node(r, d);
        if (!d->hid_on) disconnect(r, d);
        return NT_STATUS_SUCCESS;
    }
    case IOCTL_NOVABTH_REMOVE:
        return forget(r, d);
    case IOCTL_NOVABTH_SET_SCAN: {
        if (!sys || in_len < 4) return NT_STATUS_INVALID_PARAMETER;
        UINT8 scan = (UINT8)(*(UINT32 *)sys & 3);
        if (cmd(r, 0x0C1A, &scan, 1, NULL, 0)) return NT_STATUS_UNSUCCESSFUL;
        r->scan = scan;
        return NT_STATUS_SUCCESS;
    }
    }
    return NT_STATUS_INVALID_DEVICE_REQUEST;
}
