/*
 * tls.c — NovaOS TLS: Mbed TLS glue, root certificate store, entropy
 */

#define MBEDTLS_ALLOW_PRIVATE_ACCESS            /* handshake state, for TlsResumed */
#include "tls.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/spinlock.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../hal/rtc.h"
#include "../arch/x86_64/cpu.h"

#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/oid.h"
#include "mbedtls/error.h"
#include "mbedtls/version.h"
#include "psa/crypto.h"
#include "pfx.h"

#define SESSION_CACHE  8

struct TlsConn {
    mbedtls_ssl_context ssl;
    struct tcp_pcb     *pcb;
    struct pbuf        *rxq;           /* received ciphertext not yet consumed */
    bool                eof;           /* TCP FIN seen */
    bool                full;          /* server sent a certificate: not resumed */
    char                host[128];
    UINT16              port;
};

static mbedtls_entropy_context  g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static mbedtls_ssl_config       g_conf;
static mbedtls_x509_crt         g_roots_boot;
static mbedtls_x509_crt        *g_roots = &g_roots_boot;    /* (a new list after TlsRemoveRoot) */
static int                      g_nroots, g_nbuiltin;
static bool                     g_ready;

static struct {
    bool                used;
    char                host[128];
    UINT16              port;
    UINT64              stamp;
    mbedtls_ssl_session session;
} g_cache[SESSION_CACHE];

/* -----------------------------------------------------------------------
 * Entropy pool
 *
 * A running SHA-256 over every unpredictable input we have: RDRAND (when
 * the CPU has it), TSC readings at boot and at each received frame (the
 * net thread stirs one per packet), the RTC and the MAC address.  Mbed
 * TLS's entropy collector draws from it (MBEDTLS_ENTROPY_HARDWARE_ALT) to
 * seed and reseed its CTR-DRBGs; each output block is a digest of the pool
 * plus a counter, fed back so outputs never repeat.
 * ----------------------------------------------------------------------- */
static mbedtls_sha256_context g_pool;
static UINT64                 g_pool_ctr;
static bool                   g_pool_ready;
/* The pool's own lock (a leaf, held for one digest): programs asking for
 * random bytes (NtNovaGetRandom, as every Firefox process does as it
 * starts) need not queue for the network lock */
static KSpinLock              g_pool_lock = KSPINLOCK_INIT;

static bool rdrand64(UINT64 *out)
{
    for (int i = 0; i < 10; i++) {
        UINT64 v;
        UINT8 ok;
        /* rdrand %rax (encoded by hand: not in the kernel's -march) */
        __asm__ volatile (".byte 0x48, 0x0f, 0xc7, 0xf0\n\tsetc %1"
                          : "=a"(v), "=qm"(ok) : : "cc");
        if (ok) { *out = v; return true; }
    }
    return false;
}

static void pool_init(void)
{
    if (g_pool_ready) return;
    mbedtls_sha256_init(&g_pool);
    mbedtls_sha256_starts(&g_pool, 0);
    g_pool_ready = true;
}

void TlsStirEntropy(const void *data, UINT32 len)
{
    IrqState s = spin_lock_irqsave(&g_pool_lock);
    pool_init();
    mbedtls_sha256_update(&g_pool, data, len);
    spin_unlock_irqrestore(&g_pool_lock, s);
}

void TlsEntropyOutput(UINT8 *out, size_t len)
{
    while (len) {
        UINT8 block[32];
        IrqState s = spin_lock_irqsave(&g_pool_lock);
        pool_init();
        mbedtls_sha256_context c;
        mbedtls_sha256_init(&c);
        mbedtls_sha256_clone(&c, &g_pool);
        UINT64 x[2] = { ++g_pool_ctr, rdtsc() };
        mbedtls_sha256_update(&c, (const UINT8 *)x, sizeof(x));
        mbedtls_sha256_finish(&c, block);
        mbedtls_sha256_free(&c);
        mbedtls_sha256_update(&g_pool, block, sizeof(block));   /* ratchet */
        spin_unlock_irqrestore(&g_pool_lock, s);
        size_t n = len < sizeof(block) ? len : sizeof(block);
        memcpy(out, block, n);
        out += n; len -= n;
        memset(block, 0, sizeof(block));
    }
}

static int seed_pool(void)
{
    pool_init();
    RtcTime rt;
    rtc_read(&rt);
    TlsStirEntropy(&rt, sizeof(rt));
    int got = 0;
    if ((cpuid(1, 0).ecx >> 30) & 1) {
        for (int i = 0; i < 8; i++) {
            UINT64 v;
            if (rdrand64(&v)) { TlsStirEntropy(&v, sizeof(v)); got++; }
        }
    }
    for (int i = 0; i < 64; i++) {                 /* TSC jitter: weak, but cheap */
        for (volatile int j = 0; j < (i & 7) * 50; j++) { }
        UINT64 t = rdtsc();
        TlsStirEntropy(&t, sizeof(t));
    }
    return got;
}

