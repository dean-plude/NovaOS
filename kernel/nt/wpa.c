/*
 * wpa.c — the station side of WPA2-Personal (wpa.h): the PSK, the 4-way
 * handshake and the group key handshake, IEEE 802.11-2020 12.7.6 and
 * 12.7.7, over Mbed TLS's HMAC-SHA1, PBKDF2 and AES key wrap
 * (third_party/mbedtls, which the kernel already links)
 *
 * An EAPOL-Key frame (802.1X-2020 11.9, 802.11-2020 12.7.2) is the EAPOL
 * header (version, type 3, length) and a key descriptor: type 2 (RSN), Key
 * Information, Key Length, Replay Counter, Key Nonce, EAPOL-Key IV, Key
 * RSC, reserved, Key MIC, Key Data Length, Key Data; big-endian.  The
 * handshake, the access point (the authenticator) first:
 *
 *   1  ANonce                     -> the station picks an SNonce and derives
 *                                    the PTK (KCK, KEK, TK) from the PMK, both
 *                                    addresses and both nonces
 *   2  SNonce, its RSN element, MIC (with the KCK)
 *   3  Install, MIC, the key data (wrapped with the KEK): the access point's
 *      RSN element (checked against its beacon's: no downgrade) and the GTK
 *   4  MIC                        -> then the TK and the GTK are installed
 *
 * and later the group key handshake (a new GTK, then the reply) at the
 * access point's rekeying interval.  Written here rather than vendored:
 * hostapd's wpa_supplicant (BSD) is a user-mode program built around its
 * own event loop and driver layer, far more than these two exchanges; the
 * cryptography is Mbed TLS's.
 */
#include "wpa.h"
#include "../lib/string.h"
#include "../net/tls.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/nist_kw.h"

#define EAPOL_KEY       3
#define DESC_RSN        2
#define KI_VERSION      0x0007      /* Key Information: descriptor version (2: HMAC-SHA1-128, AES key wrap) */
#define KI_PAIRWISE     0x0008
#define KI_INSTALL      0x0040
#define KI_ACK          0x0080
#define KI_MIC          0x0100
#define KI_SECURE       0x0200
#define KI_ENCRYPTED    0x1000
#define DESC_LEN        95          /* (the key descriptor before its key data) */
#define OFF_INFO        1
#define OFF_REPLAY      5
#define OFF_NONCE       13
#define OFF_RSC         61
#define OFF_MIC         77
#define OFF_DATA_LEN    93

static UINT16 be16(const UINT8 *p) { return (UINT16)(p[0] << 8 | p[1]); }
static void   put16(UINT8 *p, UINT32 v) { p[0] = (UINT8)(v >> 8); p[1] = (UINT8)v; }

static UINT64 be64(const UINT8 *p)
{
    UINT64 v = 0;
    for (int i = 0; i < 8; i++) v = v << 8 | p[i];
    return v;
}

static bool hmac_sha1(const UINT8 *key, UINT32 klen, const UINT8 *data, UINT32 len, UINT8 out[20])
{
    return !mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), key, klen, data, len, out);
}

bool WpaPassphraseKey(const char *passphrase, const char *ssid, UINT8 pmk[WPA_PMK_LEN])
{
    size_t n = strlen(passphrase);
    if (n == 64) {                                      /* (the PSK in hex) */
        for (int i = 0; i < 64; i++) {
            char c = passphrase[i];
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) return false;
            pmk[i / 2] = (UINT8)(i & 1 ? pmk[i / 2] | v : v << 4);
        }
        return true;
    }
    if (n < 8 || n > 63) return false;
    for (size_t i = 0; i < n; i++)
        if (passphrase[i] < 32 || passphrase[i] > 126) return false;
    return !mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA1, (const unsigned char *)passphrase, n,
                                          (const unsigned char *)ssid, strlen(ssid), 4096, WPA_PMK_LEN, pmk);
}

/* PRF-384 (802.11-2020 12.7.1.2): the PTK from the PMK, "Pairwise key
 * expansion" and Min(AA, SPA) || Max(AA, SPA) || Min(ANonce, SNonce) ||
 * Max(ANonce, SNonce) */
static void derive_ptk(WpaSupplicant *s, UINT8 *ptk)
{
    static const char label[] = "Pairwise key expansion";
    UINT8 in[sizeof(label) + 6 + 6 + 32 + 32 + 1];     /* (the label, its 0 octet, the data, the counter) */
    UINT32 n = sizeof(label);                          /* (sizeof counts the label's NUL: the 0 octet) */
    memcpy(in, label, sizeof(label));
    bool a_first = memcmp(s->aa, s->spa, 6) < 0, an_first = memcmp(s->anonce, s->snonce, WPA_NONCE_LEN) < 0;
    memcpy(in + n, a_first ? s->aa : s->spa, 6);
    memcpy(in + n + 6, a_first ? s->spa : s->aa, 6);
    memcpy(in + n + 12, an_first ? s->anonce : s->snonce, WPA_NONCE_LEN);
    memcpy(in + n + 44, an_first ? s->snonce : s->anonce, WPA_NONCE_LEN);
    n += 76;
    UINT8 out[60];
    for (UINT8 i = 0; i < 3; i++) {
        in[n] = i;
        hmac_sha1(s->pmk, WPA_PMK_LEN, in, n + 1, out + 20 * i);
    }
    memcpy(ptk, out, WPA_KCK_LEN + WPA_KEK_LEN + WPA_TK_LEN);
    memset(out, 0, sizeof(out));
}

