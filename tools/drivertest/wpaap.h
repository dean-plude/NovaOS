/*
 * wpaap.h — the access point's half of WPA2-Personal for wditest.sys
 * (tools/drivertest/wditest.c): the cryptography the authenticator needs
 * (SHA-1, HMAC-SHA1, the 802.11 PRF, AES-128 and AES key wrap) and the
 * EAPOL-Key frames it sends (802.11-2020 12.7.6, 12.7.7)
 *
 * Written apart from the kernel's supplicant (kernel/nt/wpa.c, over Mbed
 * TLS) on purpose: the handshake only completes when two independent
 * implementations agree on every MIC and key.  Freestanding C, no imports,
 * so a host program can check it against Python's hashlib and the
 * cryptography package (tests/tools/test_wpaap.py).
 */
#pragma once

typedef unsigned char      u8;
typedef unsigned int       u32;
typedef unsigned long long u64;

static void wpa_copy(void *d, const void *s, unsigned int n)
{
    u8 *a = d;
    const u8 *b = s;
    while (n--) *a++ = *b++;
}

static void wpa_zero(void *d, unsigned int n)
{
    volatile u8 *a = d;
    while (n--) *a++ = 0;
}

/* ---- SHA-1 (FIPS 180-4) ---- */
typedef struct { u32 h[5]; u64 len; u8 buf[64]; u32 n; } Sha1;