/* -----------------------------------------------------------------------
 * Initialization
 * ----------------------------------------------------------------------- */
void TlsInit(void)
{
    int rdrand = seed_pool();

    psa_status_t ps = psa_crypto_init();                 /* TLS 1.3 needs PSA */
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_drbg);
    static const char pers[] = "NovaOS TLS client";
    int r = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                                  (const unsigned char *)pers, sizeof(pers) - 1);
    if (ps != PSA_SUCCESS || r) {
        kprintf("[TLS] Crypto initialization failed (psa %d, drbg -0x%x)\n", (int)ps, -r);
        return;
    }

    mbedtls_x509_crt_init(g_roots);
    for (int i = 0; i < g_tls_builtin_root_count; i++)
        if (!mbedtls_x509_crt_parse_der_nocopy(g_roots, g_tls_builtin_roots[i].der,
                                               g_tls_builtin_roots[i].len))
            g_nroots++;
    g_nbuiltin = g_nroots;

    mbedtls_ssl_config_init(&g_conf);
    r = mbedtls_ssl_config_defaults(&g_conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (r) { kprintf("[TLS] Configuration failed (-0x%x)\n", -r); return; }
    mbedtls_ssl_conf_authmode(&g_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&g_conf, g_roots, NULL);
    mbedtls_ssl_conf_rng(&g_conf, mbedtls_ctr_drbg_random, &g_drbg);
    mbedtls_ssl_conf_min_tls_version(&g_conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&g_conf, MBEDTLS_SSL_VERSION_TLS1_3);
    mbedtls_ssl_conf_session_tickets(&g_conf, MBEDTLS_SSL_SESSION_TICKETS_ENABLED);
    /* keep TLS 1.3 tickets so reconnects can resume */
    mbedtls_ssl_conf_tls13_enable_signal_new_session_tickets(
        &g_conf, MBEDTLS_SSL_TLS1_3_SIGNAL_NEW_SESSION_TICKETS_ENABLED);
    for (int i = 0; i < SESSION_CACHE; i++) mbedtls_ssl_session_init(&g_cache[i].session);
    g_ready = true;

    kprintf("[TLS] Mbed TLS %s ready: TLS 1.2/1.3, %d trusted roots; entropy from %sTSC jitter%s\n",
            MBEDTLS_VERSION_STRING, g_nroots, rdrand ? "RDRAND, " : "",
            rdrand ? "" : " and packet timing (no RDRAND)");
}

/* -----------------------------------------------------------------------
 * Root store
 * ----------------------------------------------------------------------- */
int TlsRootCount(void)     { return g_nroots; }
int TlsUserRootCount(void) { return g_nroots - g_nbuiltin; }

/* Copy an X.500 string value as printable ASCII (BMPString: low bytes). */
static void copy_str(const mbedtls_x509_buf *v, char *buf, int cap)
{
    int o = 0;
    size_t step = v->tag == MBEDTLS_ASN1_BMP_STRING ? 2 : 1;
    for (size_t i = step - 1; i < v->len && o < cap - 1; i += step) {
        UINT8 c = v->p[i];
        if (step == 2 && v->p[i - 1]) c = '?';
        buf[o++] = (c >= 0x20 && c < 0x7F) ? (char)c : (c >= 0x80 ? '?' : ' ');
    }
    buf[o] = '\0';
}

bool TlsRootCert(int i, const UINT8 **der, UINT32 *len)
{
    if (i < 0 || i >= g_nroots) return false;
    const mbedtls_x509_crt *c = g_roots;
    for (int k = 0; k < i && c; k++) c = c->next;
    if (!c || !c->raw.len) return false;
    *der = c->raw.p;
    *len = (UINT32)c->raw.len;
    return true;
}

bool TlsRootName(int i, char *buf, int cap, bool *user)
{
    if (i < 0 || i >= g_nroots || cap < 2) return false;
    const mbedtls_x509_crt *c = g_roots;
    for (int k = 0; k < i && c; k++) c = c->next;
    if (!c) return false;
    if (user) *user = i >= g_nbuiltin;
    buf[0] = '\0';
    int best = 3;
    for (const mbedtls_x509_name *n = &c->subject; n; n = n->next) {
        int rank = !MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid) ? 0 :
                   !MBEDTLS_OID_CMP(MBEDTLS_OID_AT_ORGANIZATION, &n->oid) ? 1 :
                   !MBEDTLS_OID_CMP(MBEDTLS_OID_AT_ORG_UNIT, &n->oid) ? 2 : 3;
        if (rank < best) { copy_str(&n->val, buf, cap); best = rank; }
    }
    return true;
}