/* The frame's MIC (HMAC-SHA1-128 with @kck over the frame, its MIC field zero) */
static bool mic_ok(const UINT8 *kck, const UINT8 *f, UINT32 len)
{
    UINT8 copy[1024], mac[20];
    if (len > sizeof(copy)) return false;
    memcpy(copy, f, len);
    memset(copy + 4 + OFF_MIC, 0, 16);
    hmac_sha1(kck, WPA_KCK_LEN, copy, len, mac);
    UINT8 d = 0;
    for (int i = 0; i < 16; i++) d |= (UINT8)(mac[i] ^ f[4 + OFF_MIC + i]);
    return d == 0;
}

/* An EAPOL-Key frame from the station: @info, the access point's replay
 * counter, @nonce (or zeros), @data; its MIC with @kck */
static UINT32 build(UINT8 *out, UINT16 info, const UINT8 *replay, const UINT8 *nonce, const UINT8 *data, UINT32 dlen,
                    const UINT8 *kck)
{
    UINT32 len = 4 + DESC_LEN + dlen;
    memset(out, 0, len);
    out[0] = 1;                                         /* (802.1X-2001's version, as wpa_supplicant sends) */
    out[1] = EAPOL_KEY;
    put16(out + 2, DESC_LEN + dlen);
    UINT8 *b = out + 4;
    b[0] = DESC_RSN;
    put16(b + OFF_INFO, info);
    memcpy(b + OFF_REPLAY, replay, 8);
    if (nonce) memcpy(b + OFF_NONCE, nonce, WPA_NONCE_LEN);
    put16(b + OFF_DATA_LEN, dlen);
    if (dlen) memcpy(b + DESC_LEN, data, dlen);
    UINT8 mac[20];
    hmac_sha1(kck, WPA_KCK_LEN, out, len, mac);
    memcpy(b + OFF_MIC, mac, 16);
    return len;
}

/* The key data, unwrapped with the KEK (RFC 3394): its RSN element and GTK KDE */
static bool key_data(WpaSupplicant *s, const UINT8 *d, UINT32 n, const UINT8 **ie, UINT32 *ie_len)
{
    UINT8 plain[256];
    size_t pn = 0;
    if (n < 24 || n % 8 || n - 8 > sizeof(plain)) { s->why = "key data of a wrong length"; return false; }
    mbedtls_nist_kw_context kw;
    mbedtls_nist_kw_init(&kw);
    int r = mbedtls_nist_kw_setkey(&kw, MBEDTLS_CIPHER_ID_AES, s->ptk + WPA_KCK_LEN, 128, 0);
    if (!r) r = mbedtls_nist_kw_unwrap(&kw, MBEDTLS_KW_MODE_KW, d, n, plain, &pn, sizeof(plain));
    mbedtls_nist_kw_free(&kw);
    if (r) { s->why = "key data that does not unwrap"; return false; }
    static UINT8 ies[256];                              /* (one handshake at a time: the caller holds the adapter) */
    memcpy(ies, plain, pn);
    memset(plain, 0, sizeof(plain));
    *ie = NULL;
    *ie_len = 0;
    s->gtk_len = 0;
    for (UINT32 off = 0; off + 2 <= pn;) {
        UINT8 id = ies[off], len = ies[off + 1];
        if (id == 0xDD && len == 0) break;              /* (the padding) */
        if (off + 2 + len > pn) break;
        const UINT8 *e = ies + off + 2;
        if (id == 48) {
            *ie = ies + off;
            *ie_len = 2u + len;
        } else if (id == 0xDD && len >= 6 + 5 && e[0] == 0x00 && e[1] == 0x0F && e[2] == 0xAC && e[3] == 1 &&
                   len - 6u <= WPA_GTK_MAX) {           /* GTK KDE: key ID, reserved, the GTK */
            s->gtk_id = e[4] & 3;
            s->gtk_len = (UINT8)(len - 6);
            memcpy(s->gtk, e + 6, s->gtk_len);
        }
        off += 2u + len;
    }
    if (!s->gtk_len) { s->why = "no group key in the key data"; return false; }
    return true;
}

void WpaStart(WpaSupplicant *s)
{
    memset(s->ptk, 0, sizeof(s->ptk));
    memset(s->tptk, 0, sizeof(s->tptk));
    s->have_ptk = s->have_tptk = s->have_replay = false;
    s->replay = 0;
    s->gtk_len = 0;
    s->why = NULL;
}

