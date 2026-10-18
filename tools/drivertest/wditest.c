/*
 * wditest.sys — a WDI Wi-Fi miniport for QEMU's edu device, NovaOS's test
 * of the WDI command layer (roadmap step 28.1, kernel/nt/wdi.c)
 *
 * QEMU has no Wi-Fi card, and Intel's Netwtw14.sys stops at its hardware
 * probe on a stand-in (the devices suite's ax211 boot), so this driver is
 * the Wi-Fi card: a WDI lower edge as Intel's is, registered with
 * NdisMRegisterWdiMiniportDriver, whose radio is a table of three access
 * points.  It answers the WDI messages NovaOS's upper edge sends as an IHV
 * driver must: OID requests of type NdisRequestMethod whose buffer is a
 * WDI_MESSAGE_HEADER and TLVs (kernel/nt/wdi.h), the answer written into
 * the same buffer; a task (create port, scan, connect, disconnect, delete
 * port) is finished by
 * an NDIS_STATUS_WDI_INDICATION_*_COMPLETE status indication with the
 * task's TransactionId, from a timer, after the OID request; a scan pends
 * its OID request, indicates the access points it "hears" (WDI_TLV_BSS_ENTRY:
 * BSSID, signal, channel and the beacon's frame body), completes the
 * request with NdisMOidRequestComplete and then indicates the scan's end.
 *
 * Its data path is a WDI TAL's (MiniportWdiTalTxRxInitialize's handlers):
 * NovaOS's TxMgr says frames are queued (TxDataSend) and the driver takes
 * them (TxDequeueIndication), "transmits" them and gives them back
 * (TxTransferCompleteIndication); frames it "receives" it indicates
 * (RxInorderDataIndication, from a timer as from an interrupt), the upper
 * edge takes them (RxGetMpdus) and returns them (RxReturnFrames).  The open
 * network, NovaGuest, connects (OID_WDI_TASK_CONNECT: the driver indicates
 * its peer, the access point, and the association's result); behind it is
 * the smallest of routers, at 192.168.77.1, which the driver plays itself:
 * it answers ARP for that address, offers and acknowledges 192.168.77.100
 * by DHCP, and answers pings.  So DHCP and ping in NovaOS go through the
 * whole data path, 802.11 frames both ways.
 *
 * NovaNet is WPA2-Personal (passphrase "nova-wpa2-passphrase"): the driver
 * is its access point's authenticator too (tools/drivertest/wpaap.h).  It
 * wants EAPOL exempted from encryption before the connection
 * (OID_WDI_SET_PRIVACY_EXEMPTION_LIST), associates, sends message 1 of the
 * 4-way handshake, checks message 2's MIC (a wrong passphrase stops there:
 * no message 3) and RSN element, sends message 3 with the group key, and
 * after message 4 wants the keys it derived to come back in
 * OID_WDI_SET_ADD_CIPHER_KEYS.  Until then it drops the station's data
 * frames; after, the frames it "receives" are marked Protected, as a
 * driver indicates frames it decrypted.  After the first ping it answers
 * it replaces the group key (the group key handshake) and checks the new
 * one comes back too.
 *
 * Each check it makes of what it is sent goes to the debugger (DbgPrint),
 * where the wifi self-test reads it.
 *
 * Built by tools/drivertest/mksys.py; the wifi self-test installs it with
 * "pnputil /add-driver wditest.inf /install", scans with
 * "netsh wlan show networks mode=bssid" and connects with
 * "netsh wlan connect name=NovaGuest" (and NovaNet with key=).
 */
#include "../../kernel/nt/ndis.h"
#include "../../kernel/nt/wdi.h"
#include "wpaap.h"

#define IMPORT __declspec(dllimport) NT_API
IMPORT unsigned int DbgPrint(const char *fmt, ...);
IMPORT NDIS_STATUS NdisMRegisterWdiMiniportDriver(NT_DRIVER_OBJECT *obj, NT_UNICODE_STRING *reg, void *ctx,
                                                  NDIS_MINIPORT_DRIVER_CHARACTERISTICS *ch,
                                                  NDIS_MINIPORT_DRIVER_WDI_CHARACTERISTICS *wdi, void **out);
IMPORT void        NdisMDeregisterWdiMiniportDriver(void *h);
IMPORT void       *NdisAllocateMemoryWithTagPriority(void *h, unsigned int len, unsigned int tag, int prio);
IMPORT void        NdisFreeMemoryWithTagPriority(void *h, void *p, unsigned int tag);
IMPORT void        NdisMIndicateStatusEx(void *h, NDIS_STATUS_INDICATION *ind);
IMPORT void        NdisMOidRequestComplete(void *h, NDIS_OID_REQUEST *r, NDIS_STATUS st);
IMPORT void       *NdisAllocateNetBufferListPool(void *h, NET_BUFFER_LIST_POOL_PARAMETERS *p);
IMPORT void        NdisFreeNetBufferListPool(void *pool);
IMPORT NET_BUFFER_LIST *NdisAllocateNetBufferAndNetBufferList(void *pool, unsigned short ctx, unsigned short backfill,
                                                              NT_MDL *mdl, unsigned int off, unsigned long long len);
IMPORT void        NdisFreeNetBufferList(NET_BUFFER_LIST *nbl);

typedef void (NT_API *TIMER_FN)(void *ss1, void *ctx, void *ss2, void *ss3);
typedef struct {
    NDIS_OBJECT_HEADER Header;
    unsigned int       AllocationTag;
    TIMER_FN           TimerFunction;
    void              *FunctionContext;
} TIMER_CHARACTERISTICS;
IMPORT NDIS_STATUS NdisAllocateTimerObject(void *h, TIMER_CHARACTERISTICS *ch, void **out);
IMPORT unsigned char NdisSetTimerObject(void *t, long long due, unsigned int period_ms, void *ctx);
IMPORT unsigned char NdisCancelTimerObject(void *t);
IMPORT void        NdisFreeTimerObject(void *t);

typedef void (NT_API *COMPLETE_FN)(void *adapter_handle, NDIS_STATUS st);

#define TAG       0x54494457u       /* 'WDIT' */
#define WDI_VER   ((1 << 16) | (1 << 8) | 8)
#define PORT      1
#define NDIS_OBJECT_TYPE_TIMER_CHARACTERISTICS 0x97

static void *g_driver;

/* ---- the radio: three access points ---- */
static const unsigned char MAC[6] = { 0x02, 0x4e, 0x6f, 0x76, 0x61, 0x00 };

typedef struct {
    unsigned char  bssid[6];
    const char    *ssid;
    int            rssi;
    unsigned int   quality, channel, band;
    unsigned short capability;
    const unsigned char *ies;       /* after the SSID element */
    unsigned int   ies_len;
} Ap;