int TlsImportRoots(const void *data, UINT32 len, char *err, int err_cap)
{
    err[0] = '\0';
    if (!g_ready) { ksnprintf(err, err_cap, "TLS is not available"); return 0; }

    const UINT8 *d = data;
    bool pem = false;
    for (UINT32 i = 0; i + 10 <= len; i++)
        if (!memcmp(d + i, "-----BEGIN", 10)) { pem = true; break; }

    /* PEM input must be NUL-terminated, with the NUL counted in the length */
    UINT8 *copy = kmalloc(len + 1);
    if (!copy) { ksnprintf(err, err_cap, "Out of memory"); return 0; }
    memcpy(copy, d, len);
    copy[len] = 0;

    mbedtls_x509_crt tmp;
    mbedtls_x509_crt_init(&tmp);
    int r = mbedtls_x509_crt_parse(&tmp, copy, pem ? len + 1 : len);
    kfree(copy);
    if (r < 0) {
        char e[64];
        mbedtls_strerror(r, e, sizeof(e));
        ksnprintf(err, err_cap, "Not a valid certificate (%s)", e);
        mbedtls_x509_crt_free(&tmp);
        return 0;
    }

    int added = 0, dups = 0, not_ca = 0;
    for (mbedtls_x509_crt *c = &tmp; c && c->raw.len; c = c->next) {
        if (!mbedtls_x509_crt_get_ca_istrue(c)) { not_ca++; continue; }
        bool dup = false;
        for (const mbedtls_x509_crt *t = g_roots; t && t->raw.len; t = t->next)
            if (t->raw.len == c->raw.len && !memcmp(t->raw.p, c->raw.p, c->raw.len)) { dup = true; break; }
        if (dup) { dups++; continue; }
        /* appended at the end: connections holding the chain are unaffected */
        if (!mbedtls_x509_crt_parse_der(g_roots, c->raw.p, c->raw.len)) { added++; g_nroots++; }
    }
    mbedtls_x509_crt_free(&tmp);

    if (!added)
        ksnprintf(err, err_cap, "%s", dups ? "Certificate is already trusted" :
                                not_ca ? "Not a CA certificate (basicConstraints CA:FALSE)" :
                                         "No certificates found");
    return added;
}

/* -----------------------------------------------------------------------
 * Personal store ("My"): certificates with their private keys, from
 * PKCS #12 files (certutil -importpfx), for TLS client authentication
 * (EAP-TLS: nt/eap.c).  A removed one (certutil -delstore my) is only
 * marked so while NovaOS runs, as a session may still point at it: it is
 * no longer listed, found or saved, and its slot stays taken until the
 * next start.
 * ----------------------------------------------------------------------- */
#define PERSONAL_MAX 8

static struct {
    mbedtls_x509_crt   chain;          /* the certificate, then the rest of the file's chain */
    mbedtls_pk_context key;
    char               name[64];       /* its subject's common name */
    char               issuer[64];
    bool               removed;
} g_personal[PERSONAL_MAX];
static int g_npersonal;

static void cn_of(const mbedtls_x509_name *n, char *buf, int cap)
{
    buf[0] = '\0';
    for (; n; n = n->next)
        if (!MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid)) copy_str(&n->val, buf, cap);
}

static int add_personal(char *name, int name_cap, char *err, int err_cap);

int TlsImportPfx(const void *data, UINT32 len, const char *password, char *name, int name_cap, char *err,
                 int err_cap)
{
    err[0] = '\0';
    if (name_cap) name[0] = '\0';
    if (!g_ready) { ksnprintf(err, err_cap, "TLS is not available"); return 0; }
    if (g_npersonal >= PERSONAL_MAX) { ksnprintf(err, err_cap, "The personal store is full"); return 0; }
    mbedtls_x509_crt *chain = &g_personal[g_npersonal].chain;
    mbedtls_pk_context *key = &g_personal[g_npersonal].key;
    mbedtls_x509_crt_init(chain);
    mbedtls_pk_init(key);
    int r = PfxParse(data, len, password, chain, key, mbedtls_ctr_drbg_random, &g_drbg);
    if (r != PFX_OK) { ksnprintf(err, err_cap, "%s", PfxErrorText(r)); return 0; }
    return add_personal(name, name_cap, err, err_cap);
}

/* The new entry g_personal[g_npersonal] (its chain and key parsed): kept,
 * unless the store has that certificate already */