void WpaClear(WpaSupplicant *s)
{
    WpaStart(s);
    memset(s->pmk, 0, sizeof(s->pmk));
    memset(s->gtk, 0, sizeof(s->gtk));
}

int WpaReceive(WpaSupplicant *s, const UINT8 *f, UINT32 len, UINT8 *reply, UINT32 *reply_len)
{
    *reply_len = 0;
    s->why = "a short frame";
    if (len < 4 + DESC_LEN || f[1] != EAPOL_KEY) { if (len >= 2) s->why = "not an EAPOL-Key frame"; return WPA_DROP; }
    UINT32 blen = be16(f + 2);
    if (4 + blen > len || blen < DESC_LEN) return WPA_DROP;
    len = 4 + blen;
    const UINT8 *b = f + 4;
    if (b[0] != DESC_RSN) { s->why = "not an RSN key descriptor (WPA's?)"; return WPA_DROP; }
    UINT16 info = be16(b + OFF_INFO);
    if ((info & KI_VERSION) != 2) { s->why = "a key descriptor version other than 2 (HMAC-SHA1, AES key wrap)"; return WPA_DROP; }
    UINT32 dlen = be16(b + OFF_DATA_LEN);
    if (DESC_LEN + dlen > blen) return WPA_DROP;
    if (!(info & KI_ACK)) { s->why = "not from the authenticator"; return WPA_DROP; }
    UINT64 replay = be64(b + OFF_REPLAY);
    if ((info & KI_MIC) && s->have_replay && replay <= s->replay) { s->why = "a replayed frame"; return WPA_DROP; }
    if ((info & KI_PAIRWISE) && !(info & KI_MIC)) {
        /* message 1: a new SNonce, the PTK it makes; message 2 */
        memcpy(s->anonce, b + OFF_NONCE, WPA_NONCE_LEN);
        TlsEntropyOutput(s->snonce, WPA_NONCE_LEN);
        derive_ptk(s, s->tptk);
        s->have_tptk = true;
        *reply_len = build(reply, 2 | KI_PAIRWISE | KI_MIC, b + OFF_REPLAY, s->snonce, s->own_ie, s->own_ie_len, s->tptk);
        return WPA_REPLY;
    }
    if ((info & KI_PAIRWISE) && (info & KI_MIC) && (info & KI_INSTALL)) {
        /* message 3: the MIC proves the PTK (and so the passphrase); the GTK; message 4 */
        if (!s->have_tptk || memcmp(s->anonce, b + OFF_NONCE, WPA_NONCE_LEN)) { s->why = "message 3 without message 1"; return WPA_DROP; }
        if (!mic_ok(s->tptk, f, len)) { s->why = "message 3's MIC is wrong"; return WPA_DROP; }
        memcpy(s->ptk, s->tptk, sizeof(s->ptk));
        s->have_ptk = true;
        if (!(info & KI_ENCRYPTED)) { s->why = "message 3's key data is not encrypted"; return WPA_DROP; }
        const UINT8 *ie;
        UINT32 ie_len;
        if (!key_data(s, b + DESC_LEN, dlen, &ie, &ie_len)) return WPA_DROP;
        if (s->ap_ie_len && (!ie || ie_len != s->ap_ie_len || memcmp(ie, s->ap_ie, ie_len))) {
            s->why = "the access point's RSN element differs from its beacon's (a downgrade?)";
            return WPA_DROP;
        }
        memcpy(s->gtk_rsc, b + OFF_RSC, 6);
        s->replay = replay;
        s->have_replay = true;
        *reply_len = build(reply, 2 | KI_PAIRWISE | KI_MIC | KI_SECURE, b + OFF_REPLAY, NULL, NULL, 0, s->ptk);
        return WPA_PAIRWISE;
    }
    if (!(info & KI_PAIRWISE) && (info & KI_MIC) && (info & KI_SECURE)) {
        /* group message 1: a new GTK; group message 2 */
        if (!s->have_ptk) { s->why = "a group key before the pairwise key"; return WPA_DROP; }
        if (!mic_ok(s->ptk, f, len)) { s->why = "the group message's MIC is wrong"; return WPA_DROP; }
        const UINT8 *ie;
        UINT32 ie_len;
        if (!(info & KI_ENCRYPTED) || !key_data(s, b + DESC_LEN, dlen, &ie, &ie_len)) return WPA_DROP;
        memcpy(s->gtk_rsc, b + OFF_RSC, 6);
        s->replay = replay;
        s->have_replay = true;
        *reply_len = build(reply, 2 | KI_MIC | KI_SECURE, b + OFF_REPLAY, NULL, NULL, 0, s->ptk);
        return WPA_GROUP;
    }
    s->why = "an EAPOL-Key frame the station does not expect";
    return WPA_DROP;
}