/* Supported rates, DS parameter set (channel 6), RSN (CCMP, PSK), HT capabilities */
static const unsigned char IES_WPA2[] = {
    1, 8, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
    3, 1, 6,
    48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 2, 0x0c, 0x00,
    45, 26, 0x6f, 0x01, 0x17, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
/* Rates, RSN (CCMP, SAE, management frame protection required), HT, VHT, HE */
static const unsigned char IES_WPA3[] = {
    1, 8, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
    48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 8, 0xc0, 0x00,
    45, 26, 0xef, 0x09, 0x17, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    191, 12, 0x91, 0x59, 0x82, 0x0f, 0xea, 0xff, 0, 0, 0xea, 0xff, 0, 0,
    255, 7, 35, 0x05, 0x00, 0x08, 0x12, 0x00, 0x10,
};
/* Rates, DS parameter set (channel 11), HT: an open network */
static const unsigned char IES_OPEN[] = {
    1, 8, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
    3, 1, 11,
    45, 26, 0x2c, 0x01, 0x17, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const Ap APS[] = {
    { { 0x02, 0x4e, 0x6f, 0x76, 0x61, 0x01 }, "NovaNet",    -45, 90,  6, WDI_BAND_ID_2400, 0x0411,
      IES_WPA2, sizeof(IES_WPA2) },
    { { 0x02, 0x4e, 0x6f, 0x76, 0x61, 0x02 }, "NovaNet-5G", -60, 72, 36, WDI_BAND_ID_5000, 0x0011,
      IES_WPA3, sizeof(IES_WPA3) },
    { { 0x02, 0x4e, 0x6f, 0x76, 0x61, 0x03 }, "NovaGuest",  -78, 35, 11, WDI_BAND_ID_2400, 0x0401,
      IES_OPEN, sizeof(IES_OPEN) },
};
#define NAPS (int)(sizeof(APS) / sizeof(APS[0]))

#define NNET      0                 /* APS[NNET]: the WPA2-Personal network, which connects with its passphrase */
#define NGUEST    2                 /* APS[NGUEST]: the open network, which connects */
#define PEER      1                 /* its peer ID */
#define RXQ       16

/* the router behind NovaGuest, which this driver plays (its MAC is the AP's BSSID) */
static const unsigned char ROUTER_IP[4] = { 192, 168, 77, 1 }, CLIENT_IP[4] = { 192, 168, 77, 100 },
                           MASK[4] = { 255, 255, 255, 0 };

/* NovaNet's PMK: PBKDF2-HMAC-SHA1("nova-wpa2-passphrase", "NovaNet", 4096, 32) */
static const unsigned char PMK[32] = {
    0x35, 0x68, 0x81, 0x1a, 0xf5, 0x81, 0x19, 0x10, 0x42, 0xf3, 0x6d, 0xe6, 0x1f, 0x6f, 0x46, 0xcb,
    0x29, 0xa4, 0x01, 0x96, 0xb4, 0x80, 0x41, 0xee, 0x91, 0xdf, 0x37, 0x77, 0xb1, 0x52, 0x27, 0xc4,
};
/* The RSN element the driver associates with (PSK, CCMP), and its association request's body */
static const unsigned char STA_RSN[22] = { 48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0,
                                           0x00, 0x0f, 0xac, 2, 0, 0 };
#define AP_RSN (IES_WPA2 + 13)      /* (NovaNet's beacon's RSN element: after its rates and DS parameter set) */
#define AP_RSN_LEN 22

/* ---- the adapter ---- */
typedef struct {
    void             *handle;       /* NDIS's adapter handle */
    COMPLETE_FN       open_done, close_done;
    void             *timer, *rx_timer;
    /* the task the timer finishes */
    unsigned int      task, tid;
    unsigned short    port;
    NDIS_OID_REQUEST *pending;      /* a scan's OID request, completed by the timer */
    int               port_up, connected;
    unsigned char     ind[2048];
    /* the data path */
    void             *dp;           /* the upper edge's NdisMiniportDataPathHandle */
    NDIS_WDI_DATA_API *api;
    void             *pool;         /* NBLs for received frames */
    int               started, dp_port;
    volatile int      rx_lock;
    NET_BUFFER_LIST  *rxq[RXQ];     /* received frames waiting for RxGetMpdus */
    int               nrx;
    unsigned int      tx_frames, rx_frames, arps, dhcps, pings, dhcp_said, ping_said;
    unsigned char     frame[2048], reply[1600];
    /* the connection: which access point; WPA2's state */
    int               ap, wpa2, exempt, hs, port_open, rekeyed, dropped;
    WpaAp             wpa;
    unsigned char     eapol[512];
} Adapter;
enum { HS_NONE, HS_MSG1, HS_MSG3, HS_DONE, HS_GROUP1 };

static void *zero(void *p, unsigned long long n)
{
    volatile unsigned char *b = p;
    while (n--) *b++ = 0;
    return p;
}

static void copy(void *d, const void *s, unsigned long long n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--) *a++ = *b++;
}

static unsigned int put(unsigned char *buf, unsigned int off, unsigned short type, const void *v, unsigned int n)
{
    WDI_TLV_HEADER h = { type, (unsigned short)n };
    copy(buf + off, &h, sizeof(h));
    if (n) copy(buf + off + sizeof(h), v, n);
    return off + sizeof(h) + n;
}

/* A TLV's value at the top level of @buf, or NULL */
static const unsigned char *find(const unsigned char *buf, unsigned int len, unsigned short type, unsigned int *n)
{
    for (unsigned int off = 0; off + sizeof(WDI_TLV_HEADER) <= len;) {
        const WDI_TLV_HEADER *h = (const WDI_TLV_HEADER *)(buf + off);
        if (off + sizeof(*h) + h->Length > len) return 0;
        if (h->Type == type) { *n = h->Length; return buf + off + sizeof(*h); }
        off += sizeof(*h) + h->Length;
    }
    return 0;
}

static void indicate(Adapter *a, unsigned int id, unsigned int tid, unsigned int len)
{
    WDI_MESSAGE_HEADER *h = (WDI_MESSAGE_HEADER *)a->ind;
    h->PortId = a->port;
    h->Reserved = 0;
    h->Status = NDIS_STATUS_SUCCESS;
    h->TransactionId = tid;
    h->IhvSpecificId = 0;
    NDIS_STATUS_INDICATION ind;
    zero(&ind, sizeof(ind));
    ind.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    ind.Header.Revision = 1;
    ind.Header.Size = sizeof(ind);
    ind.SourceHandle = a->handle;
    ind.StatusCode = (NDIS_STATUS)WDI_INDICATION(id);
    ind.StatusBuffer = a->ind;
    ind.StatusBufferSize = sizeof(*h) + len;
    NdisMIndicateStatusEx(a->handle, &ind);
}

/* One WDI_TLV_BSS_ENTRY for @ap at @off in @buf */
static unsigned int bss_entry(unsigned char *buf, unsigned int off, const Ap *ap)
{
    unsigned char e[400], frame[300];
    unsigned int n = 0, f = 0, sl = 0;
    while (ap->ssid[sl]) sl++;
    zero(frame, 12);                                        /* timestamp */
    frame[8] = 100;                                         /* beacon interval (TU) */
    frame[10] = (unsigned char)ap->capability;
    frame[11] = (unsigned char)(ap->capability >> 8);
    f = 12;
    frame[f++] = 0;
    frame[f++] = (unsigned char)sl;
    copy(frame + f, ap->ssid, sl);
    f += sl;
    copy(frame + f, ap->ies, ap->ies_len);
    f += ap->ies_len;
    WDI_SIGNAL_INFO si = { ap->rssi, ap->quality };
    WDI_BSS_ENTRY_CHANNEL_INFO ci = { ap->channel, ap->band };
    n = put(e, n, WDI_TLV_BSSID, ap->bssid, 6);
    n = put(e, n, WDI_TLV_BEACON_FRAME, frame, f);
    n = put(e, n, WDI_TLV_BSS_ENTRY_SIGNAL_INFO, &si, sizeof(si));
    n = put(e, n, WDI_TLV_BSS_ENTRY_CHANNEL_INFO, &ci, sizeof(ci));
    return put(buf, off, WDI_TLV_BSS_ENTRY, e, n);
}

/* ---- the data path: a TAL ---- */
typedef void (NT_API *DEQUEUE_FN)(void *dp, unsigned int quantum, unsigned char max, unsigned short credit,
                                  NET_BUFFER_LIST **out);
typedef void (NT_API *TRANSFER_DONE_FN)(void *dp, unsigned int status, NET_BUFFER_LIST *nbl);
typedef void (NT_API *RX_IND_FN)(void *dp, unsigned int level, WDI_PEER_ID peer, unsigned char tid,
                                 NDIS_RECEIVE_THROTTLE_PARAMETERS *thr, NDIS_STATUS *st);
typedef void (NT_API *PEER_CREATE_FN)(void *dp, unsigned short port, WDI_PEER_ID peer, WDI_MAC_ADDRESS mac);
typedef void (NT_API *PEER_DELETE_FN)(void *dp, unsigned short port, WDI_PEER_ID peer, NDIS_STATUS *st);
typedef WDI_FRAME_METADATA *(NT_API *ALLOC_META_FN)(void *dp);
typedef void (NT_API *FREE_META_FN)(void *dp, WDI_FRAME_METADATA *m);

static unsigned char *mdl_va(NT_MDL *m)
{
    if (m->MdlFlags & 0x5) return m->MappedSystemVa;   /* (MDL_MAPPED_TO_SYSTEM_VA, MDL_SOURCE_IS_NONPAGED_POOL) */
    return (unsigned char *)m->StartVa + m->ByteOffset;
}

static void lock(volatile int *l)   { while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE)) {} }
static void unlock(volatile int *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }

static int same(const unsigned char *a, const unsigned char *b, int n)
{
    while (n--) if (*a++ != *b++) return 0;
    return 1;
}

static unsigned short csum(const unsigned char *p, unsigned int n)
{
    unsigned int s = 0;
    for (unsigned int i = 0; i + 1 < n; i += 2) s += (unsigned int)(p[i] << 8 | p[i + 1]);
    if (n & 1) s += (unsigned int)p[n - 1] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (unsigned short)~s;
}

