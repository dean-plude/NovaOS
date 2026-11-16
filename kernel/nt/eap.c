/*
 * eap.c — NovaOS's EAP peer for WPA2-Enterprise (eap.h): EAPOL (802.1X-2020
 * 11), EAP (RFC 3748), PEAP version 0 ([MS-PEAP]) with EAP-MSCHAPv2
 * inside (RFC 2759, [MS-CHAP]) and the Result TLV ([MS-PEAP] 2.2.8.1)
 *
 * The exchange, the access point (relaying its server) first:
 *
 *   Request/Identity               -> Response/Identity (the user name)
 *   Request/PEAP, Start            -> Response/PEAP: ClientHello
 *   Request/PEAP: ServerHello, Certificate, ServerKeyExchange, Done, in
 *     fragments (each one acknowledged with an empty Response/PEAP)
 *                                  -> the certificate checked against the
 *                                     trusted roots; ClientKeyExchange,
 *                                     ChangeCipherSpec, Finished
 *   Request/PEAP: ChangeCipherSpec, Finished -> an empty Response/PEAP
 *   then inside the tunnel (TLS application data), PEAPv0's way: an inner
 *   EAP request without its code, identifier and length (only an EAP-TLV
 *   has its whole header):
 *   Identity                       -> the user name
 *   MSCHAPv2 Challenge             -> MSCHAPv2 Response: the peer's
 *                                     challenge and the NT-Response
 *   MSCHAPv2 Success ("S=...")     -> checked: the server knows the
 *                                     password too; MSCHAPv2 Success
 *     (or MSCHAPv2 Failure "E=691" -> MSCHAPv2 Failure: a wrong password)
 *   EAP-TLV Result (success)       -> Result (success)
 *   Success (outside the tunnel)   -> the MSK from the tunnel's keys
 *
 * Written here rather than vendored: hostapd's wpa_supplicant (BSD) has
 * these, but as a user-mode program with its own event loop, TLS layer and
 * configuration, far more than this one method needs.  The cryptography
 * is Mbed TLS's (SHA-1, DES and TLS through net/tls.c) except MD4, which
 * Mbed TLS 3 dropped: it is RFC 1320's, below.
 */
#include "eap.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../net/tls.h"
#include "../ke/printf.h"
#include "mbedtls/sha1.h"
#include "mbedtls/des.h"

#define EAPOL_VERSION   2
#define EAPOL_EAP       0
#define EAPOL_START     1

#define EAP_REQUEST     1
#define EAP_RESPONSE    2
#define EAP_SUCCESS_CODE 3
#define EAP_FAILURE_CODE 4

#define TYPE_IDENTITY   1
#define TYPE_NOTIFICATION 2
#define TYPE_NAK        3
#define TYPE_PEAP       25
#define TYPE_MSCHAPV2   26
#define TYPE_TLV        33

#define PEAP_L          0x80        /* (flags: the TLS message's length follows) */
#define PEAP_M          0x40        /* (more fragments follow) */
#define PEAP_S          0x20        /* (start) */
#define PEAP_VERSION    0x07

#define CHAP_CHALLENGE  1
#define CHAP_RESPONSE   2
#define CHAP_SUCCESS    3
#define CHAP_FAILURE    4

#define TLV_RESULT      3           /* (with the mandatory bit: 0x8003) */
#define TLS_MAX_MESSAGE 32768       /* (a fragmented TLS message the server sends: its certificate chain) */

static UINT16 be16(const UINT8 *p) { return (UINT16)(p[0] << 8 | p[1]); }
static void   put16(UINT8 *p, UINT32 v) { p[0] = (UINT8)(v >> 8); p[1] = (UINT8)v; }
static void   put32(UINT8 *p, UINT32 v) { put16(p, v >> 16); put16(p + 2, v); }

/* ---- MD4 (RFC 1320) ---- */
static UINT32 md4_rol(UINT32 x, int n) { return x << n | x >> (32 - n); }

static void md4_block(UINT32 h[4], const UINT8 *p)
{
    UINT32 x[16], a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 16; i++) x[i] = (UINT32)p[4 * i] | (UINT32)p[4 * i + 1] << 8 | (UINT32)p[4 * i + 2] << 16 |
                                        (UINT32)p[4 * i + 3] << 24;
