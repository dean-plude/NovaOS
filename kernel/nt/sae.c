/*
 * sae.c — SAE for WPA3-Personal, the station's side (sae.h), IEEE
 * 802.11-2020 12.4.4 and 12.4.5, over Mbed TLS's P-256, bignum and HMAC
 * (third_party/mbedtls, which the kernel already links)
 *
 * The password element (PWE) by hunting and pecking (12.4.4.2.2): for
 * counter 1, 2, ... pwd-seed = HMAC-SHA256(max(A, B) || min(A, B),
 * password || counter), x = KDF-256(pwd-seed, "SAE Hunting and Pecking",
 * p); the first x below p whose x^3 + ax + b is a square is the PWE's, its
 * y the root whose parity is pwd-seed's.  All 40 rounds always run, so how
 * long it takes does not tell which round found it.  Then the commit
 * (12.4.5.2): scalar = (rand + mask) mod r, element = -(mask * PWE); the
 * shared secret (12.4.5.4): K = rand * (peer-scalar * PWE + PEER-ELEMENT),
 * keyseed = HMAC-SHA256(0^32, x(K)), KCK || PMK = KDF-512(keyseed,
 * "SAE KCK and PMK", (scalar + peer-scalar) mod r), PMKID = the first 16
 * bytes of that sum; and the confirm (12.4.5.5): HMAC-SHA256(KCK,
 * send-confirm || scalar || element || peer-scalar || peer-element).
 *
 * The arithmetic is Mbed TLS's; the steps follow the standard, checked
 * against its Annex J.10 test vector (tests/host/sae_handshake.c), as
 * hostapd's src/common/sae.c (BSD) does.  Hash-to-element (H2E, the
 * standard's newer PWE) and the other groups are not here: an access point
 * that insists on them is refused with a clear reason.
 */
#include "sae.h"
#include "../lib/string.h"
#include "../net/tls.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"

#define HNP_ROUNDS 40               /* (hunting and pecking's k: at least 40, 12.4.4.2.2) */

static int rng(void *ctx, unsigned char *out, size_t n)
{
    (void)ctx;
    TlsEntropyOutput(out, n);
    return 0;
}

static void hmac256(const UINT8 *key, UINT32 klen, const UINT8 *data, UINT32 len, UINT8 out[32])
{
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, klen, data, len, out);
}

/* KDF-Hash-Length (12.7.1.7.2) with SHA-256: @bits bits from @key, @label
 * and @ctx (HMAC(key, i || label || ctx || bits), i and bits 16-bit
 * little-endian, i from 1) */
static void kdf256(const UINT8 *key, const char *label, const UINT8 *ctx, UINT32 clen, UINT32 bits, UINT8 *out)
{
    UINT8 in[2 + 64 + 64 + 2], h[32];
    UINT32 ll = (UINT32)strlen(label);
    memcpy(in + 2, label, ll);
    memcpy(in + 2 + ll, ctx, clen);
    in[2 + ll + clen] = (UINT8)bits;
    in[3 + ll + clen] = (UINT8)(bits >> 8);
    for (UINT32 i = 1, done = 0; done < bits / 8; i++) {
        in[0] = (UINT8)i;
        in[1] = (UINT8)(i >> 8);
        hmac256(key, 32, in, 4 + ll + clen, h);
        UINT32 n = bits / 8 - done < 32 ? bits / 8 - done : 32;
        memcpy(out + done, h, n);
        done += n;
    }
    memset(h, 0, sizeof(h));
}

typedef struct {
    mbedtls_ecp_group grp;
    mbedtls_mpi a, b, t, u;
    mbedtls_ecp_point P, Q, R;
} Ctx;

static bool ctx_init(Ctx *c)
{
    mbedtls_ecp_group_init(&c->grp);
    mbedtls_mpi_init(&c->a); mbedtls_mpi_init(&c->b); mbedtls_mpi_init(&c->t); mbedtls_mpi_init(&c->u);
    mbedtls_ecp_point_init(&c->P); mbedtls_ecp_point_init(&c->Q); mbedtls_ecp_point_init(&c->R);
    return !mbedtls_ecp_group_load(&c->grp, MBEDTLS_ECP_DP_SECP256R1);
}

