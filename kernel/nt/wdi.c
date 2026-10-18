/*
 * wdi.c — the WDI upper edge for a Wi-Fi miniport: its command layer and
 * its data path (roadmap step 28.1)
 *
 * On Windows a Wi-Fi driver such as Intel's Netwtw14.sys is a WDI "lower
 * edge": it does not talk to NDIS's protocols itself, Microsoft's
 * wdiwifi.sys is the NDIS miniport for it and drives it with WDI messages.
 * ndis.c is the part of that upper edge that allocates and opens the
 * adapter (MiniportWdiAllocateAdapter, MiniportWdiOpenAdapter); this file is
 * the rest of what NovaOS has of it so far.  The command layer:
 *
 *   - a command is an NDIS method OID (OID_WDI_*) whose buffer holds a
 *     WDI_MESSAGE_HEADER and TLVs (wdi.h); ndis.c hands it to the
 *     miniport's MiniportOidRequest and waits for it, also when it pends;
 *   - a task (OID_WDI_TASK_*) is accepted by that request and finished by
 *     a status indication, NDIS_STATUS_WDI_INDICATION_*_COMPLETE, whose
 *     header carries the task's TransactionId; a command here waits for
 *     both;
 *   - other indications come when the driver has something to say: a
 *     scan's BSS entries (NDIS_STATUS_WDI_INDICATION_BSS_ENTRY_LIST), kept
 *     here as the adapter's list of access points; a connection's
 *     association result.
 *
 * The data path is calls, not messages (wdi.h): the driver's TAL takes
 * 802.11 data frames from the TxMgr here and hands received ones to the
 * RxMgr here.  Frames the network stack sends (Ethernet, kernel/net) become
 * 802.11 MSDUs to the access point (ToDS, LLC/SNAP); 802.11 frames the
 * driver receives become Ethernet frames for the stack again.
 *
 * WdiAttach brings the adapter up in the order wdiwifi.sys does: the data
 * path (MiniportWdiTalTxRxInitialize), the adapter's capabilities
 * (OID_WDI_GET_ADAPTER_CAPABILITIES: its MAC address, firmware, radio state,
 * bands and data path attributes), its configuration
 * (OID_WDI_SET_ADAPTER_CONFIGURATION), the data path's start
 * (TalTxRxStart), then its station port (OID_WDI_TASK_CREATE_PORT and
 * TalTxRxAddPort).  A scan (WlanScan, from netsh wlan show networks) is
 * OID_WDI_TASK_SCAN on that port; a connection (WlanConnect, from netsh
 * wlan connect) is OID_WDI_TASK_CONNECT with the access points the scan
 * heard, the driver indicating its peer (the access point) and the
 * association's result; then the adapter is the stack's (NetAttach) and
 * DHCP runs over Wi-Fi.
 *
 * A WPA2-Personal network (an RSN element offering the PSK AKM and CCMP)
 * connects with its passphrase: before OID_WDI_TASK_CONNECT, EAPOL frames
 * are exempted from encryption (OID_WDI_SET_PRIVACY_EXEMPTION_LIST); once
 * associated, the access point's EAPOL-Key frames (Ethertype 0x888E) come
 * up the data path to the supplicant (wpa.c) instead of the stack, its
 * replies go down it with their exemption in the frame's metadata, and when
 * the 4-way handshake is done the pairwise and group keys go to the driver
 * (OID_WDI_SET_ADD_CIPHER_KEYS), which encrypts and decrypts with them.
 * Only then does the stack get the adapter; until then, and whenever a
 * frame comes unencrypted, only EAPOL passes.  A later group key handshake
 * (the access point's rekeying) is answered from a work item.  Not yet here
 * (docs/drivers.md): WPA3's SAE, wlanapi.dll and Settings' Wi-Fi page.
 */
#include "ntapi.h"
#include "ndis.h"
#include "wdi.h"
#include "wlan.h"
#include "wpa.h"
#include "../ke/irql.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../net/net.h"

#define WDI_MAX_ADAPTERS  4
#define CMD_BUF           8192      /* (a command's buffer: its answer comes back in it) */
#define TASK_WAIT_S       30
#define SCAN_MAX_MS       4000
#define CONNECT_WAIT_S    30
#define BSS_RAW           512       /* (a BSS entry's TLVs, kept for OID_WDI_TASK_CONNECT) */
#define TX_FRAMES         32        /* frames the stack may have queued or with the driver */
#define TX_FRAME_BUF      2048
#define RX_SLOTS          64        /* received frames waiting for the stack */
#define RX_FRAME          1536
#define POOL_TAG          0x49445749u   /* 'IWDI' */
#define EAPOL_SLOTS       4         /* EAPOL frames waiting for the supplicant */
#define EAPOL_MAX         512
#define HANDSHAKE_MS      8000      /* (an access point resends message 1 a few times, a second apart) */

/* A frame on its way out: an NBL of one 802.11 MSDU with its metadata */
typedef struct {
    NET_BUFFER_LIST    nbl;
    NET_BUFFER         nb;
    WDI_FRAME_METADATA meta;
    NT_MDL            *mdl;
    UINT8             *buf;
    volatile int       state;       /* TXF_* */
} TxFrame;
enum { TXF_FREE, TXF_QUEUED, TXF_DRIVER };

struct WdiHost {
    bool           used;
    void          *adapter;         /* ndis.c's NdisAdapter; NULL once detached */
    WdiRequestFn   request;
    WlanInfo       info;
    UINT32         next_tid;
    UINT16         port_id;         /* the station port's WDI port number */
    NT_KEVENT      cmd_lock;        /* (one command at a time: a synchronization event) */
    /* the task being waited for */
    volatile UINT32 task_tid;
    UINT32         task_done_id;    /* the WDI_INDICATION_* that finishes it */
    NT_KEVENT      task_done;
    NDIS_STATUS    task_status;
    WDI_PORT_ATTRIBUTES port_attr;  /* (from WDI_INDICATION_CREATE_PORT_COMPLETE) */
    /* the access points the driver has indicated */
    NT_KSPIN_LOCK  bss_lock;
    WlanBss        bss[WLAN_MAX_BSS];
    UINT8          bss_raw[WLAN_MAX_BSS][BSS_RAW];
    UINT16         bss_raw_len[WLAN_MAX_BSS];
    int            nbss;
    /* the data path: the driver's TAL */
    void          *mp_ctx;          /* its MiniportAdapterContext */
    void          *tal_init, *tal_deinit;
    void          *tal;             /* its TAL_TXRX_HANDLE, once initialized */
    NDIS_MINIPORT_WDI_DATA_HANDLERS dh;
    UINT32         meta_extra;      /* (what it wants after each RX metadata) */
    bool           tal_started, tal_port;
    WDI_DATAPATH_CAPABILITIES dpcaps;
    TxFrame       *tx;
    NT_KSPIN_LOCK  tx_lock;
    UINT16         txq[TX_FRAMES];  /* queued frames (indices into tx), oldest first */
    UINT32         txq_head, txq_tail;
    WDI_FRAME_ID   next_frame;
    volatile bool  tx_paused;
    NT_KSPIN_LOCK  rx_lock;
    UINT8         *rx;              /* RX_SLOTS Ethernet frames of RX_FRAME */
    UINT16         rx_len[RX_SLOTS];
    UINT32         rx_head, rx_tail;
    UINT64         rx_dropped;
    /* the connection */
    volatile bool  peer_up;
    WDI_PEER_ID    peer;
    UINT8          peer_mac[6];
    bool           peer_deleted;    /* (the driver deleted it: confirmed after the disconnect) */
    bool           assoc_seen;
    WDI_ASSOCIATION_RESULT_PARAMETERS assoc;
    UINT8          assoc_ie[WPA_IE_MAX];   /* the RSN element the driver associated with (its request's) */
    UINT8          assoc_ie_len;
    /* WPA2: the supplicant and the EAPOL frames for it */
    volatile bool  rsna;            /* the connection is secured: only EAPOL may come unencrypted */
    volatile bool  keys;            /* the driver has the pairwise key */
    volatile bool  handshaking;     /* WlanConnect runs the handshake (else a work item answers rekeys) */
    WpaSupplicant  wpa;
    NT_KSPIN_LOCK  eapol_lock;
    UINT8          eapol[EAPOL_SLOTS][EAPOL_MAX];
    UINT16         eapol_len[EAPOL_SLOTS];
    UINT32         eapol_head, eapol_tail;
    NT_KEVENT      eapol_event;
    NT_IO_WORKITEM *eapol_work;
    volatile bool  work_queued;
};

/* (hosts are never freed, only reused: netsh can hold one while its
 * device goes away, and finds it detached when it gets the command lock) */
static NT_KSPIN_LOCK g_lock;
static WdiHost       g_hosts[WDI_MAX_ADAPTERS];

/* -----------------------------------------------------------------------
 * TLVs
 * ----------------------------------------------------------------------- */

/* The value of the first @type TLV at the top level of @buf, or NULL */
static const UINT8 *tlv_find(const UINT8 *buf, UINT32 len, UINT16 type, UINT32 *vlen)
{
    UINT32 off = 0;
    while (off + sizeof(WDI_TLV_HEADER) <= len) {
        const WDI_TLV_HEADER *h = (const WDI_TLV_HEADER *)(buf + off);
        UINT32 n = h->Length;
        if (off + sizeof(*h) + n > len) return NULL;
        if (h->Type == type) {
            *vlen = n;
            return buf + off + sizeof(*h);
        }
        off += sizeof(*h) + n;
    }
    return NULL;
}

/* Copy a TLV's value into @dst when it is at least @need bytes long */
static bool tlv_get(const UINT8 *buf, UINT32 len, UINT16 type, void *dst, UINT32 need)
{
    UINT32 n;
    const UINT8 *v = tlv_find(buf, len, type, &n);
    if (!v || n < need) return false;
    memcpy(dst, v, need);
    return true;
}

static UINT32 tlv_put(UINT8 *buf, UINT32 off, UINT16 type, const void *val, UINT16 n)
{
    WDI_TLV_HEADER h = { type, n };
    memcpy(buf + off, &h, sizeof(h));
    if (n) memcpy(buf + off + sizeof(h), val, n);
    return off + sizeof(h) + n;
}

/* -----------------------------------------------------------------------
 * Commands
 * ----------------------------------------------------------------------- */

static void lock(WdiHost *w)   { nt_KeWaitForSingleObject(&w->cmd_lock, 0, 0, 0, NULL); }
static void unlock(WdiHost *w) { nt_KeSetEvent(&w->cmd_lock, 0, 0); }

/* Send WDI command @msg (OID_WDI_*) with the @tlen bytes of TLVs in
 * @buf + sizeof(WDI_MESSAGE_HEADER) to port @port.  A task (@done_id, its
 * completion indication, non-zero) is waited for until that indication
 * comes.  The answer's TLVs are left in @buf, *@rlen bytes of them; its
 * status is returned.  The caller holds the command lock. */
