/*
 * ci.c — code integrity for kernel-mode drivers: the job Windows' ci.dll
 * does when a driver loads (step 30.7, docs/design/windows-drivers.md)
 *
 * A driver image is signed in one of two ways, as on Windows:
 *
 *   embedded   an Authenticode signature in the image's certificate table
 *              (a PKCS #7 SignedData over an SpcIndirectDataContent that
 *              carries the image's Authenticode digest)
 *   catalog    the image's Authenticode digest listed in a signed catalog
 *              (a PKCS #7 SignedData over a certificate trust list) that
 *              setupapi installed in CatRoot when the package was staged
 *
 * Either way the image's digest is recomputed here, so an image changed
 * on disk after it was signed or installed no longer matches and is
 * refused.  Whose signature counts is the policy Windows 11 x64 has with
 * Secure Boot on:
 *
 *   - normally only Microsoft's driver signatures: the signer's chain ends
 *     at a Microsoft product root (ci_roots.c) and the signing certificate
 *     is one for drivers (WHQL, attestation, Windows components).  The
 *     certificates' validity periods are not checked, as Windows does not
 *     for drivers;
 *   - with test signing on, any intact signature (a test certificate whose
 *     root nobody trusts, as makecert's are);
 *   - an unsigned image never, test signing or not.
 *
 * Only Mbed TLS (third_party/mbedtls: X.509, RSA and ECDSA, the hashes) is
 * used; the PKCS #7 and catalog structures are read here.
 */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS              /* (a certificate's signature fields) */
#include "ci.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "mbedtls/md.h"
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"

extern const UINT8 g_ci_roots_der[];
extern const UINT32 g_ci_roots_len[];
extern const int g_ci_nroots;

typedef struct { const UINT8 *p; size_t n; } Span;

static bool tlv(const UINT8 **pp, const UINT8 *end, UINT8 *tag, Span *val, Span *all)
{
    const UINT8 *p = *pp;
    if (end - p < 2) return false;
    UINT8 t = p[0];
    size_t n = p[1], h = 2;
    if (n & 0x80) {
        size_t k = n & 0x7F;
        if (!k || k > 4 || (size_t)(end - p) < 2 + k) return false;
        n = 0;
        for (size_t i = 0; i < k; i++) n = (n << 8) | p[2 + i];
        h += k;
    }
    if (n > (size_t)(end - p) - h) return false;
    if (tag) *tag = t;
    if (val) *val = (Span){ p + h, n };
    if (all) *all = (Span){ p, h + n };
    *pp = p + h + n;
    return true;
}

static bool expect(const UINT8 **pp, const UINT8 *end, UINT8 want, Span *val, Span *all)
{
    const UINT8 *p = *pp;
    UINT8 t;
    if (!tlv(&p, end, &t, val, all) || t != want) return false;
    *pp = p;
    return true;
}

static bool oid_is(Span oid, const char *der, size_t n) { return oid.n == n && !memcmp(oid.p, der, n); }
#define OID_IS(s, o) oid_is(s, o, sizeof(o) - 1)

#define OID_SIGNED_DATA     MBEDTLS_OID_PKCS7 "\x02"
#define OID_SPC_INDIRECT    "\x2B\x06\x01\x04\x01\x82\x37\x02\x01\x04"     /* 1.3.6.1.4.1.311.2.1.4 */
#define OID_CTL             "\x2B\x06\x01\x04\x01\x82\x37\x0A\x01"         /* 1.3.6.1.4.1.311.10.1 */
#define OID_CONTENT_TYPE    MBEDTLS_OID_PKCS9 "\x03"
#define OID_MESSAGE_DIGEST  MBEDTLS_OID_PKCS9 "\x04"

/* The signing certificate's extended key usages that make a Microsoft
 * signature a driver signature */
