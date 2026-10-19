/*
 * um_wlan.c — Wi-Fi for user mode: what wlanapi.dll asks NovaOS's WDI
 * upper edge (kernel/nt/wdi.c, kernel/nt/wlan.h) through NtNovaSockCtl
 * ops 18-21 (layouts and ops in userland/include/novawlan.h)
 *
 * Windows' wlanapi.dll reaches the WLAN AutoConfig service by RPC; here
 * the kernel is that service: a scan or a connection runs on the calling
 * thread, under the adapter's command lock, as netsh wlan's do on its
 * worker thread.
 */

#include "um_internal.h"
#include "../ke/probe.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../nt/wlan.h"
#include "../nt/wdi.h"
#include "../../userland/include/novawlan.h"

static UINT32 freq_khz(UINT32 band, UINT32 ch)
{
    if (band == WDI_BAND_ID_2400) return ch == 14 ? 2484000 : 2407000 + ch * 5000;
    if (band == WDI_BAND_ID_6000) return 5950000 + ch * 5000;
    return 5000000 + ch * 5000;
}

static void to_user_bss(const WlanBss *b, NovaWlanBss *o)
{
    memset(o, 0, sizeof(*o));
    memcpy(o->bssid, b->bssid, 6);
    strncpy(o->ssid, b->ssid, sizeof(o->ssid) - 1);
    o->ssid_len = (unsigned char)strlen(o->ssid);
    bool psk = b->akms & (1u << 2), sae = b->akms & (1u << 8);
    o->security = !b->privacy && !b->rsn_len ? NOVAWLAN_OPEN :
                  !b->rsn_len || !(b->pairwise & (1u << 4)) ? NOVAWLAN_OTHER :
                  sae ? NOVAWLAN_WPA3 : psk ? NOVAWLAN_WPA2 : NOVAWLAN_OTHER;
    o->mixed = psk && sae;
    o->privacy = b->privacy;
    o->phy = !b->radio ? 0 : !strcmp(b->radio, "802.11ax") ? 3 : !strcmp(b->radio, "802.11ac") ? 2 :
             !strcmp(b->radio, "802.11n") ? 1 : 0;
    o->rssi = b->rssi;
    o->quality = b->quality;
    o->channel = b->channel;
    o->freq_khz = freq_khz(b->band, b->channel);
    o->band = b->band == WDI_BAND_ID_2400 ? 1 : b->band == WDI_BAND_ID_6000 ? 3 : 2;
}

INT64 um_wlan_ctl(UINT64 op, UINT64 arg, UINT64 out)
{
    switch (op) {
    case NOVAWLAN_IF: {
        if (!out) return WlanCount();
        WlanInfo *w = kzalloc(sizeof(*w));
        if (!w) return -1;
        INT64 r = -1;
        if (arg < 64 && WlanGetInfo((int)arg, w)) {
            NovaWlanIf o;
            memset(&o, 0, sizeof(o));
            strncpy(o.description, w->description, sizeof(o.description) - 1);
            memcpy(o.mac, w->mac, 6);
            o.connected = w->connected;
            o.radio = (w->hw_radio ? 1 : 0) | (w->sw_radio ? 2 : 0);
            if (w->connected) to_user_bss(&w->bss, &o.bss);
            o.rx_frames = w->rx_frames;
            o.tx_frames = w->tx_frames;
            r = NT_SUCCESS(CopyToUser((void *)(uintptr_t)out, &o, sizeof(o))) ? (INT64)sizeof(o) : -1;
        }
        kfree(w);
        return r;
    }
    case NOVAWLAN_SCAN: {
        int max = (int)(arg >> 8);
        if (max > NOVAWLAN_MAX_BSS) max = NOVAWLAN_MAX_BSS;
        if (max <= 0 || !out) return -1;
        WlanBss *b = kmalloc(sizeof(WlanBss) * NOVAWLAN_MAX_BSS);
        if (!b) return -1;
        UINT32 st;
        int n = WlanScan((int)(arg & 0xFF), b, max, &st);
        for (int i = 0; i < n; i++) {
            NovaWlanBss o;
            to_user_bss(&b[i], &o);
            if (!NT_SUCCESS(CopyToUser((NovaWlanBss *)(uintptr_t)out + i, &o, sizeof(o)))) { n = -1; break; }
        }
        kfree(b);
        return n;
    }
    case NOVAWLAN_CONNECT: {
        NovaWlanConnect c;
        if (!out || !NT_SUCCESS(CopyFromUser(&c, (const void *)(uintptr_t)out, sizeof(c)))) return NOVAWLAN_FAILED;
        c.ssid[sizeof(c.ssid) - 1] = c.key[sizeof(c.key) - 1] = '\0';
        UINT32 st;
        int r = WlanConnect((int)arg, c.ssid, c.key[0] ? c.key : NULL, &st);
        memset(&c, 0, sizeof(c));                                   /* (the passphrase) */
        return r;
    }
    case NOVAWLAN_DISCONNECT: {
        UINT32 st;
        return WlanDisconnect((int)arg, &st);
    }
    }
    return -1;
}