static u32 rol(u32 x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(Sha1 *c, const u8 *p)
{
    u32 w[80], a = c->h[0], b = c->h[1], d = c->h[3], e = c->h[4], cc = c->h[2];
    for (int i = 0; i < 16; i++) w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++) {
        u32 f, k;
        if (i < 20)      { f = (b & cc) | (~b & d);           k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else             { f = b ^ cc ^ d;                    k = 0xCA62C1D6; }
        u32 t = rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rol(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void sha1_init(Sha1 *c)
{
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE; c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len = 0;
    c->n = 0;
}

static void sha1_update(Sha1 *c, const u8 *p, unsigned int n)
{
    c->len += n;
    while (n--) {
        c->buf[c->n++] = *p++;
        if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; }
    }
}

static void sha1_final(Sha1 *c, u8 out[20])
{
    u64 bits = c->len * 8;
    u8 pad = 0x80, z = 0;
    sha1_update(c, &pad, 1);
    while (c->n != 56) sha1_update(c, &z, 1);
    u8 l[8];
    for (int i = 0; i < 8; i++) l[i] = (u8)(bits >> (56 - 8 * i));
    sha1_update(c, l, 8);
    for (int i = 0; i < 20; i++) out[i] = (u8)(c->h[i / 4] >> (24 - 8 * (i % 4)));
}

/* HMAC-SHA1 (RFC 2104) of two concatenated pieces (@b may be empty) */
static void ap_hmac(const u8 *key, unsigned int klen, const u8 *a, unsigned int an, const u8 *b, unsigned int bn,
                      u8 out[20])
{
    u8 k[64], pad[64], inner[20];
    wpa_zero(k, 64);
    if (klen > 64) {
        Sha1 c;
        sha1_init(&c);
        sha1_update(&c, key, klen);
        sha1_final(&c, k);
    } else {
        wpa_copy(k, key, klen);
    }
    Sha1 c;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha1_init(&c);
    sha1_update(&c, pad, 64);
    sha1_update(&c, a, an);
    if (bn) sha1_update(&c, b, bn);
    sha1_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    sha1_init(&c);
    sha1_update(&c, pad, 64);
    sha1_update(&c, inner, 20);
    sha1_final(&c, out);
}

/* The PTK (KCK, KEK, TK: 48 bytes) from the PMK, both addresses and both nonces (PRF-384) */
static int less(const u8 *a, const u8 *b, int n)
{
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i];
    return 0;
}

static void wpa_ptk(const u8 pmk[32], const u8 aa[6], const u8 spa[6], const u8 anonce[32], const u8 snonce[32],
                    u8 ptk[48])
{
    static const char label[] = "Pairwise key expansion";
    u8 in[sizeof(label) + 76 + 1], out[60];
    unsigned int n = sizeof(label);                     /* (the label and its 0 octet) */
    wpa_copy(in, label, sizeof(label));
    int a = less(aa, spa, 6), b = less(anonce, snonce, 32);
    wpa_copy(in + n, a ? aa : spa, 6);
    wpa_copy(in + n + 6, a ? spa : aa, 6);
    wpa_copy(in + n + 12, b ? anonce : snonce, 32);
    wpa_copy(in + n + 44, b ? snonce : anonce, 32);
    n += 76;
    for (u8 i = 0; i < 3; i++) {
        in[n] = i;
        ap_hmac(pmk, 32, in, n + 1, 0, 0, out + 20 * i);
    }
    wpa_copy(ptk, out, 48);
}

/* ---- AES-128 encryption (FIPS 197) and key wrap (RFC 3394) ---- */
static const u8 SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

static u8 xtime(u8 x) { return (u8)(x << 1 ^ (x & 0x80 ? 0x1b : 0)); }

static void aes128_encrypt(const u8 key[16], const u8 in[16], u8 out[16])
{
    u8 rk[176], s[16];
    wpa_copy(rk, key, 16);
    u8 rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        u8 t[4] = { rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1] };
        if (i % 16 == 0) {
            u8 x = t[0];
            t[0] = SBOX[t[1]] ^ rcon; t[1] = SBOX[t[2]]; t[2] = SBOX[t[3]]; t[3] = SBOX[x];
            rcon = xtime(rcon);
        }
        for (int j = 0; j < 4; j++) rk[i + j] = rk[i - 16 + j] ^ t[j];
    }
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
    for (int r = 1; r <= 10; r++) {
        u8 t[16];
        for (int i = 0; i < 16; i++) t[i] = SBOX[s[(i + 4 * (i % 4)) % 16]];   /* SubBytes, ShiftRows */
        if (r < 10)
            for (int c = 0; c < 4; c++) {                                       /* MixColumns */
                u8 *p = t + 4 * c, a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3], x = a0 ^ a1 ^ a2 ^ a3;
                p[0] ^= x ^ xtime(a0 ^ a1);
                p[1] ^= x ^ xtime(a1 ^ a2);
                p[2] ^= x ^ xtime(a2 ^ a3);
                p[3] ^= x ^ xtime(a3 ^ a0);
            }
        for (int i = 0; i < 16; i++) s[i] = t[i] ^ rk[16 * r + i];
    }
    wpa_copy(out, s, 16);
}

/* RFC 3394 key wrap of @n bytes (a multiple of 8, at least 16) into @out (n + 8 bytes) */
static void aes_wrap(const u8 kek[16], const u8 *in, unsigned int n, u8 *out)
{
    u8 a[8], b[16];
    unsigned int m = n / 8;
    for (int i = 0; i < 8; i++) a[i] = 0xA6;
    wpa_copy(out + 8, in, n);
    for (unsigned int j = 0; j < 6; j++)
        for (unsigned int i = 1; i <= m; i++) {
            wpa_copy(b, a, 8);
            wpa_copy(b + 8, out + 8 * i, 8);
            aes128_encrypt(kek, b, b);
            u64 t = (u64)m * j + i;
            for (int k = 0; k < 8; k++) a[k] = b[k] ^ (u8)(t >> (56 - 8 * k));
            wpa_copy(out + 8 * i, b + 8, 8);
        }
    wpa_copy(out, a, 8);
}

/* ---- EAPOL-Key frames ---- */
#define KI_VER2     0x0002
#define KI_PAIRWISE 0x0008
#define KI_INSTALL  0x0040
#define KI_ACK      0x0080
#define KI_MIC      0x0100
#define KI_SECURE   0x0200
#define KI_ENC      0x1000
#define EK_DESC     95              /* (the key descriptor before its key data) */