static NDIS_STATUS command(WdiHost *w, UINT32 msg, UINT16 port, UINT8 *buf, UINT32 tlen, UINT32 done_id,
                           UINT32 *rlen, UINT32 wait_s)
{
    WDI_MESSAGE_HEADER *h = (WDI_MESSAGE_HEADER *)buf;
    memset(h, 0, sizeof(*h));
    h->PortId = port;
    h->TransactionId = ++w->next_tid ? w->next_tid : ++w->next_tid;
    if (done_id) {
        nt_KeClearEvent(&w->task_done);
        w->task_done_id = done_id;
        w->task_status = NDIS_STATUS_FAILURE;
        __atomic_store_n(&w->task_tid, h->TransactionId, __ATOMIC_RELEASE);
    }
    UINT32 written = 0;
    NDIS_STATUS st = w->request(w->adapter, WDI_OID(msg), port, buf, sizeof(*h) + tlen, CMD_BUF, &written);
    if (rlen) *rlen = written > sizeof(*h) ? written - sizeof(*h) : 0;
    if (st == NDIS_STATUS_SUCCESS && written >= sizeof(*h) && h->Status != NDIS_STATUS_SUCCESS) st = h->Status;
    if (done_id && st == NDIS_STATUS_SUCCESS) {
        INT64 t = -(INT64)wait_s * 10000000;
        if (nt_KeWaitForSingleObject(&w->task_done, 0, 0, 0, &t) != NT_STATUS_SUCCESS) {
            kprintf("[WDI] %s: task %u did not complete in %u s\n", w->info.service, msg, wait_s);
            st = NDIS_STATUS_FAILURE;
        } else {
            st = w->task_status;
        }
    }
    if (done_id) __atomic_store_n(&w->task_tid, 0, __ATOMIC_RELEASE);
    return st;
}

static void mac_str(const UINT8 *m, char *out)
{
    ksnprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* OID_WDI_GET_ADAPTER_CAPABILITIES: what the adapter is */
static bool get_capabilities(WdiHost *w, UINT8 *buf)
{
    UINT32 n = 0;
    NDIS_STATUS st = command(w, WDI_GET_ADAPTER_CAPABILITIES, WDI_PORT_ID_ADAPTER, buf, 0, 0, &n, 0);
    if (st != NDIS_STATUS_SUCCESS) {
        kprintf("[WDI] %s: adapter capabilities failed (0x%08x)\n", w->info.service, (UINT32)st);
        return false;
    }
    const UINT8 *m = buf + sizeof(WDI_MESSAGE_HEADER);
    UINT32 ilen;
    const UINT8 *ia = tlv_find(m, n, WDI_TLV_INTERFACE_ATTRIBUTES, &ilen);
    WDI_INTERFACE_CAPABILITIES ic;
    if (!ia || !tlv_get(ia, ilen, WDI_TLV_INTERFACE_CAPABILITIES, &ic, sizeof(ic))) {
        kprintf("[WDI] %s: adapter capabilities have no interface capabilities\n", w->info.service);
        return false;
    }
    memcpy(w->info.mac, ic.Address, 6);
    w->info.hw_radio = ic.HardwareRadioState;
    w->info.sw_radio = ic.SoftwareRadioState;
    w->info.max_rx_kbps = ic.MaxRxRate;
    w->info.max_tx_kbps = ic.MaxTxRate;
    UINT32 fl;
    const UINT8 *fw = tlv_find(ia, ilen, WDI_TLV_FIRMWARE_VERSION, &fl);
    if (fw) {
        if (fl >= sizeof(w->info.firmware)) fl = sizeof(w->info.firmware) - 1;
        memcpy(w->info.firmware, fw, fl);
        w->info.firmware[fl] = 0;
    }
    /* (one WDI_TLV_BAND_INFO per band) */
    UINT32 off = 0;
    while (off + sizeof(WDI_TLV_HEADER) <= n) {
        const WDI_TLV_HEADER *t = (const WDI_TLV_HEADER *)(m + off);
        if (off + sizeof(*t) + t->Length > n) break;
        WDI_BAND_CAPABILITIES bc;
        if (t->Type == WDI_TLV_BAND_INFO &&
            tlv_get(m + off + sizeof(*t), t->Length, WDI_TLV_BAND_CAPABILITIES, &bc, sizeof(bc)) && bc.BandState &&
            bc.BandID < 32)
            w->info.bands |= 1u << bc.BandID;
        off += sizeof(*t) + t->Length;
    }
    /* (the data path's: the TxMgr's queueing and how frames complete) */
    UINT32 dl;
    const UINT8 *da = tlv_find(m, n, WDI_TLV_DATAPATH_ATTRIBUTES, &dl);
    memset(&w->dpcaps, 0, sizeof(w->dpcaps));
    w->dpcaps.MaxNumPeers = 1;
    w->dpcaps.TxTargetPriorityQueueing = 1;
    if (da) tlv_get(da, dl, WDI_TLV_DATAPATH_CAPABILITIES, &w->dpcaps, sizeof(w->dpcaps));
    char mac[18];
    mac_str(w->info.mac, mac);
    kprintf("[WDI] %s: adapter capabilities: MAC %s, firmware %s, radio %s/%s, bands%s%s%s\n", w->info.service, mac,
            w->info.firmware[0] ? w->info.firmware : "(none)", w->info.hw_radio ? "on" : "off",
            w->info.sw_radio ? "on" : "off", w->info.bands & (1u << WDI_BAND_ID_2400) ? " 2.4" : "",
            w->info.bands & (1u << WDI_BAND_ID_5000) ? " 5" : "", w->info.bands & (1u << WDI_BAND_ID_6000) ? " 6" : "");
    return true;
}

/* OID_WDI_SET_ADAPTER_CONFIGURATION: how link quality maps to the five
 * signal bars (the one setting the message must have), no platform-level
 * device reset */
static bool set_configuration(WdiHost *w, UINT8 *buf)
{
    static const WDI_LINK_QUALITY_BAR_MAP_ENTRY bars[] = {
        { 0, 19, 1 }, { 20, 39, 2 }, { 40, 59, 3 }, { 60, 79, 4 }, { 80, 100, 5 },
    };
    UINT8 pldr = 0;
    UINT8 *t = buf + sizeof(WDI_MESSAGE_HEADER);
    UINT32 tlen = tlv_put(t, 0, WDI_TLV_LINK_QUALITY_BAR_MAP, bars, sizeof(bars));
    tlen = tlv_put(t, tlen, WDI_TLV_PLDR_SUPPORT, &pldr, 1);
    NDIS_STATUS st = command(w, WDI_SET_ADAPTER_CONFIGURATION, WDI_PORT_ID_ADAPTER, buf, tlen, 0, NULL, 0);
    kprintf("[WDI] %s: adapter configured (0x%08x)\n", w->info.service, (UINT32)st);
    return st == NDIS_STATUS_SUCCESS;
}

/* OID_WDI_TASK_CREATE_PORT: the station port scans and connections use */
static bool create_port(WdiHost *w, UINT8 *buf)
{
    WDI_CREATE_PORT_PARAMETERS p = { WDI_OPERATION_MODE_STA, 0 };
    UINT32 tlen = tlv_put(buf + sizeof(WDI_MESSAGE_HEADER), 0, WDI_TLV_CREATE_PORT_PARAMETERS, &p, sizeof(p));
    memset(&w->port_attr, 0, sizeof(w->port_attr));
    NDIS_STATUS st = command(w, WDI_TASK_CREATE_PORT, WDI_PORT_ID_ADAPTER, buf, tlen,
                             WDI_INDICATION_CREATE_PORT_COMPLETE, NULL, TASK_WAIT_S);
    if (st != NDIS_STATUS_SUCCESS) {
        kprintf("[WDI] %s: creating the station port failed (0x%08x)\n", w->info.service, (UINT32)st);
        return false;
    }
    w->port_id = w->port_attr.PortNumber;
    w->info.port = true;
    char mac[18];
    mac_str(w->port_attr.MacAddress, mac);
    kprintf("[WDI] %s: station port %u created (MAC %s)\n", w->info.service, w->port_id, mac);
    return true;
}

static void delete_port(WdiHost *w, UINT8 *buf)
{
    WDI_DELETE_PORT_PARAMETERS p = { w->port_id };
    UINT32 tlen = tlv_put(buf + sizeof(WDI_MESSAGE_HEADER), 0, WDI_TLV_DELETE_PORT_PARAMETERS, &p, sizeof(p));
    NDIS_STATUS st = command(w, WDI_TASK_DELETE_PORT, WDI_PORT_ID_ADAPTER, buf, tlen,
                             WDI_INDICATION_DELETE_PORT_COMPLETE, NULL, TASK_WAIT_S);
    kprintf("[WDI] %s: station port %u deleted (0x%08x)\n", w->info.service, w->port_id, (UINT32)st);
    w->info.port = false;
}

/* -----------------------------------------------------------------------
 * BSS entries: what an access point's beacon or probe response says
 * ----------------------------------------------------------------------- */

static const char *akm_auth(UINT8 akm)
{
    switch (akm) {
    case 1: case 3: case 5: return "WPA2-Enterprise";
    case 2: case 4: case 6: return "WPA2-Personal";
    case 8: case 9:         return "WPA3-Personal";
    case 12: case 13:       return "WPA3-Enterprise";
    case 18:                return "OWE";
    default:                return "WPA2";
    }
}

/* The frame's fixed fields and elements (WDI_TLV_BEACON_FRAME or
 * WDI_TLV_PROBE_RESPONSE_FRAME: the frame body, timestamp first; a frame
 * that still has its 24-byte MAC header is skipped past it) */
static void parse_frame(WlanBss *b, const UINT8 *f, UINT32 n)
{
    if (n >= 36 && (f[0] == 0x80 || f[0] == 0x50) && f[1] == 0 && !memcmp(f + 16, b->bssid, 6)) {
        f += 24;
        n -= 24;
    }
    if (n < 12) return;
    UINT16 cap = (UINT16)(f[10] | f[11] << 8);
    b->privacy = cap & 0x10;
    bool rsn = false, wpa = false, ht = false, vht = false, he = false;
    for (UINT32 off = 12; off + 2 <= n;) {
        UINT8 id = f[off], len = f[off + 1];
        const UINT8 *e = f + off + 2;
        if (off + 2 + len > n) break;
        if (id == 0 && len <= 32) {                                  /* SSID */
            memcpy(b->ssid, e, len);
            b->ssid[len] = 0;
        } else if (id == 3 && len >= 1 && !b->channel) {             /* DS Parameter Set */
            b->channel = e[0];
        } else if (id == 48 && len >= 8) {                           /* RSN */
            rsn = true;
            if (2u + len <= sizeof(b->rsn)) {                        /* (kept: the 4-way handshake checks it) */
                memcpy(b->rsn, e - 2, 2u + len);
                b->rsn_len = (UINT8)(2 + len);
            }
            static const UINT8 ieee[3] = { 0x00, 0x0F, 0xAC };
            if (!memcmp(e + 2, ieee, 3)) b->group = e[5];
            UINT32 p = 6;                                            /* (version, group cipher) */
            UINT16 np = (UINT16)(e[p] | e[p + 1] << 8);
            p += 2;
            b->cipher = "CCMP";
            for (UINT16 i = 0; i < np && p + 4 <= len; i++, p += 4) {
                if (!memcmp(e + p, ieee, 3) && e[p + 3] < 32) b->pairwise |= 1u << e[p + 3];
                if (e[p + 3] == 8 || e[p + 3] == 9) b->cipher = "GCMP";
                else if (e[p + 3] == 2 && np == 1) b->cipher = "TKIP";
            }
            if (p + 2 <= len) {
                UINT16 na = (UINT16)(e[p] | e[p + 1] << 8);
                p += 2;
                b->auth = NULL;
                for (UINT16 i = 0; i < na && p + 4 <= len; i++, p += 4) {
                    const char *a = akm_auth(e[p + 3]);
                    if (!memcmp(e + p, ieee, 3) && e[p + 3] < 32) b->akms |= 1u << e[p + 3];
                    /* (an AP offering SAE and PSK, WPA3 transition mode, shows as WPA3) */
                    if (!b->auth || !strcmp(a, "WPA3-Personal")) b->auth = a;
                }
                if (p + 2 <= len) b->rsn_caps = (UINT16)(e[p] | e[p + 1] << 8);
            }
        } else if (id == 221 && len >= 4 && e[0] == 0x00 && e[1] == 0x50 && e[2] == 0xF2 && e[3] == 1) {
            wpa = true;                                              /* (WPA, before RSN) */
        } else if (id == 45) {
            ht = true;
        } else if (id == 191) {
            vht = true;
        } else if (id == 255 && len >= 1 && e[0] == 35) {            /* (element ID extension: HE capabilities) */
            he = true;
        }
        off += 2u + len;
    }
    if (!rsn) {
        b->auth = wpa ? "WPA-Personal" : "Open";
        b->cipher = wpa ? "TKIP" : b->privacy ? "WEP" : "None";
    }
    if (!b->auth) b->auth = "WPA2";
    b->radio = he ? "802.11ax" : vht ? "802.11ac" : ht ? "802.11n" : b->band == WDI_BAND_ID_2400 ? "802.11g" : "802.11a";
}

/* One WDI_TLV_BSS_ENTRY into the list (replacing the entry with its BSSID) */
static void add_bss(WdiHost *w, const UINT8 *v, UINT32 n)
{
    WlanBss b;
    memset(&b, 0, sizeof(b));
    if (!tlv_get(v, n, WDI_TLV_BSSID, b.bssid, 6)) return;
    WDI_SIGNAL_INFO si;
    if (tlv_get(v, n, WDI_TLV_BSS_ENTRY_SIGNAL_INFO, &si, sizeof(si))) {
        b.rssi = si.RSSI;
        b.quality = si.LinkQuality > 100 ? 100 : si.LinkQuality;
    }
    WDI_BSS_ENTRY_CHANNEL_INFO ci;
    if (tlv_get(v, n, WDI_TLV_BSS_ENTRY_CHANNEL_INFO, &ci, sizeof(ci))) {
        b.channel = ci.ChannelNumber;
        b.band = ci.BandId;
    }
    UINT32 fl;
    const UINT8 *f = tlv_find(v, n, WDI_TLV_PROBE_RESPONSE_FRAME, &fl);
    if (!f) f = tlv_find(v, n, WDI_TLV_BEACON_FRAME, &fl);
    if (f) parse_frame(&b, f, fl);
    else { b.auth = "Open"; b.cipher = "None"; b.radio = "802.11"; }
    UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->bss_lock);
    int i;
    for (i = 0; i < w->nbss && memcmp(w->bss[i].bssid, b.bssid, 6); i++) {}
    if (i < WLAN_MAX_BSS) {
        w->bss[i] = b;
        w->bss_raw_len[i] = (UINT16)(n <= BSS_RAW ? n : 0);
        if (n <= BSS_RAW) memcpy(w->bss_raw[i], v, n);
        if (i == w->nbss) w->nbss++;
    }
    nt_KeReleaseSpinLock(&w->bss_lock, old);
}