#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
    static const int r1[4] = { 3, 7, 11, 19 }, r2[4] = { 3, 5, 9, 13 }, r3[4] = { 3, 9, 11, 15 };
    static const int o2[16] = { 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15 };
    static const int o3[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
    for (int i = 0; i < 16; i++) {
        UINT32 t = md4_rol(a + F(b, c, d) + x[i], r1[i & 3]);
        a = d; d = c; c = b; b = t;
    }
    for (int i = 0; i < 16; i++) {
        UINT32 t = md4_rol(a + G(b, c, d) + x[o2[i]] + 0x5A827999u, r2[i & 3]);
        a = d; d = c; c = b; b = t;
    }
    for (int i = 0; i < 16; i++) {
        UINT32 t = md4_rol(a + H(b, c, d) + x[o3[i]] + 0x6ED9EBA1u, r3[i & 3]);
        a = d; d = c; c = b; b = t;
    }
#undef F
#undef G
#undef H
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    memset(x, 0, sizeof(x));
}

void Md4(const UINT8 *data, UINT32 len, UINT8 out[16])
{
    UINT32 h[4] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u };
    UINT32 k = 0;
    for (; k + 64 <= len; k += 64) md4_block(h, data + k);
    UINT8 last[128];
    UINT32 rest = len - k, n = rest < 56 ? 64 : 128;
    memset(last, 0, sizeof(last));
    memcpy(last, data + k, rest);
    last[rest] = 0x80;
    UINT64 bits = (UINT64)len * 8;
    for (int i = 0; i < 8; i++) last[n - 8 + i] = (UINT8)(bits >> (8 * i));
    md4_block(h, last);
    if (n == 128) md4_block(h, last + 64);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (UINT8)(h[i] >> (8 * j));
    memset(last, 0, sizeof(last));
}

/* ---- MS-CHAPv2 (RFC 2759 8) ---- */

/* NtPasswordHash: MD4 of the password as UTF-16LE (from UTF-8; the BMP) */
static void nt_password_hash(const char *password, UINT8 hash[16])
{
    UINT8 u[2 * EAP_NAME_MAX];
    UINT32 n = 0;
    const UINT8 *s = (const UINT8 *)password;
    while (*s && n + 2 <= sizeof(u)) {
        UINT32 c = *s++;
        if (c >= 0xE0 && s[0] && s[1]) { c = (c & 0x0F) << 12 | (s[0] & 0x3Fu) << 6 | (s[1] & 0x3Fu); s += 2; }
        else if (c >= 0xC0 && s[0]) { c = (c & 0x1F) << 6 | (s[0] & 0x3Fu); s++; }
        u[n++] = (UINT8)c;
        u[n++] = (UINT8)(c >> 8);
    }
    Md4(u, n, hash);
    memset(u, 0, sizeof(u));
}

/* ChallengeHash: the first 8 bytes of SHA-1(peer challenge, authenticator challenge, user name) */
static void challenge_hash(const UINT8 peer[16], const UINT8 auth[16], const char *user, UINT8 out[8])
{
    mbedtls_sha1_context c;
    UINT8 d[20];
    mbedtls_sha1_init(&c);
    mbedtls_sha1_starts(&c);
    mbedtls_sha1_update(&c, peer, 16);
    mbedtls_sha1_update(&c, auth, 16);
    mbedtls_sha1_update(&c, (const UINT8 *)user, strlen(user));
    mbedtls_sha1_finish(&c, d);
    mbedtls_sha1_free(&c);
    memcpy(out, d, 8);
}

/* DesEncrypt: @clear under the 56-bit key @k7 (seven bytes, spread over eight with parity bits) */
static void des_encrypt(const UINT8 clear[8], const UINT8 k7[7], UINT8 out[8])
{
    UINT8 k[8] = { k7[0], (UINT8)(k7[0] << 7 | k7[1] >> 1), (UINT8)(k7[1] << 6 | k7[2] >> 2),
                   (UINT8)(k7[2] << 5 | k7[3] >> 3), (UINT8)(k7[3] << 4 | k7[4] >> 4),
                   (UINT8)(k7[4] << 3 | k7[5] >> 5), (UINT8)(k7[5] << 2 | k7[6] >> 6), (UINT8)(k7[6] << 1) };
    mbedtls_des_key_set_parity(k);
    mbedtls_des_context c;
    mbedtls_des_init(&c);
    mbedtls_des_setkey_enc(&c, k);
    mbedtls_des_crypt_ecb(&c, clear, out);
    mbedtls_des_free(&c);
    memset(k, 0, sizeof(k));
}