/* Queue an Ethernet-style frame (@dst, the router as source, @type, @payload)
 * as a received 802.11 MSDU from the access point (FromDS: address 1 the
 * destination, 2 the BSSID, 3 the source); the RX timer indicates it */
static void receive(Adapter *a, const unsigned char *dst, unsigned short type, const unsigned char *payload,
                    unsigned int n)
{
    const unsigned char *bssid = APS[a->ap].bssid;
    NET_BUFFER_LIST *nbl = NdisAllocateNetBufferAndNetBufferList(a->pool, 0, 0, 0, 0, 24 + 8 + n);
    WDI_FRAME_METADATA *m = nbl ? ((ALLOC_META_FN)a->api->AllocateWiFiFrameMetaData)(a->dp) : 0;
    if (!m) {
        if (nbl) NdisFreeNetBufferList(nbl);
        DbgPrint("wditest: no room for a received frame\n");
        return;
    }
    NET_BUFFER *nb = nbl->FirstNetBuffer;
    unsigned char *f = mdl_va(nb->MdlChain) + nb->DataOffset;
    zero(f, 24);
    f[0] = DOT11_FC0_DATA;
    f[1] = DOT11_FC1_FROM_DS;
    if (a->port_open && type != ETHERTYPE_EAPOL) f[1] |= DOT11_FC1_PROTECTED;    /* (decrypted, as the hardware does) */
    copy(f + 4, dst, 6);
    copy(f + 10, bssid, 6);
    copy(f + 16, bssid, 6);
    static const unsigned char snap[6] = { 0xAA, 0xAA, 0x03, 0, 0, 0 };
    copy(f + 24, snap, 6);
    f[30] = (unsigned char)(type >> 8);
    f[31] = (unsigned char)type;
    copy(f + 32, payload, n);
    m->pNBL = nbl;
    m->u.rxPayloadType = WDI_FRAME_MSDU;
    nbl->MiniportReserved[0] = m;
    lock(&a->rx_lock);
    int ok = a->nrx < RXQ;
    if (ok) a->rxq[a->nrx++] = nbl;
    unlock(&a->rx_lock);
    if (!ok) {
        ((FREE_META_FN)a->api->FreeWiFiFrameMetaData)(a->dp, m);
        NdisFreeNetBufferList(nbl);
        return;
    }
    NdisSetTimerObject(a->rx_timer, -10000, 0, a);     /* (1 ms: as the hardware's interrupt would) */
}

static NT_API void rx_timer(void *ss1, void *ctx, void *ss2, void *ss3)
{
    (void)ss1; (void)ss2; (void)ss3;
    Adapter *a = ctx;
    if (!a->nrx || !a->connected) return;
    NDIS_RECEIVE_THROTTLE_PARAMETERS thr = { NDIS_INDICATE_ALL_NBLS, 0 };
    NDIS_STATUS st = NDIS_STATUS_SUCCESS;
    ((RX_IND_FN)a->api->RxInorderDataIndication)(a->dp, WDI_RX_INDICATION_DISPATCH_GENERAL, PEER, WDI_EXT_TID_NON_QOS,
                                                 &thr, &st);
}

/* DHCP (RFC 2131) from the router: an offer or an acknowledgment of CLIENT_IP */
static void dhcp_reply(Adapter *a, const unsigned char *req, unsigned int n, unsigned char kind)
{
    unsigned char *ip = a->reply, *u = ip + 20, *b = u + 8;
    zero(ip, 20 + 8 + 300);
    b[0] = 2; b[1] = 1; b[2] = 6;                      /* (a reply, Ethernet addresses) */
    copy(b + 4, req + 4, 4);                           /* xid */
    copy(b + 10, req + 10, 2);                         /* flags */
    copy(b + 16, CLIENT_IP, 4);                        /* yiaddr */
    copy(b + 20, ROUTER_IP, 4);                        /* siaddr */
    copy(b + 28, req + 28, 16);                        /* chaddr */
    static const unsigned char cookie[4] = { 99, 130, 83, 99 };
    copy(b + 236, cookie, 4);
    unsigned char *o = b + 240;
    *o++ = 53; *o++ = 1; *o++ = kind;
    *o++ = 54; *o++ = 4; copy(o, ROUTER_IP, 4); o += 4;
    *o++ = 51; *o++ = 4; *o++ = 0; *o++ = 0; *o++ = 0x0e; *o++ = 0x10;     /* (an hour) */
    *o++ = 1;  *o++ = 4; copy(o, MASK, 4); o += 4;
    *o++ = 3;  *o++ = 4; copy(o, ROUTER_IP, 4); o += 4;
    *o++ = 6;  *o++ = 4; copy(o, ROUTER_IP, 4); o += 4;
    *o++ = 255;
    unsigned int blen = (unsigned int)(o - b);
    if (blen < 300) blen = 300;
    unsigned int ulen = 8 + blen, tot = 20 + ulen;
    (void)n;
    ip[0] = 0x45; ip[2] = (unsigned char)(tot >> 8); ip[3] = (unsigned char)tot; ip[8] = 64; ip[9] = 17;
    copy(ip + 12, ROUTER_IP, 4);
    ip[16] = ip[17] = ip[18] = ip[19] = 255;          /* (to the broadcast address) */
    unsigned short c = csum(ip, 20);
    ip[10] = (unsigned char)(c >> 8); ip[11] = (unsigned char)c;
    u[1] = 67; u[3] = 68; u[4] = (unsigned char)(ulen >> 8); u[5] = (unsigned char)ulen;   /* (no UDP checksum) */
    static const unsigned char bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    receive(a, bcast, 0x0800, ip, tot);
}

/* An EAPOL-Key frame from the station: the authenticator's half of the
 * 4-way handshake (messages 2 and 4) and of the group key handshake */
static void eapol(Adapter *a, const unsigned char *f, unsigned int n)
{
    const unsigned char *sta = MAC;
    if (!a->wpa2) { DbgPrint("wditest: an EAPOL frame on an open network\n"); return; }
    if (a->hs == HS_MSG1) {
        if (!ap_msg2(&a->wpa, f, n)) {
            DbgPrint("wditest: 4-way handshake message 2 refused: %s; no message 3\n", a->wpa.why);
            return;
        }
        DbgPrint("wditest: 4-way handshake message 2: MIC good, RSN element its association's; sending message 3\n");
        unsigned int k = ap_msg3(&a->wpa, a->eapol);
        a->hs = HS_MSG3;
        receive(a, sta, ETHERTYPE_EAPOL, a->eapol, k);
    } else if (a->hs == HS_MSG3) {
        if (!ap_msg4(&a->wpa, f, n)) { DbgPrint("wditest: 4-way handshake message 4 refused: %s\n", a->wpa.why); return; }
        a->hs = HS_DONE;
        DbgPrint("wditest: 4-way handshake message 4: MIC good; the handshake is done\n");
    } else if (a->hs == HS_GROUP1) {
        if (!ap_group2(&a->wpa, f, n)) { DbgPrint("wditest: group key message 2 refused: %s\n", a->wpa.why); return; }
        a->hs = HS_DONE;
        DbgPrint("wditest: group key handshake message 2: MIC good\n");
    } else {
        DbgPrint("wditest: an EAPOL frame the access point did not expect\n");
    }
}

/* One frame NovaOS sent: an 802.11 MSDU to the access point.  What the
 * router behind it answers: ARP for its address, DHCP, pings to it */