/* -----------------------------------------------------------------------
 * The data path: the TxMgr and RxMgr the driver's TAL calls (its
 * NdisMiniportDataPathHandle is the WdiHost), and the TAL calls made here
 * ----------------------------------------------------------------------- */
typedef NDIS_STATUS (NT_API *TAL_INIT)(void *mp_ctx, void *dp, NDIS_WDI_DATA_API *api, void **tal,
                                       NDIS_MINIPORT_WDI_DATA_HANDLERS *handlers, UINT32 *meta_extra);
typedef void        (NT_API *TAL_FN)(void *tal);
typedef NDIS_STATUS (NT_API *TAL_START)(void *tal, WDI_TXRX_TARGET_CONFIGURATION *cfg, TAL_TXRX_PARAMETERS *out);
typedef void        (NT_API *TAL_ADD_PORT)(void *tal, UINT16 port, UINT32 opmode);
typedef void        (NT_API *TAL_PORT)(void *tal, UINT16 port);
typedef void        (NT_API *TAL_PEER_CONFIG)(void *tal, UINT16 port, WDI_PEER_ID peer, UINT32 *qos);
typedef void        (NT_API *TAL_PEER_DELETE_CONFIRM)(void *tal, UINT16 port, WDI_PEER_ID peer);
typedef void        (NT_API *TX_DESC_INIT)(void *tal, NET_BUFFER_LIST *nbl, NDIS_STATUS *st);
typedef void        (NT_API *TX_DESC_DEINIT)(void *tal, NET_BUFFER_LIST *nbl);
typedef void        (NT_API *TX_DATA_SEND)(void *tal, UINT16 port, WDI_PEER_ID peer, UINT8 tid, UINT16 queued,
                                           UINT32 active, UINT8 robust);
typedef void        (NT_API *RX_GET_MPDUS)(void *tal, WDI_PEER_ID peer, UINT8 tid, NET_BUFFER_LIST **out);
typedef void        (NT_API *RX_RETURN)(void *tal, NET_BUFFER_LIST *nbl);

#define TAL_CALL(w, fn, type, ...) do { if ((w)->dh.fn) ((type)(w)->dh.fn)(__VA_ARGS__); } while (0)

static WdiHost *g_net_host;             /* the connected adapter the stack uses */

static UINT8 *mdl_va(NT_MDL *m)
{
    if (m->MdlFlags & (NT_MDL_MAPPED_TO_SYSTEM_VA | NT_MDL_SOURCE_IS_NONPAGED_POOL)) return m->MappedSystemVa;
    return (UINT8 *)m->StartVa + m->ByteOffset;
}

/* Copy an NBL's first NET_BUFFER (its MDL chain) into @out: its length */
static UINT32 nb_copy(NET_BUFFER_LIST *nbl, UINT8 *out, UINT32 cap)
{
    NET_BUFFER *nb = nbl->FirstNetBuffer;
    if (!nb) return 0;
    UINT32 left = nb->DataLength, off = nb->CurrentMdlOffset, n = 0;
    for (NT_MDL *m = nb->CurrentMdl; m && left && n < cap; m = m->Next, off = 0) {
        UINT32 k = m->ByteCount > off ? m->ByteCount - off : 0;
        if (k > left) k = left;
        if (k > cap - n) k = cap - n;
        memcpy(out + n, mdl_va(m) + off, k);
        n += k;
        left -= k;
    }
    return n;
}

/* TxMgr: the driver takes queued frames (each given its target descriptor first) */
static NT_API void dp_tx_dequeue(void *h, UINT32 quantum, UINT8 max, UINT16 credit, NET_BUFFER_LIST **out)
{
    (void)quantum; (void)credit;
    WdiHost *w = h;
    *out = NULL;
    NET_BUFFER_LIST *head = NULL, **tail = &head;
    UINT32 limit = max == 0xFF || !max ? TX_FRAMES : max;
    for (UINT32 k = 0; k < limit; k++) {
        UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->tx_lock);
        TxFrame *f = NULL;
        if (w->txq_tail != w->txq_head) f = &w->tx[w->txq[w->txq_tail++ % TX_FRAMES]];
        nt_KeReleaseSpinLock(&w->tx_lock, old);
        if (!f) break;
        NDIS_STATUS st = NDIS_STATUS_SUCCESS;
        TAL_CALL(w, TxTargetDescInitHandler, TX_DESC_INIT, w->tal, &f->nbl, &st);
        if (st != NDIS_STATUS_SUCCESS) {            /* (no descriptor for it: dropped, as a full queue drops) */
            __atomic_store_n(&f->state, TXF_FREE, __ATOMIC_RELEASE);
            continue;
        }
        __atomic_store_n(&f->state, TXF_DRIVER, __ATOMIC_RELEASE);
        f->nbl.Next = NULL;
        *tail = &f->nbl;
        tail = &f->nbl.Next;
    }
    *out = head;
}

static TxFrame *tx_frame_of(WdiHost *w, NET_BUFFER_LIST *nbl)
{
    TxFrame *f = (TxFrame *)((UINT8 *)nbl - __builtin_offsetof(TxFrame, nbl));
    return w->tx && f >= w->tx && f < w->tx + TX_FRAMES ? f : NULL;
}

/* TxMgr: the driver has transferred frames to its hardware (and, as no
 * frame asks for a send completion, they are done) */
static NT_API void dp_tx_transfer_complete(void *h, UINT32 status, NET_BUFFER_LIST *nbl)
{
    (void)status;
    WdiHost *w = h;
    for (NET_BUFFER_LIST *next; nbl; nbl = next) {
        next = nbl->Next;
        TxFrame *f = tx_frame_of(w, nbl);
        if (!f) continue;
        TAL_CALL(w, TxTargetDescDeInitHandler, TX_DESC_DEINIT, w->tal, nbl);
        w->info.tx_frames++;
        __atomic_store_n(&f->state, TXF_FREE, __ATOMIC_RELEASE);
    }
}

static NT_API void dp_tx_send_complete(void *h, UINT32 status, UINT16 n, WDI_FRAME_ID *ids, void *data)
{
    (void)h; (void)status; (void)n; (void)ids; (void)data;     /* (frames here never ask for one) */
}

static NT_API void dp_tx_query_ratid(void *h, UINT16 port, WDI_PEER_ID peer, UINT8 tid, NDIS_STATUS *st, UINT16 *qlen)
{
    (void)port; (void)peer; (void)tid;
    WdiHost *w = h;
    *st = NDIS_STATUS_SUCCESS;
    *qlen = (UINT16)(w->txq_head - w->txq_tail);
}

static NT_API void dp_tx_pause(void *h, UINT16 port, WDI_PEER_ID peer, UINT32 tids, UINT32 reason)
{
    (void)port; (void)peer; (void)tids; (void)reason;
    ((WdiHost *)h)->tx_paused = true;
}

static void tx_kick(WdiHost *w);
static NT_API void dp_tx_restart(void *h, UINT16 port, WDI_PEER_ID peer, UINT32 tids, UINT32 reason)
{
    (void)port; (void)peer; (void)tids; (void)reason;
    WdiHost *w = h;
    w->tx_paused = false;
    tx_kick(w);
}

static NT_API void dp_tx_release_frames(void *h, UINT16 port, WDI_PEER_ID peer, UINT32 tids, UINT8 max, UINT16 credit,
                                        NET_BUFFER_LIST **out)
{
    (void)port; (void)peer; (void)tids;
    dp_tx_dequeue(h, 0, max, credit, out);         /* (one queue per port: the same frames) */
}

