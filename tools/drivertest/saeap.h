/*
 * saeap.h — the access point's half of SAE (WPA3-Personal's password
 * authentication, IEEE 802.11-2020 12.4) for wditest.sys
 * (tools/drivertest/wditest.c): NIST P-256 (group 19), hunting and
 * pecking, the commit, the shared secret's KCK and PMK, and the confirm
 *
 * Written apart from the kernel's station side (kernel/nt/sae.c, over Mbed
 * TLS) on purpose, as wpaap.h is for the 4-way handshake: the connection
 * only completes when two implementations agree.  Its own arithmetic:
 * 256-bit numbers as eight 32-bit limbs, Montgomery multiplication modulo
 * p, Jacobian points, double-and-add.  None of it is constant-time: it is
 * a test access point's, whose secrets are not secret.  Freestanding C, no
 * imports (tests/host/sae_handshake.c runs it on the host).
 */
#pragma once

#include "wpaap.h"

#define SAEAP_LEN 32

typedef struct { u32 v[8]; } Fe;            /* (limb 0 least significant) */
typedef struct { Fe x, y, z; } Jp;          /* (Jacobian, Montgomery form; z = 0: the point at infinity) */

static const Fe P256_P = { { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 0, 1, 0xFFFFFFFF } };
static const Fe P256_N = { { 0xFC632551, 0xF3B9CAC2, 0xA7179E84, 0xBCE6FAAD, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0xFFFFFFFF } };
static const Fe P256_B = { { 0x27D2604B, 0x3BCE3C3E, 0xCC53B0F6, 0x651D06B0, 0x769886BC, 0xB3EBBD55, 0xAA3A93E7,
                             0x5AC635D8 } };

static void fe_load(Fe *r, const u8 *b)     /* (32 bytes, big-endian) */
{
    for (int i = 0; i < 8; i++)
        r->v[i] = (u32)b[31 - 4 * i] | (u32)b[30 - 4 * i] << 8 | (u32)b[29 - 4 * i] << 16 | (u32)b[28 - 4 * i] << 24;
}

static void fe_store(const Fe *a, u8 *b)
{
    for (int i = 0; i < 8; i++) {
        b[31 - 4 * i] = (u8)a->v[i];
        b[30 - 4 * i] = (u8)(a->v[i] >> 8);
        b[29 - 4 * i] = (u8)(a->v[i] >> 16);
        b[28 - 4 * i] = (u8)(a->v[i] >> 24);
    }
}

static int fe_cmp(const Fe *a, const Fe *b)
{
    for (int i = 7; i >= 0; i--)
        if (a->v[i] != b->v[i]) return a->v[i] < b->v[i] ? -1 : 1;
    return 0;
}

static int fe_is_zero(const Fe *a)
{
    u32 d = 0;
    for (int i = 0; i < 8; i++) d |= a->v[i];
    return !d;
}

/* r = a + b mod m, r = a - b mod m (a, b below m) */
static void fe_add(Fe *r, const Fe *a, const Fe *b, const Fe *m)
{
    u64 c = 0;
    Fe t;
    for (int i = 0; i < 8; i++) { c += (u64)a->v[i] + b->v[i]; t.v[i] = (u32)c; c >>= 32; }
    if (c || fe_cmp(&t, m) >= 0) {
        u64 br = 0;
        for (int i = 0; i < 8; i++) { u64 d = (u64)t.v[i] - m->v[i] - br; t.v[i] = (u32)d; br = d >> 63; }
    }
    *r = t;
}

static void fe_sub(Fe *r, const Fe *a, const Fe *b, const Fe *m)
{
    u64 br = 0;
    Fe t;
    for (int i = 0; i < 8; i++) { u64 d = (u64)a->v[i] - b->v[i] - br; t.v[i] = (u32)d; br = d >> 63; }
    if (br) {
        u64 c = 0;
        for (int i = 0; i < 8; i++) { c += (u64)t.v[i] + m->v[i]; t.v[i] = (u32)c; c >>= 32; }
    }
    *r = t;
}