static int add_personal(char *name, int name_cap, char *err, int err_cap)
{
    mbedtls_x509_crt *chain = &g_personal[g_npersonal].chain;
    mbedtls_pk_context *key = &g_personal[g_npersonal].key;
    for (int i = 0; i < g_npersonal; i++)                   /* (the same certificate again: kept once) */
        if (!g_personal[i].removed && g_personal[i].chain.raw.len == chain->raw.len &&
            !memcmp(g_personal[i].chain.raw.p, chain->raw.p, chain->raw.len)) {
            mbedtls_x509_crt_free(chain);
            mbedtls_pk_free(key);
            ksnprintf(err, err_cap, "The certificate is already in the personal store");
            return 0;
        }
    cn_of(&chain->subject, g_personal[g_npersonal].name, sizeof(g_personal[0].name));
    cn_of(&chain->issuer, g_personal[g_npersonal].issuer, sizeof(g_personal[0].issuer));
    if (name_cap) ksnprintf(name, name_cap, "%s", g_personal[g_npersonal].name);
    g_personal[g_npersonal].removed = false;
    g_npersonal++;
    return 1;
}

/* One DER element's length (tag, length, contents) at @p, 0 if it does not fit in @room */
static UINT32 der_len(const UINT8 *p, UINT32 room)
{
    if (room < 2) return 0;
    UINT32 n = p[1], h = 2;
    if (n & 0x80) {
        UINT32 k = n & 0x7F;
        if (!k || k > 3 || room < 2 + k) return 0;
        n = 0;
        for (UINT32 i = 0; i < k; i++) n = n << 8 | p[2 + i];
        h += k;
    }
    return n <= room - h ? h + n : 0;
}

int TlsPersonalAdd(const void *chain_der, UINT32 chain_len, const void *key_der, UINT32 key_len, char *name,
                   int name_cap, char *err, int err_cap)
{
    err[0] = '\0';
    if (name_cap) name[0] = '\0';
    if (!g_ready) { ksnprintf(err, err_cap, "TLS is not available"); return 0; }
    if (g_npersonal >= PERSONAL_MAX) { ksnprintf(err, err_cap, "The personal store is full"); return 0; }
    mbedtls_x509_crt *chain = &g_personal[g_npersonal].chain;
    mbedtls_pk_context *key = &g_personal[g_npersonal].key;
    mbedtls_x509_crt_init(chain);
    mbedtls_pk_init(key);
    const UINT8 *p = chain_der;
    bool ok = chain_len > 0;
    for (UINT32 o = 0, n; ok && o < chain_len; o += n) {
        n = der_len(p + o, chain_len - o);
        ok = n && !mbedtls_x509_crt_parse_der(chain, p + o, n);
    }
    ok = ok && !mbedtls_pk_parse_key(key, key_der, key_len, NULL, 0, mbedtls_ctr_drbg_random, &g_drbg) &&
         !mbedtls_pk_check_pair(&chain->pk, key, mbedtls_ctr_drbg_random, &g_drbg);
    if (!ok) {
        mbedtls_x509_crt_free(chain);
        mbedtls_pk_free(key);
        ksnprintf(err, err_cap, "The saved certificate or its key could not be read");
        return 0;
    }
    return add_personal(name, name_cap, err, err_cap);
}

int TlsPersonalExport(int i, UINT8 *out, UINT32 cap, UINT32 *chain_len, UINT32 *key_len)
{
    if (i < 0 || i >= g_npersonal || g_personal[i].removed) return 0;
    UINT32 o = 0;
    for (const mbedtls_x509_crt *c = &g_personal[i].chain; c && c->raw.len; c = c->next) {
        if (c->raw.len > cap - o) return 0;
        memcpy(out + o, c->raw.p, c->raw.len);
        o += (UINT32)c->raw.len;
    }
    /* (mbedtls_pk_write_key_der writes at the end of the room it is given) */
    int k = mbedtls_pk_write_key_der(&g_personal[i].key, out + o, cap - o);
    if (k <= 0) return 0;
    memmove(out + o, out + cap - k, (UINT32)k);
    *chain_len = o;
    *key_len = (UINT32)k;
    return 1;
}

int TlsPersonalCount(void)
{
    int n = 0;
    for (int i = 0; i < g_npersonal; i++) n += !g_personal[i].removed;
    return n;
}

int TlsPersonalSlots(void) { return g_npersonal; }

bool TlsPersonalCert(int i, const UINT8 **der, UINT32 *len)
{
    if (i < 0 || i >= g_npersonal || g_personal[i].removed) return false;
    *der = g_personal[i].chain.raw.p;
    *len = (UINT32)g_personal[i].chain.raw.len;
    return true;
}

int TlsPersonalRemove(int i)
{
    if (i < 0 || i >= g_npersonal || g_personal[i].removed) return 0;
    g_personal[i].removed = true;               /* (its chain and key stay: a session may hold them) */
    return 1;
}

bool TlsPersonalName(int i, char *name, int name_cap, char *issuer, int issuer_cap)
{
    if (i < 0 || i >= g_npersonal || g_personal[i].removed) return false;
    if (name_cap) ksnprintf(name, name_cap, "%s", g_personal[i].name);
    if (issuer_cap) ksnprintf(issuer, issuer_cap, "%s", g_personal[i].issuer);
    return true;
}