static const struct { const char *oid; size_t n; } k_driver_eku[] = {
    { "\x2B\x06\x01\x04\x01\x82\x37\x0A\x03\x05", 10 },        /* Windows Hardware Driver Verification (WHQL) */
    { "\x2B\x06\x01\x04\x01\x82\x37\x0A\x03\x05\x01", 11 },    /* Windows Hardware Driver Attested Verification */
    { "\x2B\x06\x01\x04\x01\x82\x37\x0A\x03\x06", 10 },        /* Windows System Component Verification */
    { "\x2B\x06\x01\x04\x01\x82\x37\x0A\x03\x27", 10 },        /* Windows Hardware Driver Extended Verification */
    { "\x2B\x06\x01\x04\x01\x82\x37\x3D\x04\x01", 10 },        /* Early Launch Antimalware Driver */
};

/* -----------------------------------------------------------------------
 * The image's Authenticode digest
 * ----------------------------------------------------------------------- */
typedef struct { UINT32 cks, dirent, cert_off, cert_size; } Pe;

static bool pe_layout(const UINT8 *f, UINT32 n, Pe *pe)
{
    if (n < 64 || f[0] != 'M' || f[1] != 'Z') return false;
    UINT32 e = *(const UINT32 *)(f + 60);
    if ((UINT64)e + 24 + 112 + 40 > n || memcmp(f + e, "PE\0\0", 4)) return false;
    UINT16 magic = *(const UINT16 *)(f + e + 24);
    if (magic != 0x10B && magic != 0x20B) return false;
    pe->cks = e + 24 + 64;
    pe->dirent = e + 24 + (magic == 0x20B ? 112 : 96) + 4 * 8;
    pe->cert_off = *(const UINT32 *)(f + pe->dirent);
    pe->cert_size = *(const UINT32 *)(f + pe->dirent + 4);
    if (!pe->cert_off || !pe->cert_size || pe->cert_off < pe->dirent + 8 || (UINT64)pe->cert_off + pe->cert_size > n) {
        pe->cert_off = n;                               /* unsigned: all of it */
        pe->cert_size = 0;
    }
    return true;
}

/* @md's digest of everything before the certificate table but the
 * checksum and the table's directory entry (*@len its length) */
static bool pe_digest(const UINT8 *f, const Pe *pe, mbedtls_md_type_t md, UINT8 *out, size_t *len)
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(md);
    if (!info) return false;
    mbedtls_md_context_t c;
    mbedtls_md_init(&c);
    bool ok = !mbedtls_md_setup(&c, info, 0) && !mbedtls_md_starts(&c) &&
              !mbedtls_md_update(&c, f, pe->cks) &&
              !mbedtls_md_update(&c, f + pe->cks + 4, pe->dirent - pe->cks - 4) &&
              !mbedtls_md_update(&c, f + pe->dirent + 8, pe->cert_off - pe->dirent - 8) &&
              !mbedtls_md_finish(&c, out);
    mbedtls_md_free(&c);
    *len = mbedtls_md_get_size(info);
    return ok;
}

static bool alg_md(Span algid, mbedtls_md_type_t *md)
{
    const UINT8 *p = algid.p;
    Span oid;
    if (!expect(&p, algid.p + algid.n, 0x06, &oid, NULL)) return false;
    mbedtls_asn1_buf b = { MBEDTLS_ASN1_OID, oid.n, (unsigned char *)oid.p };
    return !mbedtls_oid_get_md_alg(&b, md);
}

/* -----------------------------------------------------------------------
 * PKCS #7 SignedData
 * ----------------------------------------------------------------------- */
typedef struct {
    Span ctype;                 /* the inner content's type (OID value) */
    Span content;               /* the inner content's value: what is digested */
    mbedtls_x509_crt certs;     /* the certificates sent with it */
    Span issuer, serial;        /* the (first) signer's certificate, by issuer and serial number */
    Span digest_alg, auth, sig;
    const mbedtls_x509_crt *signer;
} P7;