/* Montgomery multiplication modulo p: r = a * b / 2^256 mod p (p's -p^-1 mod 2^32 is 1) */
static void fe_mul(Fe *r, const Fe *a, const Fe *b)
{
    u32 t[10];
    wpa_zero(t, sizeof(t));
    for (int i = 0; i < 8; i++) {
        u64 c = 0;
        for (int j = 0; j < 8; j++) { c += (u64)t[j] + (u64)a->v[j] * b->v[i]; t[j] = (u32)c; c >>= 32; }
        c += t[8];
        t[8] = (u32)c;
        t[9] = (u32)(c >> 32);
        u32 q = t[0];
        c = ((u64)t[0] + (u64)q * P256_P.v[0]) >> 32;
        for (int j = 1; j < 8; j++) { c += (u64)t[j] + (u64)q * P256_P.v[j]; t[j - 1] = (u32)c; c >>= 32; }
        c += t[8];
        t[7] = (u32)c;
        t[8] = t[9] + (u32)(c >> 32);
        t[9] = 0;
    }
    Fe x;
    wpa_copy(x.v, t, 32);
    if (t[8] || fe_cmp(&x, &P256_P) >= 0) {
        u64 br = 0;
        for (int i = 0; i < 8; i++) { u64 d = (u64)x.v[i] - P256_P.v[i] - br; x.v[i] = (u32)d; br = d >> 63; }
    }
    *r = x;
}

/* 2^512 mod p (to bring a number into Montgomery form), and 1 in it */
static void fe_consts(Fe *r2, Fe *one)
{
    Fe x;
    wpa_zero(&x, sizeof(x));
    x.v[0] = 1;
    for (int i = 0; i < 512; i++) fe_add(&x, &x, &x, &P256_P);
    *r2 = x;
    wpa_zero(&x, sizeof(x));
    x.v[0] = 1;
    fe_mul(one, &x, r2);
}

static void fe_to(Fe *r, const Fe *a)       /* (into Montgomery form) */
{
    Fe r2, one;
    fe_consts(&r2, &one);
    fe_mul(r, a, &r2);
}

static void fe_from(Fe *r, const Fe *a)     /* (out of it) */
{
    Fe one;
    wpa_zero(&one, sizeof(one));
    one.v[0] = 1;
    fe_mul(r, a, &one);
}

/* r = a^e (Montgomery form in and out; @e a plain number) */
static void fe_pow(Fe *r, const Fe *a, const Fe *e)
{
    Fe r2, x;
    fe_consts(&r2, &x);
    for (int i = 255; i >= 0; i--) {
        fe_mul(&x, &x, &x);
        if (e->v[i / 32] >> (i % 32) & 1) fe_mul(&x, &x, a);
    }
    *r = x;
}

static void fe_inv(Fe *r, const Fe *a)
{
    Fe e = P256_P;
    e.v[0] -= 2;
    fe_pow(r, a, &e);
}

/* y^2 for x on the curve: x^3 - 3x + b (Montgomery form) */
static void curve_rhs(Fe *r, const Fe *x)
{
    Fe t, b, three = *x;
    fe_mul(&t, x, x);
    fe_mul(&t, &t, x);
    fe_add(&three, &three, x, &P256_P);
    fe_add(&three, &three, x, &P256_P);
    fe_sub(&t, &t, &three, &P256_P);
    fe_to(&b, &P256_B);
    fe_add(r, &t, &b, &P256_P);
}