static NT_API void dp_tx_inject(void *h, UINT16 port, WDI_PEER_ID peer, UINT8 tid, NET_BUFFER_LIST *nbl, UINT8 unicast,
                                UINT8 legacy, UINT16 ethertype, UINT32 exemption)
{
    (void)h; (void)port; (void)peer; (void)tid; (void)nbl; (void)unicast; (void)legacy; (void)ethertype; (void)exemption;
    kprintf("[WDI] the driver injected a frame of its own (not supported yet)\n");
}

static NT_API void dp_confirm(void *h) { (void)h; }   /* (TX abort, RX stop and RX flush: never asked for) */

static void rx_push(WdiHost *w, const UINT8 *f, UINT32 n);

/* RxMgr: the driver has frames from @peer: take them, then give them back */
static NT_API void dp_rx_inorder(void *h, UINT32 level, WDI_PEER_ID peer, UINT8 tid, NDIS_RECEIVE_THROTTLE_PARAMETERS *thr,
                                 NDIS_STATUS *st)
{
    (void)level; (void)thr;
    WdiHost *w = h;
    static UINT8 frame[2400];               /* (RX indications are serialized per adapter: one at a time here) */
    for (int rounds = 0; rounds < 16; rounds++) {
        NET_BUFFER_LIST *nbl = NULL;
        TAL_CALL(w, RxGetMpdusHandler, RX_GET_MPDUS, w->tal, peer, tid, &nbl);
        if (!nbl) break;
        for (NET_BUFFER_LIST *x = nbl; x; x = x->Next) {
            UINT32 n = nb_copy(x, frame, sizeof(frame));
            rx_push(w, frame, n);
        }
        TAL_CALL(w, RxReturnFramesHandler, RX_RETURN, w->tal, nbl);
    }
    if (st) *st = NDIS_STATUS_SUCCESS;
}

/* The driver associated: its peer is the access point */
static NT_API void dp_peer_create(void *h, UINT16 port, WDI_PEER_ID peer, WDI_MAC_ADDRESS mac)
{
    WdiHost *w = h;
    w->peer = peer;
    memcpy(w->peer_mac, mac.Address, 6);
    w->peer_deleted = false;
    __atomic_store_n(&w->peer_up, true, __ATOMIC_RELEASE);
    char m[18];
    mac_str(mac.Address, m);
    kprintf("[WDI] %s: peer %u on port %u: %s\n", w->info.service, peer, port, m);
}

static NT_API void dp_peer_delete(void *h, UINT16 port, WDI_PEER_ID peer, NDIS_STATUS *st)
{
    WdiHost *w = h;
    __atomic_store_n(&w->peer_up, false, __ATOMIC_RELEASE);
    w->peer_deleted = true;
    kprintf("[WDI] %s: peer %u on port %u deleted\n", w->info.service, peer, port);
    if (st) *st = NDIS_STATUS_SUCCESS;
}

static NT_API WDI_FRAME_METADATA *dp_alloc_meta(void *h)
{
    WdiHost *w = h;
    WDI_FRAME_METADATA *m = nt_ExAllocatePoolWithTag(0, sizeof(*m) + w->meta_extra, POOL_TAG);
    if (m) memset(m, 0, sizeof(*m) + w->meta_extra);
    return m;
}

static NT_API void dp_free_meta(void *h, WDI_FRAME_METADATA *m) { (void)h; if (m) nt_ExFreePool(m); }

static NT_API void dp_tx_suspect(void *h, UINT64 ctx, NET_BUFFER_LIST *nbl, UINT8 *transfer, UINT8 *send, NDIS_STATUS *st)
{
    (void)h; (void)ctx; (void)nbl;
    *transfer = 1;
    *send = 0;
    *st = NDIS_STATUS_SUCCESS;
}

static NDIS_WDI_DATA_API g_data_api = {
    { NDIS_OBJECT_TYPE_WDI_DATA_API, 2, sizeof(NDIS_WDI_DATA_API) },
    (void *)dp_tx_dequeue, (void *)dp_tx_transfer_complete, (void *)dp_tx_send_complete, (void *)dp_tx_query_ratid,
    (void *)dp_tx_pause, (void *)dp_tx_restart, (void *)dp_tx_release_frames, (void *)dp_tx_inject,
    (void *)dp_confirm, (void *)dp_rx_inorder, (void *)dp_confirm, (void *)dp_confirm, (void *)dp_peer_create,
    (void *)dp_peer_delete, (void *)dp_alloc_meta, (void *)dp_free_meta, (void *)dp_tx_suspect,
};

static void tx_free_all(WdiHost *w)
{
    if (!w->tx) return;
    for (int i = 0; i < TX_FRAMES; i++) {
        if (w->tx[i].mdl) nt_IoFreeMdl(w->tx[i].mdl);
        kfree(w->tx[i].buf);
    }
    kfree(w->tx);
    w->tx = NULL;
    kfree(w->rx);
    w->rx = NULL;
}

/* MiniportWdiTalTxRxInitialize: the driver's data path handlers */
static bool tal_initialize(WdiHost *w)
{
    if (!w->tal_init) {
        kprintf("[WDI] %s: the driver has no data path (TalTxRxInitialize)\n", w->info.service);
        return false;
    }
    w->tx = kzalloc(sizeof(TxFrame) * TX_FRAMES);
    w->rx = kzalloc((size_t)RX_SLOTS * RX_FRAME);
    if (!w->tx || !w->rx) { tx_free_all(w); return false; }
    for (int i = 0; i < TX_FRAMES; i++) {
        TxFrame *f = &w->tx[i];
        f->buf = kzalloc(TX_FRAME_BUF);
        f->mdl = f->buf ? nt_IoAllocateMdl(f->buf, TX_FRAME_BUF, 0, 0, NULL) : NULL;
        if (!f->mdl) { tx_free_all(w); return false; }
        nt_MmBuildMdlForNonPagedPool(f->mdl);
    }
    w->txq_head = w->txq_tail = w->rx_head = w->rx_tail = 0;
    w->tx_lock = w->rx_lock = 0;
    memset(&w->dh, 0, sizeof(w->dh));
    w->dh.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_WDI_DATA_HANDLERS;
    w->dh.Header.Revision = 2;
    w->dh.Header.Size = sizeof(w->dh);
    w->meta_extra = 0;
    w->tal = NULL;
    NDIS_STATUS st = ((TAL_INIT)w->tal_init)(w->mp_ctx, w, &g_data_api, &w->tal, &w->dh, &w->meta_extra);
    kprintf("[WDI] %s: data path initialized (0x%08x)%s%s\n", w->info.service, (UINT32)st,
            w->dh.TxDataSendHandler ? ", TX" : "", w->dh.RxGetMpdusHandler ? ", RX" : "");
    if (st != NDIS_STATUS_SUCCESS || !w->dh.TxDataSendHandler || !w->dh.RxGetMpdusHandler ||
        !w->dh.TalTxRxStartHandler) {
        if (st == NDIS_STATUS_SUCCESS && w->tal_deinit) ((TAL_FN)w->tal_deinit)(w->tal);
        w->tal = NULL;
        tx_free_all(w);
        return false;
    }
    return true;
}

/* TalTxRxStart: the data path configured from the adapter's capabilities */
static bool tal_start(WdiHost *w)
{
    WDI_TXRX_TARGET_CONFIGURATION cfg;
    memset(&cfg, 0, sizeof(cfg));
    WDI_TXRX_CAPABILITIES *c = &cfg.TxRxCapabilities;
    c->InterconnectType = w->dpcaps.InterconnectType;
    c->TargetPriorityQueueing = w->dpcaps.TxTargetPriorityQueueing;
    c->MaxScatterGatherElementsPerFrame = w->dpcaps.TxMaxScatterGatherElementsPerFrame;
    c->ExplicitSendCompleteFlagRequired = w->dpcaps.TxExplicitSendCompleteFlagRequired;
    c->MinEffectiveSize = w->dpcaps.TxMinEffectiveFrameSize;
    c->FrameSizeGranularity = w->dpcaps.TxFrameSizeGranularity;
    c->RxTxForwarding = w->dpcaps.RxTxForwarding;
    c->MaxThroughput = w->dpcaps.RxMaxThroughput;
    cfg.MaxNumPorts = 1;
    cfg.MaxNumPeers = w->dpcaps.MaxNumPeers ? w->dpcaps.MaxNumPeers : 1;
    TAL_TXRX_PARAMETERS out = { 0 };
    NDIS_STATUS st = ((TAL_START)w->dh.TalTxRxStartHandler)(w->tal, &cfg, &out);
    kprintf("[WDI] %s: data path started (0x%08x): %s queueing, %u peer(s), %u transfers at a time\n",
            w->info.service, (UINT32)st, c->TargetPriorityQueueing ? "port" : "peer/TID", cfg.MaxNumPeers,
            out.MaxOutstandingTransfers);
    w->tal_started = st == NDIS_STATUS_SUCCESS;
    return w->tal_started;
}

static void tal_shutdown(WdiHost *w)
{
    if (!w->tal) return;
    if (w->tal_started) TAL_CALL(w, TalTxRxStopHandler, TAL_FN, w->tal);
    w->tal_started = false;
    if (w->tal_deinit) ((TAL_FN)w->tal_deinit)(w->tal);
    w->tal = NULL;
    for (int i = 0; w->tx && i < TX_FRAMES; i++)
        if (w->tx[i].state == TXF_DRIVER) kprintf("[WDI] %s: a frame was still with the driver\n", w->info.service);
    tx_free_all(w);
    kprintf("[WDI] %s: data path stopped (%llu frames sent, %llu received)\n", w->info.service,
            (unsigned long long)w->info.tx_frames, (unsigned long long)w->info.rx_frames);
}

/* Tell the driver frames are queued for the access point */
static void tx_kick(WdiHost *w)
{
    if (w->tx_paused || !w->peer_up || w->txq_head == w->txq_tail) return;
    UINT8 tid = w->assoc.WMMQoSEnabled ? 0 : WDI_EXT_TID_NON_QOS;
    UINT8 old = nt_KfRaiseIrql(DISPATCH_LEVEL);
    ((TX_DATA_SEND)w->dh.TxDataSendHandler)(w->tal, w->port_id, w->peer, tid, (UINT16)(w->txq_head - w->txq_tail),
                                            (UINT32)(w->txq_head - w->txq_tail), 0);
    nt_KeLowerIrql(old);
}

/* A frame as an 802.11 MSDU to the access point (ToDS: address 1 the
 * BSSID, 2 the source, 3 the destination; LLC/SNAP, @type, @payload),
 * queued for the driver with its exemption from encryption (WDI_EXEMPT_*);
 * NULL if there is no room or no connection */