/* An EAPOL-Key frame from the access point into @f: its length.  @kck
 * (or 0: no MIC) signs it; @data is the key data, wrapped with @kek if given */
static unsigned int eapol_key(u8 *f, unsigned int info, unsigned int keylen, u64 replay, const u8 *nonce,
                              const u8 *rsc, const u8 *data, unsigned int dlen, const u8 *kek, const u8 *kck)
{
    unsigned int wl = kek ? dlen + 8 : dlen;
    wpa_zero(f, 4 + EK_DESC + wl);
    f[0] = 2;                                           /* (802.1X-2004) */
    f[1] = 3;
    f[2] = (u8)((EK_DESC + wl) >> 8);
    f[3] = (u8)(EK_DESC + wl);
    u8 *b = f + 4;
    b[0] = 2;
    b[1] = (u8)(info >> 8);
    b[2] = (u8)info;
    b[3] = (u8)(keylen >> 8);
    b[4] = (u8)keylen;
    for (int i = 0; i < 8; i++) b[5 + i] = (u8)(replay >> (56 - 8 * i));
    if (nonce) wpa_copy(b + 13, nonce, 32);
    if (rsc) wpa_copy(b + 61, rsc, 8);
    b[93] = (u8)(wl >> 8);
    b[94] = (u8)wl;
    if (kek) aes_wrap(kek, data, dlen, b + EK_DESC);
    else if (dlen) wpa_copy(b + EK_DESC, data, dlen);
    if (kck) {
        u8 mic[20];
        ap_hmac(kck, 16, f, 4 + EK_DESC + wl, 0, 0, mic);
        wpa_copy(b + 77, mic, 16);
    }
    return 4 + EK_DESC + wl;
}

/* Whether frame @f's MIC is right for @kck (the MIC field taken as zero) */
static int eapol_mic_ok(const u8 *f, unsigned int n, const u8 *kck)
{
    static u8 c[512];
    u8 mic[20];
    if (n > sizeof(c) || n < 4 + EK_DESC) return 0;
    wpa_copy(c, f, n);
    wpa_zero(c + 4 + 77, 16);
    ap_hmac(kck, 16, c, n, 0, 0, mic);
    for (int i = 0; i < 16; i++) if (mic[i] != f[4 + 77 + i]) return 0;
    return 1;
}

/* ---- the authenticator: one station's 4-way and group key handshakes ---- */
typedef struct {
    u8  pmk[32], aa[6], spa[6];
    const u8 *ap_ie, *sta_ie;       /* its RSN element (the beacon's), the station's (its association request's) */
    unsigned int ap_ie_len, sta_ie_len;
    u8  anonce[32], snonce[32], ptk[48];
    u8  gtk[16], gtk_id, rsc[8];
    u64 replay;
    const char *why;                /* (why the last frame was refused) */
} WpaAp;

#define AP_KCK(a) ((a)->ptk)
#define AP_KEK(a) ((a)->ptk + 16)
#define AP_TK(a)  ((a)->ptk + 32)

static unsigned int be16_at(const u8 *p) { return (unsigned int)(p[0] << 8 | p[1]); }

/* Message 1: the ANonce */
static unsigned int ap_msg1(WpaAp *a, u8 *f)
{
    a->replay++;
    return eapol_key(f, KI_VER2 | KI_PAIRWISE | KI_ACK, 16, a->replay, a->anonce, 0, 0, 0, 0, 0);
}

/* The station's frame @f checked: an EAPOL-Key frame with @want of the
 * Key Information bits it should have, the replay counter just sent, a
 * right MIC (after message 2 derives the PTK); 1 if it passes */