static bool p7_parse(const UINT8 *b, size_t n, P7 *m)
{
    memset(m, 0, sizeof(*m));
    mbedtls_x509_crt_init(&m->certs);
    const UINT8 *p = b, *end = b + n;
    Span ci, oid, x, sd;
    if (!expect(&p, end, 0x30, &ci, NULL)) return false;
    p = ci.p;
    end = ci.p + ci.n;
    if (!expect(&p, end, 0x06, &oid, NULL) || !OID_IS(oid, OID_SIGNED_DATA) || !expect(&p, end, 0xA0, &x, NULL)) return false;
    p = x.p;
    if (!expect(&p, x.p + x.n, 0x30, &sd, NULL)) return false;
    p = sd.p;
    end = sd.p + sd.n;
    Span inner, c;
    if (!expect(&p, end, 0x02, NULL, NULL) || !expect(&p, end, 0x31, NULL, NULL) || !expect(&p, end, 0x30, &inner, NULL))
        return false;
    const UINT8 *q = inner.p;
    if (!expect(&q, inner.p + inner.n, 0x06, &m->ctype, NULL) || !expect(&q, inner.p + inner.n, 0xA0, &c, NULL)) return false;
    q = c.p;
    if (!tlv(&q, c.p + c.n, NULL, &m->content, NULL)) return false;
    UINT8 t;
    if (p < end && *p == 0xA0) {                        /* certificates [0] IMPLICIT */
        Span certs;
        tlv(&p, end, NULL, &certs, NULL);
        for (const UINT8 *r = certs.p; r < certs.p + certs.n;) {
            Span one;
            if (!tlv(&r, certs.p + certs.n, &t, NULL, &one)) return false;
            if (t == 0x30) mbedtls_x509_crt_parse_der(&m->certs, one.p, one.n);   /* (ones Mbed TLS cannot read are left out) */
        }
    }
    if (p < end && *p == 0xA1) tlv(&p, end, NULL, NULL, NULL);     /* crls */
    Span sis, si;
    if (!expect(&p, end, 0x31, &sis, NULL)) return false;
    p = sis.p;
    if (!expect(&p, sis.p + sis.n, 0x30, &si, NULL)) return false;
    p = si.p;
    end = si.p + si.n;
    Span ias;
    if (!expect(&p, end, 0x02, NULL, NULL) || !expect(&p, end, 0x30, &ias, NULL)) return false;
    q = ias.p;
    if (!tlv(&q, ias.p + ias.n, &t, NULL, &m->issuer) || t != 0x30 || !expect(&q, ias.p + ias.n, 0x02, &m->serial, NULL))
        return false;
    if (!expect(&p, end, 0x30, &m->digest_alg, NULL)) return false;
    if (p >= end || *p != 0xA0) return false;           /* (Authenticode always has authenticated attributes) */
    tlv(&p, end, NULL, NULL, &m->auth);
    if (!expect(&p, end, 0x30, NULL, NULL) || !expect(&p, end, 0x04, &m->sig, NULL)) return false;
    for (const mbedtls_x509_crt *k = &m->certs; k && k->raw.p; k = k->next)
        if (k->issuer_raw.len == m->issuer.n && !memcmp(k->issuer_raw.p, m->issuer.p, m->issuer.n) &&
            k->serial.len == m->serial.n && !memcmp(k->serial.p, m->serial.p, m->serial.n))
            m->signer = k;
    return true;
}

static void p7_free(P7 *m) { mbedtls_x509_crt_free(&m->certs); }