static TxFrame *tx_send(WdiHost *w, const UINT8 *dst, const UINT8 *src, UINT16 type, const UINT8 *payload, UINT32 plen,
                        UINT32 exemption)
{
    if (!w->tx || !w->peer_up || DOT11_DATA_HEADER + DOT11_SNAP_LEN + plen > TX_FRAME_BUF) return NULL;
    TxFrame *f = NULL;
    for (int i = 0; i < TX_FRAMES && !f; i++)
        if (__atomic_load_n(&w->tx[i].state, __ATOMIC_ACQUIRE) == TXF_FREE) {
            int want = TXF_FREE;
            if (__atomic_compare_exchange_n(&w->tx[i].state, &want, TXF_QUEUED, false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE))
                f = &w->tx[i];
        }
    if (!f) return NULL;
    DOT11_DATA_HEADER_T *h = (DOT11_DATA_HEADER_T *)f->buf;
    memset(h, 0, sizeof(*h));
    h->FrameControl[0] = DOT11_FC0_DATA;
    h->FrameControl[1] = DOT11_FC1_TO_DS;              /* (the driver sets Protected when it encrypts) */
    memcpy(h->Address1, w->peer_mac, 6);
    memcpy(h->Address2, src, 6);
    memcpy(h->Address3, dst, 6);
    static const UINT8 snap[6] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00 };
    UINT8 *p = f->buf + DOT11_DATA_HEADER;
    memcpy(p, snap, 6);
    p[6] = (UINT8)(type >> 8);
    p[7] = (UINT8)type;
    memcpy(p + 8, payload, plen);
    UINT32 n = DOT11_DATA_HEADER + DOT11_SNAP_LEN + plen;
    f->mdl->ByteCount = n;
    memset(&f->meta, 0, sizeof(f->meta));
    f->meta.pNBL = &f->nbl;
    f->meta.FrameID = ++w->next_frame ? w->next_frame : ++w->next_frame;
    WDI_TX_METADATA *t = &f->meta.u.txMetaData;
    t->PortID = w->port_id;
    t->PeerID = w->peer;
    t->ExTID = w->assoc.WMMQoSEnabled ? 0 : WDI_EXT_TID_NON_QOS;
    t->IsUnicast = !(dst[0] & 1);
    t->Ethertype = type;
    t->ExemptionAction = exemption;
    memset(&f->nbl, 0, sizeof(f->nbl));
    memset(&f->nb, 0, sizeof(f->nb));
    f->nbl.FirstNetBuffer = &f->nb;
    f->nbl.MiniportReserved[0] = &f->meta;
    f->nbl.SourceHandle = w;
    f->nb.MdlChain = f->nb.CurrentMdl = f->mdl;
    f->nb.DataLength = n;
    UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->tx_lock);
    w->txq[w->txq_head++ % TX_FRAMES] = (UINT16)(f - w->tx);
    nt_KeReleaseSpinLock(&w->tx_lock, old);
    tx_kick(w);
    return f;
}

/* Wait (up to a second) for frame @f, frame ID @id, to have gone to the hardware */
static void tx_wait(TxFrame *f, WDI_FRAME_ID id)
{
    for (int k = 0; f && k < 100; k++) {
        if (__atomic_load_n(&f->state, __ATOMIC_ACQUIRE) == TXF_FREE || f->meta.FrameID != id) return;
        INT64 t = -100000;                              /* (10 ms) */
        nt_KeDelayExecutionThread(0, 0, &t);
    }
}

/* An Ethernet frame from the stack (encrypted by the driver on a secured connection) */
static bool wifi_transmit(const void *frame, UINT16 len)
{
    WdiHost *w = g_net_host;
    const UINT8 *e = frame;
    if (!w || len < 14) return false;
    return tx_send(w, e, e + 6, (UINT16)(e[12] << 8 | e[13]), e + 14, len - 14u, WDI_EXEMPT_NO_EXEMPTION) != NULL;
}

static void eapol_push(WdiHost *w, const UINT8 *f, UINT32 n);

/* An 802.11 data frame from the driver as an Ethernet frame for the stack */
static void rx_push(WdiHost *w, const UINT8 *f, UINT32 n)
{
    bool prot = n >= 2 && (f[1] & DOT11_FC1_PROTECTED);
    if (n < DOT11_DATA_HEADER + DOT11_SNAP_LEN || (f[0] & 0x0C) != 0x08 || (prot && !w->keys)) {
        w->rx_dropped++;                                /* (not data, or encrypted with no key to have decrypted it) */
        return;
    }
    bool to = f[1] & DOT11_FC1_TO_DS, from = f[1] & DOT11_FC1_FROM_DS;
    UINT32 hl = DOT11_DATA_HEADER + (to && from ? 6 : 0);
    if (f[0] & 0x80) hl += 2 + ((f[1] & 0x80) ? 4 : 0);    /* (QoS control, and HT control with the Order bit) */
    const UINT8 *da = f + 4, *sa = from ? (to ? f + 24 : f + 16) : f + 10;
    const UINT8 *l = f + hl;
    /* (the driver decrypted a protected frame; it may have left its CCMP header) */
    if (prot && n >= hl + DOT11_CCMP_HEADER + DOT11_SNAP_LEN && !(l[0] == 0xAA && l[1] == 0xAA && l[2] == 0x03) &&
        l[8] == 0xAA && l[9] == 0xAA && l[10] == 0x03) {
        hl += DOT11_CCMP_HEADER;
        l += DOT11_CCMP_HEADER;
    }
    if (n < hl + DOT11_SNAP_LEN || l[0] != 0xAA || l[1] != 0xAA || l[2] != 0x03) { w->rx_dropped++; return; }
    UINT16 type = (UINT16)(l[6] << 8 | l[7]);
    if (type == ETHERTYPE_EAPOL) {                      /* (the supplicant's, not the stack's) */
        eapol_push(w, l + DOT11_SNAP_LEN, n - hl - DOT11_SNAP_LEN);
        return;
    }
    if (w->rsna && !prot) { w->rx_dropped++; return; }  /* (ExcludeUnencrypted: only EAPOL comes in the clear) */
    UINT32 plen = n - hl - 6;                           /* (the Ethertype and the payload) */
    if (12 + plen > RX_FRAME) { w->rx_dropped++; return; }
    UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->rx_lock);
    if (w->rx_head - w->rx_tail < RX_SLOTS) {
        UINT32 k = w->rx_head % RX_SLOTS;
        UINT8 *e = w->rx + (UINT64)k * RX_FRAME;
        memcpy(e, da, 6);
        memcpy(e + 6, sa, 6);
        memcpy(e + 12, l + 6, plen);
        w->rx_len[k] = (UINT16)(12 + plen);
        w->rx_head++;
        w->info.rx_frames++;
    } else {
        w->rx_dropped++;
    }
    nt_KeReleaseSpinLock(&w->rx_lock, old);
}

static int wifi_receive(void *buf, int cap)
{
    WdiHost *w = g_net_host;
    if (!w || !w->rx) return 0;
    int n = 0;
    UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->rx_lock);
    if (w->rx_tail != w->rx_head) {
        UINT32 k = w->rx_tail % RX_SLOTS;
        n = w->rx_len[k] < cap ? w->rx_len[k] : cap;
        memcpy(buf, w->rx + (UINT64)k * RX_FRAME, (size_t)n);
        w->rx_tail++;
    }
    nt_KeReleaseSpinLock(&w->rx_lock, old);
    return n;
}

static const char *wifi_name(void) { return g_net_host ? g_net_host->info.description : "Wi-Fi"; }
static void wifi_mac(UINT8 mac[6]) { if (g_net_host) memcpy(mac, g_net_host->info.mac, 6); }
static bool wifi_link(void) { return g_net_host && g_net_host->info.connected && g_net_host->peer_up; }
static const NetAdapter g_wifi_ops = { wifi_name, wifi_mac, wifi_link, wifi_transmit, wifi_receive };

bool WlanCarriesNet(void) { return g_net_host != NULL; }

/* -----------------------------------------------------------------------
 * WPA2: EAPOL frames to the supplicant (wpa.c), its replies, the keys
 * ----------------------------------------------------------------------- */
static NT_API void eapol_work(NT_DEVICE_OBJECT *dev, void *ctx);

/* An EAPOL frame from the access point (from rx_push, at DISPATCH_LEVEL):
 * queued for the handshake WlanConnect runs, or for the work item that
 * answers a rekey */
static void eapol_push(WdiHost *w, const UINT8 *f, UINT32 n)
{
    if (!w->rsna || n > EAPOL_MAX) { w->rx_dropped++; return; }
    UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->eapol_lock);
    bool room = w->eapol_head - w->eapol_tail < EAPOL_SLOTS;
    if (room) {
        UINT32 k = w->eapol_head++ % EAPOL_SLOTS;
        memcpy(w->eapol[k], f, n);
        w->eapol_len[k] = (UINT16)n;
    }
    nt_KeReleaseSpinLock(&w->eapol_lock, old);
    if (!room) { w->rx_dropped++; return; }
    if (w->handshaking) nt_KeSetEvent(&w->eapol_event, 0, 0);
    else if (w->eapol_work && !__atomic_exchange_n(&w->work_queued, true, __ATOMIC_ACQ_REL))
        nt_IoQueueWorkItem(w->eapol_work, eapol_work, NT_DelayedWorkQueue, w);
}

static bool eapol_pop(WdiHost *w, UINT8 *out, UINT32 *n)
{
    UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->eapol_lock);
    bool got = w->eapol_tail != w->eapol_head;
    if (got) {
        UINT32 k = w->eapol_tail++ % EAPOL_SLOTS;
        *n = w->eapol_len[k];
        memcpy(out, w->eapol[k], *n);
        memset(w->eapol[k], 0, *n);
    }
    nt_KeReleaseSpinLock(&w->eapol_lock, old);
    return got;
}

/* OID_WDI_SET_ADD_CIPHER_KEYS: the handshake's pairwise key (for the
 * access point, both ways) and its group key (by index, inbound), CCMP;
 * or the group key alone (a rekey) */
static bool install_keys(WdiHost *w, UINT8 *buf, bool pairwise)
{
    UINT8 *t = buf + sizeof(WDI_MESSAGE_HEADER), k[128];
    UINT32 tlen = 0, n;
    if (pairwise) {
        WDI_CIPHER_KEY_TYPE_INFO ti = { WDI_CIPHER_ALGO_CCMP, WDI_CIPHER_KEY_DIRECTION_BOTH, 0,
                                        WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY };
        n = tlv_put(k, 0, WDI_TLV_PEER_MAC_ADDRESS, w->peer_mac, 6);
        n = tlv_put(k, n, WDI_TLV_CIPHER_KEY_TYPE_INFO, &ti, sizeof(ti));
        n = tlv_put(k, n, WDI_TLV_CIPHER_KEY_CCMP_KEY, WpaTk(&w->wpa), WPA_TK_LEN);
        tlen = tlv_put(t, tlen, WDI_TLV_SET_CIPHER_KEY_INFO, k, (UINT16)n);
    }
    UINT32 id = w->wpa.gtk_id;
    WDI_CIPHER_KEY_TYPE_INFO gi = { WDI_CIPHER_ALGO_CCMP, WDI_CIPHER_KEY_DIRECTION_INBOUND, 0,
                                    WDI_CIPHER_KEY_TYPE_GROUP_KEY };
    n = tlv_put(k, 0, WDI_TLV_CIPHER_KEY_ID, &id, sizeof(id));
    n = tlv_put(k, n, WDI_TLV_CIPHER_KEY_TYPE_INFO, &gi, sizeof(gi));
    n = tlv_put(k, n, WDI_TLV_CIPHER_KEY_RECEIVE_SEQUENCE_COUNT, w->wpa.gtk_rsc, 6);
    n = tlv_put(k, n, WDI_TLV_CIPHER_KEY_CCMP_KEY, w->wpa.gtk, w->wpa.gtk_len);
    tlen = tlv_put(t, tlen, WDI_TLV_SET_CIPHER_KEY_INFO, k, (UINT16)n);
    memset(k, 0, sizeof(k));
    NDIS_STATUS st = command(w, WDI_SET_ADD_CIPHER_KEYS, w->port_id, buf, tlen, 0, NULL, 0);
    memset(t, 0, tlen);                                 /* (no key material left in the buffer) */
    if (pairwise) kprintf("[WPA] %s: pairwise key and group key %u installed (0x%08x)\n", w->info.service, id, (UINT32)st);
    else kprintf("[WPA] %s: group key %u installed (0x%08x)\n", w->info.service, id, (UINT32)st);
    if (st == NDIS_STATUS_SUCCESS && pairwise) w->keys = true;
    return st == NDIS_STATUS_SUCCESS;
}

