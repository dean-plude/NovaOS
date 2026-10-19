/*
 * wlan.h — the Wi-Fi adapters NovaOS's WDI upper edge (kernel/nt/wdi.c)
 * drives, for the rest of the kernel: netsh wlan (kernel/apps/terminal.c)
 * lists them, scans with them and connects them; a connected one carries
 * the network stack's traffic (net.c's adapter, kernel/net/net.h)
 */
#pragma once

#include "../include/types.h"

#define WLAN_MAX_BSS 32

/* An access point a scan found (one BSS) */
typedef struct {
    UINT8  bssid[6];
    char   ssid[33];
    INT32  rssi;            /* dBm */
    UINT32 quality;         /* 0-100: netsh's "Signal" */
    UINT32 channel;
    UINT32 band;            /* WDI_BAND_ID_*: 2.4, 5 or 6 GHz */
    bool   privacy;         /* the capability field's Privacy bit */
    const char *auth;       /* "Open", "WPA2-Personal", "WPA3-Personal", ... (from its RSN element) */
    const char *cipher;     /* "None", "CCMP", "GCMP", "TKIP", "WEP" */
    const char *radio;      /* "802.11n", "802.11ac", "802.11ax" or "802.11g" (from its elements) */
    UINT32 akms;            /* bit n: it offers AKM suite 00-0f-ac:n (2 PSK, 8 SAE, ...) */
    UINT32 pairwise;        /* bit n: pairwise cipher suite 00-0f-ac:n (4 CCMP) */
    UINT8  group;           /* its group cipher suite (4 CCMP) */
    UINT8  rsn[64];         /* its RSN element, as its beacon has it (rsn_len 0: none) */
    UINT8  rsn_len;
    UINT16 rsn_caps;        /* its RSN capabilities (0x40: management frame protection required) */
} WlanBss;

typedef struct {
    char   description[96]; /* the device's DeviceDesc */
    char   service[64];
    char   firmware[64];
    UINT8  mac[6];
    bool   hw_radio, sw_radio;
    bool   port;            /* its station port exists (WDI_TASK_CREATE_PORT) */
    UINT32 max_rx_kbps, max_tx_kbps;
    UINT32 bands;           /* bit (1 << WDI_BAND_ID_*) per band it has enabled */
    bool   connected;       /* associated with an access point: */
    WlanBss bss;            /* that access point */
    UINT64 rx_frames, tx_frames;   /* (data frames through the data path) */
} WlanInfo;

/* How many Wi-Fi adapters are open; the @i'th one's details */
int  WlanCount(void);
bool WlanGetInfo(int i, WlanInfo *out);
/* Scan with the @i'th adapter (OID_WDI_TASK_SCAN): up to @max access points
 * it found, strongest first, or -1 if the scan failed (*@status says why) */
int  WlanScan(int i, WlanBss *out, int max, UINT32 *status);
/* The same in the background (netsh's and Settings' Wi-Fi page): start
 * one (false if there is no such adapter or a scan is running), then poll
 * for its outcome (false while it runs; then *@n is WlanScan's answer) */
bool WlanScanStart(int i);
bool WlanScanDone(WlanBss *out, int max, int *n, UINT32 *status);

/* Connect the @i'th adapter to network @ssid (OID_WDI_TASK_CONNECT, with the
 * access points its last scan heard, scanning first if none had that SSID);
 * a WPA2-Personal network needs @key, its passphrase (or the 64-digit hex
 * PSK), and its 4-way handshake (nt/wpa.h) gives the driver its keys
 * (OID_WDI_SET_ADD_CIPHER_KEYS).  Once associated (and the handshake done)
 * it is the stack's network adapter (NetAttach) until WlanDisconnect
 * (OID_WDI_TASK_DISCONNECT).  Their answer: */
#define WLAN_OK          0
#define WLAN_NO_NETWORK  (-1)  /* no access point with that SSID in range */
#define WLAN_NEEDS_KEY   (-2)  /* a secured network, and no @key */
#define WLAN_FAILED      (-3)  /* the driver refused or the association failed: *@status */
#define WLAN_NO_ADAPTER  (-4)
#define WLAN_NOT_CONNECTED (-5)
#define WLAN_BAD_KEY     (-6)  /* the 4-way handshake failed: the access point did not take the key */
#define WLAN_UNSUPPORTED (-7)  /* security NovaOS does not have yet (WPA3's SAE, WPA, WEP, TKIP, Enterprise) */
#define WLAN_KEY_FORMAT  (-8)  /* @key is not 8 to 63 characters or 64 hex digits */
int  WlanConnect(int i, const char *ssid, const char *key, UINT32 *status);
int  WlanDisconnect(int i, UINT32 *status);
/* The same in the background (netsh's and Settings'), sharing the scan's: @ssid NULL
 * disconnects; then poll for the answer */
bool WlanConnectStart(int i, const char *ssid, const char *key);
bool WlanConnectDone(int *result, UINT32 *status);
/* Whether a Wi-Fi adapter is the stack's adapter now (ipconfig's "Wireless LAN adapter") */
bool WlanCarriesNet(void);

/* ndis.c's side.  @request sends an OID_WDI_* method request to the
 * miniport (its buffer: @in_len bytes of message, room for @out_len) and
 * waits for it; it returns the NDIS status and the bytes written. */
typedef struct WdiHost WdiHost;
typedef INT32 (*WdiRequestFn)(void *adapter, UINT32 oid, UINT16 port, void *buf, UINT32 in_len, UINT32 out_len,
                              UINT32 *written);
/* After MiniportWdiOpenAdapter: the data path (the driver's
 * MiniportWdiTalTxRxInitialize, @tal_init, given @mp_ctx, its
 * MiniportAdapterContext), the adapter's capabilities and configuration,
 * and its station port; NULL if there is no room for it */
WdiHost *WdiAttach(void *adapter, const char *service, const char *desc, WdiRequestFn request, void *mp_ctx,
                   void *tal_init, void *tal_deinit);
/* An NDIS_STATUS_WDI_INDICATION_* @adapter's miniport indicated (any IRQL
 * up to DISPATCH_LEVEL; also while WdiAttach is bringing it up) */
void     WdiIndicate(void *adapter, UINT32 code, const void *buf, UINT32 len);
/* Before MiniportWdiCloseAdapter: disconnects, deletes the port, stops the data path */
void     WdiDetach(WdiHost *w);