static void transmit(Adapter *a, NET_BUFFER_LIST *nbl)
{
    WDI_FRAME_METADATA *m = nbl->MiniportReserved[0];
    NET_BUFFER *nb = nbl->FirstNetBuffer;
    unsigned int n = 0, off = nb->CurrentMdlOffset, left = nb->DataLength;
    for (NT_MDL *d = nb->CurrentMdl; d && left && n < sizeof(a->frame); d = d->Next, off = 0) {
        unsigned int k = d->ByteCount - off;
        if (k > left) k = left;
        if (k > sizeof(a->frame) - n) k = sizeof(a->frame) - n;
        copy(a->frame + n, mdl_va(d) + off, k);
        n += k;
        left -= k;
    }
    a->tx_frames++;
    const unsigned char *f = a->frame;
    if (!m || m->pNBL != nbl || m->u.txMetaData.PortID != PORT || m->u.txMetaData.PeerID != PEER) {
        DbgPrint("wditest: a frame without its metadata (port %u, peer %u)\n", m ? m->u.txMetaData.PortID : 0,
                 m ? m->u.txMetaData.PeerID : 0);
        return;
    }
    if (n < 32 || f[0] != DOT11_FC0_DATA || f[1] != DOT11_FC1_TO_DS || !same(f + 4, APS[a->ap].bssid, 6) ||
        !same(f + 10, MAC, 6) || f[24] != 0xAA || f[25] != 0xAA || f[26] != 3) {
        DbgPrint("wditest: not an 802.11 data frame to the access point (fc %02x %02x, %u bytes)\n", f[0], f[1], n);
        return;
    }
    unsigned short type = (unsigned short)(f[30] << 8 | f[31]), ethertype = m->u.txMetaData.Ethertype;
    const unsigned char *p = f + 32, *src = f + 10;
    unsigned int pl = n - 32;
    if (type != ethertype) DbgPrint("wditest: frame metadata says Ethertype %04x, the frame %04x\n", ethertype, type);
    if (type == ETHERTYPE_EAPOL) {
        if (m->u.txMetaData.ExemptionAction != WDI_EXEMPT_ON_KEY_MAPPING_KEY_UNAVAILABLE)
            DbgPrint("wditest: an EAPOL frame with exemption %u\n", m->u.txMetaData.ExemptionAction);
        eapol(a, p, pl);
        return;
    }
    if (a->wpa2 && (!a->port_open || m->u.txMetaData.ExemptionAction != WDI_EXEMPT_NO_EXEMPTION)) {
        if (!a->dropped++) DbgPrint("wditest: a data frame before the keys (exemption %u): dropped\n",
                                    m->u.txMetaData.ExemptionAction);
        return;
    }
    if (type == 0x0806 && pl >= 28 && p[7] == 1 && same(p + 24, ROUTER_IP, 4)) {     /* ARP: who has the router? */
        unsigned char r[28];
        copy(r, p, 6);
        r[7] = 2;
        copy(r + 8, APS[a->ap].bssid, 6);
        copy(r + 14, ROUTER_IP, 4);
        copy(r + 18, p + 8, 10);
        if (!a->arps++) DbgPrint("wditest: ARP: 192.168.77.1 is at the router\n");
        receive(a, src, 0x0806, r, 28);
    } else if (type == 0x0800 && pl >= 28 && p[9] == 17 && p[22] == 0 && p[23] == 67 && pl >= 28 + 240) {
        const unsigned char *b = p + 28;               /* (BOOTP: its options after the cookie) */
        unsigned char kind = 0;
        for (unsigned int k = 240; k + 2 < pl - 28 && b[k] != 255; k += b[k] ? 2u + b[k + 1] : 1u)
            if (b[k] == 53) kind = b[k + 2];
        a->dhcps++;
        if (kind == 1 || kind == 3) {
            DbgPrint("wditest: DHCP %s: %s 192.168.77.100\n", kind == 1 ? "discover" : "request",
                     kind == 1 ? "offering" : "acknowledging");
            dhcp_reply(a, b, pl - 28, kind == 1 ? 2 : 5);
        }
    } else if (type == 0x0800 && pl >= 28 && p[9] == 1 && same(p + 16, ROUTER_IP, 4) && p[20] == 8) {
        unsigned int tot = (unsigned int)(p[2] << 8 | p[3]), hl = (p[0] & 15) * 4u;
        if (tot > pl || tot > sizeof(a->reply) || hl < 20) return;
        unsigned char *r = a->reply;                   /* the echo reply */
        copy(r, p, tot);
        copy(r + 12, p + 16, 4);
        copy(r + 16, p + 12, 4);
        r[8] = 64;
        r[10] = r[11] = 0;
        unsigned short c = csum(r, hl);
        r[10] = (unsigned char)(c >> 8); r[11] = (unsigned char)c;
        r[hl] = 0;
        r[hl + 2] = r[hl + 3] = 0;
        c = csum(r + hl, tot - hl);
        r[hl + 2] = (unsigned char)(c >> 8); r[hl + 3] = (unsigned char)c;
        if (!a->pings++) DbgPrint("wditest: ping from %u.%u.%u.%u to the router: answering\n", p[12], p[13], p[14], p[15]);
        receive(a, src, 0x0800, r, tot);
        if (a->wpa2 && a->hs == HS_DONE && !a->rekeyed) {       /* (the access point's rekeying interval, early) */
            a->rekeyed = 1;
            for (int i = 0; i < 16; i++) a->wpa.gtk[i] = (unsigned char)(0xB0 + i);
            a->wpa.gtk_id = 2;
            a->wpa.rsc[0] = 0;
            unsigned int k = ap_group1(&a->wpa, a->eapol);
            a->hs = HS_GROUP1;
            DbgPrint("wditest: group key handshake: sending group key 2\n");
            receive(a, src, ETHERTYPE_EAPOL, a->eapol, k);
        }
    }
}

/* TxDataSend: frames are queued for the access point; take them, "send" them, give them back */
static NT_API void tx_data_send(void *tal, unsigned short port, WDI_PEER_ID peer, unsigned char tid,
                                unsigned short queued, unsigned int active, unsigned char robust)
{
    (void)queued; (void)active; (void)robust; (void)tid;
    Adapter *a = tal;
    if (port != PORT || peer != PEER || !a->connected) {
        DbgPrint("wditest: send on port %u to peer %u refused\n", port, peer);
        return;
    }
    for (;;) {
        NET_BUFFER_LIST *nbl = 0;
        ((DEQUEUE_FN)a->api->TxDequeueIndication)(a->dp, 0xFFFFFFFFu, 8, 0xFFFF, &nbl);
        if (!nbl) break;
        for (NET_BUFFER_LIST *x = nbl; x; x = x->Next) {
            if (x->MiniportReserved[1] != a) DbgPrint("wditest: a frame without its target descriptor\n");
            transmit(a, x);
        }
        ((TRANSFER_DONE_FN)a->api->TxTransferCompleteIndication)(a->dp, WDI_TX_FRAME_STATUS_OK, nbl);
    }
}

static NT_API void tx_desc_init(void *tal, NET_BUFFER_LIST *nbl, NDIS_STATUS *st)
{
    nbl->MiniportReserved[1] = tal;                    /* (the "target descriptor") */
    *st = NDIS_STATUS_SUCCESS;
}

static NT_API void tx_desc_deinit(void *tal, NET_BUFFER_LIST *nbl) { (void)tal; nbl->MiniportReserved[1] = 0; }

/* RxGetMpdus: the frames the last indication announced */
static NT_API void rx_get_mpdus(void *tal, WDI_PEER_ID peer, unsigned char tid, NET_BUFFER_LIST **out)
{
    (void)tid;
    Adapter *a = tal;
    *out = 0;
    if (peer != PEER) return;
    lock(&a->rx_lock);
    NET_BUFFER_LIST **tail = out;
    for (int i = 0; i < a->nrx; i++) {
        a->rxq[i]->Next = 0;
        *tail = a->rxq[i];
        tail = &a->rxq[i]->Next;
        a->rx_frames++;
    }
    a->nrx = 0;
    unlock(&a->rx_lock);
}

static NT_API void rx_return(void *tal, NET_BUFFER_LIST *nbl)
{
    Adapter *a = tal;
    for (NET_BUFFER_LIST *next; nbl; nbl = next) {
        next = nbl->Next;
        ((FREE_META_FN)a->api->FreeWiFiFrameMetaData)(a->dp, nbl->MiniportReserved[0]);
        NdisFreeNetBufferList(nbl);
    }
}

static NT_API NDIS_STATUS tal_start(void *tal, WDI_TXRX_TARGET_CONFIGURATION *cfg, TAL_TXRX_PARAMETERS *out)
{
    Adapter *a = tal;
    DbgPrint("wditest: data path started: %s, %s queueing, %u port(s), %u peer(s)\n",
             cfg->TxRxCapabilities.InterconnectType == WDI_INTERCONNECT_MESSAGE_BASED ? "message based" : "memory mapped",
             cfg->TxRxCapabilities.TargetPriorityQueueing ? "port" : "peer/TID", cfg->MaxNumPorts, cfg->MaxNumPeers);
    out->MaxOutstandingTransfers = 16;
    a->started = 1;
    return NDIS_STATUS_SUCCESS;
}

static NT_API void tal_stop(void *tal) { ((Adapter *)tal)->started = 0; DbgPrint("wditest: data path stopped\n"); }

static NT_API void tal_add_port(void *tal, unsigned short port, unsigned int mode)
{
    ((Adapter *)tal)->dp_port = port;
    DbgPrint("wditest: data path port %u added (mode 0x%x)\n", port, mode);
}