void MschapNtResponse(const UINT8 auth_challenge[16], const UINT8 peer_challenge[16], const char *user,
                      const char *password, UINT8 response[24])
{
    UINT8 ch[8], z[21];
    challenge_hash(peer_challenge, auth_challenge, user, ch);
    memset(z, 0, sizeof(z));
    nt_password_hash(password, z);
    for (int i = 0; i < 3; i++) des_encrypt(ch, z + 7 * i, response + 8 * i);
    memset(z, 0, sizeof(z));
}

void MschapAuthenticatorResponse(const char *password, const UINT8 nt_response[24], const UINT8 peer_challenge[16],
                                 const UINT8 auth_challenge[16], const char *user, char out[43])
{
    static const char magic1[] = "Magic server to client signing constant";
    static const char magic2[] = "Pad to make it do more than one iteration";
    UINT8 h[16], hh[16], d[20], ch[8];
    nt_password_hash(password, h);
    Md4(h, 16, hh);
    mbedtls_sha1_context c;
    mbedtls_sha1_init(&c);
    mbedtls_sha1_starts(&c);
    mbedtls_sha1_update(&c, hh, 16);
    mbedtls_sha1_update(&c, nt_response, 24);
    mbedtls_sha1_update(&c, (const UINT8 *)magic1, sizeof(magic1) - 1);
    mbedtls_sha1_finish(&c, d);
    challenge_hash(peer_challenge, auth_challenge, user, ch);
    mbedtls_sha1_starts(&c);
    mbedtls_sha1_update(&c, d, 20);
    mbedtls_sha1_update(&c, ch, 8);
    mbedtls_sha1_update(&c, (const UINT8 *)magic2, sizeof(magic2) - 1);
    mbedtls_sha1_finish(&c, d);
    mbedtls_sha1_free(&c);
    static const char hex[] = "0123456789ABCDEF";
    out[0] = 'S';
    out[1] = '=';
    for (int i = 0; i < 20; i++) {
        out[2 + 2 * i] = hex[d[i] >> 4];
        out[3 + 2 * i] = hex[d[i] & 15];
    }
    out[42] = 0;
    memset(h, 0, sizeof(h));
    memset(hh, 0, sizeof(hh));
}

/* The user name MS-CHAPv2 hashes: without a "DOMAIN\" in front */
static const char *chap_user(const char *identity)
{
    const char *b = identity;
    for (const char *s = identity; *s; s++)
        if (*s == '\\') b = s + 1;
    return b;
}

/* ---- the peer ---- */

bool EapStart(EapPeer *p, const char *identity, const char *password)
{
    EapClear(p);
    if (!identity || !password || !identity[0] || !password[0] || strlen(identity) >= EAP_NAME_MAX ||
        strlen(password) >= EAP_NAME_MAX)
        return false;
    strcpy(p->identity, identity);
    strcpy(p->password, password);
    return true;
}

void EapClear(EapPeer *p)
{
    if (p->tls) TlsEapFree(p->tls);
    if (p->in) kfree(p->in);
    if (p->out) kfree(p->out);
    memset(p, 0, sizeof(*p));
}

UINT32 EapStartFrame(UINT8 *out)
{
    out[0] = EAPOL_VERSION;
    out[1] = EAPOL_START;
    put16(out + 2, 0);
    return 4;
}

/* An EAP response (@type, @data) as an EAPOL frame in @reply */
static UINT32 response(EapPeer *p, UINT8 type, const UINT8 *data, UINT32 n, UINT8 *reply)
{
    UINT32 len = 5 + n;
    reply[0] = EAPOL_VERSION;
    reply[1] = EAPOL_EAP;
    put16(reply + 2, len);
    reply[4] = EAP_RESPONSE;
    reply[5] = p->id;
    put16(reply + 6, len);
    reply[8] = type;
    if (n) memcpy(reply + 9, data, n);
    return 4 + len;
}

static int eap_fail(EapPeer *p, int result, const char *why)
{
    p->result = result;
    p->why = why;
    return EAP_FAILURE;
}

/* The next PEAP fragment of what TLS has to send (or an acknowledgment, if nothing) */
static int peap_send(EapPeer *p, UINT8 *reply, UINT32 *reply_len)
{
    UINT8 frag[6 + EAP_FRAGMENT];
    UINT32 left = p->out_len - p->out_off, k = left > EAP_FRAGMENT ? EAP_FRAGMENT : left, o = 0;
    frag[o++] = 0;                                  /* (flags, PEAP version 0) */
    if (left && p->out_off == 0 && left > EAP_FRAGMENT) {
        frag[0] |= PEAP_L;
        put32(frag + o, p->out_len);
        o += 4;
    }
    if (left > k) frag[0] |= PEAP_M;
    if (k) memcpy(frag + o, p->out + p->out_off, k);
    p->out_off += k;
    if (p->out_off == p->out_len) p->out_len = p->out_off = 0;
    *reply_len = response(p, TYPE_PEAP, frag, o + k, reply);
    return EAP_REPLY;
}

