/* The kernel's WPA2-Enterprise EAP peer (kernel/nt/eap.c: PEAPv0 with
 * EAP-MSCHAPv2) against wditest.sys's authentication server
 * (tools/drivertest/eapap.h) on the host, without a VM: MD4 (RFC 1320) and
 * MS-CHAPv2 (RFC 2759 9.2) known answers on both sides, then whole sign-ins
 * frame by frame: the right password (both ends must derive the same
 * MSK), a wrong one, an unknown user and a server certificate from an
 * untrusted root.  The station's TLS is Mbed TLS, as net/tls.c's EAP
 * sessions set it up (below: the same calls over memory, trusting the root
 * named on the command line).  It prints values for
 * tests/tools/test_eap_peap.py to check. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <stdarg.h>
#include "../../kernel/nt/eap.c"

/* the kernel's pieces eap.c uses, on the host */
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }
void kprintf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/oid.h"

/* ---- net/tls.c's EAP sessions, on the host ---- */
static mbedtls_x509_crt         g_roots;
static mbedtls_ctr_drbg_context g_cli_drbg;
static mbedtls_ssl_config       g_cli;

struct TlsEap {
    mbedtls_ssl_context ssl;
    UINT8 in[32768], out[32768];
    UINT32 in_len, in_off, out_len, verify;
};

static int fixed_entropy(void *c, unsigned char *o, size_t n) { (void)c; static UINT8 k; while (n--) *o++ = ++k * 37; return 0; }

static int cli_send(void *ctx, const unsigned char *b, size_t n)
{
    TlsEap *e = ctx;
    memcpy(e->out + e->out_len, b, n);
    e->out_len += n;
    return (int)n;
}

static int cli_recv(void *ctx, unsigned char *b, size_t n)
{
    TlsEap *e = ctx;
    UINT32 left = e->in_len - e->in_off;
    if (!left) return MBEDTLS_ERR_SSL_WANT_READ;
    if (n > left) n = left;
    memcpy(b, e->in + e->in_off, n);
    e->in_off += n;
    if (e->in_off == e->in_len) e->in_off = e->in_len = 0;
    return (int)n;
}

TlsEap *TlsEapNew(void)
{
    TlsEap *e = calloc(1, sizeof(*e));
    mbedtls_ssl_init(&e->ssl);
    if (mbedtls_ssl_setup(&e->ssl, &g_cli) || mbedtls_ssl_set_hostname(&e->ssl, NULL)) return NULL;
    mbedtls_ssl_set_bio(&e->ssl, e, cli_send, cli_recv, NULL);
    return e;
}
void TlsEapInput(TlsEap *e, const void *d, UINT32 n) { memcpy(e->in + e->in_len, d, n); e->in_len += n; }
int TlsEapHandshake(TlsEap *e)
{
    int r = mbedtls_ssl_handshake(&e->ssl);
    if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) e->verify = mbedtls_ssl_get_verify_result(&e->ssl);
    return !r ? 1 : r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ? 0 : r;
}
UINT32 TlsEapPending(TlsEap *e) { return e->out_len; }
UINT32 TlsEapOutput(TlsEap *e, UINT8 *o, UINT32 cap)
{
    UINT32 n = e->out_len < cap ? e->out_len : cap;
    memcpy(o, e->out, n);
    memmove(e->out, e->out + n, e->out_len - n);
    e->out_len -= n;
    return n;
}
int TlsEapWrite(TlsEap *e, const void *d, UINT32 n) { int r = mbedtls_ssl_write(&e->ssl, d, n); return r < 0 && r != MBEDTLS_ERR_SSL_WANT_READ ? r : r < 0 ? 0 : r; }
int TlsEapRead(TlsEap *e, void *b, UINT32 cap) { int r = mbedtls_ssl_read(&e->ssl, b, cap); return r == MBEDTLS_ERR_SSL_WANT_READ ? 0 : r; }
bool TlsEapUntrusted(TlsEap *e) { return e->verify != 0; }
bool TlsEapExportKey(TlsEap *e, const char *label, UINT8 *o, UINT32 n)
{
    return !mbedtls_ssl_export_keying_material(&e->ssl, o, n, label, strlen(label), NULL, 0, 0);
}
void TlsEapPeerName(TlsEap *e, char *b, int cap)
{
    const mbedtls_x509_crt *c = mbedtls_ssl_get_peer_cert(&e->ssl);
    b[0] = 0;
    for (const mbedtls_x509_name *n = c ? &c->subject : NULL; n; n = n->next)
        if (!MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid)) snprintf(b, cap, "%.*s", (int)n->val.len, n->val.p);
}
void TlsEapRandom(UINT8 *o, UINT32 n) { while (n--) *o++ = (UINT8)(n * 29 + 3); }
void TlsEapFree(TlsEap *e) { mbedtls_ssl_free(&e->ssl); free(e); }

/* ---- the server ---- */
#define IMPORT
#define DbgPrint printf
long long KeQueryPerformanceCounter(long long *f) { (void)f; static long long t; return t += 977; }
#include "../../tools/drivertest/wpaap.h"
#include "../../tools/drivertest/eapap.h"

