/*
 * wpa.h — the station side of WPA2- and WPA3-Personal's key handshakes
 * (RSNA, IEEE 802.11-2020 12.7): the key the passphrase makes, the 4-way
 * handshake that derives the pairwise key from it and brings the group
 * key, and the group key handshake that replaces the group key later.
 * kernel/nt/wdi.c carries its EAPOL-Key frames (Ethertype 0x888E) and
 * gives the keys it derives to the Wi-Fi driver (OID_WDI_SET_ADD_CIPHER_KEYS);
 * the driver encrypts and decrypts with them.
 *
 * WPA2-Personal with CCMP: AKM 00-0f-ac:2 (PSK), key descriptor version 2
 * (HMAC-SHA1-128 MICs, AES key wrap for the key data, the PRF over
 * HMAC-SHA1).  WPA3-Personal: AKM 00-0f-ac:8 (SAE, sae.h, gives the PMK),
 * key descriptor version 0 (AES-128-CMAC MICs, the KDF over HMAC-SHA256),
 * and management frame protection, so message 3 and the group key
 * handshake also bring the IGTK (BIP-CMAC-128).
 */
#pragma once

#include "../include/types.h"

#define WPA_PMK_LEN   32
#define WPA_NONCE_LEN 32
#define WPA_KCK_LEN   16
#define WPA_KEK_LEN   16
#define WPA_TK_LEN    16            /* (CCMP-128) */
#define WPA_GTK_MAX   32
#define WPA_IE_MAX    64
#define WPA_FRAME_MAX 256           /* (an EAPOL-Key frame the station sends) */
#define WPA_IGTK_LEN  16            /* (BIP-CMAC-128) */
#define WPA_AKM_PSK   2
#define WPA_AKM_SAE   8

/* The PMK for @passphrase on network @ssid: PBKDF2-HMAC-SHA1, 4096
 * rounds, 32 bytes (802.11-2020 J.4.1); a 64-digit hex string is the PMK
 * itself.  False if @passphrase is neither 8 to 63 printable characters
 * nor 64 hex digits. */
bool WpaPassphraseKey(const char *passphrase, const char *ssid, UINT8 pmk[WPA_PMK_LEN]);

typedef struct {
    /* set before the handshake */
    UINT8  akm;                     /* WPA_AKM_PSK or WPA_AKM_SAE (0: PSK) */
    bool   mfp;                     /* management frame protection: message 3 must bring an IGTK */
    UINT8  pmk[WPA_PMK_LEN];
    UINT8  aa[6], spa[6];           /* the authenticator's (the access point's) address, the station's */
    UINT8  own_ie[WPA_IE_MAX];      /* the RSN element the station associated with */
    UINT8  own_ie_len;
    UINT8  ap_ie[WPA_IE_MAX];       /* the access point's, from its beacon (0 bytes: not checked) */
    UINT8  ap_ie_len;
    /* the handshake's */
    UINT8  anonce[WPA_NONCE_LEN], snonce[WPA_NONCE_LEN];
    UINT8  ptk[WPA_KCK_LEN + WPA_KEK_LEN + WPA_TK_LEN];    /* KCK, KEK, TK */
    UINT8  tptk[WPA_KCK_LEN + WPA_KEK_LEN + WPA_TK_LEN];   /* (from message 1, until message 3 proves it) */
    bool   have_ptk, have_tptk;
    UINT64 replay;                  /* the last replay counter a MIC'd frame had */
    bool   have_replay;
    /* what the last frame installed: */
    UINT8  gtk[WPA_GTK_MAX];
    UINT8  gtk_len, gtk_id;
    UINT8  gtk_rsc[6];
    UINT8  igtk[WPA_IGTK_LEN];      /* (mfp: the IGTK, its key ID (4 or 5) and IPN) */
    bool   have_igtk;
    UINT16 igtk_id;
    UINT8  igtk_ipn[6];
    const char *why;                /* (why the last frame was dropped) */
} WpaSupplicant;

/* What WpaReceive did with an EAPOL frame */
enum {
    WPA_DROP,         /* not for the station, or failed a check (sup->why says which) */
    WPA_REPLY,        /* message 1: send the reply (message 2) */
    WPA_PAIRWISE,     /* message 3: send the reply (message 4), then install the TK, the GTK (and the IGTK) */
    WPA_GROUP,        /* group message 1: send the reply, then install the new GTK (and IGTK) */
};

/* Start a handshake: forget the last one's keys and replay counter */
void WpaStart(WpaSupplicant *s);
/* One EAPOL frame (@frame, @len: from its EAPOL header) from the access
 * point: the reply to send in @reply (*@reply_len bytes, an EAPOL frame) */
int  WpaReceive(WpaSupplicant *s, const UINT8 *frame, UINT32 len, UINT8 *reply, UINT32 *reply_len);
/* The pairwise key (the TK) the handshake derived */
static inline const UINT8 *WpaTk(const WpaSupplicant *s) { return s->ptk + WPA_KCK_LEN + WPA_KEK_LEN; }
/* The PTK for @akm (WPA_AKM_*) from @pmk, both addresses and both nonces:
 * the 802.11 PRF-384 (HMAC-SHA1) for PSK, KDF-SHA256-384 for SAE */
void WpaDerivePtk(UINT8 akm, const UINT8 *pmk, const UINT8 aa[6], const UINT8 spa[6], const UINT8 *anonce,
                  const UINT8 *snonce, UINT8 ptk[WPA_KCK_LEN + WPA_KEK_LEN + WPA_TK_LEN]);
/* Forget every key */
void WpaClear(WpaSupplicant *s);