/* An authenticated attribute's first value */
static bool auth_attr(const P7 *m, const char *oid, size_t oid_n, Span *val)
{
    const UINT8 *p = m->auth.p, *end = m->auth.p + m->auth.n;
    Span set;
    if (!tlv(&p, end, NULL, &set, NULL)) return false;
    for (p = set.p; p < set.p + set.n;) {
        Span a, o, vals;
        if (!expect(&p, set.p + set.n, 0x30, &a, NULL)) return false;
        const UINT8 *q = a.p;
        if (!expect(&q, a.p + a.n, 0x06, &o, NULL) || !expect(&q, a.p + a.n, 0x31, &vals, NULL)) return false;
        if (oid_is(o, oid, oid_n)) {
            q = vals.p;
            return tlv(&q, vals.p + vals.n, NULL, NULL, val);
        }
    }
    return false;
}

/* Whether the signer signed the content: the messageDigest attribute is
 * the content's digest and the signature over the attributes verifies */
static bool p7_signed(const P7 *m)
{
    mbedtls_md_type_t md;
    if (!m->signer || !alg_md(m->digest_alg, &md)) return false;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(md);
    UINT8 h[MBEDTLS_MD_MAX_SIZE];
    Span ct, dig;
    const UINT8 *p;
    Span v;
    if (!info || !auth_attr(m, OID_CONTENT_TYPE, sizeof(OID_CONTENT_TYPE) - 1, &ct) ||
        !auth_attr(m, OID_MESSAGE_DIGEST, sizeof(OID_MESSAGE_DIGEST) - 1, &dig)) return false;
    p = ct.p;
    if (!expect(&p, ct.p + ct.n, 0x06, &v, NULL) || v.n != m->ctype.n || memcmp(v.p, m->ctype.p, v.n)) return false;
    p = dig.p;
    if (!expect(&p, dig.p + dig.n, 0x04, &v, NULL)) return false;
    if (mbedtls_md(info, m->content.p, m->content.n, h) || v.n != mbedtls_md_get_size(info) || memcmp(v.p, h, v.n))
        return false;
    /* the signature is over the attributes' DER as a SET OF */
    mbedtls_md_context_t c;
    mbedtls_md_init(&c);
    static const UINT8 set_tag = 0x31;
    bool ok = !mbedtls_md_setup(&c, info, 0) && !mbedtls_md_starts(&c) && !mbedtls_md_update(&c, &set_tag, 1) &&
              !mbedtls_md_update(&c, m->auth.p + 1, m->auth.n - 1) && !mbedtls_md_finish(&c, h);
    mbedtls_md_free(&c);
    return ok && !mbedtls_pk_verify((mbedtls_pk_context *)&m->signer->pk, md, h, mbedtls_md_get_size(info),
                                    m->sig.p, m->sig.n);
}

/* -----------------------------------------------------------------------
 * Whose signature it is
 * ----------------------------------------------------------------------- */
static mbedtls_x509_crt g_roots;
static bool g_roots_ready;

static const mbedtls_x509_crt *roots(void)
{
    if (!g_roots_ready) {
        mbedtls_x509_crt_init(&g_roots);
        const UINT8 *p = g_ci_roots_der;
        for (int i = 0; i < g_ci_nroots; p += g_ci_roots_len[i], i++)
            if (mbedtls_x509_crt_parse_der_nocopy(&g_roots, p, g_ci_roots_len[i]))
                kprintf("[NT] code integrity: Microsoft root %d unreadable\n", i);
        g_roots_ready = true;
    }
    return g_roots.raw.p ? &g_roots : NULL;
}

static bool same(const mbedtls_x509_buf *a, const mbedtls_x509_buf *b) { return a->len == b->len && !memcmp(a->p, b->p, a->len); }

/* Whether @parent's key signed @child */
static bool signed_by(const mbedtls_x509_crt *child, const mbedtls_x509_crt *parent)
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(child->sig_md);
    UINT8 h[MBEDTLS_MD_MAX_SIZE];
    if (!info || !same(&child->issuer_raw, &parent->subject_raw) || mbedtls_md(info, child->tbs.p, child->tbs.len, h))
        return false;
    return !mbedtls_pk_verify_ext(child->sig_pk, child->sig_opts, (mbedtls_pk_context *)&parent->pk, child->sig_md, h,
                                  mbedtls_md_get_size(info), child->sig.p, child->sig.len);
}