/* ---- points ---- */
static void jp_double(Jp *r, const Jp *p)
{
    if (fe_is_zero(&p->z)) { *r = *p; return; }
    Fe delta, gamma, beta, alpha, t, u, x3, y3, z3;
    fe_mul(&delta, &p->z, &p->z);
    fe_mul(&gamma, &p->y, &p->y);
    fe_mul(&beta, &p->x, &gamma);
    fe_sub(&t, &p->x, &delta, &P256_P);
    fe_add(&u, &p->x, &delta, &P256_P);
    fe_mul(&alpha, &t, &u);
    fe_add(&t, &alpha, &alpha, &P256_P);
    fe_add(&alpha, &t, &alpha, &P256_P);                    /* alpha = 3 (x - delta)(x + delta) */
    fe_mul(&x3, &alpha, &alpha);
    fe_add(&t, &beta, &beta, &P256_P);
    fe_add(&t, &t, &t, &P256_P);                            /* 4 beta */
    fe_sub(&x3, &x3, &t, &P256_P);
    fe_sub(&x3, &x3, &t, &P256_P);                          /* x3 = alpha^2 - 8 beta */
    fe_add(&z3, &p->y, &p->z, &P256_P);
    fe_mul(&z3, &z3, &z3);
    fe_sub(&z3, &z3, &gamma, &P256_P);
    fe_sub(&z3, &z3, &delta, &P256_P);                      /* z3 = (y + z)^2 - gamma - delta */
    fe_sub(&t, &t, &x3, &P256_P);
    fe_mul(&y3, &alpha, &t);
    fe_mul(&u, &gamma, &gamma);
    fe_add(&u, &u, &u, &P256_P);
    fe_add(&u, &u, &u, &P256_P);
    fe_add(&u, &u, &u, &P256_P);                            /* 8 gamma^2 */
    fe_sub(&y3, &y3, &u, &P256_P);                          /* y3 = alpha (4 beta - x3) - 8 gamma^2 */
    r->x = x3; r->y = y3; r->z = z3;
}

static void jp_add(Jp *r, const Jp *p, const Jp *q)
{
    if (fe_is_zero(&p->z)) { *r = *q; return; }
    if (fe_is_zero(&q->z)) { *r = *p; return; }
    Fe z1z1, z2z2, u1, u2, s1, s2, h, i, j, rr, v, t, x3, y3, z3;
    fe_mul(&z1z1, &p->z, &p->z);
    fe_mul(&z2z2, &q->z, &q->z);
    fe_mul(&u1, &p->x, &z2z2);
    fe_mul(&u2, &q->x, &z1z1);
    fe_mul(&s1, &p->y, &q->z);
    fe_mul(&s1, &s1, &z2z2);
    fe_mul(&s2, &q->y, &p->z);
    fe_mul(&s2, &s2, &z1z1);
    fe_sub(&h, &u2, &u1, &P256_P);
    fe_sub(&rr, &s2, &s1, &P256_P);
    if (fe_is_zero(&h)) {
        if (fe_is_zero(&rr)) { jp_double(r, p); return; }
        wpa_zero(r, sizeof(*r));                            /* (p = -q) */
        return;
    }
    fe_add(&rr, &rr, &rr, &P256_P);
    fe_add(&i, &h, &h, &P256_P);
    fe_mul(&i, &i, &i);
    fe_mul(&j, &h, &i);
    fe_mul(&v, &u1, &i);
    fe_mul(&x3, &rr, &rr);
    fe_sub(&x3, &x3, &j, &P256_P);
    fe_sub(&x3, &x3, &v, &P256_P);
    fe_sub(&x3, &x3, &v, &P256_P);                          /* x3 = r^2 - J - 2V */
    fe_sub(&t, &v, &x3, &P256_P);
    fe_mul(&y3, &rr, &t);
    fe_mul(&t, &s1, &j);
    fe_add(&t, &t, &t, &P256_P);
    fe_sub(&y3, &y3, &t, &P256_P);                          /* y3 = r (V - x3) - 2 s1 J */
    fe_add(&z3, &p->z, &q->z, &P256_P);
    fe_mul(&z3, &z3, &z3);
    fe_sub(&z3, &z3, &z1z1, &P256_P);
    fe_sub(&z3, &z3, &z2z2, &P256_P);
    fe_mul(&z3, &z3, &h);                                   /* z3 = ((z1 + z2)^2 - z1z1 - z2z2) H */
    r->x = x3; r->y = y3; r->z = z3;
}

static void jp_mul(Jp *r, const Fe *k, const Jp *p)        /* (@k a plain number) */
{
    Jp acc;
    wpa_zero(&acc, sizeof(acc));
    for (int i = 255; i >= 0; i--) {
        jp_double(&acc, &acc);
        if (k->v[i / 32] >> (i % 32) & 1) jp_add(&acc, &acc, p);
    }
    *r = acc;
}