/* Take what TLS has written into the send buffer */
static void tls_collect(EapPeer *p)
{
    UINT32 n = TlsEapPending(p->tls);
    if (!n) return;
    if (!p->out) p->out = kmalloc(TLS_MAX_MESSAGE);
    if (!p->out) return;
    if (p->out_off) {                               /* (whatever was sent already: gone) */
        memmove(p->out, p->out + p->out_off, p->out_len - p->out_off);
        p->out_len -= p->out_off;
        p->out_off = 0;
    }
    if (n > TLS_MAX_MESSAGE - p->out_len) n = TLS_MAX_MESSAGE - p->out_len;
    p->out_len += TlsEapOutput(p->tls, p->out + p->out_len, n);
}

/* One inner EAP response (PEAPv0: no header, unless @full) through the tunnel */
static int inner_reply(EapPeer *p, UINT8 type, const UINT8 *data, UINT32 n, bool full, UINT8 *reply,
                       UINT32 *reply_len)
{
    UINT8 m[128];
    UINT32 k = 0;
    if (full) {
        m[k++] = EAP_RESPONSE;
        m[k++] = p->id;
        put16(m + k, 5 + n);
        k += 2;
    }
    m[k++] = type;
    if (n > sizeof(m) - k) return eap_fail(p, EAP_RESULT_FAILED, "an inner response too long");
    memcpy(m + k, data, n);
    k += n;
    int r = TlsEapWrite(p->tls, m, k);
    memset(m, 0, sizeof(m));
    if (r < 0) return eap_fail(p, EAP_RESULT_FAILED, "the tunnel refused the inner response");
    tls_collect(p);
    return peap_send(p, reply, reply_len);
}