static int ap_check(WpaAp *a, const u8 *f, unsigned int n, unsigned int want)
{
    a->why = "not an EAPOL-Key frame";
    if (n < 4 + EK_DESC || f[1] != 3 || f[4] != 2 || 4 + be16_at(f + 2) > n) return 0;
    n = 4 + be16_at(f + 2);
    const u8 *b = f + 4;
    unsigned int info = be16_at(b + 1);
    a->why = "the wrong Key Information";
    if ((info & (KI_PAIRWISE | KI_INSTALL | KI_ACK | KI_MIC | KI_SECURE | 7)) != want) return 0;
    u64 r = 0;
    for (int i = 0; i < 8; i++) r = r << 8 | b[5 + i];
    a->why = "the wrong replay counter";
    if (r != a->replay) return 0;
    if ((want & (KI_PAIRWISE | KI_SECURE)) == KI_PAIRWISE) {            /* (message 2: its SNonce makes the PTK) */
        wpa_copy(a->snonce, b + 13, 32);
        wpa_ptk(a->pmk, a->aa, a->spa, a->anonce, a->snonce, a->ptk);
    }
    a->why = "a wrong MIC (not the passphrase)";
    if (!eapol_mic_ok(f, n, AP_KCK(a))) return 0;
    if ((want & (KI_PAIRWISE | KI_SECURE)) == KI_PAIRWISE) {
        unsigned int dl = be16_at(b + 93);
        a->why = "an RSN element other than its association request's";
        if (dl != a->sta_ie_len || 4 + EK_DESC + dl > n) return 0;
        for (unsigned int i = 0; i < dl; i++) if (b[EK_DESC + i] != a->sta_ie[i]) return 0;
    }
    a->why = 0;
    return 1;
}

static int ap_msg2(WpaAp *a, const u8 *f, unsigned int n) { return ap_check(a, f, n, KI_VER2 | KI_PAIRWISE | KI_MIC); }

/* The GTK KDE (00-0f-ac:1: the key ID, then the GTK) at @p: its length */
static unsigned int gtk_kde(WpaAp *a, u8 *p)
{
    static const u8 h[6] = { 0xDD, 22, 0x00, 0x0F, 0xAC, 1 };
    wpa_copy(p, h, 6);
    p[6] = a->gtk_id & 3;
    p[7] = 0;
    wpa_copy(p + 8, a->gtk, 16);
    return 24;
}

/* Message 3: install, the access point's RSN element and the GTK (wrapped with the KEK) */
static unsigned int ap_msg3(WpaAp *a, u8 *f)
{
    u8 d[128];
    unsigned int n = 0;
    wpa_copy(d, a->ap_ie, a->ap_ie_len);
    n = a->ap_ie_len;
    n += gtk_kde(a, d + n);
    if (n % 8) { d[n++] = 0xDD; while (n % 8) d[n++] = 0; }  /* (padding, 802.11-2020 12.7.2) */
    a->replay++;
    unsigned int len = eapol_key(f, KI_VER2 | KI_PAIRWISE | KI_INSTALL | KI_ACK | KI_MIC | KI_SECURE | KI_ENC, 16,
                                 a->replay, a->anonce, a->rsc, d, n, AP_KEK(a), AP_KCK(a));
    wpa_zero(d, sizeof(d));
    return len;
}

static int ap_msg4(WpaAp *a, const u8 *f, unsigned int n)
{
    return ap_check(a, f, n, KI_VER2 | KI_PAIRWISE | KI_MIC | KI_SECURE);
}

/* Group message 1: a new GTK (a->gtk, a->gtk_id), wrapped with the KEK */
static unsigned int ap_group1(WpaAp *a, u8 *f)
{
    u8 d[24];
    gtk_kde(a, d);
    a->replay++;
    unsigned int len = eapol_key(f, KI_VER2 | KI_ACK | KI_MIC | KI_SECURE | KI_ENC, 0, a->replay, 0, a->rsc, d, 24,
                                 AP_KEK(a), AP_KCK(a));
    wpa_zero(d, sizeof(d));
    return len;
}

static int ap_group2(WpaAp *a, const u8 *f, unsigned int n) { return ap_check(a, f, n, KI_VER2 | KI_MIC | KI_SECURE); }