typedef enum { TRUST_MICROSOFT, TRUST_TEST, TRUST_BROKEN } Trust;

/* Follow the signer's chain through the certificates sent with it: to a
 * Microsoft root, or to its end (a root nobody trusts, or an issuer not
 * sent) */
static Trust chain(const P7 *m)
{
    const mbedtls_x509_crt *cur = m->signer, *r = roots();
    for (int depth = 0; cur && depth < 8; depth++) {
        for (const mbedtls_x509_crt *k = r; k && k->raw.p; k = k->next)
            if (same(&cur->raw, &k->raw) || signed_by(cur, k)) goto microsoft;
        if (same(&cur->issuer_raw, &cur->subject_raw)) return TRUST_TEST;
        const mbedtls_x509_crt *up = NULL;
        for (const mbedtls_x509_crt *k = &m->certs; k && k->raw.p && !up; k = k->next)
            if (k != cur && same(&k->subject_raw, &cur->issuer_raw)) {
                if (!signed_by(cur, k)) return TRUST_BROKEN;
                up = k;
            }
        if (!up) return TRUST_TEST;
        cur = up;
    }
    return TRUST_BROKEN;

microsoft:
    if (!(m->signer->ext_types & MBEDTLS_X509_EXT_EXTENDED_KEY_USAGE)) return TRUST_TEST;
    for (unsigned i = 0; i < sizeof(k_driver_eku) / sizeof(k_driver_eku[0]); i++)
        if (!mbedtls_x509_crt_check_extended_key_usage(m->signer, k_driver_eku[i].oid, k_driver_eku[i].n)) return TRUST_MICROSOFT;
    return TRUST_TEST;                                  /* (Microsoft's, but not a driver signature) */
}

static void signer_name(const P7 *m, char *out, size_t cap)
{
    out[0] = 0;
    for (const mbedtls_x509_name *n = m->signer ? &m->signer->subject : NULL; n; n = n->next)
        if (!MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid)) {
            size_t k = n->val.len < cap - 1 ? n->val.len : cap - 1;
            memcpy(out, n->val.p, k);
            out[k] = 0;
        }
}

/* -----------------------------------------------------------------------
 * The two kinds of signature
 * ----------------------------------------------------------------------- */
typedef enum { SIG_NONE, SIG_MISMATCH, SIG_BROKEN, SIG_TEST, SIG_MICROSOFT } Verdict;

static Verdict judge(const P7 *m)
{
    if (!p7_signed(m)) return SIG_BROKEN;
    Trust t = chain(m);
    return t == TRUST_MICROSOFT ? SIG_MICROSOFT : t == TRUST_TEST ? SIG_TEST : SIG_BROKEN;
}

/* The embedded signature: the first PKCS #7 entry of the certificate table */
static Verdict embedded(const UINT8 *f, const Pe *pe, CiResult *res)
{
    for (UINT64 pos = pe->cert_off; pe->cert_size && pos + 8 <= (UINT64)pe->cert_off + pe->cert_size;) {
        UINT32 n = *(const UINT32 *)(f + pos);
        UINT16 kind = *(const UINT16 *)(f + pos + 6);
        if (n <= 8 || pos + n > (UINT64)pe->cert_off + pe->cert_size) return SIG_BROKEN;
        if (kind != 2) { pos += (n + 7) & ~7u; continue; }    /* (WIN_CERT_TYPE_PKCS_SIGNED_DATA) */
        P7 m;
        Verdict v = SIG_BROKEN;
        if (p7_parse(f + pos + 8, n - 8, &m) && OID_IS(m.ctype, OID_SPC_INDIRECT)) {
            /* SpcIndirectDataContent { SpcAttributeTypeAndOptionalValue, DigestInfo { alg, digest } } */
            const UINT8 *p = m.content.p, *end = m.content.p + m.content.n;
            Span di, alg, dig;
            mbedtls_md_type_t md;
            UINT8 h[MBEDTLS_MD_MAX_SIZE];
            size_t hl = 0;
            if (expect(&p, end, 0x30, NULL, NULL) && expect(&p, end, 0x30, &di, NULL)) {
                const UINT8 *q = di.p;
                if (expect(&q, di.p + di.n, 0x30, &alg, NULL) && expect(&q, di.p + di.n, 0x04, &dig, NULL) &&
                    alg_md(alg, &md) && pe_digest(f, pe, md, h, &hl))
                    v = hl == dig.n && !memcmp(h, dig.p, hl) ? judge(&m) : SIG_MISMATCH;
            }
            if (v >= SIG_TEST) { signer_name(&m, res->signer, sizeof(res->signer)); res->catalog[0] = 0; }
        }
        p7_free(&m);
        return v;
    }
    return SIG_NONE;
}