static void ctx_free(Ctx *c)
{
    mbedtls_ecp_group_free(&c->grp);
    mbedtls_mpi_free(&c->a); mbedtls_mpi_free(&c->b); mbedtls_mpi_free(&c->t); mbedtls_mpi_free(&c->u);
    mbedtls_ecp_point_free(&c->P); mbedtls_ecp_point_free(&c->Q); mbedtls_ecp_point_free(&c->R);
}

static bool point_read(Ctx *c, mbedtls_ecp_point *P, const UINT8 *xy)
{
    return !mbedtls_mpi_read_binary(&P->X, xy, SAE_PRIME_LEN) &&
           !mbedtls_mpi_read_binary(&P->Y, xy + SAE_PRIME_LEN, SAE_PRIME_LEN) && !mbedtls_mpi_lset(&P->Z, 1) &&
           !mbedtls_ecp_check_pubkey(&c->grp, P);
}

static bool point_write(const mbedtls_ecp_point *P, UINT8 *xy)
{
    return !mbedtls_mpi_write_binary(&P->X, xy, SAE_PRIME_LEN) &&
           !mbedtls_mpi_write_binary(&P->Y, xy + SAE_PRIME_LEN, SAE_PRIME_LEN);
}

/* A random number in [2, r - 1] (rejection sampling: r is just under 2^256) */
static bool random_scalar(Ctx *c, mbedtls_mpi *x)
{
    UINT8 b[SAE_PRIME_LEN];
    for (int tries = 0; tries < 64; tries++) {
        TlsEntropyOutput(b, sizeof(b));
        if (mbedtls_mpi_read_binary(x, b, sizeof(b))) break;
        if (mbedtls_mpi_cmp_int(x, 1) > 0 && mbedtls_mpi_cmp_mpi(x, &c->grp.N) < 0) {
            memset(b, 0, sizeof(b));
            return true;
        }
    }
    memset(b, 0, sizeof(b));
    return false;
}