static NT_API void tal_delete_port(void *tal, unsigned short port)
{
    ((Adapter *)tal)->dp_port = 0;
    DbgPrint("wditest: data path port %u deleted\n", port);
}

static NT_API void tal_peer_config(void *tal, unsigned short port, WDI_PEER_ID peer, unsigned int *qos)
{
    (void)tal;
    DbgPrint("wditest: peer %u on port %u configured (QoS %u)\n", peer, port, qos ? *qos : 0);
}

static NT_API void tal_peer_delete_confirm(void *tal, unsigned short port, WDI_PEER_ID peer)
{
    Adapter *a = tal;
    DbgPrint("wditest: peer %u on port %u deleted (%u frames sent, %u received)\n", peer, port, a->tx_frames,
             a->rx_frames);
}

static NT_API void tal_nothing(void *tal) { (void)tal; }

/* MiniportWdiTalTxRxInitialize */
static NT_API NDIS_STATUS tal_initialize(void *ctx, void *dp, NDIS_WDI_DATA_API *api, void **tal,
                                         NDIS_MINIPORT_WDI_DATA_HANDLERS *h, unsigned int *extra)
{
    Adapter *a = ctx;
    if (!api || api->Header.Type != NDIS_OBJECT_TYPE_WDI_DATA_API || api->Header.Size < 4 + 16 * 8 ||
        !api->TxDequeueIndication || !api->RxInorderDataIndication || !api->AllocateWiFiFrameMetaData) {
        DbgPrint("wditest: the upper edge gave no data path API\n");
        return NDIS_STATUS_FAILURE;
    }
    NET_BUFFER_LIST_POOL_PARAMETERS pp;
    zero(&pp, sizeof(pp));
    pp.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    pp.Header.Revision = 1;
    pp.Header.Size = sizeof(pp);
    pp.fAllocateNetBuffer = 1;
    pp.PoolTag = TAG;
    pp.DataSize = 2048;
    a->pool = NdisAllocateNetBufferListPool(a->handle, &pp);
    if (!a->pool) return NDIS_STATUS_RESOURCES;
    a->dp = dp;
    a->api = api;
    h->TxTargetDescInitHandler = (void *)tx_desc_init;
    h->TxTargetDescDeInitHandler = (void *)tx_desc_deinit;
    h->TxDataSendHandler = (void *)tx_data_send;
    h->RxGetMpdusHandler = (void *)rx_get_mpdus;
    h->RxReturnFramesHandler = (void *)rx_return;
    h->TalTxRxStartHandler = (void *)tal_start;
    h->TalTxRxStopHandler = (void *)tal_stop;
    h->TalTxRxAddPortHandler = (void *)tal_add_port;
    h->TalTxRxDeletePortHandler = (void *)tal_delete_port;
    h->TalTxRxPeerConfigHandler = (void *)tal_peer_config;
    h->TalTxRxPeerDeleteConfirmHandler = (void *)tal_peer_delete_confirm;
    h->RxResumeHandler = (void *)tal_nothing;
    *tal = a;
    *extra = 16;
    DbgPrint("wditest: data path initialized (API revision %u)\n", api->Header.Revision);
    return NDIS_STATUS_SUCCESS;
}

static NT_API void tal_deinitialize(void *tal)
{
    Adapter *a = tal;
    NdisCancelTimerObject(a->rx_timer);
    lock(&a->rx_lock);
    NET_BUFFER_LIST *left = 0;
    for (int i = a->nrx - 1; i >= 0; i--) { a->rxq[i]->Next = left; left = a->rxq[i]; }
    a->nrx = 0;
    unlock(&a->rx_lock);
    rx_return(a, left);
    NdisFreeNetBufferListPool(a->pool);
    a->pool = 0;
    DbgPrint("wditest: data path deinitialized\n");
}

/* The timer: finish the task the last OID request started */
static NT_API void task_timer(void *ss1, void *ctx, void *ss2, void *ss3)
{
    (void)ss1; (void)ss2; (void)ss3;
    Adapter *a = ctx;
    unsigned char *t = a->ind + sizeof(WDI_MESSAGE_HEADER);
    unsigned int n = 0, task = a->task;
    a->task = 0;                    /* (the next request may come as soon as the task is done) */
    switch (task) {
    case WDI_TASK_CREATE_PORT: {
        WDI_PORT_ATTRIBUTES pa;
        copy(pa.MacAddress, MAC, 6);
        pa.PortNumber = PORT;
        n = put(t, 0, WDI_TLV_PORT_ATTRIBUTES, &pa, sizeof(pa));
        a->port = WDI_PORT_ID_ADAPTER;
        a->port_up = 1;
        indicate(a, WDI_INDICATION_CREATE_PORT_COMPLETE, a->tid, n);
        DbgPrint("wditest: station port %u created\n", PORT);
        break;
    }
    case WDI_TASK_DELETE_PORT:
        a->port = WDI_PORT_ID_ADAPTER;
        a->port_up = 0;
        indicate(a, WDI_INDICATION_DELETE_PORT_COMPLETE, a->tid, 0);
        DbgPrint("wditest: station port %u deleted\n", PORT);
        break;
    case WDI_TASK_SCAN: {
        /* what the radio heard: two entries unsolicited as they come in, the
         * last with the task's id; then the request completes and the scan ends */
        a->port = PORT;
        for (int i = 0; i < 2; i++) n = bss_entry(t, n, &APS[i]);
        indicate(a, WDI_INDICATION_BSS_ENTRY_LIST, WDI_TRANSACTION_ID_UNSOLICIT, n);
        n = bss_entry(t, 0, &APS[2]);
        indicate(a, WDI_INDICATION_BSS_ENTRY_LIST, a->tid, n);
        NDIS_OID_REQUEST *r = a->pending;
        a->pending = 0;
        if (r) {
            r->DATA.METHOD_INFORMATION.BytesWritten = sizeof(WDI_MESSAGE_HEADER);
            NdisMOidRequestComplete(a->handle, r, NDIS_STATUS_SUCCESS);
        }
        indicate(a, WDI_INDICATION_SCAN_COMPLETE, a->tid, 0);
        DbgPrint("wditest: scan complete: %d BSS entries indicated\n", NAPS);
        break;
    }
    case WDI_TASK_CONNECT: {
        /* associated: the peer first (the data path's), then the association's result (with the
         * association request the driver sent: its RSN element), then the task's end */
        const Ap *ap = &APS[a->ap];
        a->port = PORT;
        a->connected = 1;
        WDI_MAC_ADDRESS mac;
        copy(mac.Address, ap->bssid, 6);
        ((PEER_CREATE_FN)a->api->PeerCreateIndication)(a->dp, PORT, PEER, mac);
        unsigned char r[160], q[64];
        WDI_ASSOCIATION_RESULT_PARAMETERS ar;
        zero(&ar, sizeof(ar));
        ar.AssociationStatus = WDI_ASSOC_STATUS_SUCCESS;
        ar.AuthAlgorithm = a->wpa2 ? WDI_AUTH_ALGO_RSNA_PSK : WDI_AUTH_ALGO_80211_OPEN;
        ar.UnicastCipherAlgorithm = ar.MulticastDataCipherAlgorithm = a->wpa2 ? WDI_CIPHER_ALGO_CCMP : WDI_CIPHER_ALGO_NONE;
        ar.PortAuthorized = !a->wpa2;
        unsigned int m = put(r, 0, WDI_TLV_BSSID, ap->bssid, 6);
        m = put(r, m, WDI_TLV_ASSOCIATION_RESULT_PARAMETERS, &ar, sizeof(ar));
        if (a->wpa2) {
            unsigned int sl = 0, k = 4;
            while (ap->ssid[sl]) sl++;
            zero(q, 4);
            q[0] = 0x11; q[1] = 0x04; q[2] = 10;            /* (capability: ESS, privacy; listen interval) */
            q[k++] = 0; q[k++] = (unsigned char)sl;
            copy(q + k, ap->ssid, sl);
            k += sl;
            copy(q + k, STA_RSN, sizeof(STA_RSN));
            k += sizeof(STA_RSN);
            m = put(r, m, WDI_TLV_ASSOCIATION_REQUEST_FRAME, q, k);
        }
        n = put(t, 0, WDI_TLV_ASSOCIATION_RESULT, r, m);
        indicate(a, WDI_INDICATION_ASSOCIATION_RESULT, a->tid, n);
        indicate(a, WDI_INDICATION_CONNECT_COMPLETE, a->tid, 0);
        DbgPrint("wditest: associated with %s, peer %u\n", ap->ssid, PEER);
        if (a->wpa2) {                                      /* the 4-way handshake: message 1 */
            WpaAp *w = &a->wpa;
            zero(w, sizeof(*w));
            copy(w->pmk, PMK, 32);
            copy(w->aa, ap->bssid, 6);
            copy(w->spa, MAC, 6);
            w->ap_ie = AP_RSN;
            w->ap_ie_len = AP_RSN_LEN;
            w->sta_ie = STA_RSN;
            w->sta_ie_len = sizeof(STA_RSN);
            for (int i = 0; i < 32; i++) w->anonce[i] = (unsigned char)(a->tid * 31 + i * 7);
            for (int i = 0; i < 16; i++) w->gtk[i] = (unsigned char)(0xA0 + i);
            w->gtk_id = 1;
            w->rsc[0] = 5;
            unsigned int k = ap_msg1(w, a->eapol);
            a->hs = HS_MSG1;
            DbgPrint("wditest: 4-way handshake message 1 (ANonce) to the station\n");
            receive(a, MAC, ETHERTYPE_EAPOL, a->eapol, k);
        }
        break;
    }
    case WDI_TASK_DISCONNECT: {
        a->port = PORT;
        a->connected = 0;
        a->port_open = a->hs = a->rekeyed = a->dropped = 0;    /* (and the connection's keys are gone) */
        zero(&a->wpa, sizeof(a->wpa));
        NDIS_STATUS st = NDIS_STATUS_SUCCESS;
        ((PEER_DELETE_FN)a->api->PeerDeleteIndication)(a->dp, PORT, PEER, &st);
        indicate(a, WDI_INDICATION_DISCONNECT_COMPLETE, a->tid, 0);
        DbgPrint("wditest: disassociated from %s\n", APS[a->ap].ssid);
        break;
    }
    }
}

