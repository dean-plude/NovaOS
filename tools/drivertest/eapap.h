/*
 * eapap.h — the authentication server behind wditest.sys's WPA2-Enterprise
 * network, NovaCorp (roadmap step 28.1): EAP over EAPOL from the station,
 * answered as a RADIUS server with PEAP version 0 and EAP-MSCHAPv2 would
 * (hostapd's eap_server or FreeRADIUS, with the access point relaying)
 *
 * Its own half of each exchange, written apart from NovaOS's peer
 * (kernel/nt/eap.c) from the same documents ([MS-PEAP], RFC 2759,
 * [MS-CHAP], RFC 3748): Request/Identity, PEAP Start, the TLS server's
 * flights in fragments (TLS 1.2 by Mbed TLS, compiled into this driver,
 * with the test certificate tools/drivertest/mksys.py makes:
 * "radius.novacorp.test" under "NovaCorp Test Root"), then inside the
 * tunnel the inner Identity, an MS-CHAPv2 challenge checked against the
 * one user it knows (alice, "nova-peap-password"), the Result TLV, and
 * EAP-Success.  The MSK comes from the TLS master secret through the TLS
 * PRF with "client EAP encryption" (RFC 5216 2.3); its first 32 bytes are
 * the PMK of the 4-way handshake after (wpaap.h, AKM 1).
 *
 * A station with a certificate answers PEAP's start with a Nak naming
 * EAP-TLS (RFC 5216), and the server starts that instead: the same TLS
 * server, now asking for the station's certificate (CertificateRequest)
 * and checking it chains to NovaCorp Test Root (CERT_ROOT), as
 * FreeRADIUS's eap tls does with its ca_file; nothing inside the tunnel,
 * EAP-Success after the station acknowledges the server's Finished, the
 * MSK the same way.  A certificate from anywhere else gets the TLS alert
 * in a request, then EAP-Failure.
 */
#pragma once

#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha1.h"
#include "mbedtls/des.h"
#include "wditest_certs.h"          /* (made by mksys.py: CERT_SERVER, KEY_SERVER, CERT_ROOT) */

#define EAP_USER      "alice"
#define EAP_PASSWORD  "nova-peap-password"
#define EAP_SERVER    "NovaCorp-RADIUS"
#define EAP_FRAG      1000          /* (TLS bytes per PEAP fragment the server sends) */
#define EAP_TLSBUF    8192

typedef struct {
    int   state;                    /* EAPS_* */
    u8    method;                   /* 25 (PEAP) or 13 (EAP-TLS: the station asked for it with a Nak) */
    u8    id;                       /* the identifier of the request outstanding */
    char  identity[64];
    mbedtls_ssl_context ssl;
    int   ssl_ready;
    u8    in[EAP_TLSBUF], out[EAP_TLSBUF];  /* TLS bytes from the station not yet read; to it not yet sent */
    unsigned int in_len, in_off, out_len, out_off;
    u8    master[48], randoms[64];  /* (for the MSK: from the key export callback) */
    int   have_master;
    mbedtls_tls_prf_types prf;
    u8    challenge[16], chap_id;
    int   chap_ok;
    u8    msk[64];
    const char *why;
} EapAp;
enum { EAPS_IDLE, EAPS_IDENTITY, EAPS_TLS, EAPS_INNER_IDENTITY, EAPS_CHAP, EAPS_CHAP_RESULT, EAPS_TLV, EAPS_DONE,
       EAPS_FAILED, EAPS_TLS_DONE, EAPS_ALERT };

/* ---- the server's TLS ---- */
static mbedtls_ssl_config       g_eap_conf;
static mbedtls_ctr_drbg_context g_eap_drbg;
static mbedtls_ssl_config       g_eaptls_conf;    /* (EAP-TLS: the same, asking for the station's certificate) */
static mbedtls_x509_crt         g_eap_crt, g_eap_root;
static mbedtls_pk_context       g_eap_key;
static int                      g_eap_conf_ready;