static bool same_ci(const char *a, const char *b, UINT32 n)
{
    for (UINT32 i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
        if (!x) return true;
    }
    return true;
}

int TlsPersonalFind(const char *user)
{
    if (!user || !user[0]) {                                /* (the newest) */
        for (int i = g_npersonal - 1; i >= 0; i--)
            if (!g_personal[i].removed) return i;
        return -1;
    }
    /* the certificate named @user, or its bare name: "CORP\\alice", "alice@corp" */
    const char *bare = user;
    for (const char *s = user; *s; s++)
        if (*s == '\\') bare = s + 1;
    UINT32 bl = 0;
    while (bare[bl] && bare[bl] != '@') bl++;
    for (int i = g_npersonal - 1; i >= 0; i--)
        if (g_personal[i].removed) continue;
        else if (same_ci(g_personal[i].name, user, sizeof(g_personal[i].name)) ||
            (strlen(g_personal[i].name) == bl && same_ci(g_personal[i].name, bare, bl)))
            return i;
    return -1;
}

/* -----------------------------------------------------------------------
 * Session cache
 * ----------------------------------------------------------------------- */
static int cache_find(const char *host, UINT16 port)
{
    for (int i = 0; i < SESSION_CACHE; i++)
        if (g_cache[i].used && g_cache[i].port == port && !strcmp(g_cache[i].host, host))
            return i;
    return -1;
}

void TlsSaveSession(TlsConn *c)
{
    if (!c || !mbedtls_ssl_is_handshake_over(&c->ssl)) return;
    mbedtls_ssl_session s;
    mbedtls_ssl_session_init(&s);
    /* (TLS 1.3: fails until the server's NewSessionTicket has arrived) */
    if (mbedtls_ssl_get_session(&c->ssl, &s)) { mbedtls_ssl_session_free(&s); return; }

    int i = cache_find(c->host, c->port);
    if (i < 0) {                                     /* free slot, else least recent */
        i = 0;
        for (int k = 0; k < SESSION_CACHE; k++) {
            if (!g_cache[k].used) { i = k; break; }
            if (g_cache[k].stamp < g_cache[i].stamp) i = k;
        }
    }
    mbedtls_ssl_session_free(&g_cache[i].session);
    g_cache[i].session = s;                          /* take ownership */
    g_cache[i].used = true;
    g_cache[i].port = c->port;
    g_cache[i].stamp = sched_ticks();
    strncpy(g_cache[i].host, c->host, sizeof(g_cache[i].host) - 1);
    g_cache[i].host[sizeof(g_cache[i].host) - 1] = '\0';
}

/* -----------------------------------------------------------------------
 * Connections: Mbed TLS reads and writes TCP through these callbacks
 * ----------------------------------------------------------------------- */
static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    TlsConn *c = ctx;
    if (!c->pcb) return MBEDTLS_ERR_SSL_CONN_EOF;
    size_t room = tcp_sndbuf(c->pcb);
    if (len > room) len = room;
    if (len > 0xFFFF) len = 0xFFFF;
    if (!len || tcp_write(c->pcb, buf, (u16_t)len, TCP_WRITE_FLAG_COPY) != ERR_OK)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return (int)len;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    TlsConn *c = ctx;
    if (!c->rxq) return c->eof ? 0 : MBEDTLS_ERR_SSL_WANT_READ;
    u16_t n = (u16_t)(len < c->rxq->tot_len ? len : c->rxq->tot_len);
    pbuf_copy_partial(c->rxq, buf, n, 0);
    c->rxq = pbuf_free_header(c->rxq, n);
    if (c->pcb) tcp_recved(c->pcb, n);               /* reopen the TCP window */
    return n;
}

TlsConn *TlsClientNew(const char *host, UINT16 port, void *pcb)
{
    if (!g_ready) return NULL;
    TlsConn *c = kzalloc(sizeof(*c));
    if (!c) return NULL;
    mbedtls_ssl_init(&c->ssl);
    if (mbedtls_ssl_setup(&c->ssl, &g_conf) || mbedtls_ssl_set_hostname(&c->ssl, host)) {
        mbedtls_ssl_free(&c->ssl);
        kfree(c);
        return NULL;
    }
    c->pcb = pcb;
    c->port = port;
    strncpy(c->host, host, sizeof(c->host) - 1);
    mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, NULL);
    int i = cache_find(host, port);
    if (i >= 0 && mbedtls_ssl_set_session(&c->ssl, &g_cache[i].session)) g_cache[i].used = false;
    return c;
}

void TlsInput(TlsConn *c, void *pbuf)
{
    struct pbuf *p = pbuf;
    if (!p) { c->eof = true; return; }
    if (c->rxq) pbuf_cat(c->rxq, p); else c->rxq = p;
}

static void flush(TlsConn *c)
{
    if (c->pcb) tcp_output(c->pcb);
}