static void start_task(Adapter *a, unsigned int task, unsigned int tid, int ms)
{
    a->task = task;
    a->tid = tid;
    NdisSetTimerObject(a->timer, -(long long)ms * 10000, 0, a);
}

/* ---- the WDI messages ---- */
static NDIS_STATUS capabilities(unsigned char *t, unsigned int room, unsigned int *len)
{
    unsigned char ia[200], caps[64], bi[32];
    zero(caps, sizeof(caps));
    WDI_INTERFACE_CAPABILITIES *ic = (WDI_INTERFACE_CAPABILITIES *)caps;
    ic->MTUSize = 1500;
    ic->MaxMultiCastListSize = 32;
    copy(ic->Address, MAC, 6);
    ic->MaxTxRate = ic->MaxRxRate = 1201000;
    ic->HardwareRadioState = ic->SoftwareRadioState = 1;
    static const char fw[] = "wditest 1.0";
    unsigned int n = put(ia, 0, WDI_TLV_INTERFACE_CAPABILITIES, caps, 40);
    n = put(ia, n, WDI_TLV_FIRMWARE_VERSION, fw, sizeof(fw));
    unsigned int out = put(t, 0, WDI_TLV_INTERFACE_ATTRIBUTES, ia, n);
    out = put(t, out, WDI_TLV_STATION_ATTRIBUTES, 0, 0);
    /* a message-based data path (as a USB or SDIO one is), one peer, port queueing */
    WDI_DATAPATH_CAPABILITIES dc;
    zero(&dc, sizeof(dc));
    dc.InterconnectType = WDI_INTERCONNECT_MESSAGE_BASED;
    dc.MaxNumPeers = 1;
    dc.TxTargetPriorityQueueing = 1;
    dc.TxMaxScatterGatherElementsPerFrame = 1;
    dc.RxMaxThroughput = 100000;
    unsigned char da[32];
    unsigned int m = put(da, 0, WDI_TLV_DATAPATH_CAPABILITIES, &dc, sizeof(dc));
    out = put(t, out, WDI_TLV_DATAPATH_ATTRIBUTES, da, m);
    static const unsigned int bands[] = { WDI_BAND_ID_2400, WDI_BAND_ID_5000 };
    for (int i = 0; i < 2; i++) {
        WDI_BAND_CAPABILITIES bc = { bands[i], 1 };
        m = put(bi, 0, WDI_TLV_BAND_CAPABILITIES, &bc, sizeof(bc));
        out = put(t, out, WDI_TLV_BAND_INFO, bi, m);
    }
    if (out > room) return NDIS_STATUS_BUFFER_TOO_SHORT;
    *len = out;
    DbgPrint("wditest: adapter capabilities answered (%u bytes of TLVs)\n", out);
    return NDIS_STATUS_SUCCESS;
}

/* OID_WDI_SET_ADD_CIPHER_KEYS: each key checked against the one the
 * handshake gave the access point (the pairwise key for its BSSID, both
 * ways; the group key by its index, inbound, from its RSC) */
static NDIS_STATUS add_keys(Adapter *a, WDI_MESSAGE_HEADER *h, const unsigned char *t, unsigned int tlen)
{
    if (h->PortId != PORT || !a->wpa2 || !a->connected) return NDIS_STATUS_INVALID_PARAMETER;
    for (unsigned int off = 0; off + sizeof(WDI_TLV_HEADER) <= tlen;) {
        const WDI_TLV_HEADER *x = (const WDI_TLV_HEADER *)(t + off);
        if (off + sizeof(*x) + x->Length > tlen) break;
        const unsigned char *k = t + off + sizeof(*x);
        unsigned int kl = x->Length, n, ml = 0, il = 0, rl = 0, cl = 0;
        off += sizeof(*x) + x->Length;
        if (x->Type != WDI_TLV_SET_CIPHER_KEY_INFO) continue;
        WDI_CIPHER_KEY_TYPE_INFO ti;
        const unsigned char *ty = find(k, kl, WDI_TLV_CIPHER_KEY_TYPE_INFO, &n);
        const unsigned char *mac = find(k, kl, WDI_TLV_PEER_MAC_ADDRESS, &ml);
        const unsigned char *id = find(k, kl, WDI_TLV_CIPHER_KEY_ID, &il);
        const unsigned char *rsc = find(k, kl, WDI_TLV_CIPHER_KEY_RECEIVE_SEQUENCE_COUNT, &rl);
        const unsigned char *ccmp = find(k, kl, WDI_TLV_CIPHER_KEY_CCMP_KEY, &cl);
        if (!ty || n < sizeof(ti) || !ccmp || cl != 16) {
            DbgPrint("wditest: a cipher key without its type or CCMP key\n");
            return NDIS_STATUS_INVALID_PARAMETER;
        }
        copy(&ti, ty, sizeof(ti));
        if (ti.KeyType == WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY) {
            int ok = ti.CipherAlgorithm == WDI_CIPHER_ALGO_CCMP && ti.Direction == WDI_CIPHER_KEY_DIRECTION_BOTH &&
                     mac && ml == 6 && same(mac, APS[a->ap].bssid, 6) && same(ccmp, AP_TK(&a->wpa), 16) &&
                     a->hs >= HS_DONE;
            DbgPrint("wditest: pairwise key for %02x:%02x:%02x:%02x:%02x:%02x: %s\n", mac ? mac[0] : 0, mac ? mac[1] : 0,
                     mac ? mac[2] : 0, mac ? mac[3] : 0, mac ? mac[4] : 0, mac ? mac[5] : 0,
                     ok ? "the 4-way handshake's" : "NOT the 4-way handshake's");
            if (!ok) return NDIS_STATUS_INVALID_PARAMETER;
            a->port_open = 1;
        } else if (ti.KeyType == WDI_CIPHER_KEY_TYPE_GROUP_KEY) {
            unsigned int kid = 99;
            if (id && il >= 4) copy(&kid, id, 4);
            int ok = ti.CipherAlgorithm == WDI_CIPHER_ALGO_CCMP && ti.Direction == WDI_CIPHER_KEY_DIRECTION_INBOUND &&
                     kid == a->wpa.gtk_id && same(ccmp, a->wpa.gtk, 16) && rsc && rl == 6 && same(rsc, a->wpa.rsc, 6);
            DbgPrint("wditest: group key %u: %s\n", kid, ok ? "the access point's" : "NOT the access point's");
            if (!ok) return NDIS_STATUS_INVALID_PARAMETER;
        } else {
            DbgPrint("wditest: a cipher key of type %u\n", ti.KeyType);
            return NDIS_STATUS_NOT_SUPPORTED;
        }
    }
    return NDIS_STATUS_SUCCESS;
}