IMPORT long long KeQueryPerformanceCounter(long long *freq);

static int eap_entropy(void *ctx, unsigned char *out, size_t n)
{
    (void)ctx;
    static unsigned long long ctr;
    for (size_t i = 0; i < n; i++) {
        unsigned long long t = (unsigned long long)KeQueryPerformanceCounter(0) + ++ctr * 0x9E3779B97F4A7C15ull;
        out[i] = (unsigned char)(t ^ t >> 17 ^ t >> 31);
    }
    return 0;
}

static int eap_tls_init(void)
{
    if (g_eap_conf_ready) return g_eap_conf_ready > 0;
    g_eap_conf_ready = -1;
    static const int suites[] = { MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, 0 };
    static const char pers[] = "wditest RADIUS";
    mbedtls_ctr_drbg_init(&g_eap_drbg);
    mbedtls_x509_crt_init(&g_eap_crt);
    mbedtls_x509_crt_init(&g_eap_root);
    mbedtls_pk_init(&g_eap_key);
    mbedtls_ssl_config_init(&g_eap_conf);
    mbedtls_ssl_config_init(&g_eaptls_conf);
    int r = mbedtls_ctr_drbg_seed(&g_eap_drbg, eap_entropy, 0, (const unsigned char *)pers, sizeof(pers) - 1);
    if (!r) r = mbedtls_x509_crt_parse_der(&g_eap_crt, CERT_SERVER, sizeof(CERT_SERVER));
    if (!r) r = mbedtls_pk_parse_key(&g_eap_key, KEY_SERVER, sizeof(KEY_SERVER), 0, 0, mbedtls_ctr_drbg_random,
                                     &g_eap_drbg);
    if (!r) r = mbedtls_x509_crt_parse_der(&g_eap_root, CERT_ROOT, sizeof(CERT_ROOT));
    if (!r) r = mbedtls_ssl_config_defaults(&g_eap_conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT);
    if (!r) r = mbedtls_ssl_config_defaults(&g_eaptls_conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT);
    if (r) {
        DbgPrint("wditest: the RADIUS server's TLS did not start (-0x%x)\n", (unsigned int)-r);
        return 0;
    }
    mbedtls_ssl_conf_rng(&g_eap_conf, mbedtls_ctr_drbg_random, &g_eap_drbg);
    mbedtls_ssl_conf_min_tls_version(&g_eap_conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&g_eap_conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_ciphersuites(&g_eap_conf, suites);
    mbedtls_ssl_conf_authmode(&g_eap_conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_own_cert(&g_eap_conf, &g_eap_crt, &g_eap_key);
    mbedtls_ssl_conf_rng(&g_eaptls_conf, mbedtls_ctr_drbg_random, &g_eap_drbg);
    mbedtls_ssl_conf_min_tls_version(&g_eaptls_conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&g_eaptls_conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_ciphersuites(&g_eaptls_conf, suites);
    mbedtls_ssl_conf_authmode(&g_eaptls_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&g_eaptls_conf, &g_eap_root, 0);
    mbedtls_ssl_conf_own_cert(&g_eaptls_conf, &g_eap_crt, &g_eap_key);
    g_eap_conf_ready = 1;
    return 1;
}

static int eap_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    EapAp *e = ctx;
    if (len > sizeof(e->out) - e->out_len) len = sizeof(e->out) - e->out_len;
    if (!len) return MBEDTLS_ERR_SSL_WANT_WRITE;
    wpa_copy(e->out + e->out_len, buf, (unsigned int)len);
    e->out_len += (unsigned int)len;
    return (int)len;
}

static int eap_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    EapAp *e = ctx;
    unsigned int left = e->in_len - e->in_off;
    if (!left) return MBEDTLS_ERR_SSL_WANT_READ;
    if (len > left) len = left;
    wpa_copy(buf, e->in + e->in_off, (unsigned int)len);
    e->in_off += (unsigned int)len;
    if (e->in_off == e->in_len) e->in_off = e->in_len = 0;
    return (int)len;
}

static void eap_export(void *ctx, mbedtls_ssl_key_export_type type, const unsigned char *secret, size_t slen,
                       const unsigned char cr[32], const unsigned char sr[32], mbedtls_tls_prf_types prf)
{
    EapAp *e = ctx;
    if (type != MBEDTLS_SSL_KEY_EXPORT_TLS12_MASTER_SECRET || slen != 48) return;
    wpa_copy(e->master, secret, 48);
    wpa_copy(e->randoms, cr, 32);
    wpa_copy(e->randoms + 32, sr, 32);
    e->prf = prf;
    e->have_master = 1;
}

static void eap_ap_clear(EapAp *e)
{
    if (e->ssl_ready) mbedtls_ssl_free(&e->ssl);
    wpa_zero(e, sizeof(*e));
}

/* ---- MS-CHAPv2, the server's half (RFC 2759 8) ---- */
static unsigned int ap_rol(unsigned int x, int n) { return x << n | x >> (32 - n); }

static void ap_md4(const u8 *m, unsigned int n, u8 out[16])
{
    unsigned int h[4] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u };
    u8 blk[64];
    unsigned long long bits = (unsigned long long)n << 3;
    unsigned int total = (n + 9 + 63) / 64 * 64;
    for (unsigned int off = 0; off < total; off += 64) {
        for (unsigned int i = 0; i < 64; i++) {
            unsigned int k = off + i;
            blk[i] = k < n ? m[k] : k == n ? 0x80 : k >= total - 8 ? (u8)(bits >> (8 * (k - (total - 8)))) : 0;
        }
        unsigned int x[16], a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 16; i++) x[i] = blk[4 * i] | blk[4 * i + 1] << 8 | blk[4 * i + 2] << 16 | (unsigned)blk[4 * i + 3] << 24;
        /* round 1: F, shifts 3 7 11 19, words in order */
        for (int i = 0; i < 16; i++) {
            unsigned int f = (b & c) | (~b & d), s = (const int[]){ 3, 7, 11, 19 }[i % 4];
            unsigned int t = ap_rol(a + f + x[i], s);
            a = d; d = c; c = b; b = t;
        }
        /* round 2: G, + 5A827999, shifts 3 5 9 13, words by column */
        for (int i = 0; i < 16; i++) {
            unsigned int g = (b & c) | (b & d) | (c & d), s = (const int[]){ 3, 5, 9, 13 }[i % 4];
            unsigned int t = ap_rol(a + g + x[(i % 4) * 4 + i / 4] + 0x5a827999u, s);
            a = d; d = c; c = b; b = t;
        }
        /* round 3: H, + 6ED9EBA1, shifts 3 9 11 15, words 0 8 4 12 2 10 6 14 ... */
        static const u8 o3[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
        for (int i = 0; i < 16; i++) {
            unsigned int s = (const int[]){ 3, 9, 11, 15 }[i % 4];
            unsigned int t = ap_rol(a + (b ^ c ^ d) + x[o3[i]] + 0x6ed9eba1u, s);
            a = d; d = c; c = b; b = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    for (int i = 0; i < 16; i++) out[i] = (u8)(h[i / 4] >> (8 * (i % 4)));
}

static void ap_pw_hash(const char *pw, u8 out[16])
{
    u8 u[128];
    unsigned int n = 0;
    while (*pw && n + 2 <= sizeof(u)) { u[n++] = (u8)*pw++; u[n++] = 0; }    /* (ASCII as UTF-16LE) */
    ap_md4(u, n, out);
}

static void ap_challenge_hash(const u8 peer[16], const u8 auth[16], const char *user, u8 out[8])
{
    Sha1 c;
    u8 d[20];
    unsigned int ul = 0;
    while (user[ul]) ul++;
    sha1_init(&c);
    sha1_update(&c, peer, 16);
    sha1_update(&c, auth, 16);
    sha1_update(&c, (const u8 *)user, ul);
    sha1_final(&c, d);
    wpa_copy(out, d, 8);
}

static void ap_des(const u8 clear[8], const u8 *k7, u8 out[8])
{
    u8 k[8];
    for (int i = 0; i < 8; i++) {           /* (56 bits into 8 bytes, 7 at a time, the low bit for parity) */
        unsigned int bit = (unsigned int)i * 7, v = (unsigned int)(k7[bit / 8] << 8 | (bit / 8 + 1 < 7 ? k7[bit / 8 + 1] : 0));
        k[i] = (u8)(((v >> (9 - bit % 8)) & 0x7f) << 1);
    }
    mbedtls_des_context c;
    mbedtls_des_init(&c);
    mbedtls_des_setkey_enc(&c, k);
    mbedtls_des_crypt_ecb(&c, clear, out);
    mbedtls_des_free(&c);
}

static void ap_nt_response(const u8 auth[16], const u8 peer[16], const char *user, const char *pw, u8 out[24])
{
    u8 ch[8], z[21];
    ap_challenge_hash(peer, auth, user, ch);
    wpa_zero(z, sizeof(z));
    ap_pw_hash(pw, z);
    for (int i = 0; i < 3; i++) ap_des(ch, z + 7 * i, out + 8 * i);
}

static void ap_auth_response(const char *pw, const u8 nt[24], const u8 peer[16], const u8 auth[16], const char *user,
                             char out[43])
{
    static const char m1[] = "Magic server to client signing constant", m2[] = "Pad to make it do more than one iteration";
    u8 h[16], hh[16], d[20], ch[8];
    ap_pw_hash(pw, h);
    ap_md4(h, 16, hh);
    Sha1 c;
    sha1_init(&c);
    sha1_update(&c, hh, 16);
    sha1_update(&c, nt, 24);
    sha1_update(&c, (const u8 *)m1, sizeof(m1) - 1);
    sha1_final(&c, d);
    ap_challenge_hash(peer, auth, user, ch);
    sha1_init(&c);
    sha1_update(&c, d, 20);
    sha1_update(&c, ch, 8);
    sha1_update(&c, (const u8 *)m2, sizeof(m2) - 1);
    sha1_final(&c, d);
    out[0] = 'S';
    out[1] = '=';
    for (int i = 0; i < 20; i++) {
        out[2 + 2 * i] = "0123456789ABCDEF"[d[i] >> 4];
        out[3 + 2 * i] = "0123456789ABCDEF"[d[i] & 15];
    }
    out[42] = 0;
}

/* ---- EAP ---- */

/* An EAPOL frame in @f holding an EAP packet: @code, the request's
 * identifier, and (for a request) @type and @data */
static unsigned int eap_frame(EapAp *e, u8 *f, u8 code, u8 type, const u8 *data, unsigned int n)
{
    unsigned int len = code == 1 ? 5 + n : 4;
    f[0] = 2; f[1] = 0; f[2] = (u8)(len >> 8); f[3] = (u8)len;
    f[4] = code; f[5] = e->id; f[6] = (u8)(len >> 8); f[7] = (u8)len;
    if (code == 1) {
        f[8] = type;
        if (n) wpa_copy(f + 9, data, n);
    }
    return 4 + len;
}

static unsigned int eap_request(EapAp *e, u8 *f, u8 type, const u8 *data, unsigned int n)
{
    e->id++;
    return eap_frame(e, f, 1, type, data, n);
}

/* The next PEAP fragment of the server's TLS bytes (or an empty request: an acknowledgment) */
static unsigned int peap_request(EapAp *e, u8 *f)
{
    u8 b[6 + EAP_FRAG];
    unsigned int left = e->out_len - e->out_off, k = left > EAP_FRAG ? EAP_FRAG : left, o = 1;
    b[0] = 0;
    if (left > EAP_FRAG && !e->out_off) {
        b[0] |= 0x80;
        b[1] = (u8)(e->out_len >> 24); b[2] = (u8)(e->out_len >> 16); b[3] = (u8)(e->out_len >> 8); b[4] = (u8)e->out_len;
        o = 5;
    }
    if (left > k) b[0] |= 0x40;
    wpa_copy(b + o, e->out + e->out_off, k);
    e->out_off += k;
    if (e->out_off == e->out_len) e->out_off = e->out_len = 0;
    return eap_request(e, f, e->method, b, o + k);
}

/* An inner request through the tunnel (PEAPv0: @m without its EAP header, unless it is an EAP-TLV) */
static unsigned int inner_request(EapAp *e, u8 *f, const u8 *m, unsigned int n)
{
    if (mbedtls_ssl_write(&e->ssl, m, n) < 0) { e->why = "the tunnel refused the inner request"; return 0; }
    return peap_request(e, f);
}

static unsigned int eap_ap_fail(EapAp *e, u8 *f, const char *why)
{
    e->why = why;
    e->state = EAPS_FAILED;
    e->id++;
    return eap_frame(e, f, 4, 0, 0, 0);
}

/* A new TLS session for e->method: PEAP's, or EAP-TLS's asking for the station's certificate */
static int eap_ap_session(EapAp *e)
{
    if (e->ssl_ready) mbedtls_ssl_free(&e->ssl);
    mbedtls_ssl_init(&e->ssl);
    e->ssl_ready = 1;
    e->in_len = e->in_off = e->out_len = e->out_off = 0;
    e->have_master = 0;
    if (mbedtls_ssl_setup(&e->ssl, e->method == 13 ? &g_eaptls_conf : &g_eap_conf)) return 0;
    mbedtls_ssl_set_bio(&e->ssl, e, eap_bio_send, eap_bio_recv, 0);
    mbedtls_ssl_set_export_keys_cb(&e->ssl, eap_export, e);
    return 1;
}

/* The station's certificate's common name */
static void eap_ap_peer_cn(EapAp *e, char *out, unsigned int cap)
{
    const mbedtls_x509_crt *c = mbedtls_ssl_get_peer_cert(&e->ssl);
    out[0] = 0;
    for (const mbedtls_x509_name *n = c ? &c->subject : 0; n; n = n->next)
        if (n->oid.len == 3 && n->oid.p[0] == 0x55 && n->oid.p[1] == 0x04 && n->oid.p[2] == 0x03) {
            unsigned int k = n->val.len < cap - 1 ? (unsigned int)n->val.len : cap - 1;
            wpa_copy(out, n->val.p, k);
            out[k] = 0;
        }
}

/* Start a sign-in (once associated): Request/Identity */
static unsigned int eap_ap_start(EapAp *e, u8 *f)
{
    eap_ap_clear(e);
    e->state = EAPS_IDENTITY;
    return eap_request(e, f, 1, 0, 0);
}

/* A response inside the tunnel: what comes next */
static unsigned int eap_ap_inner(EapAp *e, u8 *f, const u8 *m, unsigned int n)
{
    if (e->state == EAPS_INNER_IDENTITY) {
        if (n < 2 || m[0] != 1) return eap_ap_fail(e, f, "the inner response is not an identity");
        unsigned int k = n - 1 < sizeof(e->identity) - 1 ? n - 1 : sizeof(e->identity) - 1;
        wpa_copy(e->identity, m + 1, k);
        e->identity[k] = 0;
        DbgPrint("wditest: RADIUS: inner identity %s; MS-CHAPv2 challenge\n", e->identity);
        u8 c[5 + 1 + 16 + sizeof(EAP_SERVER)];
        unsigned int nl = sizeof(EAP_SERVER) - 1, ml = 4 + 1 + 16 + nl;
        mbedtls_ctr_drbg_random(&g_eap_drbg, e->challenge, 16);
        e->chap_id = (u8)(e->id + 0x20);
        c[0] = 26; c[1] = 1; c[2] = e->chap_id; c[3] = (u8)(ml >> 8); c[4] = (u8)ml; c[5] = 16;
        wpa_copy(c + 6, e->challenge, 16);
        wpa_copy(c + 22, EAP_SERVER, nl);
        e->state = EAPS_CHAP;
        return inner_request(e, f, c, 22 + nl);
    }
    if (e->state == EAPS_CHAP) {
        /* type 26, Response (2), id, MS-Length, Value-Size 49: peer challenge, 8 reserved, NT-Response, flags; name */
        if (n < 1 + 4 + 1 + 49 || m[0] != 26 || m[1] != 2 || m[2] != e->chap_id || m[5] != 49)
            return eap_ap_fail(e, f, "not an MS-CHAPv2 response to the challenge");
        const u8 *peer = m + 6, *nt = m + 6 + 16 + 8;
        char name[64];
        unsigned int nl = n - (1 + 4 + 1 + 49) < sizeof(name) - 1 ? n - (1 + 4 + 1 + 49) : sizeof(name) - 1;
        wpa_copy(name, m + 1 + 4 + 1 + 49, nl);
        name[nl] = 0;
        const char *user = name;                        /* (a DOMAIN\ in front is not hashed) */
        for (const char *s = name; *s; s++)
            if (*s == '\\') user = s + 1;
        u8 want[24];
        ap_nt_response(e->challenge, peer, user, EAP_PASSWORD, want);
        int known = 1;
        for (const char *a = user, *b = EAP_USER; *a || *b; a++, b++)
            if (*a != *b) { known = 0; break; }
        unsigned char diff = 0;
        for (int i = 0; i < 24; i++) diff |= (unsigned char)(want[i] ^ nt[i]);
        e->chap_ok = known && !diff;
        u8 r[128];
        unsigned int k;
        if (e->chap_ok) {
            char s[43];
            ap_auth_response(EAP_PASSWORD, nt, peer, e->challenge, user, s);
            static const char msg[] = " M=Welcome to NovaCorp";
            k = 5;
            wpa_copy(r + k, s, 42);
            k += 42;
            wpa_copy(r + k, msg, sizeof(msg) - 1);
            k += sizeof(msg) - 1;
            r[1] = 3;
            DbgPrint("wditest: RADIUS: MS-CHAPv2 response from %s: NT-Response good; success with the "
                     "authenticator response\n", name);
        } else {
            static const char msg[] = "E=691 R=0 C=00000000000000000000000000000000 V=3 M=Authentication failed";
            k = 5;
            wpa_copy(r + k, msg, sizeof(msg) - 1);
            k += sizeof(msg) - 1;
            r[1] = 4;
            DbgPrint("wditest: RADIUS: MS-CHAPv2 response from %s: %s; failure E=691\n", name,
                     known ? "NT-Response wrong (not the password)" : "no such user");
        }
        r[0] = 26;
        r[2] = e->chap_id;
        r[3] = (u8)((k - 1) >> 8);
        r[4] = (u8)(k - 1);
        e->state = EAPS_CHAP_RESULT;
        return inner_request(e, f, r, k);
    }
    if (e->state == EAPS_CHAP_RESULT) {
        if (n != 2 || m[0] != 26 || m[1] != (e->chap_ok ? 3 : 4))
            return eap_ap_fail(e, f, "the station did not acknowledge MS-CHAPv2's result");
        /* the Result TLV, with its whole EAP header (PEAPv0's EAP-TLV) */
        u8 t[11];
        t[0] = 1; t[1] = (u8)(e->id + 1); t[2] = 0; t[3] = 11; t[4] = 33;  /* (the outer request's identifier) */
        t[5] = 0x80; t[6] = 3; t[7] = 0; t[8] = 2; t[9] = 0; t[10] = (u8)(e->chap_ok ? 1 : 2);
        e->state = EAPS_TLV;
        return inner_request(e, f, t, sizeof(t));
    }
    if (e->state == EAPS_TLV) {
        if (n != 11 || m[0] != 2 || m[4] != 33 || m[6] != 3 || m[10] != (e->chap_ok ? 1 : 2))
            return eap_ap_fail(e, f, "the station's Result TLV is not the server's");
        if (!e->chap_ok) return eap_ap_fail(e, f, "MS-CHAPv2 failed");
        /* the MSK: the TLS PRF over the master secret with "client EAP encryption" and both randoms */
        if (!e->have_master || mbedtls_ssl_tls_prf(e->prf, e->master, 48, "client EAP encryption", e->randoms, 64,
                                                   e->msk, 64))
            return eap_ap_fail(e, f, "no master secret for the MSK");
        e->state = EAPS_DONE;
        DbgPrint("wditest: RADIUS: Result TLV success from the station; EAP-Success, the PMK from the tunnel\n");
        e->id++;
        return eap_frame(e, f, 3, 0, 0, 0);
    }
    return eap_ap_fail(e, f, "an inner response out of order");
}

/* One EAPOL frame from the station: the next request to send in @f (its
 * length; 0: none), EAP-Success or EAP-Failure once done (e->state) */
static unsigned int eap_ap_receive(EapAp *e, const u8 *p, unsigned int n, u8 *f)
{
    if (n < 4) return 0;
    if (p[1] == 1) {                                    /* EAPOL-Start: from the beginning */
        DbgPrint("wditest: RADIUS: EAPOL-Start: Request/Identity again\n");
        return eap_ap_start(e, f);
    }
    unsigned int el = n >= 8 ? (unsigned int)(p[6] << 8 | p[7]) : 0;
    if (p[1] != 0 || el < 5 || el + 4 > n || p[4] != 2) { DbgPrint("wditest: RADIUS: not an EAP response\n"); return 0; }
    if (p[5] != e->id) {
        DbgPrint("wditest: RADIUS: response %u to request %u: dropped\n", p[5], e->id);
        return 0;
    }
    const u8 *d = p + 9;
    unsigned int dn = el - 5;
    u8 type = p[8];
    if (e->state == EAPS_IDENTITY) {
        if (type != 1) return eap_ap_fail(e, f, "not an identity response");
        unsigned int k = dn < sizeof(e->identity) - 1 ? dn : sizeof(e->identity) - 1;
        wpa_copy(e->identity, d, k);
        e->identity[k] = 0;
        if (!eap_tls_init()) return eap_ap_fail(e, f, "the server's TLS did not start");
        e->method = 25;
        if (!eap_ap_session(e)) return eap_ap_fail(e, f, "no TLS session");
        e->state = EAPS_TLS;
        DbgPrint("wditest: RADIUS: identity %s; PEAP start\n", e->identity);
        u8 start = 0x20;                                /* (S, version 0) */
        return eap_request(e, f, 25, &start, 1);
    }
    if (type == 3 && e->state == EAPS_TLS && e->method == 25 && dn >= 1 && d[0] == 13) {
        /* a Nak asking for EAP-TLS: the station has a certificate */
        e->method = 13;
        if (!eap_ap_session(e)) return eap_ap_fail(e, f, "no TLS session");
        DbgPrint("wditest: RADIUS: the station asked for EAP-TLS (a Nak): EAP-TLS start\n");
        u8 start = 0x20;                                /* (S) */
        return eap_request(e, f, 13, &start, 1);
    }
    if (type == 3) return eap_ap_fail(e, f, "the station refused PEAP and EAP-TLS (a Nak)");
    if (type != e->method || !dn) return eap_ap_fail(e, f, e->method == 13 ? "not an EAP-TLS response" :
                                                                             "not a PEAP response");
    u8 flags = d[0];
    d++;
    dn--;
    if (e->method == 25 && (flags & 0x07)) return eap_ap_fail(e, f, "not PEAP version 0");
    if (flags & 0x80) { if (dn < 4) return eap_ap_fail(e, f, "a PEAP length cut short"); d += 4; dn -= 4; }
    if (!dn && e->out_len) return peap_request(e, f);   /* (an acknowledgment: the next fragment) */
    if (dn) {
        if (e->in_len + dn > sizeof(e->in)) return eap_ap_fail(e, f, "a TLS message from the station too long");
        wpa_copy(e->in + e->in_len, d, dn);
        e->in_len += dn;
    }
    if (flags & 0x40) return peap_request(e, f);        /* (more fragments to come: acknowledged) */
    if (e->state == EAPS_TLS) {
        int r = mbedtls_ssl_handshake(&e->ssl);
        if (r && r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            DbgPrint("wditest: RADIUS: TLS handshake failed (-0x%x)%s\n", (unsigned int)-r,
                     r == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE ? ": the station sent an alert (it did not trust the "
                                                                "certificate?)" :
                     r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ? ": the station's certificate is not from NovaCorp "
                                                                "Test Root" : "");
            if (e->method == 13 && e->out_len) {        /* (EAP-TLS: the alert to the station first, then Failure) */
                e->state = EAPS_ALERT;
                return peap_request(e, f);
            }
            return eap_ap_fail(e, f, "the TLS handshake failed");
        }
        if (!r && e->method == 13) {
            char cn[64];
            eap_ap_peer_cn(e, cn, sizeof(cn));
            DbgPrint("wditest: RADIUS: EAP-TLS handshake done (%s): the station's certificate %s is from NovaCorp "
                     "Test Root\n", mbedtls_ssl_get_ciphersuite(&e->ssl), cn);
            e->state = EAPS_TLS_DONE;
            return peap_request(e, f);                  /* (its ChangeCipherSpec and Finished) */
        }
        if (!r) {
            DbgPrint("wditest: RADIUS: TLS handshake done (%s); tunnel up\n", mbedtls_ssl_get_ciphersuite(&e->ssl));
            e->state = EAPS_INNER_IDENTITY;
            if (e->out_len) return peap_request(e, f);  /* (its Finished first; the inner identity after the ack) */
        } else {
            return peap_request(e, f);                  /* (its next flight, or an acknowledgment) */
        }
    }
    if (e->state == EAPS_ALERT) return eap_ap_fail(e, f, "the station's certificate was refused");
    if (e->state == EAPS_TLS_DONE) {                    /* (EAP-TLS: the station acknowledged the Finished) */
        if (!e->have_master || mbedtls_ssl_tls_prf(e->prf, e->master, 48, "client EAP encryption", e->randoms, 64,
                                                   e->msk, 64))
            return eap_ap_fail(e, f, "no master secret for the MSK");
        e->state = EAPS_DONE;
        DbgPrint("wditest: RADIUS: EAP-TLS done; EAP-Success, the PMK from the TLS keys\n");
        e->id++;
        return eap_frame(e, f, 3, 0, 0, 0);
    }
    if (e->state == EAPS_INNER_IDENTITY && !e->in_len && !dn) {
        u8 ident = 1;                                   /* (an inner Request/Identity: just its type) */
        return inner_request(e, f, &ident, 1);
    }
    u8 m[256];
    int k = mbedtls_ssl_read(&e->ssl, m, sizeof(m));
    if (k <= 0) return eap_ap_fail(e, f, "nothing in the tunnel");
    return eap_ap_inner(e, f, m, (unsigned int)k);
}