/* The EAPOL frames queued for the supplicant: each one's reply sent (with
 * its exemption: unencrypted until there is a pairwise key), and after
 * message 3 or a group message the keys installed (once the reply has
 * gone, so it goes out under the old keys).  The caller holds the command
 * lock.  Returns the last frame's WPA_* outcome that was not a drop. */
static int eapol_step(WdiHost *w, UINT8 *buf)
{
    static UINT8 frame[EAPOL_MAX], reply[WPA_FRAME_MAX];     /* (under the command lock) */
    UINT32 n, rl;
    int last = WPA_DROP;
    while (eapol_pop(w, frame, &n)) {
        int r = WpaReceive(&w->wpa, frame, n, reply, &rl);
        if (r == WPA_DROP) {
            kprintf("[WPA] %s: EAPOL frame dropped: %s\n", w->info.service, w->wpa.why ? w->wpa.why : "?");
            continue;
        }
        kprintf("[WPA] %s: %s\n", w->info.service,
                r == WPA_REPLY ? "4-way handshake message 1 (the ANonce): sending message 2" :
                r == WPA_PAIRWISE ? "4-way handshake message 3 (MIC good, group key): sending message 4" :
                                    "group key handshake message 1 (a new group key): sending message 2");
        TxFrame *f = tx_send(w, w->peer_mac, w->port_attr.MacAddress, ETHERTYPE_EAPOL, reply, rl,
                             WDI_EXEMPT_ON_KEY_MAPPING_KEY_UNAVAILABLE);
        if (!f) kprintf("[WPA] %s: no room to send the reply\n", w->info.service);
        if (r != WPA_REPLY) {
            tx_wait(f, f ? f->meta.FrameID : 0);
            install_keys(w, buf, r == WPA_PAIRWISE);
        }
        memset(reply, 0, rl);
        last = r;
    }
    memset(frame, 0, sizeof(frame));
    return last;
}

/* A rekey after the connection (the access point's group key handshake, or
 * a new 4-way handshake), at PASSIVE_LEVEL */
static NT_API void eapol_work(NT_DEVICE_OBJECT *dev, void *ctx)
{
    (void)dev;
    WdiHost *w = ctx;
    __atomic_store_n(&w->work_queued, false, __ATOMIC_RELEASE);
    lock(w);
    UINT8 *buf = w->adapter && w->info.connected && w->rsna ? kmalloc(CMD_BUF) : NULL;
    if (buf) {
        eapol_step(w, buf);
        kfree(buf);
    } else {
        UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->eapol_lock);
        w->eapol_tail = w->eapol_head;
        nt_KeReleaseSpinLock(&w->eapol_lock, old);
    }
    unlock(w);
}

/* -----------------------------------------------------------------------
 * Indications (from NdisMIndicateStatusEx, at up to DISPATCH_LEVEL)
 * ----------------------------------------------------------------------- */
void WdiIndicate(void *adapter, UINT32 code, const void *buf, UINT32 len)
{
    WdiHost *w = NULL;
    for (int i = 0; i < WDI_MAX_ADAPTERS && !w; i++)
        if (g_hosts[i].used && g_hosts[i].adapter == adapter) w = &g_hosts[i];
    if (!w || len < sizeof(WDI_MESSAGE_HEADER)) return;
    const WDI_MESSAGE_HEADER *h = buf;
    const UINT8 *m = (const UINT8 *)buf + sizeof(*h);
    UINT32 n = len - sizeof(*h), id = code & 0xFFFF;
    if (id == WDI_INDICATION_BSS_ENTRY_LIST) {
        UINT32 off = 0;
        while (off + sizeof(WDI_TLV_HEADER) <= n) {
            const WDI_TLV_HEADER *t = (const WDI_TLV_HEADER *)(m + off);
            if (off + sizeof(*t) + t->Length > n) break;
            if (t->Type == WDI_TLV_BSS_ENTRY) add_bss(w, m + off + sizeof(*t), t->Length);
            off += sizeof(*t) + t->Length;
        }
    }
    if (id == WDI_INDICATION_ASSOCIATION_RESULT) {      /* (one per access point tried: the last one counts) */
        UINT32 off = 0;
        while (off + sizeof(WDI_TLV_HEADER) <= n) {
            const WDI_TLV_HEADER *t = (const WDI_TLV_HEADER *)(m + off);
            if (off + sizeof(*t) + t->Length > n) break;
            WDI_ASSOCIATION_RESULT_PARAMETERS ap;
            const UINT8 *r = m + off + sizeof(*t);
            if (t->Type == WDI_TLV_ASSOCIATION_RESULT &&
                tlv_get(r, t->Length, WDI_TLV_ASSOCIATION_RESULT_PARAMETERS, &ap, sizeof(ap))) {
                w->assoc = ap;
                w->assoc_seen = true;
                /* the RSN element of the request it sent (capability, listen interval, then elements):
                 * message 2 of the 4-way handshake repeats it */
                UINT32 ql;
                const UINT8 *q = tlv_find(r, t->Length, WDI_TLV_ASSOCIATION_REQUEST_FRAME, &ql);
                w->assoc_ie_len = 0;
                for (UINT32 k = 4; q && k + 2 <= ql && k + 2u + q[k + 1] <= ql; k += 2u + q[k + 1])
                    if (q[k] == 48 && 2u + q[k + 1] <= sizeof(w->assoc_ie)) {
                        memcpy(w->assoc_ie, q + k, 2u + q[k + 1]);
                        w->assoc_ie_len = (UINT8)(2 + q[k + 1]);
                        break;
                    }
            }
            off += sizeof(*t) + t->Length;
        }
    }
    UINT32 tid = __atomic_load_n(&w->task_tid, __ATOMIC_ACQUIRE);
    if (tid && h->TransactionId == tid && id == w->task_done_id) {
        if (id == WDI_INDICATION_CREATE_PORT_COMPLETE)
            tlv_get(m, n, WDI_TLV_PORT_ATTRIBUTES, &w->port_attr, sizeof(w->port_attr));
        w->task_status = h->Status;
        nt_KeSetEvent(&w->task_done, 0, 0);
    }
}

/* -----------------------------------------------------------------------
 * Adapters (ndis.c attaches one after its open, detaches it before the close)
 * ----------------------------------------------------------------------- */
static void disconnect_locked(WdiHost *w, UINT8 *buf, bool quiet);

WdiHost *WdiAttach(void *adapter, const char *service, const char *desc, WdiRequestFn request, void *mp_ctx,
                   void *tal_init, void *tal_deinit)
{
    WdiHost *w = NULL;
    UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&g_lock);
    for (int i = 0; i < WDI_MAX_ADAPTERS && !w; i++)
        if (!g_hosts[i].used) { w = &g_hosts[i]; w->used = true; }
    nt_KeReleaseSpinLock(&g_lock, old);
    UINT8 *buf = w ? kmalloc(CMD_BUF) : NULL;
    if (!buf) {
        if (w) w->used = false;
        kprintf("[WDI] %s: no room for another Wi-Fi adapter\n", service);
        return NULL;
    }
    memset(&w->info, 0, sizeof(w->info));
    strncpy(w->info.service, service, sizeof(w->info.service) - 1);
    strncpy(w->info.description, desc && desc[0] ? desc : service, sizeof(w->info.description) - 1);
    w->request = request;
    w->nbss = 0;
    w->mp_ctx = mp_ctx;
    w->tal_init = tal_init;
    w->tal_deinit = tal_deinit;
    w->tal_started = w->tal_port = w->peer_up = w->peer_deleted = w->assoc_seen = w->tx_paused = false;
    w->rsna = w->keys = w->handshaking = false;
    nt_KeInitializeEvent(&w->cmd_lock, NT_SynchronizationEvent, 0);   /* (taken: the bring-up below holds it) */
    nt_KeInitializeEvent(&w->task_done, NT_NotificationEvent, 0);
    nt_KeInitializeEvent(&w->eapol_event, NT_SynchronizationEvent, 0);
    w->bss_lock = w->eapol_lock = 0;
    w->eapol_head = w->eapol_tail = 0;
    if (!w->eapol_work) w->eapol_work = nt_IoAllocateWorkItem(NULL);   /* (kept with the host: it is reused) */
    w->adapter = adapter;
    /* (wdiwifi.sys's order: the data path, capabilities, configuration, the data path's start, the port) */
    bool dp = tal_initialize(w);
    if (get_capabilities(w, buf)) {
        set_configuration(w, buf);
        if (dp) tal_start(w);
        if (create_port(w, buf) && w->tal_started) {        /* (without a data path it still scans) */
            TAL_CALL(w, TalTxRxAddPortHandler, TAL_ADD_PORT, w->tal, w->port_id, WDI_OPERATION_MODE_STA);
            w->tal_port = true;
        }
    }
    unlock(w);
    kfree(buf);
    return w;
}

void WdiDetach(WdiHost *w)
{
    if (!w) return;
    lock(w);                    /* (waits for a scan or connection in progress) */
    UINT8 *buf = kmalloc(CMD_BUF);
    if (buf && w->info.connected) disconnect_locked(w, buf, false);
    if (w->tal_port) TAL_CALL(w, TalTxRxDeletePortHandler, TAL_PORT, w->tal, w->port_id);
    w->tal_port = false;
    if (buf && w->info.port) delete_port(w, buf);
    kfree(buf);
    tal_shutdown(w);
    w->adapter = NULL;
    w->info.port = false;
    unlock(w);
    w->used = false;
}

/* The @i'th attached adapter, locked, or NULL */
static WdiHost *host_locked(int i)
{
    for (int k = 0, n = 0; k < WDI_MAX_ADAPTERS; k++) {
        WdiHost *w = &g_hosts[k];
        if (!w->used || !w->adapter || n++ != i) continue;
        lock(w);
        if (w->adapter) return w;
        unlock(w);
        return NULL;
    }
    return NULL;
}

int WlanCount(void)
{
    int n = 0;
    for (int i = 0; i < WDI_MAX_ADAPTERS; i++) n += g_hosts[i].used && g_hosts[i].adapter;
    return n;
}

bool WlanGetInfo(int i, WlanInfo *out)
{
    WdiHost *w = host_locked(i);
    if (!w) return false;
    *out = w->info;
    unlock(w);
    return true;
}