static NT_API NDIS_STATUS oid_request(void *ctx, NDIS_OID_REQUEST *r)
{
    Adapter *a = ctx;
    if (r->RequestType != NdisRequestMethod) return NDIS_STATUS_NOT_SUPPORTED;
    unsigned int oid = r->DATA.METHOD_INFORMATION.Oid;
    unsigned char *buf = r->DATA.METHOD_INFORMATION.InformationBuffer;
    unsigned int in = r->DATA.METHOD_INFORMATION.InputBufferLength;
    unsigned int room = r->DATA.METHOD_INFORMATION.OutputBufferLength;
    if ((oid & 0xFFFF0000u) != WDI_OID(0) || in < sizeof(WDI_MESSAGE_HEADER) || room < sizeof(WDI_MESSAGE_HEADER)) {
        DbgPrint("wditest: OID 0x%08x is not a WDI message\n", oid);
        return NDIS_STATUS_INVALID_OID;
    }
    WDI_MESSAGE_HEADER *h = (WDI_MESSAGE_HEADER *)buf;
    unsigned char *t = buf + sizeof(*h);
    unsigned int tlen = in - sizeof(*h), out = 0, n;
    unsigned int msg = oid & 0xFFFF;
    NDIS_STATUS st = NDIS_STATUS_SUCCESS;
    if (a->task) return NDIS_STATUS_FAILURE;                /* (one task at a time) */
    switch (msg) {
    case WDI_GET_ADAPTER_CAPABILITIES:
        if (h->PortId != WDI_PORT_ID_ADAPTER) DbgPrint("wditest: capabilities asked of port %u\n", h->PortId);
        st = capabilities(t, room - sizeof(*h), &out);
        break;
    case WDI_TASK_CREATE_PORT: {
        WDI_CREATE_PORT_PARAMETERS p;
        const unsigned char *v = find(t, tlen, WDI_TLV_CREATE_PORT_PARAMETERS, &n);
        if (!v || n < sizeof(p) || a->port_up) { st = NDIS_STATUS_INVALID_PARAMETER; break; }
        copy(&p, v, sizeof(p));
        DbgPrint("wditest: create port task %u: modes 0x%x, NDIS port %u\n", h->TransactionId, p.OpModeMask,
                 p.NdisPortNumber);
        start_task(a, msg, h->TransactionId, 10);
        break;
    }
    case WDI_TASK_DELETE_PORT: {
        WDI_DELETE_PORT_PARAMETERS p;
        const unsigned char *v = find(t, tlen, WDI_TLV_DELETE_PORT_PARAMETERS, &n);
        if (!v || n < sizeof(p)) { st = NDIS_STATUS_INVALID_PARAMETER; break; }
        copy(&p, v, sizeof(p));
        if (p.PortNumber != PORT || !a->port_up) { st = NDIS_STATUS_INVALID_PARAMETER; break; }
        start_task(a, msg, h->TransactionId, 10);
        break;
    }
    case WDI_TASK_SCAN: {
        WDI_SCAN_MODE mode;
        WDI_SCAN_DWELL_TIME dw;
        const unsigned char *b = find(t, tlen, WDI_TLV_BSSID, &n);
        int wild = b && n == 6 && b[0] == 0xff && b[1] == 0xff && b[2] == 0xff && b[3] == 0xff && b[4] == 0xff &&
                   b[5] == 0xff;
        const unsigned char *s = find(t, tlen, WDI_TLV_SSID, &n);
        unsigned int ssid_len = s ? n : 0;
        const unsigned char *m = find(t, tlen, WDI_TLV_SCAN_MODE, &n);
        if (!m || n < sizeof(mode)) m = 0; else copy(&mode, m, sizeof(mode));
        const unsigned char *d = find(t, tlen, WDI_TLV_SCAN_DWELL_TIME, &n);
        if (!d || n < sizeof(dw)) d = 0; else copy(&dw, d, sizeof(dw));
        if (h->PortId != PORT || !a->port_up || !b || !s || !m || !d) {
            DbgPrint("wditest: scan task refused: port %u, BSSID %s, SSID %s, mode %s, dwell %s\n", h->PortId,
                     b ? "yes" : "no", s ? "yes" : "no", m ? "yes" : "no", d ? "yes" : "no");
            st = NDIS_STATUS_INVALID_PARAMETER;
            break;
        }
        DbgPrint("wditest: scan task %u on port %u: %s BSSID, %s, type %u, dwell %u/%u ms, at most %u ms\n",
                 h->TransactionId, h->PortId, wild ? "wildcard" : "one", ssid_len ? "one SSID" : "any SSID",
                 mode.AllowedScanType, dw.ActiveChannelDwellTime, dw.PassiveChannelDwellTime, dw.MaximumScanTime);
        h->Status = NDIS_STATUS_SUCCESS;
        a->pending = r;
        start_task(a, msg, h->TransactionId, 200);
        return NDIS_STATUS_PENDING;
    }
    case WDI_SET_ADAPTER_CONFIGURATION: {
        const unsigned char *v = find(t, tlen, WDI_TLV_LINK_QUALITY_BAR_MAP, &n);
        if (!v || !n || n % sizeof(WDI_LINK_QUALITY_BAR_MAP_ENTRY)) { st = NDIS_STATUS_INVALID_PARAMETER; break; }
        DbgPrint("wditest: adapter configuration: %u signal bars\n", n / (unsigned int)sizeof(WDI_LINK_QUALITY_BAR_MAP_ENTRY));
        break;
    }
    case WDI_TASK_CONNECT: {
        /* the settings, the SSID and algorithms; then the access points to try */
        unsigned int cl, sl, al, bl, el;
        const unsigned char *cp = find(t, tlen, WDI_TLV_CONNECT_PARAMETERS, &cl);
        const unsigned char *ss = cp ? find(cp, cl, WDI_TLV_SSID, &sl) : 0;
        const unsigned char *au = cp ? find(cp, cl, WDI_TLV_AUTH_ALGO_LIST, &al) : 0;
        const unsigned char *be = find(t, tlen, WDI_TLV_CONNECT_BSS_ENTRY, &el);
        const unsigned char *bs = be ? find(be, el, WDI_TLV_BSSID, &bl) : 0;
        const unsigned char *bf = be ? find(be, el, WDI_TLV_BEACON_FRAME, &n) : 0;
        int entries = 0;
        for (unsigned int off = 0; off + sizeof(WDI_TLV_HEADER) <= tlen;) {
            const WDI_TLV_HEADER *x = (const WDI_TLV_HEADER *)(t + off);
            if (off + sizeof(*x) + x->Length > tlen) break;
            entries += x->Type == WDI_TLV_CONNECT_BSS_ENTRY;
            off += sizeof(*x) + x->Length;
        }
        const char *g = "(another)";
        for (int i = 0; i < NAPS; i++) {
            unsigned int gl = 0;
            while (APS[i].ssid[gl]) gl++;
            if (ss && sl == gl && same(ss, (const unsigned char *)APS[i].ssid, (int)gl)) g = APS[i].ssid;
        }
        unsigned int algo = 0;
        if (au && al >= 4) copy(&algo, au, 4);
        if (h->PortId != PORT || !a->port_up || !ss || !au || !bs || bl != 6 || !bf || a->connected) {
            DbgPrint("wditest: connect task refused: port %u, SSID %s, auth %s, BSS entry %s, beacon %s\n", h->PortId,
                     ss ? "yes" : "no", au ? "yes" : "no", bs ? "yes" : "no", bf ? "yes" : "no");
            st = NDIS_STATUS_INVALID_PARAMETER;
            break;
        }
        DbgPrint("wditest: connect task %u on port %u: SSID %s, auth %u, %d BSS entr%s, first %02x:%02x:%02x:%02x:%02x:%02x\n",
                 h->TransactionId, h->PortId, g,
                 algo, entries, entries == 1 ? "y" : "ies", bs[0], bs[1], bs[2], bs[3], bs[4], bs[5]);
        int target = -1;
        for (int i = 0; i < NAPS; i++)
            if (same(bs, APS[i].bssid, 6)) target = i;
        if (target == NNET) {
            /* NovaNet: WPA2-Personal, CCMP, the passphrase's handshake; EAPOL exempted first */
            WDI_CONNECTION_SETTINGS cs;
            unsigned int ul, ml, csl, uc = 0, mc = 0;
            const unsigned char *u = find(cp, cl, WDI_TLV_UNICAST_CIPHER_ALGO_LIST, &ul);
            const unsigned char *mu = find(cp, cl, WDI_TLV_MULTICAST_CIPHER_ALGO_LIST, &ml);
            const unsigned char *c = find(cp, cl, WDI_TLV_CONNECTION_SETTINGS, &csl);
            if (u && ul >= 4) copy(&uc, u, 4);
            if (mu && ml >= 4) copy(&mc, mu, 4);
            zero(&cs, sizeof(cs));
            if (c && csl >= sizeof(cs)) copy(&cs, c, sizeof(cs));
            DbgPrint("wditest: connect to NovaNet: auth %u, ciphers %u/%u, exclude unencrypted %u, EAPOL exempted %s\n",
                     algo, uc, mc, cs.ExcludeUnencrypted, a->exempt ? "yes" : "no");
            if (algo != WDI_AUTH_ALGO_RSNA_PSK || uc != WDI_CIPHER_ALGO_CCMP || mc != WDI_CIPHER_ALGO_CCMP ||
                !cs.ExcludeUnencrypted || !a->exempt) {
                st = NDIS_STATUS_NOT_SUPPORTED;
                break;
            }
            a->wpa2 = 1;
        } else if (target == NGUEST && algo == WDI_AUTH_ALGO_80211_OPEN) {
            a->wpa2 = 0;
        } else {
            st = NDIS_STATUS_NOT_SUPPORTED;                /* (NovaNet-5G is WPA3's SAE: not here) */
            break;
        }
        a->ap = target;
        a->port_open = a->hs = a->rekeyed = a->dropped = 0;
        start_task(a, msg, h->TransactionId, 20);
        break;
    }
    case WDI_SET_PRIVACY_EXEMPTION_LIST: {
        WDI_PRIVACY_EXEMPTION_PARAMETERS e;
        const unsigned char *v = find(t, tlen, WDI_TLV_PRIVACY_EXEMPTION_ENTRY, &n);
        if (!v || n < sizeof(e) || h->PortId != PORT) { st = NDIS_STATUS_INVALID_PARAMETER; break; }
        copy(&e, v, sizeof(e));
        unsigned int type = (unsigned int)(v[0] << 8 | v[1]);              /* (big-endian) */
        a->exempt = type == ETHERTYPE_EAPOL && e.ExemptionActionType == WDI_EXEMPT_ON_KEY_MAPPING_KEY_UNAVAILABLE &&
                    e.ExemptionPacketType == WDI_EXEMPT_PACKET_TYPE_BOTH;
        DbgPrint("wditest: privacy exemption: Ethertype %04x, action %u, packets %u\n", type, e.ExemptionActionType,
                 e.ExemptionPacketType);
        break;
    }
    case WDI_SET_ADD_CIPHER_KEYS:
        st = add_keys(a, h, t, tlen);
        break;
    case WDI_TASK_DISCONNECT: {
        WDI_DISCONNECT_PARAMETERS p;
        const unsigned char *v = find(t, tlen, WDI_TLV_DISCONNECT_PARAMETERS, &n);
        if (!v || n < sizeof(p) || !a->connected) { st = NDIS_STATUS_INVALID_PARAMETER; break; }
        copy(&p, v, sizeof(p));
        DbgPrint("wditest: disconnect task %u: from %02x:%02x:%02x:%02x:%02x:%02x, reason %u\n", h->TransactionId,
                 p.MacAddress[0], p.MacAddress[1], p.MacAddress[2], p.MacAddress[3], p.MacAddress[4], p.MacAddress[5],
                 p.Disassociation80211Reason);
        start_task(a, msg, h->TransactionId, 10);
        break;
    }
    default:
        DbgPrint("wditest: WDI message %u not handled\n", msg);
        st = NDIS_STATUS_NOT_SUPPORTED;
    }
    h->Status = st;
    r->DATA.METHOD_INFORMATION.BytesWritten = sizeof(*h) + out;
    return st;
}