/* The PWE (12.4.4.2.2) into c->P */
static bool derive_pwe(Ctx *c, const char *password, const UINT8 sta[6], const UINT8 ap[6])
{
    UINT8 key[12], in[256], seed[32], x[32], found_x[32] = { 0 }, prime[32];
    UINT32 pl = (UINT32)strlen(password);
    if (pl + 1 > sizeof(in)) return false;
    bool sta_first = memcmp(sta, ap, 6) > 0;
    memcpy(key, sta_first ? sta : ap, 6);
    memcpy(key + 6, sta_first ? ap : sta, 6);
    memcpy(in, password, pl);
    mbedtls_mpi_write_binary(&c->grp.P, prime, sizeof(prime));
    /* (p - 1) / 2: Euler's criterion; x^3 + ax + b by the curve's A (NULL in Mbed TLS: -3) and B */
    mbedtls_mpi pm1;
    mbedtls_mpi_init(&pm1);
    bool ok = !mbedtls_mpi_sub_int(&pm1, &c->grp.P, 1) && !mbedtls_mpi_shift_r(&pm1, 1);
    UINT8 found = 0, found_lsb = 0;
    for (int counter = 1; ok && counter <= HNP_ROUNDS; counter++) {
        in[pl] = (UINT8)counter;
        hmac256(key, sizeof(key), in, pl + 1, seed);
        kdf256(seed, "SAE Hunting and Pecking", prime, sizeof(prime), 256, x);
        ok = !mbedtls_mpi_read_binary(&c->a, x, sizeof(x));
        bool below = ok && mbedtls_mpi_cmp_mpi(&c->a, &c->grp.P) < 0;
        /* y^2 = x^3 - 3x + b mod p */
        ok = ok && !mbedtls_mpi_mul_mpi(&c->b, &c->a, &c->a) && !mbedtls_mpi_mod_mpi(&c->b, &c->b, &c->grp.P) &&
             !mbedtls_mpi_sub_int(&c->b, &c->b, 3) && !mbedtls_mpi_mul_mpi(&c->b, &c->b, &c->a) &&
             !mbedtls_mpi_add_mpi(&c->b, &c->b, &c->grp.B) && !mbedtls_mpi_mod_mpi(&c->b, &c->b, &c->grp.P) &&
             !mbedtls_mpi_exp_mod(&c->t, &c->b, &pm1, &c->grp.P, NULL);
        bool square = ok && below && mbedtls_mpi_cmp_int(&c->t, 1) == 0;
        /* (the first one is kept; later rounds still run) */
        UINT8 take = (UINT8)(square && !found);
        UINT8 mask = (UINT8)-take;
        for (int i = 0; i < 32; i++) found_x[i] = (UINT8)((found_x[i] & ~mask) | (x[i] & mask));
        found_lsb = (UINT8)((found_lsb & ~mask) | ((seed[31] & 1) & mask));
        found |= take;
    }
    memset(seed, 0, sizeof(seed));
    memset(x, 0, sizeof(x));
    memset(in, 0, sizeof(in));
    /* y = (x^3 + ax + b)^((p + 1) / 4): p is 3 mod 4; the root with pwd-seed's parity */
    ok = ok && found && !mbedtls_mpi_read_binary(&c->a, found_x, 32) &&
         !mbedtls_mpi_mul_mpi(&c->b, &c->a, &c->a) && !mbedtls_mpi_mod_mpi(&c->b, &c->b, &c->grp.P) &&
         !mbedtls_mpi_sub_int(&c->b, &c->b, 3) && !mbedtls_mpi_mul_mpi(&c->b, &c->b, &c->a) &&
         !mbedtls_mpi_add_mpi(&c->b, &c->b, &c->grp.B) && !mbedtls_mpi_mod_mpi(&c->b, &c->b, &c->grp.P) &&
         !mbedtls_mpi_add_int(&pm1, &c->grp.P, 1) && !mbedtls_mpi_shift_r(&pm1, 2) &&
         !mbedtls_mpi_exp_mod(&c->t, &c->b, &pm1, &c->grp.P, NULL);
    if (ok && (UINT8)mbedtls_mpi_get_bit(&c->t, 0) != found_lsb) ok = !mbedtls_mpi_sub_mpi(&c->t, &c->grp.P, &c->t);
    ok = ok && !mbedtls_mpi_copy(&c->P.X, &c->a) && !mbedtls_mpi_copy(&c->P.Y, &c->t) && !mbedtls_mpi_lset(&c->P.Z, 1);
    memset(found_x, 0, sizeof(found_x));
    mbedtls_mpi_free(&pm1);
    return ok;
}

bool SaeCommit(SaeState *s, const char *password, const UINT8 sta[6], const UINT8 ap[6])
{
    UINT8 token[SAE_TOKEN_MAX], token_len = s->token_len;
    memcpy(token, s->token, token_len);
    SaeClear(s);
    memcpy(s->token, token, s->token_len = token_len);
    Ctx c;
    bool ok = ctx_init(&c);
    s->why = "the password gives no password element";
    ok = ok && derive_pwe(&c, password, sta, ap) && point_write(&c.P, s->pwe);
    /* rand and mask (c.a, c.b); scalar = rand + mask mod r, which must be more than 1 */
    for (int tries = 0; ok && tries < 8; tries++) {
        ok = random_scalar(&c, &c.a) && random_scalar(&c, &c.b) && !mbedtls_mpi_add_mpi(&c.t, &c.a, &c.b) &&
             !mbedtls_mpi_mod_mpi(&c.t, &c.t, &c.grp.N);
        if (ok && mbedtls_mpi_cmp_int(&c.t, 1) > 0) break;
    }
    /* element = -(mask * PWE) */
    ok = ok && !mbedtls_ecp_mul(&c.grp, &c.Q, &c.b, &c.P, rng, NULL) &&
         !mbedtls_mpi_sub_mpi(&c.Q.Y, &c.grp.P, &c.Q.Y) &&
         !mbedtls_mpi_write_binary(&c.t, s->scalar, SAE_PRIME_LEN) && point_write(&c.Q, s->element) &&
         !mbedtls_mpi_write_binary(&c.a, s->rand, SAE_PRIME_LEN);
    ctx_free(&c);
    if (ok) s->why = NULL;
    return ok;
}