/* A point from its affine coordinates (64 bytes, big-endian x then y): 0
 * if either is not below p or it is not on the curve */
static int jp_load(Jp *r, const u8 *xy)
{
    Fe x, y, l, rhs, one, r2;
    fe_load(&x, xy);
    fe_load(&y, xy + 32);
    if (fe_cmp(&x, &P256_P) >= 0 || fe_cmp(&y, &P256_P) >= 0) return 0;
    fe_to(&r->x, &x);
    fe_to(&r->y, &y);
    fe_consts(&r2, &one);
    r->z = one;
    fe_mul(&l, &r->y, &r->y);
    curve_rhs(&rhs, &r->x);
    return fe_cmp(&l, &rhs) == 0;
}

/* Its affine coordinates; 0 at infinity */
static int jp_store(const Jp *p, u8 *xy)
{
    if (fe_is_zero(&p->z)) return 0;
    Fe zi, zi2, x, y;
    fe_inv(&zi, &p->z);
    fe_mul(&zi2, &zi, &zi);
    fe_mul(&x, &p->x, &zi2);
    fe_mul(&zi2, &zi2, &zi);
    fe_mul(&y, &p->y, &zi2);
    fe_from(&x, &x);
    fe_from(&y, &y);
    fe_store(&x, xy);
    fe_store(&y, xy + 32);
    return 1;
}

/* ---- SAE ---- */
typedef struct {
    u8  pwe[64];
    u8  rand[32], scalar[32], element[64];
    u8  peer_scalar[32], peer_element[64];
    u8  kck[32], pmk[32], pmkid[16];
    const char *why;
} SaeAp;

/* The password element for @pw between @a and @b (12.4.4.2.2, hunting and
 * pecking: the first of 40 rounds whose x is on the curve; this side does
 * not hide which) */
static int saeap_pwe(SaeAp *s, const char *pw, const u8 a[6], const u8 b[6])
{
    u8 key[12], in[80], seed[32], val[32], prime[32];
    unsigned int pl = 0;
    while (pw[pl]) pl++;
    if (pl + 1 > sizeof(in)) return 0;
    int a_first = less(b, a, 6);                            /* (max(a, b) || min(a, b)) */
    wpa_copy(key, a_first ? a : b, 6);
    wpa_copy(key + 6, a_first ? b : a, 6);
    wpa_copy(in, pw, pl);
    fe_store(&P256_P, prime);
    Fe half = P256_P, quarter = P256_P;                     /* (p - 1) / 2, (p + 1) / 4 */
    for (int i = 0; i < 8; i++) half.v[i] = P256_P.v[i] >> 1 | (i < 7 ? P256_P.v[i + 1] << 31 : 0);
    Fe one;
    wpa_zero(&one, sizeof(one));
    one.v[0] = 1;
    fe_add(&quarter, &half, &one, &P256_P);                 /* ((p - 1) / 2 + 1) / 2 = (p + 1) / 4 */
    for (int i = 0; i < 8; i++) quarter.v[i] = quarter.v[i] >> 1 | (i < 7 ? quarter.v[i + 1] << 31 : 0);
    for (int counter = 1; counter <= 40; counter++) {
        in[pl] = (u8)counter;
        ap_hmac256(key, 12, in, pl + 1, 0, 0, seed);
        ap_kdf256(seed, 32, "SAE Hunting and Pecking", prime, 32, 256, val);
        Fe x, xm, rhs, l, y;
        fe_load(&x, val);
        if (fe_cmp(&x, &P256_P) >= 0) continue;
        fe_to(&xm, &x);
        curve_rhs(&rhs, &xm);
        fe_pow(&l, &rhs, &half);
        fe_from(&l, &l);
        if (fe_cmp(&l, &one)) continue;                     /* (not a square) */
        fe_pow(&y, &rhs, &quarter);
        fe_from(&y, &y);
        if ((y.v[0] & 1) != (seed[31] & 1u)) fe_sub(&y, &P256_P, &y, &P256_P);
        fe_store(&x, s->pwe);
        fe_store(&y, s->pwe + 32);
        return 1;
    }
    return 0;
}

/* The commit from @rand and @mask (32 bytes each, below r): scalar = rand
 * + mask mod r, element = -(mask * PWE) */