/* An inner EAP request, from the tunnel */
static int inner(EapPeer *p, const UINT8 *m, UINT32 n, UINT8 *reply, UINT32 *reply_len)
{
    bool full = n >= 5 && (m[0] == EAP_REQUEST || m[0] == EAP_SUCCESS_CODE || m[0] == EAP_FAILURE_CODE) &&
                be16(m + 2) == n;
    if (full && m[0] != EAP_REQUEST) {              /* (an inner Success or Failure: Microsoft's extensions) */
        return m[0] == EAP_FAILURE_CODE ? eap_fail(p, p->chap_sent && !p->chap_ok ? EAP_RESULT_BAD_PASSWORD :
                                                   EAP_RESULT_FAILED, "the server ended the inner method")
                                        : inner_reply(p, TYPE_TLV, NULL, 0, true, reply, reply_len);
    }
    const UINT8 *d = full ? m + 4 : m;
    UINT32 dn = full ? n - 4 : n;
    if (!dn) { p->why = "an empty inner request"; return EAP_DROP; }
    UINT8 type = d[0];
    d++;
    dn--;
    if (type == TYPE_IDENTITY)
        return inner_reply(p, TYPE_IDENTITY, (const UINT8 *)p->identity, strlen(p->identity), full, reply, reply_len);
    if (type == TYPE_TLV) {                         /* the Result TLV: answered with the same result */
        UINT32 result = 0;
        for (UINT32 k = 0; k + 4 <= dn;) {
            UINT32 t = be16(d + k) & 0x3FFF, l = be16(d + k + 2);
            if (k + 4 + l > dn) break;
            if (t == TLV_RESULT && l == 2) result = be16(d + k + 4);
            k += 4 + l;
        }
        if (!result) { p->why = "an EAP-TLV request without a Result TLV"; return EAP_DROP; }
        bool ok = result == 1 && p->chap_ok;
        UINT8 tlv[6] = { 0x80, TLV_RESULT, 0, 2, 0, (UINT8)(ok ? 1 : 2) };
        p->result_ok = ok;
        return inner_reply(p, TYPE_TLV, tlv, sizeof(tlv), true, reply, reply_len);
    }
    if (type != TYPE_MSCHAPV2) {                    /* (a method NovaOS does not have: ask for MS-CHAPv2) */
        UINT8 want = TYPE_MSCHAPV2;
        return inner_reply(p, TYPE_NAK, &want, 1, full, reply, reply_len);
    }
    if (!dn) { p->why = "an empty MS-CHAPv2 request"; return EAP_DROP; }
    UINT8 op = d[0];
    if (op == CHAP_CHALLENGE) {
        if (dn < 5 + 16 || d[4] != 16) { p->why = "a malformed MS-CHAPv2 challenge"; return EAP_DROP; }
        memcpy(p->auth_challenge, d + 5, 16);
        TlsEapRandom(p->peer_challenge, 16);
        const char *user = chap_user(p->identity);
        MschapNtResponse(p->auth_challenge, p->peer_challenge, user, p->password, p->nt_response);
        UINT8 r[4 + 1 + 49 + EAP_NAME_MAX];
        UINT32 ul = strlen(p->identity), k = 0;
        r[k++] = CHAP_RESPONSE;
        r[k++] = d[1];                              /* (its MS-CHAPv2 identifier) */
        put16(r + k, 4 + 1 + 49 + ul);
        k += 2;
        r[k++] = 49;
        memcpy(r + k, p->peer_challenge, 16);
        k += 16;
        memset(r + k, 0, 8);
        k += 8;
        memcpy(r + k, p->nt_response, 24);
        k += 24;
        r[k++] = 0;                                 /* (flags) */
        memcpy(r + k, p->identity, ul);
        k += ul;
        p->chap_sent = true;
        return inner_reply(p, TYPE_MSCHAPV2, r, k, full, reply, reply_len);
    }
    if (op == CHAP_SUCCESS) {                       /* "S=<40 hex digits> M=...": the server's proof */
        char want[43];
        MschapAuthenticatorResponse(p->password, p->nt_response, p->peer_challenge, p->auth_challenge,
                                    chap_user(p->identity), want);
        bool good = p->chap_sent && dn >= 4 + 42 && !memcmp(d + 4, want, 42);
        if (!good) {                                /* (the server does not know the password: not ours) */
            p->why = "the server's MS-CHAPv2 proof is wrong";
            return eap_fail(p, EAP_RESULT_FAILED, "the server's MS-CHAPv2 proof is wrong: not the user's server");
        }
        p->chap_ok = true;
        UINT8 op_success = CHAP_SUCCESS;
        return inner_reply(p, TYPE_MSCHAPV2, &op_success, 1, full, reply, reply_len);
    }
    if (op == CHAP_FAILURE) {                       /* "E=691 R=0 ...": the password (or the user) is wrong */
        UINT8 op_failure = CHAP_FAILURE;
        kprintf("[EAP] the server refused the user name or password (MS-CHAPv2 failure)\n");
        return inner_reply(p, TYPE_MSCHAPV2, &op_failure, 1, full, reply, reply_len);
    }
    p->why = "an MS-CHAPv2 request NovaOS does not know";
    return EAP_DROP;
}

/* A PEAP request: a fragment of TLS, or the start */
static int peap(EapPeer *p, const UINT8 *d, UINT32 n, UINT8 *reply, UINT32 *reply_len)
{
    if (!n) { p->why = "a PEAP request without flags"; return EAP_DROP; }
    UINT8 flags = d[0];
    d++;
    n--;
    if (flags & PEAP_S) {                           /* (a new tunnel) */
        if (p->tls) TlsEapFree(p->tls);
        p->tls = TlsEapNew();
        if (!p->tls) return eap_fail(p, EAP_RESULT_FAILED, "no TLS session for PEAP (TLS is not available)");
        p->tunnel = p->chap_sent = p->chap_ok = p->result_ok = false;
        p->in_len = p->out_len = p->out_off = 0;
        int h = TlsEapHandshake(p->tls);            /* (the ClientHello) */
        if (h < 0) return eap_fail(p, EAP_RESULT_FAILED, "TLS could not start");
        tls_collect(p);
        return peap_send(p, reply, reply_len);
    }
    if (!p->tls) { p->why = "PEAP before its start"; return EAP_DROP; }
    if (flags & PEAP_L) {
        if (n < 4) { p->why = "a PEAP length cut short"; return EAP_DROP; }
        UINT32 total = (UINT32)be16(d) << 16 | be16(d + 2);
        if (total > TLS_MAX_MESSAGE) return eap_fail(p, EAP_RESULT_FAILED, "a TLS message from the server too long");
        if (!p->in_len) p->in_total = total;
        d += 4;
        n -= 4;
    }
    if (!n && p->out_len) return peap_send(p, reply, reply_len);  /* (an acknowledgment: the next fragment) */
    if (n) {
        if (!p->in) p->in = kmalloc(TLS_MAX_MESSAGE);
        if (!p->in || p->in_len + n > TLS_MAX_MESSAGE)
            return eap_fail(p, EAP_RESULT_FAILED, "a TLS message from the server too long");
        memcpy(p->in + p->in_len, d, n);
        p->in_len += n;
    }
    if (flags & PEAP_M) return peap_send(p, reply, reply_len);    /* (acknowledged: more to come) */
    if (p->in_len) TlsEapInput(p->tls, p->in, p->in_len);
    p->in_len = 0;
    if (!p->tunnel) {
        int h = TlsEapHandshake(p->tls);
        if (h < 0) {
            if (TlsEapUntrusted(p->tls))
                return eap_fail(p, EAP_RESULT_UNTRUSTED, "the server's certificate is not from a trusted root");
            return eap_fail(p, EAP_RESULT_FAILED, "the TLS handshake failed");
        }
        if (h == 1) {
            p->tunnel = true;
            TlsEapPeerName(p->tls, p->server, sizeof(p->server));
        }
        tls_collect(p);
        if (!p->tunnel || p->out_len) return peap_send(p, reply, reply_len);
    }
    /* inside the tunnel: the inner request (one per PEAP message) */
    UINT8 m[512];
    int k = TlsEapRead(p->tls, m, sizeof(m));
    if (k <= 0) return peap_send(p, reply, reply_len);            /* (nothing inside yet: acknowledged) */
    int r = inner(p, m, (UINT32)k, reply, reply_len);
    memset(m, 0, sizeof(m));
    return r;
}