/* ---- the WDI lower edge ---- */
static NT_API NDIS_STATUS allocate_adapter(void *handle, void *driver_ctx, NDIS_MINIPORT_INIT_PARAMETERS *init,
                                           NDIS_WDI_INIT_PARAMETERS *wdi, NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES *ra)
{
    (void)driver_ctx; (void)init;
    Adapter *a = NdisAllocateMemoryWithTagPriority(handle, sizeof(*a), TAG, 32);
    if (!a) return NDIS_STATUS_RESOURCES;
    zero(a, sizeof(*a));
    a->handle = handle;
    a->port = WDI_PORT_ID_ADAPTER;
    a->open_done = (COMPLETE_FN)wdi->OpenAdapterCompleteHandler;
    a->close_done = (COMPLETE_FN)wdi->CloseAdapterCompleteHandler;
    TIMER_CHARACTERISTICS tc = { { NDIS_OBJECT_TYPE_TIMER_CHARACTERISTICS, 1, sizeof(tc) }, TAG, task_timer, a };
    TIMER_CHARACTERISTICS rc = { { NDIS_OBJECT_TYPE_TIMER_CHARACTERISTICS, 1, sizeof(rc) }, TAG, rx_timer, a };
    if (NdisAllocateTimerObject(handle, &tc, &a->timer) != NDIS_STATUS_SUCCESS) {
        NdisFreeMemoryWithTagPriority(handle, a, TAG);
        return NDIS_STATUS_RESOURCES;
    }
    if (NdisAllocateTimerObject(handle, &rc, &a->rx_timer) != NDIS_STATUS_SUCCESS) {
        NdisFreeTimerObject(a->timer);
        NdisFreeMemoryWithTagPriority(handle, a, TAG);
        return NDIS_STATUS_RESOURCES;
    }
    ra->MiniportAdapterContext = a;
    DbgPrint("wditest: adapter allocated (WDI version 0x%x)\n", wdi->WdiVersion);
    return NDIS_STATUS_SUCCESS;
}

static NT_API void free_adapter(void *ctx)
{
    Adapter *a = ctx;
    NdisFreeTimerObject(a->timer);
    NdisFreeTimerObject(a->rx_timer);
    NdisFreeMemoryWithTagPriority(a->handle, a, TAG);
    DbgPrint("wditest: adapter freed\n");
}

static NT_API NDIS_STATUS open_adapter(void *ctx, NDIS_MINIPORT_INIT_PARAMETERS *init)
{
    (void)init;
    Adapter *a = ctx;
    DbgPrint("wditest: adapter open: radio on, %d access points in range\n", NAPS);
    a->open_done(a->handle, NDIS_STATUS_SUCCESS);
    return NDIS_STATUS_SUCCESS;
}

static NT_API NDIS_STATUS close_adapter(void *ctx)
{
    Adapter *a = ctx;
    NdisCancelTimerObject(a->timer);
    DbgPrint("wditest: adapter closed%s\n", a->port_up ? " (its port still up)" : "");
    a->close_done(a->handle, NDIS_STATUS_SUCCESS);
    return NDIS_STATUS_SUCCESS;
}

static NT_API void unload(NT_DRIVER_OBJECT *obj)
{
    (void)obj;
    NdisMDeregisterWdiMiniportDriver(g_driver);
    DbgPrint("wditest: unloaded\n");
}

NT_API NT_STATUS DriverEntry(NT_DRIVER_OBJECT *obj, NT_UNICODE_STRING *reg)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS ch;
    NDIS_MINIPORT_DRIVER_WDI_CHARACTERISTICS wdi;
    zero(&ch, sizeof(ch));
    zero(&wdi, sizeof(wdi));
    ch.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    ch.Header.Revision = 2;
    ch.Header.Size = sizeof(ch);
    ch.MajorNdisVersion = 6;
    ch.MinorNdisVersion = 50;
    ch.MajorDriverVersion = 1;
    ch.UnloadHandler = (void *)unload;
    ch.OidRequestHandler = (void *)oid_request;
    wdi.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_WDI_CHARACTERISTICS;
    wdi.Header.Revision = 1;
    wdi.Header.Size = sizeof(wdi);
    wdi.WdiVersion = WDI_VER;
    wdi.AllocateAdapterHandler = (void *)allocate_adapter;
    wdi.FreeAdapterHandler = (void *)free_adapter;
    wdi.OpenAdapterHandler = (void *)open_adapter;
    wdi.CloseAdapterHandler = (void *)close_adapter;
    wdi.TalTxRxInitializeHandler = (void *)tal_initialize;
    wdi.TalTxRxDeinitializeHandler = (void *)tal_deinitialize;
    NDIS_STATUS st = NdisMRegisterWdiMiniportDriver(obj, reg, 0, &ch, &wdi, &g_driver);
    DbgPrint("wditest: DriverEntry %08x\n", (unsigned int)st);
    return st == NDIS_STATUS_SUCCESS ? 0 : (NT_STATUS)0xC0000001;
}