static void hex(const char *name, const UINT8 *p, unsigned int n)
{
    printf("%s ", name);
    for (unsigned int i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

static void unhex(const char *s, UINT8 *out)
{
    for (int i = 0; s[2 * i]; i++) sscanf(s + 2 * i, "%2hhx", &out[i]);
}

/* One sign-in, frame by frame: what the peer ended with */
static int sign_in(const char *user, const char *password, UINT8 *msk_ap, UINT8 *msk_sta, int *frames)
{
    static EapAp ap;
    static EapPeer sta;
    static UINT8 f[2048], r[2048];
    assert(EapStart(&sta, user, password));
    unsigned int n = eap_ap_start(&ap, f);
    int out = EAP_DROP;
    *frames = 0;
    for (int k = 0; k < 64 && n; k++) {
        UINT32 rl = 0;
        (*frames)++;
        out = EapReceive(&sta, f, n, r, &rl);
        if (out == EAP_DROP) printf("station dropped: %s\n", sta.why);
        if (out == EAP_SUCCESS || out == EAP_FAILURE || !rl) break;
        n = eap_ap_receive(&ap, r, rl, f);
    }
    printf("sign-in %s/%s: station %s (result %d: %s), server %s%s%s\n", user, password,
           out == EAP_SUCCESS ? "success" : out == EAP_FAILURE ? "failure" : "stuck", sta.result,
           sta.why ? sta.why : "-", ap.state == EAPS_DONE ? "done" : ap.state == EAPS_FAILED ? "failed" : "waiting",
           ap.why ? ": " : "", ap.why ? ap.why : "");
    if (out == EAP_SUCCESS) {
        memcpy(msk_sta, sta.msk, 64);
        memcpy(msk_ap, ap.msk, 64);
        printf("server name %s\n", sta.server);
    }
    int res = sta.result;
    EapClear(&sta);
    eap_ap_clear(&ap);
    return res;
}

int main(int argc, char **argv)
{
    /* MD4 (RFC 1320 A.5), the kernel's and the server's */
    UINT8 d[16], want[64];
    static const char *md4v[][2] = {
        { "", "31d6cfe0d16ae931b73c59d7e0c089c0" },
        { "abc", "a448017aaf21d8525fc10ae87aa6729d" },
        { "abcdefghijklmnopqrstuvwxyz", "d79e1c308aa5bbcdeea8ed63df412da9" },
        { "12345678901234567890123456789012345678901234567890123456789012345678901234567890",
          "e33b4ddc9c38f2199c3e7b164fcc0536" },
    };
    for (unsigned i = 0; i < sizeof(md4v) / sizeof(md4v[0]); i++) {
        unhex(md4v[i][1], want);
        Md4((const UINT8 *)md4v[i][0], strlen(md4v[i][0]), d);
        assert(!memcmp(d, want, 16));
        ap_md4((const u8 *)md4v[i][0], strlen(md4v[i][0]), d);
        assert(!memcmp(d, want, 16));
    }
    printf("md4 ok\n");

    /* MS-CHAPv2 (RFC 2759 9.2): both sides' NT-Response and authenticator response */
    UINT8 auth[16], peer[16], nt[24], nt2[24];
    unhex("5b5d7c7d7b3f2f3e3c2c602132262628", auth);
    unhex("21402324255e262a28295f2b3a337c7e", peer);
    MschapNtResponse(auth, peer, "User", "clientPass", nt);
    ap_nt_response(auth, peer, "User", "clientPass", nt2);
    assert(!memcmp(nt, nt2, 24));
    hex("nt_response", nt, 24);
    char s1[43], s2[43];
    MschapAuthenticatorResponse("clientPass", nt, peer, auth, "User", s1);
    ap_auth_response("clientPass", nt, peer, auth, "User", s2);
    assert(!strcmp(s1, s2));
    printf("authenticator_response %s\n", s1);

    /* the station's TLS: trusting the root in argv[1] (none: nothing trusted) */
    mbedtls_x509_crt_init(&g_roots);
    if (argc > 1) {
        FILE *fp = fopen(argv[1], "rb");
        static UINT8 der[4096];
        size_t n = fp ? fread(der, 1, sizeof(der), fp) : 0;
        if (fp) fclose(fp);
        assert(n && !mbedtls_x509_crt_parse_der(&g_roots, der, n));
    }
    mbedtls_ctr_drbg_init(&g_cli_drbg);
    assert(!mbedtls_ctr_drbg_seed(&g_cli_drbg, fixed_entropy, NULL, NULL, 0));
    mbedtls_ssl_config_init(&g_cli);
    assert(!mbedtls_ssl_config_defaults(&g_cli, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT));
    mbedtls_ssl_conf_authmode(&g_cli, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&g_cli, &g_roots, NULL);
    mbedtls_ssl_conf_rng(&g_cli, mbedtls_ctr_drbg_random, &g_cli_drbg);
    mbedtls_ssl_conf_min_tls_version(&g_cli, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&g_cli, MBEDTLS_SSL_VERSION_TLS1_2);

    UINT8 ma[64], ms[64];
    int frames;
    if (argc > 1) {
        int r = sign_in("alice", "nova-peap-password", ma, ms, &frames);
        assert(r == EAP_RESULT_OK && !memcmp(ma, ms, 64));
        hex("msk", ms, 64);
        printf("frames %d\n", frames);
        printf("peap ok\n");
        r = sign_in("CORP\\alice", "nova-peap-password", ma, ms, &frames);
        assert(r == EAP_RESULT_OK && !memcmp(ma, ms, 64));
        printf("domain ok\n");
        r = sign_in("alice", "not-the-password", ma, ms, &frames);
        assert(r == EAP_RESULT_BAD_PASSWORD);
        printf("wrong password refused\n");
        r = sign_in("mallory", "nova-peap-password", ma, ms, &frames);
        assert(r == EAP_RESULT_BAD_PASSWORD);
        printf("unknown user refused\n");
    } else {
        int r = sign_in("alice", "nova-peap-password", ma, ms, &frames);
        assert(r == EAP_RESULT_UNTRUSTED);
        printf("untrusted server refused\n");
    }
    return 0;
}