bool SaeProcessCommit(SaeState *s, const UINT8 *scalar, const UINT8 *element)
{
    Ctx c;
    bool ok = ctx_init(&c);
    s->have_keys = false;
    /* the peer's scalar in (1, r), its element on the curve, neither the station's own reflected */
    s->why = "the access point's scalar is out of range";
    ok = ok && !mbedtls_mpi_read_binary(&c.u, scalar, SAE_PRIME_LEN) && mbedtls_mpi_cmp_int(&c.u, 1) > 0 &&
         mbedtls_mpi_cmp_mpi(&c.u, &c.grp.N) < 0;
    if (ok) s->why = "the access point's element is not on the curve";
    ok = ok && point_read(&c, &c.Q, element);
    if (ok && !memcmp(scalar, s->scalar, SAE_PRIME_LEN) && !memcmp(element, s->element, 2 * SAE_PRIME_LEN)) {
        s->why = "the access point sent the station's own commit back (a reflection)";
        ok = false;
    }
    /* K = rand * (peer-scalar * PWE + PEER-ELEMENT); its x is k */
    if (ok) s->why = "the shared secret could not be computed";
    UINT8 k[32], keyseed[32], ctx[32], keys[64], zero[32] = { 0 };
    ok = ok && point_read(&c, &c.P, s->pwe) && !mbedtls_ecp_mul(&c.grp, &c.R, &c.u, &c.P, rng, NULL) &&
         !mbedtls_mpi_lset(&c.a, 1) && !mbedtls_ecp_muladd(&c.grp, &c.R, &c.a, &c.R, &c.a, &c.Q) &&
         !mbedtls_ecp_is_zero(&c.R) && !mbedtls_mpi_read_binary(&c.b, s->rand, SAE_PRIME_LEN) &&
         !mbedtls_ecp_mul(&c.grp, &c.R, &c.b, &c.R, rng, NULL) && !mbedtls_ecp_is_zero(&c.R) &&
         !mbedtls_mpi_write_binary(&c.R.X, k, sizeof(k));
    /* (scalar + peer-scalar) mod r */
    ok = ok && !mbedtls_mpi_read_binary(&c.t, s->scalar, SAE_PRIME_LEN) && !mbedtls_mpi_add_mpi(&c.t, &c.t, &c.u) &&
         !mbedtls_mpi_mod_mpi(&c.t, &c.t, &c.grp.N) && !mbedtls_mpi_write_binary(&c.t, ctx, sizeof(ctx));
    if (ok) {
        hmac256(zero, sizeof(zero), k, sizeof(k), keyseed);
        kdf256(keyseed, "SAE KCK and PMK", ctx, sizeof(ctx), 512, keys);
        memcpy(s->kck, keys, 32);
        memcpy(s->pmk, keys + 32, 32);
        memcpy(s->pmkid, ctx, SAE_PMKID_LEN);
        memcpy(s->peer_scalar, scalar, SAE_PRIME_LEN);
        memcpy(s->peer_element, element, 2 * SAE_PRIME_LEN);
        s->have_keys = true;
        s->why = NULL;
    }
    memset(k, 0, sizeof(k));
    memset(keyseed, 0, sizeof(keyseed));
    memset(keys, 0, sizeof(keys));
    ctx_free(&c);
    return ok;
}