/* OID_WDI_TASK_SCAN on the station port (the caller holds the command
 * lock): every BSS (the wildcard BSSID and SSID) on every channel, live */
static NDIS_STATUS scan_locked(WdiHost *w, UINT8 *buf)
{
    UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->bss_lock);
    w->nbss = 0;
    nt_KeReleaseSpinLock(&w->bss_lock, old);
    static const UINT8 wildcard[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    WDI_SCAN_MODE mode = { 1, WDI_SCAN_TYPE_AUTO, 1, WDI_SCAN_TRIGGER_MANUAL };
    WDI_SCAN_DWELL_TIME dwell = { 20, 110, SCAN_MAX_MS };
    UINT8 *t = buf + sizeof(WDI_MESSAGE_HEADER);
    UINT32 tlen = tlv_put(t, 0, WDI_TLV_BSSID, wildcard, 6);
    tlen = tlv_put(t, tlen, WDI_TLV_SSID, NULL, 0);
    tlen = tlv_put(t, tlen, WDI_TLV_SCAN_MODE, &mode, sizeof(mode));
    tlen = tlv_put(t, tlen, WDI_TLV_SCAN_DWELL_TIME, &dwell, sizeof(dwell));
    NDIS_STATUS st = command(w, WDI_TASK_SCAN, w->port_id, buf, tlen, WDI_INDICATION_SCAN_COMPLETE, NULL,
                             SCAN_MAX_MS / 1000 + TASK_WAIT_S);
    kprintf("[WDI] %s: scan on port %u: 0x%08x, %d access point(s)\n", w->info.service, w->port_id, (UINT32)st,
            st == NDIS_STATUS_SUCCESS ? w->nbss : 0);
    return st;
}

int WlanScan(int i, WlanBss *out, int max, UINT32 *status)
{
    if (status) *status = (UINT32)NDIS_STATUS_FAILURE;
    UINT8 *buf = kmalloc(CMD_BUF);
    if (!buf) return -1;
    WdiHost *w = host_locked(i);
    if (!w || !w->info.port) {
        if (w) unlock(w);
        kfree(buf);
        return -1;
    }
    NDIS_STATUS st = scan_locked(w, buf);
    int n = -1;
    if (st == NDIS_STATUS_SUCCESS) {
        UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->bss_lock);
        n = w->nbss < max ? w->nbss : max;
        memcpy(out, w->bss, (size_t)n * sizeof(*out));
        nt_KeReleaseSpinLock(&w->bss_lock, old);
        /* strongest first, as netsh lists them */
        for (int a = 1; a < n; a++)
            for (int b = a; b > 0 && out[b].quality > out[b - 1].quality; b--) {
                WlanBss x = out[b];
                out[b] = out[b - 1];
                out[b - 1] = x;
            }
    }
    if (status) *status = (UINT32)st;
    unlock(w);
    kfree(buf);
    return n;
}

/* -----------------------------------------------------------------------
 * Connections
 * ----------------------------------------------------------------------- */

/* The strongest access point with @ssid the last scan heard, or -1 */
static int find_bss(WdiHost *w, const char *ssid)
{
    int best = -1;
    for (int i = 0; i < w->nbss; i++)
        if (!strcmp(w->bss[i].ssid, ssid) && (best < 0 || w->bss[i].quality > w->bss[best].quality)) best = i;
    return best;
}

/* OID_WDI_TASK_DISCONNECT from the access point; the stack goes back to
 * the adapter it had before (the caller holds the command lock) */
static void disconnect_locked(WdiHost *w, UINT8 *buf, bool quiet)
{
    UINT8 old;
    if (g_net_host == w) {
        NetDetach(&g_wifi_ops);
        g_net_host = NULL;
    }
    WDI_DISCONNECT_PARAMETERS p;
    memcpy(p.MacAddress, w->info.bss.bssid, 6);
    p.Disassociation80211Reason = 3;                /* (deauthenticated: the station is leaving) */
    UINT32 tlen = tlv_put(buf + sizeof(WDI_MESSAGE_HEADER), 0, WDI_TLV_DISCONNECT_PARAMETERS, &p, sizeof(p));
    NDIS_STATUS st = command(w, WDI_TASK_DISCONNECT, w->port_id, buf, tlen, WDI_INDICATION_DISCONNECT_COMPLETE, NULL,
                             TASK_WAIT_S);
    if (w->peer_deleted || w->peer_up) TAL_CALL(w, TalTxRxPeerDeleteConfirmHandler, TAL_PEER_DELETE_CONFIRM, w->tal,
                                                w->port_id, w->peer);
    __atomic_store_n(&w->peer_up, false, __ATOMIC_RELEASE);
    w->peer_deleted = false;
    w->info.connected = false;
    /* (the driver drops the connection's keys itself: they are not static) */
    w->rsna = w->keys = w->handshaking = false;
    WpaClear(&w->wpa);
    old = nt_KeAcquireSpinLockRaiseToDpc(&w->eapol_lock);
    w->eapol_tail = w->eapol_head;
    nt_KeReleaseSpinLock(&w->eapol_lock, old);
    /* (frames still queued are dropped; those with the driver came back with its peer) */
    old = nt_KeAcquireSpinLockRaiseToDpc(&w->tx_lock);
    while (w->tx && w->txq_tail != w->txq_head) w->tx[w->txq[w->txq_tail++ % TX_FRAMES]].state = TXF_FREE;
    nt_KeReleaseSpinLock(&w->tx_lock, old);
    if (!quiet) kprintf("[WDI] %s: disconnected from %s (0x%08x)\n", w->info.service, w->info.bss.ssid, (UINT32)st);
}

/* One WDI_TLV_CONNECT_BSS_ENTRY from a scan's BSS entry: the TLVs a
 * connect entry has (BSSID, beacon or probe response, signal, channel) */
static UINT32 connect_entry(UINT8 *out, UINT32 off, const UINT8 *raw, UINT32 n)
{
    UINT32 start = off + sizeof(WDI_TLV_HEADER), len = 0;
    for (UINT32 k = 0; k + sizeof(WDI_TLV_HEADER) <= n;) {
        const WDI_TLV_HEADER *t = (const WDI_TLV_HEADER *)(raw + k);
        if (k + sizeof(*t) + t->Length > n) break;
        if (t->Type == WDI_TLV_BSSID || t->Type == WDI_TLV_BEACON_FRAME || t->Type == WDI_TLV_PROBE_RESPONSE_FRAME ||
            t->Type == WDI_TLV_BSS_ENTRY_SIGNAL_INFO || t->Type == WDI_TLV_BSS_ENTRY_CHANNEL_INFO ||
            t->Type == WDI_TLV_BSS_ENTRY_DEVICE_CONTEXT)
            len = tlv_put(out + start, len, t->Type, raw + k + sizeof(*t), t->Length);
        k += sizeof(*t) + t->Length;
    }
    WDI_TLV_HEADER h = { WDI_TLV_CONNECT_BSS_ENTRY, (UINT16)len };
    memcpy(out + off, &h, sizeof(h));
    return start + len;
}

/* What @b's security asks of a connection: WLAN_OK and *@rsna false for an
 * open network, *@rsna true for WPA2-Personal with CCMP (also offered by a
 * WPA3 transition-mode network), else why not */
static int security_of(const WlanBss *b, const char *key, bool *rsna)
{
    *rsna = false;
    if (!b->rsn_len) return b->privacy ? WLAN_UNSUPPORTED : WLAN_OK;    /* (WEP or WPA without RSN) */
    if (!(b->akms & (1u << 2)) || !(b->pairwise & (1u << WDI_CIPHER_ALGO_CCMP)) || b->group != WDI_CIPHER_ALGO_CCMP ||
        (b->rsn_caps & 0x40))                           /* (PSK, CCMP both ways; no required MFP) */
        return WLAN_UNSUPPORTED;
    *rsna = true;
    return key && key[0] ? WLAN_OK : WLAN_NEEDS_KEY;
}

/* OID_WDI_SET_PRIVACY_EXEMPTION_LIST: EAPOL may come and go unencrypted
 * while there is no pairwise key */
static void set_exemptions(WdiHost *w, UINT8 *buf)
{
    WDI_PRIVACY_EXEMPTION_PARAMETERS e = { (UINT16)(ETHERTYPE_EAPOL >> 8 | (ETHERTYPE_EAPOL & 0xFF) << 8),
                                           WDI_EXEMPT_ON_KEY_MAPPING_KEY_UNAVAILABLE, WDI_EXEMPT_PACKET_TYPE_BOTH };
    UINT32 tlen = tlv_put(buf + sizeof(WDI_MESSAGE_HEADER), 0, WDI_TLV_PRIVACY_EXEMPTION_ENTRY, &e, sizeof(e));
    NDIS_STATUS st = command(w, WDI_SET_PRIVACY_EXEMPTION_LIST, w->port_id, buf, tlen, 0, NULL, 0);
    kprintf("[WPA] %s: EAPOL exempted from encryption (0x%08x)\n", w->info.service, (UINT32)st);
}

/* The 4-way handshake, once associated: its frames answered as they come
 * until message 3 brings the keys (the caller holds the command lock) */
static int handshake_locked(WdiHost *w, UINT8 *buf)
{
    /* (message 2 repeats the RSN element the driver associated with; if it
     * did not say, the one it was asked for: PSK, CCMP) */
    static const UINT8 rsn_psk_ccmp[] = { 48, 20, 1, 0, 0x00, 0x0F, 0xAC, 4, 1, 0, 0x00, 0x0F, 0xAC, 4,
                                          1, 0, 0x00, 0x0F, 0xAC, 2, 0, 0 };
    if (w->assoc_ie_len) memcpy(w->wpa.own_ie, w->assoc_ie, w->wpa.own_ie_len = w->assoc_ie_len);
    else memcpy(w->wpa.own_ie, rsn_psk_ccmp, w->wpa.own_ie_len = sizeof(rsn_psk_ccmp));
    bool m1 = false;
    for (int waited = 0; waited < HANDSHAKE_MS; waited += 100) {
        int r = eapol_step(w, buf);
        if (r == WPA_REPLY) m1 = true;
        if (r == WPA_PAIRWISE) return w->keys ? WLAN_OK : WLAN_FAILED;
        INT64 t = -1000000;                             /* (100 ms) */
        nt_KeWaitForSingleObject(&w->eapol_event, 0, 0, 0, &t);
    }
    kprintf("[WPA] %s: the 4-way handshake did not finish: %s\n", w->info.service,
            m1 ? "no message 3 after message 2 (the access point did not take the key)" : "no message 1 came");
    return m1 ? WLAN_BAD_KEY : WLAN_FAILED;
}