/* Whether @tag (a catalog member's tag) names the digest @h: the digest
 * itself, or (version 1 catalogs) its hex text in UTF-16 */
static bool tag_is(Span tag, const UINT8 *h, size_t hl)
{
    if (tag.n == hl && !memcmp(tag.p, h, hl)) return true;
    if (tag.n < 4 * hl) return false;
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < 2 * hl; i++) {
        UINT8 c = tag.p[2 * i], want = (UINT8)hex[(h[i / 2] >> (i & 1 ? 0 : 4)) & 15];
        if (tag.p[2 * i + 1] || (c != want && c != want + 32)) return false;
    }
    return true;
}

/* Whether catalog @m lists the image (its SHA-1 and SHA-256 digests) */
static bool lists(const P7 *m, const UINT8 *sha1, const UINT8 *sha256)
{
    if (!OID_IS(m->ctype, OID_CTL)) return false;
    /* CertificateTrustList { subjectUsage, listIdentifier?, sequenceNumber?, thisUpdate, nextUpdate?,
     *                        subjectAlgorithm, trustedSubjects?, [0] extensions? } */
    const UINT8 *p = m->content.p, *end = m->content.p + m->content.n;
    UINT8 t;
    Span subjects;
    if (!expect(&p, end, 0x30, NULL, NULL)) return false;
    if (p < end && *p == 0x04) tlv(&p, end, NULL, NULL, NULL);
    if (p < end && *p == 0x02) tlv(&p, end, NULL, NULL, NULL);
    if (!tlv(&p, end, &t, NULL, NULL) || (t != 0x17 && t != 0x18)) return false;
    if (p < end && (*p == 0x17 || *p == 0x18)) tlv(&p, end, NULL, NULL, NULL);
    if (!expect(&p, end, 0x30, NULL, NULL) || !expect(&p, end, 0x30, &subjects, NULL)) return false;
    for (p = subjects.p; p < subjects.p + subjects.n;) {
        Span member, tag, attrs;
        if (!expect(&p, subjects.p + subjects.n, 0x30, &member, NULL)) return false;
        const UINT8 *q = member.p, *qe = member.p + member.n;
        if (!expect(&q, qe, 0x04, &tag, NULL)) continue;
        if (tag_is(tag, sha1, 20) || tag_is(tag, sha256, 32)) return true;
        if (!expect(&q, qe, 0x31, &attrs, NULL)) continue;
        /* its SPC_INDIRECT_DATA attribute's DigestInfo */
        for (const UINT8 *a = attrs.p; a < attrs.p + attrs.n;) {
            Span at, o, vals, spc, di, alg, dig;
            if (!expect(&a, attrs.p + attrs.n, 0x30, &at, NULL)) break;
            const UINT8 *r = at.p;
            if (!expect(&r, at.p + at.n, 0x06, &o, NULL) || !OID_IS(o, OID_SPC_INDIRECT) ||
                !expect(&r, at.p + at.n, 0x31, &vals, NULL)) continue;
            r = vals.p;
            if (!expect(&r, vals.p + vals.n, 0x30, &spc, NULL)) continue;
            r = spc.p;
            if (!expect(&r, spc.p + spc.n, 0x30, NULL, NULL) || !expect(&r, spc.p + spc.n, 0x30, &di, NULL)) continue;
            r = di.p;
            if (!expect(&r, di.p + di.n, 0x30, &alg, NULL) || !expect(&r, di.p + di.n, 0x04, &dig, NULL)) continue;
            if ((dig.n == 20 && !memcmp(dig.p, sha1, 20)) || (dig.n == 32 && !memcmp(dig.p, sha256, 32))) return true;
        }
    }
    return false;
}