int EapReceive(EapPeer *p, const UINT8 *frame, UINT32 len, UINT8 *reply, UINT32 *reply_len)
{
    *reply_len = 0;
    p->why = NULL;
    if (len < 4 || frame[1] != EAPOL_EAP) { p->why = "not an EAP packet"; return EAP_DROP; }
    UINT32 bl = be16(frame + 2);
    if (bl + 4 > len || bl < 4) { p->why = "an EAPOL frame cut short"; return EAP_DROP; }
    const UINT8 *e = frame + 4;
    UINT32 el = be16(e + 2);
    if (el < 4 || el > bl) { p->why = "an EAP packet with a bad length"; return EAP_DROP; }
    if (p->result != EAP_RESULT_NONE && p->result != EAP_RESULT_OK && e[0] != EAP_FAILURE_CODE) {
        p->why = "the sign-in already failed";
        return EAP_DROP;
    }
    if (e[0] == EAP_SUCCESS_CODE) {
        if (!p->tunnel || !p->chap_ok || !p->result_ok) {
            /* (a Success the method never earned: no keys, so nothing to trust) */
            return eap_fail(p, EAP_RESULT_FAILED, "an EAP-Success before the inner method succeeded");
        }
        if (!TlsEapExportKey(p->tls, "client EAP encryption", p->msk, EAP_MSK_LEN))
            return eap_fail(p, EAP_RESULT_FAILED, "no keys from the tunnel");
        p->result = EAP_RESULT_OK;
        memset(p->password, 0, sizeof(p->password));
        return EAP_SUCCESS;
    }
    if (e[0] == EAP_FAILURE_CODE) {
        if (p->result == EAP_RESULT_NONE)
            eap_fail(p, p->chap_sent && !p->chap_ok ? EAP_RESULT_BAD_PASSWORD : EAP_RESULT_FAILED,
                 "the server sent EAP-Failure");
        return EAP_FAILURE;
    }
    if (e[0] != EAP_REQUEST || el < 5) { p->why = "not an EAP request"; return EAP_DROP; }
    p->started = true;
    p->id = e[1];
    UINT8 type = e[4];
    const UINT8 *d = e + 5;
    UINT32 n = el - 5;
    switch (type) {
    case TYPE_IDENTITY:
        *reply_len = response(p, TYPE_IDENTITY, (const UINT8 *)p->identity, strlen(p->identity), reply);
        return EAP_REPLY;
    case TYPE_NOTIFICATION:
        *reply_len = response(p, TYPE_NOTIFICATION, NULL, 0, reply);
        return EAP_REPLY;
    case TYPE_PEAP:
        return peap(p, d, n, reply, reply_len);
    default: {                                      /* (EAP-TLS, TTLS, ...: not yet; PEAP instead) */
        UINT8 want = TYPE_PEAP;
        *reply_len = response(p, TYPE_NAK, &want, 1, reply);
        return EAP_REPLY;
    }
    }
}