int WlanConnect(int i, const char *ssid, const char *key, UINT32 *status)
{
    if (status) *status = (UINT32)NDIS_STATUS_FAILURE;
    UINT8 *buf = kmalloc(CMD_BUF);
    if (!buf) return WLAN_FAILED;
    WdiHost *w = host_locked(i);
    if (!w || !w->info.port || !w->tal_port) {
        if (w) unlock(w);
        kfree(buf);
        return WLAN_NO_ADAPTER;
    }
    int b = find_bss(w, ssid);
    if (b < 0 && scan_locked(w, buf) == NDIS_STATUS_SUCCESS) b = find_bss(w, ssid);
    bool rsna = false;
    int r = b < 0 ? WLAN_NO_NETWORK : security_of(&w->bss[b], key, &rsna);
    UINT8 pmk[WPA_PMK_LEN];
    if (r == WLAN_OK && rsna && !WpaPassphraseKey(key, w->bss[b].ssid, pmk)) r = WLAN_KEY_FORMAT;
    if (r != WLAN_OK) {
        unlock(w);
        kfree(buf);
        return r;
    }
    if (w->info.connected) disconnect_locked(w, buf, false);
    WlanBss bss = w->bss[b];
    /* WDI_TLV_CONNECT_PARAMETERS: the settings, the SSID, the authentication and ciphers allowed */
    UINT8 *t = buf + sizeof(WDI_MESSAGE_HEADER), cp[160];
    WDI_CONNECTION_SETTINGS cs;
    memset(&cs, 0, sizeof(cs));
    cs.ExcludeUnencrypted = rsna;
    UINT32 auth = rsna ? WDI_AUTH_ALGO_RSNA_PSK : WDI_AUTH_ALGO_80211_OPEN;
    UINT32 cipher = rsna ? WDI_CIPHER_ALGO_CCMP : WDI_CIPHER_ALGO_NONE;
    if (rsna) {
        memcpy(w->wpa.pmk, pmk, sizeof(pmk));
        memset(pmk, 0, sizeof(pmk));
        memcpy(w->wpa.aa, bss.bssid, 6);
        memcpy(w->wpa.spa, w->port_attr.MacAddress, 6);
        memcpy(w->wpa.ap_ie, bss.rsn, w->wpa.ap_ie_len = bss.rsn_len);
        WpaStart(&w->wpa);
        UINT8 old = nt_KeAcquireSpinLockRaiseToDpc(&w->eapol_lock);
        w->eapol_tail = w->eapol_head;
        nt_KeReleaseSpinLock(&w->eapol_lock, old);
        nt_KeClearEvent(&w->eapol_event);
        set_exemptions(w, buf);
        w->keys = false;
        w->handshaking = w->rsna = true;                /* (message 1 may come before the connection completes) */
    }
    UINT32 n = tlv_put(cp, 0, WDI_TLV_CONNECTION_SETTINGS, &cs, sizeof(cs));
    n = tlv_put(cp, n, WDI_TLV_SSID, bss.ssid, (UINT16)strlen(bss.ssid));
    n = tlv_put(cp, n, WDI_TLV_AUTH_ALGO_LIST, &auth, sizeof(auth));
    n = tlv_put(cp, n, WDI_TLV_MULTICAST_CIPHER_ALGO_LIST, &cipher, sizeof(cipher));
    n = tlv_put(cp, n, WDI_TLV_UNICAST_CIPHER_ALGO_LIST, &cipher, sizeof(cipher));
    UINT32 tlen = tlv_put(t, 0, WDI_TLV_CONNECT_PARAMETERS, cp, (UINT16)n);
    /* the access points to try, strongest first */
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < w->nbss; k++)
            if (!strcmp(w->bss[k].ssid, bss.ssid) && (k == b) == !pass && w->bss_raw_len[k] &&
                tlen + w->bss_raw_len[k] + 64 < CMD_BUF - sizeof(WDI_MESSAGE_HEADER))
                tlen = connect_entry(t, tlen, w->bss_raw[k], w->bss_raw_len[k]);
    w->assoc_seen = false;
    w->assoc_ie_len = 0;
    memset(&w->assoc, 0, sizeof(w->assoc));
    NDIS_STATUS st = command(w, WDI_TASK_CONNECT, w->port_id, buf, tlen, WDI_INDICATION_CONNECT_COMPLETE, NULL,
                             CONNECT_WAIT_S);
    if (st == NDIS_STATUS_SUCCESS && (!w->assoc_seen || w->assoc.AssociationStatus != WDI_ASSOC_STATUS_SUCCESS))
        st = NDIS_STATUS_FAILURE;
    if (st == NDIS_STATUS_SUCCESS && !w->peer_up) {
        kprintf("[WDI] %s: associated, but the driver indicated no peer\n", w->info.service);
        st = NDIS_STATUS_FAILURE;
    }
    char mac[18];
    mac_str(bss.bssid, mac);
    if (st != NDIS_STATUS_SUCCESS) {
        kprintf("[WDI] %s: connecting to %s (%s) failed (0x%08x, association status %u)\n", w->info.service, bss.ssid,
                mac, (UINT32)st, w->assoc_seen ? w->assoc.AssociationStatus : 0xFFFFFFFFu);
        if (w->peer_up || w->peer_deleted) disconnect_locked(w, buf, true);
        w->rsna = w->handshaking = false;
        WpaClear(&w->wpa);
        if (status) *status = (UINT32)st;
        unlock(w);
        kfree(buf);
        return WLAN_FAILED;
    }
    UINT32 qos = w->assoc.WMMQoSEnabled ? 1 : 0;    /* (WDI_TXRX_PEER_CFG: QoS capable or not) */
    TAL_CALL(w, TalTxRxPeerConfigHandler, TAL_PEER_CONFIG, w->tal, w->port_id, w->peer, &qos);
    w->info.bss = bss;
    w->info.connected = true;
    w->tx_paused = false;
    kprintf("[WDI] %s: connected to %s (%s) on port %u, peer %u: %s, %s, WMM %s\n", w->info.service, bss.ssid, mac,
            w->port_id, w->peer, w->assoc.AuthAlgorithm == WDI_AUTH_ALGO_80211_OPEN ? "open" :
            w->assoc.AuthAlgorithm == WDI_AUTH_ALGO_RSNA_PSK ? "WPA2-Personal" : "secured",
            w->assoc.UnicastCipherAlgorithm == WDI_CIPHER_ALGO_NONE ? "no cipher" :
            w->assoc.UnicastCipherAlgorithm == WDI_CIPHER_ALGO_CCMP ? "CCMP" : "ciphered",
            w->assoc.WMMQoSEnabled ? "on" : "off");
    if (rsna) {
        r = handshake_locked(w, buf);
        w->handshaking = false;
        if (r != WLAN_OK) {
            disconnect_locked(w, buf, false);
            unlock(w);
            kfree(buf);
            return r;
        }
        kprintf("[WPA] %s: 4-way handshake done: the port is authorized\n", w->info.service);
    }
    if (!g_net_host) {                              /* (the stack's adapter now: DHCP runs over Wi-Fi) */
        g_net_host = w;
        NetAttach(&g_wifi_ops);
    }
    if (status) *status = NDIS_STATUS_SUCCESS;
    unlock(w);
    kfree(buf);
    return WLAN_OK;
}

int WlanDisconnect(int i, UINT32 *status)
{
    if (status) *status = (UINT32)NDIS_STATUS_FAILURE;
    UINT8 *buf = kmalloc(CMD_BUF);
    if (!buf) return WLAN_FAILED;
    WdiHost *w = host_locked(i);
    int r = !w ? WLAN_NO_ADAPTER : !w->info.connected ? WLAN_NOT_CONNECTED : WLAN_OK;
    if (r == WLAN_OK) {
        disconnect_locked(w, buf, false);
        if (status) *status = NDIS_STATUS_SUCCESS;
    }
    if (w) unlock(w);
    kfree(buf);
    return r;
}

/* -----------------------------------------------------------------------
 * A scan or a connection in the background, for netsh (the Terminal does
 * not wait): one at a time
 * ----------------------------------------------------------------------- */
enum { BG_SCAN, BG_CONNECT, BG_DISCONNECT };
static volatile bool g_bg_busy, g_bg_done;
static int           g_bg_kind, g_bg_adapter, g_bg_n;
static UINT32        g_bg_status;
static char          g_bg_ssid[33], g_bg_key[65];
static WlanBss       g_scan_bss[WLAN_MAX_BSS];

static void bg_thread(void *arg)
{
    (void)arg;
    UINT32 st = 0;
    if (g_bg_kind == BG_SCAN) g_bg_n = WlanScan(g_bg_adapter, g_scan_bss, WLAN_MAX_BSS, &st);
    else if (g_bg_kind == BG_CONNECT) g_bg_n = WlanConnect(g_bg_adapter, g_bg_ssid, g_bg_key, &st);
    else g_bg_n = WlanDisconnect(g_bg_adapter, &st);
    memset(g_bg_key, 0, sizeof(g_bg_key));             /* (the passphrase is not kept) */
    g_bg_status = st;
    __atomic_store_n(&g_bg_done, true, __ATOMIC_RELEASE);
}

static bool bg_start(int kind, int i)
{
    /* (a result nobody collected, its netsh stopped with Ctrl+C: dropped) */
    if (__atomic_load_n(&g_bg_done, __ATOMIC_ACQUIRE)) {
        g_bg_done = false;
        __atomic_store_n(&g_bg_busy, false, __ATOMIC_RELEASE);
    }
    if (i < 0 || i >= WlanCount() || __atomic_exchange_n(&g_bg_busy, true, __ATOMIC_ACQ_REL)) return false;
    g_bg_kind = kind;
    g_bg_adapter = i;
    g_bg_done = false;
    if (!sched_create_thread("wlanop", bg_thread, NULL, PRIO_SERVICE)) {
        g_bg_busy = false;
        return false;
    }
    return true;
}

static bool bg_done(int kind)
{
    if (g_bg_kind != kind || !__atomic_load_n(&g_bg_done, __ATOMIC_ACQUIRE)) return false;
    g_bg_done = false;
    __atomic_store_n(&g_bg_busy, false, __ATOMIC_RELEASE);
    return true;
}

bool WlanScanStart(int i) { return bg_start(BG_SCAN, i); }

bool WlanScanDone(WlanBss *out, int max, int *n, UINT32 *status)
{
    if (g_bg_kind != BG_SCAN || !__atomic_load_n(&g_bg_done, __ATOMIC_ACQUIRE)) return false;
    *n = g_bg_n < max ? g_bg_n : max;
    if (*n > 0) memcpy(out, g_scan_bss, (size_t)*n * sizeof(*out));
    if (status) *status = g_bg_status;
    return bg_done(BG_SCAN);
}

bool WlanConnectStart(int i, const char *ssid, const char *key)
{
    if (__atomic_load_n(&g_bg_busy, __ATOMIC_ACQUIRE) && !__atomic_load_n(&g_bg_done, __ATOMIC_ACQUIRE)) return false;
    if (ssid) {
        strncpy(g_bg_ssid, ssid, sizeof(g_bg_ssid) - 1);
        g_bg_ssid[sizeof(g_bg_ssid) - 1] = 0;
        memset(g_bg_key, 0, sizeof(g_bg_key));
        if (key) strncpy(g_bg_key, key, sizeof(g_bg_key) - 1);
    }
    return bg_start(ssid ? BG_CONNECT : BG_DISCONNECT, i);
}

bool WlanConnectDone(int *result, UINT32 *status)
{
    int kind = g_bg_kind;
    if (kind == BG_SCAN || !__atomic_load_n(&g_bg_done, __ATOMIC_ACQUIRE)) return false;
    *result = g_bg_n;
    if (status) *status = g_bg_status;
    return bg_done(kind);
}