static int saeap_commit(SaeAp *s, const u8 rand[32], const u8 mask[32])
{
    Fe r, m, sc;
    Jp pwe, e;
    fe_load(&r, rand);
    fe_load(&m, mask);
    fe_add(&sc, &r, &m, &P256_N);
    if (!jp_load(&pwe, s->pwe)) return 0;
    jp_mul(&e, &m, &pwe);
    fe_sub(&e.y, &P256_P, &e.y, &P256_P);
    wpa_copy(s->rand, rand, 32);
    fe_store(&sc, s->scalar);
    return jp_store(&e, s->element);
}

/* The station's commit checked and the keys derived from it: K = rand *
 * (peer-scalar * PWE + PEER-ELEMENT), keyseed = HMAC-SHA256(0, x(K)),
 * KCK || PMK = KDF-512(keyseed, "SAE KCK and PMK", (scalar + peer-scalar) mod r) */
static int saeap_process(SaeAp *s, const u8 scalar[32], const u8 element[64])
{
    Fe ps, r, sum, one;
    Jp pwe, pe, k;
    fe_load(&ps, scalar);
    wpa_zero(&one, sizeof(one));
    one.v[0] = 1;
    s->why = "the station's scalar is out of range";
    if (fe_cmp(&ps, &one) <= 0 || fe_cmp(&ps, &P256_N) >= 0) return 0;
    s->why = "the station's element is not on the curve";
    if (!jp_load(&pe, element)) return 0;
    s->why = "the station's commit is this access point's own";
    int same_s = 1;
    for (int i = 0; i < 32; i++) same_s &= scalar[i] == s->scalar[i];
    for (int i = 0; i < 64; i++) same_s &= element[i] == s->element[i];
    if (same_s) return 0;
    jp_load(&pwe, s->pwe);
    jp_mul(&k, &ps, &pwe);
    jp_add(&k, &k, &pe);
    fe_load(&r, s->rand);
    jp_mul(&k, &r, &k);
    u8 xy[64], keyseed[32], zero[32], ctx[32], keys[64];
    s->why = "the shared secret is the point at infinity";
    if (!jp_store(&k, xy)) return 0;
    wpa_zero(zero, 32);
    ap_hmac256(zero, 32, xy, 32, 0, 0, keyseed);
    fe_load(&r, s->scalar);
    fe_add(&sum, &r, &ps, &P256_N);
    fe_store(&sum, ctx);
    ap_kdf256(keyseed, 32, "SAE KCK and PMK", ctx, 32, 512, keys);
    wpa_copy(s->kck, keys, 32);
    wpa_copy(s->pmk, keys + 32, 32);
    wpa_copy(s->pmkid, ctx, 16);
    wpa_copy(s->peer_scalar, scalar, 32);
    wpa_copy(s->peer_element, element, 64);
    s->why = 0;
    return 1;
}

/* A confirm (12.4.5.5): HMAC-SHA256(KCK, send-confirm || scalar || element
 * || peer-scalar || peer-element), @mine the sender's side first */
static void saeap_confirm(const SaeAp *s, unsigned int sc, int mine, u8 out[32])
{
    u8 in[2 + 2 * 96];
    in[0] = (u8)sc;
    in[1] = (u8)(sc >> 8);
    u8 *a = in + 2 + (mine ? 0 : 96), *b = in + 2 + (mine ? 96 : 0);
    wpa_copy(a, s->scalar, 32);
    wpa_copy(a + 32, s->element, 64);
    wpa_copy(b, s->peer_scalar, 32);
    wpa_copy(b + 32, s->peer_element, 64);
    ap_hmac256(s->kck, 32, in, sizeof(in), 0, 0, out);
}

/* Whether the station's confirm proves the same password */
static int saeap_verify(SaeAp *s, unsigned int sc, const u8 confirm[32])
{
    u8 want[32], d = 0;
    saeap_confirm(s, sc, 0, want);
    for (int i = 0; i < 32; i++) d |= want[i] ^ confirm[i];
    s->why = d ? "the station's confirm is wrong (not the password)" : 0;
    return !d;
}