int TlsHandshake(TlsConn *c)
{
    int r = 0;
    while (!mbedtls_ssl_is_handshake_over(&c->ssl)) {
        /* A resumed handshake (TLS 1.2 abbreviated, or TLS 1.3 with a PSK)
         * never reaches the server-certificate state. */
        if (c->ssl.MBEDTLS_PRIVATE(state) == MBEDTLS_SSL_SERVER_CERTIFICATE) c->full = true;
        r = mbedtls_ssl_handshake_step(&c->ssl);
        if (r) break;
    }
    flush(c);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return TLS_AGAIN;
    if (r) return r;
    return 1;
}

int TlsWrite(TlsConn *c, const void *data, size_t len)
{
    int r = mbedtls_ssl_write(&c->ssl, data, len);
    flush(c);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return TLS_AGAIN;
    return r;
}

int TlsRead(TlsConn *c, void *buf, size_t cap)
{
    for (int spins = 0;;) {
        int r = mbedtls_ssl_read(&c->ssl, buf, cap);
        if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;   /* keep going */
        /* On a TLS 1.3 post-handshake message (NewSessionTicket) Mbed TLS
         * switches state and returns WANT_READ meaning "call me again" —
         * even when the rest of the data is already queued.  Retry while
         * there is queued ciphertext or such a message is in progress;
         * otherwise a response that arrived with the ticket would sit
         * unread until more data happened to come in. */
        if (r == MBEDTLS_ERR_SSL_WANT_READ && spins++ < 16 &&
            (c->rxq || c->ssl.MBEDTLS_PRIVATE(state) != MBEDTLS_SSL_HANDSHAKE_OVER))
            continue;
        flush(c);
        if (r > 0) return r;
        /* close_notify, or TCP closed without one (the HTTP layer decides
         * whether the response was complete) */
        if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == MBEDTLS_ERR_SSL_CONN_EOF)
            return TLS_EOF;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return TLS_AGAIN;
        return r;
    }
}

void TlsFree(TlsConn *c)
{
    if (!c) return;
    mbedtls_ssl_free(&c->ssl);                       /* zeroizes key material */
    if (c->rxq) pbuf_free(c->rxq);
    kfree(c);
}

void TlsClose(TlsConn *c)
{
    if (!c) return;
    if (c->pcb && mbedtls_ssl_is_handshake_over(&c->ssl)) {
        mbedtls_ssl_close_notify(&c->ssl);
        flush(c);
    }
    TlsFree(c);
}

/* -----------------------------------------------------------------------
 * Reporting
 * ----------------------------------------------------------------------- */
bool TlsResumed(TlsConn *c)
{
    return c && mbedtls_ssl_is_handshake_over(&c->ssl) && !c->full;
}

const char *TlsDescribe(TlsConn *c, char *buf, int cap)
{
    /* "TLSv1.3" + "TLS1-3-AES-128-GCM-SHA256" -> "TLS 1.3, TLS_AES_128_GCM_SHA256"
     * "TLSv1.2" + "TLS-ECDHE-RSA-WITH-AES-256-GCM-SHA384"
     *           -> "TLS 1.2, TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384" (IANA names) */
    const char *v = mbedtls_ssl_get_version(&c->ssl);
    const char *s = mbedtls_ssl_get_ciphersuite(&c->ssl);
    if (!s) s = "?";
    if (!strncmp(v, "TLSv", 4)) v += 4;
    char suite[64];
    int n = 0;
    if (!strncmp(s, "TLS1-3-", 7)) { s += 7; memcpy(suite, "TLS_", 4); n = 4; }
    for (; *s && n < (int)sizeof(suite) - 1; s++) suite[n++] = *s == '-' ? '_' : *s;
    suite[n] = '\0';
    ksnprintf(buf, cap, "TLS %s, %s", v, suite);
    return buf;
}

const char *TlsErrorText(TlsConn *c, int err, char *buf, int cap)
{
    if (err == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && c) {
        UINT32 f = mbedtls_ssl_get_verify_result(&c->ssl);
        if (f & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
            return "Certificate is not trusted (unknown certificate authority)";
        if (f & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE))
            return "Certificate has expired or is not yet valid";
        if (f & MBEDTLS_X509_BADCERT_CN_MISMATCH)
            return "Certificate does not match the host name";
        if (f & MBEDTLS_X509_BADCERT_REVOKED)
            return "Certificate has been revoked";
        if (f & (MBEDTLS_X509_BADCERT_BAD_MD | MBEDTLS_X509_BADCERT_BAD_PK |
                 MBEDTLS_X509_BADCERT_BAD_KEY))
            return "Certificate uses a weak or unsupported algorithm";
        ksnprintf(buf, cap, "Certificate verification failed (flags 0x%x)", f);
        return buf;
    }
    switch (err) {
    case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE:
        return "Server refused the connection (TLS alert)";
    case MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION:
        return "Server requires an unsupported TLS version";
    case MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE:
        return "TLS handshake failed (no protocol or cipher in common)";
    case MBEDTLS_ERR_SSL_CONN_EOF:
        return "Connection closed during the TLS handshake";
    case MBEDTLS_ERR_SSL_ALLOC_FAILED:
        return "Out of memory";
    }
    char e[80];
    mbedtls_strerror(err, e, sizeof(e));
    ksnprintf(buf, cap, "TLS error: %s", e);
    return buf;
}

