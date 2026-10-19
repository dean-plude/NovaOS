/*
 * novawlan.h — what NovaOS's Wi-Fi (the WDI upper edge, kernel/nt/wdi.c,
 * through kernel/um/um_wlan.c) and the Native Wifi API (userland/wlanapi,
 * wlanapi.dll) say to each other
 *
 * On Windows wlanapi.dll talks to the WLAN AutoConfig service (wlansvc)
 * by RPC, and the service to the driver.  NovaOS's Wi-Fi lives in the
 * kernel (netsh wlan and Settings' Wi-Fi page use it there), so
 * wlanapi.dll asks it through NtNovaSockCtl, with no handle:
 *
 *   op 18  adapter arg's details (NovaWlanIf into out); returns the
 *          structure's size, or -1 if there is no such adapter.  With out
 *          NULL: how many Wi-Fi adapters there are;
 *   op 19  scan with adapter (arg & 0xFF), at most (arg >> 8) access
 *          points (NovaWlanBss rows into out), strongest first; returns
 *          how many, or -1 if the scan failed;
 *   op 20  connect adapter arg to the network NovaWlanConnect (out)
 *          names, with its passphrase; returns 0 or a NOVAWLAN_* error
 *          (kernel/nt/wlan.h's WLAN_*), after the association and its
 *          handshakes (DHCP follows on its own);
 *   op 21  disconnect adapter arg; returns 0 or a NOVAWLAN_* error.
 */
#pragma once

#define NOVAWLAN_IF       18
#define NOVAWLAN_SCAN     19
#define NOVAWLAN_CONNECT  20
#define NOVAWLAN_DISCONNECT 21

#define NOVAWLAN_MAX_BSS  32

/* (kernel/nt/wlan.h's WLAN_* answers) */
#define NOVAWLAN_OK            0
#define NOVAWLAN_NO_NETWORK    (-1)
#define NOVAWLAN_NEEDS_KEY     (-2)
#define NOVAWLAN_FAILED        (-3)
#define NOVAWLAN_NO_ADAPTER    (-4)
#define NOVAWLAN_NOT_CONNECTED (-5)
#define NOVAWLAN_BAD_KEY       (-6)
#define NOVAWLAN_UNSUPPORTED   (-7)
#define NOVAWLAN_KEY_FORMAT    (-8)

/* NovaWlanBss.security */
#define NOVAWLAN_OPEN   0
#define NOVAWLAN_WPA2   1      /* WPA2-Personal (AKM 2, CCMP) */
#define NOVAWLAN_WPA3   2      /* WPA3-Personal (AKM 8, SAE) */
#define NOVAWLAN_OTHER  3      /* something NovaOS cannot join yet (WEP, WPA, Enterprise) */

typedef struct {               /* an access point a scan heard */
    unsigned char  bssid[6];
    unsigned char  ssid_len;
    unsigned char  security;   /* NOVAWLAN_* above */
    char           ssid[33];
    unsigned char  privacy;    /* the capability field's Privacy bit */
    unsigned char  mixed;      /* WPA3 offered alongside WPA2 (transition mode) */
    unsigned char  phy;        /* 0 802.11g, 1 802.11n, 2 802.11ac, 3 802.11ax */
    int            rssi;       /* dBm */
    unsigned int   quality;    /* 0-100 */
    unsigned int   channel;
    unsigned int   freq_khz;   /* the channel's centre frequency */
    unsigned int   band;       /* 1 2.4 GHz, 2 5 GHz, 3 6 GHz */
} NovaWlanBss;

typedef struct {               /* a Wi-Fi adapter */
    char           description[96];
    unsigned char  mac[6];
    unsigned char  connected;
    unsigned char  radio;      /* bit 0 hardware on, bit 1 software on */
    NovaWlanBss    bss;        /* (connected) its access point */
    unsigned long long rx_frames, tx_frames;
} NovaWlanIf;

typedef struct {               /* op 20's request */
    char ssid[33];
    char key[65];              /* "": none (an open network) */
} NovaWlanConnect;