/* HMAC-SHA256(KCK, send-confirm || scalar || element || peer-scalar || peer-element), from @mine's side */
static void confirm_of(const SaeState *s, UINT16 sc, bool mine, UINT8 out[32])
{
    UINT8 in[2 + 2 * (SAE_PRIME_LEN + 2 * SAE_PRIME_LEN)];
    const UINT32 one = SAE_PRIME_LEN + 2 * SAE_PRIME_LEN;
    in[0] = (UINT8)sc;
    in[1] = (UINT8)(sc >> 8);
    UINT8 *a = in + 2, *b = in + 2 + one;
    memcpy(mine ? a : b, s->scalar, SAE_PRIME_LEN);
    memcpy((mine ? a : b) + SAE_PRIME_LEN, s->element, 2 * SAE_PRIME_LEN);
    memcpy(mine ? b : a, s->peer_scalar, SAE_PRIME_LEN);
    memcpy((mine ? b : a) + SAE_PRIME_LEN, s->peer_element, 2 * SAE_PRIME_LEN);
    hmac256(s->kck, sizeof(s->kck), in, sizeof(in), out);
}

void SaeConfirm(SaeState *s, UINT16 *send_confirm, UINT8 confirm[32])
{
    if (s->send_confirm < 0xFFFF) s->send_confirm++;
    *send_confirm = s->send_confirm;
    confirm_of(s, s->send_confirm, true, confirm);
}

bool SaeVerifyConfirm(SaeState *s, UINT16 send_confirm, const UINT8 confirm[32])
{
    UINT8 want[32], d = 0;
    if (!s->have_keys) { s->why = "a confirm before the commits"; return false; }
    confirm_of(s, send_confirm, false, want);
    for (int i = 0; i < 32; i++) d |= (UINT8)(want[i] ^ confirm[i]);
    memset(want, 0, sizeof(want));
    s->why = d ? "the access point's confirm is wrong (not the same password)" : NULL;
    return d == 0;
}

void SaeClear(SaeState *s)
{
    memset(s, 0, sizeof(*s));
}

/* ---- the frames a driver passes up ---- */
static UINT16 le16(const UINT8 *p) { return (UINT16)(p[0] | p[1] << 8); }

/* Past a MAC header (an authentication frame's: frame control 0xB0) and the
 * authentication header (algorithm 3, sequence @seq, status), if there */
static const UINT8 *skip_headers(const UINT8 *f, UINT32 *n, UINT16 seq, UINT32 bare_len, SaeFrame *out)
{
    if (*n >= 24 + 6 && f[0] == 0xB0 && f[1] == 0) { f += 24; *n -= 24; }
    if (*n >= 6 && *n != bare_len && le16(f) == 3 && le16(f + 2) == seq) {
        out->seq = seq;
        out->status = le16(f + 4);
        f += 6;
        *n -= 6;
    }
    return f;
}

bool SaeParseCommit(const UINT8 *f, UINT32 n, SaeFrame *out)
{
    memset(out, 0, sizeof(*out));
    f = skip_headers(f, &n, 1, 2 + 3 * SAE_PRIME_LEN, out);
    if (out->status == SAE_STATUS_ANTI_CLOGGING) {      /* (its group, then the token) */
        if (n < 2) return false;
        out->group = le16(f);
        out->token = f + 2;
        out->token_len = n - 2;
        return true;
    }
    if (out->status) return true;                       /* (a refusal: the caller reads its status) */
    if (n < 2) return false;
    out->group = le16(f);
    if (out->group != SAE_GROUP) return true;           /* (the caller refuses the group) */
    if (n < 2 + 3 * SAE_PRIME_LEN) return false;
    out->scalar = f + 2;
    out->element = f + 2 + SAE_PRIME_LEN;
    return true;
}

bool SaeParseConfirm(const UINT8 *f, UINT32 n, SaeFrame *out)
{
    memset(out, 0, sizeof(*out));
    f = skip_headers(f, &n, 2, 2 + 32, out);
    if (out->status) return true;
    if (n < 2 + 32) return false;
    out->send_confirm = le16(f);
    out->confirm = f + 2;
    return true;
}