/* -----------------------------------------------------------------------
 * EAP sessions (tls.h): TLS 1.2 over memory, for PEAP's tunnel
 * ----------------------------------------------------------------------- */
#include "net_internal.h"

#define EAP_TLS_BUF 32768

struct TlsEap {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config *conf;  /* EAP-TLS: its own, with the client certificate (else g_eap_conf) */
    UINT8  *in, *out;           /* ciphertext from the server not yet read; to it not yet taken */
    UINT32  in_len, in_off, out_len;
    UINT32  verify;             /* mbedtls_ssl_get_verify_result after a failed handshake */
};

static mbedtls_ssl_config g_eap_conf;
static bool               g_eap_ready;

static bool eap_conf(mbedtls_ssl_config *c)
{
    mbedtls_ssl_config_init(c);
    if (mbedtls_ssl_config_defaults(c, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT))
        return false;
    mbedtls_ssl_conf_authmode(c, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(c, g_roots, NULL);
    mbedtls_ssl_conf_rng(c, mbedtls_ctr_drbg_random, &g_drbg);
    mbedtls_ssl_conf_min_tls_version(c, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(c, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_session_tickets(c, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
    return true;
}

/* Root store, removing an imported root (certutil -delstore root): a new
 * list without it for the configurations to use from now on.  The old
 * list stays allocated, as a handshake under way (or an EAP-TLS session's
 * own configuration) may still be checking against it. */
int TlsRemoveRoot(int i)
{
    if (!g_ready || i < g_nbuiltin || i >= g_nroots) return 0;
    mbedtls_x509_crt *n = kmalloc(sizeof(*n));
    if (!n) return 0;
    mbedtls_x509_crt_init(n);
    int k = 0, kept = 0;
    bool ok = true;
    for (const mbedtls_x509_crt *c = g_roots; ok && c && c->raw.len; c = c->next, k++) {
        if (k == i) continue;
        /* (the built-in ones point at tls_roots.c's DER, as at TlsInit) */
        ok = !(k < g_nbuiltin ? mbedtls_x509_crt_parse_der_nocopy(n, c->raw.p, c->raw.len)
                              : mbedtls_x509_crt_parse_der(n, c->raw.p, c->raw.len));
        kept += ok;
    }
    if (!ok) {
        mbedtls_x509_crt_free(n);
        kfree(n);
        return 0;
    }
    g_roots = n;
    g_nroots = kept;
    mbedtls_ssl_conf_ca_chain(&g_conf, g_roots, NULL);
    if (g_eap_ready) mbedtls_ssl_conf_ca_chain(&g_eap_conf, g_roots, NULL);
    return 1;
}

static int eap_send(void *ctx, const unsigned char *buf, size_t len)
{
    TlsEap *e = ctx;
    if (len > EAP_TLS_BUF - e->out_len) len = EAP_TLS_BUF - e->out_len;
    if (!len) return MBEDTLS_ERR_SSL_WANT_WRITE;
    memcpy(e->out + e->out_len, buf, len);
    e->out_len += (UINT32)len;
    return (int)len;
}

static int eap_recv(void *ctx, unsigned char *buf, size_t len)
{
    TlsEap *e = ctx;
    UINT32 left = e->in_len - e->in_off;
    if (!left) return MBEDTLS_ERR_SSL_WANT_READ;
    if (len > left) len = left;
    memcpy(buf, e->in + e->in_off, len);
    e->in_off += (UINT32)len;
    if (e->in_off == e->in_len) e->in_off = e->in_len = 0;
    return (int)len;
}

TlsEap *TlsEapNew(int cert)
{
    net_lock();
    TlsEap *e = NULL;
    if (!g_ready || cert >= g_npersonal) goto out;
    if (!g_eap_ready) {
        /* PEAP's and EAP-TLS's TLS: 1.2 (RFC 9427's TLS 1.3 for EAP is not what Windows sends by default),
         * the server's certificate checked against the trusted roots; no session tickets */
        if (!eap_conf(&g_eap_conf)) goto out;
        g_eap_ready = true;
    }
    e = kmalloc(sizeof(*e));
    if (!e) goto out;
    memset(e, 0, sizeof(*e));
    e->in = kmalloc(EAP_TLS_BUF);
    e->out = kmalloc(EAP_TLS_BUF);
    if (cert >= 0 && (e->conf = kmalloc(sizeof(*e->conf))) != NULL) {
        /* EAP-TLS: the client certificate (and the rest of its chain) with its key, when the server asks */
        if (!eap_conf(e->conf) ||
            mbedtls_ssl_conf_own_cert(e->conf, &g_personal[cert].chain, &g_personal[cert].key)) {
            mbedtls_ssl_config_free(e->conf);
            kfree(e->conf);
            e->conf = NULL;
        }
    }
    mbedtls_ssl_init(&e->ssl);
    if (!e->in || !e->out || (cert >= 0 && !e->conf) || mbedtls_ssl_setup(&e->ssl, e->conf ? e->conf : &g_eap_conf) ||
        mbedtls_ssl_set_hostname(&e->ssl, NULL)) {      /* (no host name: the server's is not a web address) */
        mbedtls_ssl_free(&e->ssl);
        if (e->conf) { mbedtls_ssl_config_free(e->conf); kfree(e->conf); }
        if (e->in) kfree(e->in);
        if (e->out) kfree(e->out);
        kfree(e);
        e = NULL;
        goto out;
    }
    mbedtls_ssl_set_bio(&e->ssl, e, eap_send, eap_recv, NULL);
out:
    net_unlock();
    return e;
}

void TlsEapInput(TlsEap *e, const void *data, UINT32 len)
{
    if (e->in_off) {
        memmove(e->in, e->in + e->in_off, e->in_len - e->in_off);
        e->in_len -= e->in_off;
        e->in_off = 0;
    }
    if (len > EAP_TLS_BUF - e->in_len) len = EAP_TLS_BUF - e->in_len;
    memcpy(e->in + e->in_len, data, len);
    e->in_len += len;
}

int TlsEapHandshake(TlsEap *e)
{
    net_lock();
    int r = mbedtls_ssl_handshake(&e->ssl);
    if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) e->verify = mbedtls_ssl_get_verify_result(&e->ssl);
    net_unlock();
    if (!r) return 1;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return TLS_AGAIN;
    char why[96];
    kprintf("[TLS] EAP tunnel: handshake failed: %s\n", TlsEapErrorText(e, r, why, sizeof(why)));
    return r;
}

UINT32 TlsEapPending(TlsEap *e) { return e->out_len; }

UINT32 TlsEapOutput(TlsEap *e, UINT8 *out, UINT32 cap)
{
    UINT32 n = e->out_len < cap ? e->out_len : cap;
    memcpy(out, e->out, n);
    memmove(e->out, e->out + n, e->out_len - n);
    e->out_len -= n;
    return n;
}

int TlsEapWrite(TlsEap *e, const void *data, UINT32 len)
{
    net_lock();
    int r = mbedtls_ssl_write(&e->ssl, data, len);
    net_unlock();
    return r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ? TLS_AGAIN : r;
}

int TlsEapRead(TlsEap *e, void *buf, UINT32 cap)
{
    net_lock();
    int r = mbedtls_ssl_read(&e->ssl, buf, cap);
    net_unlock();
    return r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ? TLS_AGAIN : r;
}

bool TlsEapUntrusted(TlsEap *e) { return e->verify != 0; }

const char *TlsEapErrorText(TlsEap *e, int err, char *buf, int cap)
{
    if (err == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && (e->verify & MBEDTLS_X509_BADCERT_NOT_TRUSTED))
        return "Certificate is not trusted (unknown certificate authority)";
    if (err == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        ksnprintf(buf, cap, "Certificate verification failed (flags 0x%x)", e->verify);
        return buf;
    }
    return TlsErrorText(NULL, err, buf, cap);
}

bool TlsEapExportKey(TlsEap *e, const char *label, UINT8 *out, UINT32 len)
{
    net_lock();
    int r = mbedtls_ssl_export_keying_material(&e->ssl, out, len, label, strlen(label), NULL, 0, 0);
    net_unlock();
    return r == 0;
}

void TlsEapPeerName(TlsEap *e, char *buf, int cap)
{
    buf[0] = '\0';
    const mbedtls_x509_crt *c = mbedtls_ssl_get_peer_cert(&e->ssl);
    if (!c) return;
    for (const mbedtls_x509_name *n = &c->subject; n; n = n->next)
        if (!MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid)) copy_str(&n->val, buf, cap);
}

void TlsEapRandom(UINT8 *out, UINT32 len) { TlsEntropyOutput(out, len); }

void TlsEapFree(TlsEap *e)
{
    net_lock();
    mbedtls_ssl_free(&e->ssl);
    if (e->conf) { mbedtls_ssl_config_free(e->conf); kfree(e->conf); }
    net_unlock();
    memset(e->in, 0, EAP_TLS_BUF);
    memset(e->out, 0, EAP_TLS_BUF);
    kfree(e->in);
    kfree(e->out);
    kfree(e);
}