/* The installed catalogs: the best verdict of one that lists the image */
static Verdict catalogs(const UINT8 *f, const Pe *pe, CiResult *res)
{
    UINT8 sha1[20], sha256[32];
    size_t n;
    if (!pe_digest(f, pe, MBEDTLS_MD_SHA1, sha1, &n) || !pe_digest(f, pe, MBEDTLS_MD_SHA256, sha256, &n)) return SIG_NONE;
    Verdict best = SIG_NONE;
    const UINT8 *data;
    UINT32 size;
    char name[64];
    for (int i = 0; best != SIG_MICROSOFT && CiCatalog(i, &data, &size, name, sizeof(name)); i++) {
        P7 m;
        if (p7_parse(data, size, &m) && lists(&m, sha1, sha256)) {
            Verdict v = judge(&m);
            if (v > best) {
                best = v;
                if (v >= SIG_TEST) {
                    signer_name(&m, res->signer, sizeof(res->signer));
                    strncpy(res->catalog, name, sizeof(res->catalog) - 1);
                    res->catalog[sizeof(res->catalog) - 1] = 0;
                }
            }
        }
        p7_free(&m);
        CiCatalogDone(i);
    }
    return best;
}

NTSTATUS CiValidateImage(const char *image, const UINT8 *file, UINT32 size, bool test_signing, CiResult *res)
{
    memset(res, 0, sizeof(*res));
    Pe pe;
    if (!file || !pe_layout(file, size, &pe)) {
        kprintf("[NT] %s: not loaded: not a program image, so it cannot be signed\n", image);
        return STATUS_INVALID_IMAGE_HASH;
    }
    Verdict emb = embedded(file, &pe, res), v = emb;
    if (v != SIG_MICROSOFT) {
        CiResult cat;
        memset(&cat, 0, sizeof(cat));
        Verdict c = catalogs(file, &pe, &cat);
        if (c >= SIG_TEST && c > v) { v = c; *res = cat; }
    }
    res->microsoft = v == SIG_MICROSOFT;
    const char *from = res->catalog[0] ? res->catalog : "embedded signature";
    if (v == SIG_MICROSOFT || (v == SIG_TEST && test_signing)) {
        kprintf("[NT] %s: signed by %s (%s)%s\n", image, res->signer[0] ? res->signer : "?", from,
                v == SIG_TEST ? ": a test signature, allowed by test signing" : "");
        return STATUS_SUCCESS;
    }
    if (v == SIG_TEST)
        kprintf("[NT] %s: not loaded: signed by %s (%s), which is not a Microsoft driver signature; "
                "it loads only with test signing on\n", image, res->signer[0] ? res->signer : "?", from);
    else if (emb == SIG_MISMATCH)
        kprintf("[NT] %s: not loaded: the image does not match its signature (changed after it was signed)\n", image);
    else if (emb == SIG_BROKEN)
        kprintf("[NT] %s: not loaded: its signature is damaged or its certificates do not chain\n", image);
    else
        kprintf("[NT] %s: not loaded: it is not signed (no embedded signature, and no installed catalog lists its hash)\n",
                image);
    return STATUS_INVALID_IMAGE_HASH;
}
