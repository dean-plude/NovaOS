/*
 * pfx.c — a PKCS #12 file's certificate and private key (pfx.h, RFC 7292)
 *
 *   PFX ::= SEQUENCE { version 3, authSafe ContentInfo (data: the
 *           AuthenticatedSafe's DER), macData MacData OPTIONAL }
 *   AuthenticatedSafe ::= SEQUENCE OF ContentInfo, each data (SafeContents
 *           in the clear) or encryptedData (SafeContents encrypted with a
 *           password-based scheme)
 *   SafeContents ::= SEQUENCE OF SafeBag { bagId, [0] bagValue, attributes }
 *   MacData ::= SEQUENCE { DigestInfo, macSalt, iterations DEFAULT 1 }: an
 *           HMAC over the AuthenticatedSafe, keyed by PKCS #12's key
 *           derivation (B.2, ID 3) from the password as a BMPString
 */
#include "../lib/string.h"
#include "pfx.h"
#include "mbedtls/asn1.h"
#include "mbedtls/oid.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/pkcs12.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"

#define OID_DATA           MBEDTLS_OID_PKCS "\x07\x01"
#define OID_ENCRYPTED_DATA MBEDTLS_OID_PKCS "\x07\x06"
#define OID_BAG            MBEDTLS_OID_PKCS12 "\x0a\x01"
#define OID_KEY_BAG        OID_BAG "\x01"
#define OID_SHROUDED_BAG   OID_BAG "\x02"
#define OID_CERT_BAG       OID_BAG "\x03"
#define OID_X509_CERT      MBEDTLS_OID_PKCS9 "\x16\x01"

#define SEQ   (MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE)
#define CTX0  (MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_CONSTRUCTED | 0)

static bool oid_is(const unsigned char *p, size_t n, const char *oid, size_t oid_len)
{
    return n == oid_len && !memcmp(p, oid, n);
}
#define OID_IS(p, n, o) oid_is(p, n, o, sizeof(o) - 1)

typedef struct {
    const char *pw;
    size_t pw_len;
    mbedtls_x509_crt *certs;
    mbedtls_pk_context *key;
    bool have_key;
    int (*f_rng)(void *, unsigned char *, size_t);
    void *p_rng;
} Pfx;

/* Decrypt an encryptedData's content: PBES2 (PBKDF2 with a cipher) or PKCS #12's own PBE */
static int decrypt(Pfx *x, mbedtls_asn1_buf *alg, mbedtls_asn1_buf *params, const unsigned char *in, size_t n,
                   unsigned char *out, size_t *out_len)
{
    const unsigned char *pw = (const unsigned char *)x->pw;
    if (MBEDTLS_OID_CMP(MBEDTLS_OID_PKCS5_PBES2, alg) == 0)
        return mbedtls_pkcs5_pbes2_ext(params, MBEDTLS_PKCS5_DECRYPT, pw, x->pw_len, in, n, out, n, out_len) ?
               PFX_BAD_PASSWORD : PFX_OK;
#if defined(MBEDTLS_PKCS12_C)
    mbedtls_md_type_t md;
    mbedtls_cipher_type_t cipher;
    if (mbedtls_oid_get_pkcs12_pbe_alg(alg, &md, &cipher) == 0)
        return mbedtls_pkcs12_pbe_ext(params, MBEDTLS_PKCS12_PBE_DECRYPT, cipher, md, pw, x->pw_len, in, n, out, n,
                                      out_len) ? PFX_BAD_PASSWORD : PFX_OK;
#endif
    return PFX_UNSUPPORTED;                         /* (RC2-40, RC4: Mbed TLS has neither) */
}

/* One SafeContents: its key and certificate bags */
static int safe_contents(Pfx *x, unsigned char *p, const unsigned char *end)
{
    size_t n;
    if (mbedtls_asn1_get_tag(&p, end, &n, SEQ)) return PFX_BAD_FORMAT;
    end = p + n;
    while (p < end) {
        size_t bl, ol, vl;
        if (mbedtls_asn1_get_tag(&p, end, &bl, SEQ)) return PFX_BAD_FORMAT;
        unsigned char *bag_end = p + bl, *oid;
        if (mbedtls_asn1_get_tag(&p, bag_end, &ol, MBEDTLS_ASN1_OID)) return PFX_BAD_FORMAT;
        oid = p;
        p += ol;
        if (mbedtls_asn1_get_tag(&p, bag_end, &vl, CTX0)) return PFX_BAD_FORMAT;
        unsigned char *v = p, *v_end = p + vl;
        if ((OID_IS(oid, ol, OID_KEY_BAG) || OID_IS(oid, ol, OID_SHROUDED_BAG)) && !x->have_key) {
            bool shrouded = OID_IS(oid, ol, OID_SHROUDED_BAG);
            int r = mbedtls_pk_parse_key(x->key, v, vl, shrouded ? (const unsigned char *)x->pw : NULL,
                                         shrouded ? x->pw_len : 0, x->f_rng, x->p_rng);
            if (r == MBEDTLS_ERR_PK_PASSWORD_MISMATCH || (r && shrouded && !x->pw_len)) return PFX_BAD_PASSWORD;
            if (r == MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE || r == MBEDTLS_ERR_PK_UNKNOWN_PK_ALG) return PFX_UNSUPPORTED;
            if (r) return PFX_BAD_FORMAT;
            x->have_key = true;
        } else if (OID_IS(oid, ol, OID_CERT_BAG)) {     /* CertBag: { certId, [0] OCTET STRING (the DER) } */
            size_t cl, il, dl;
            unsigned char *c = v;
            if (mbedtls_asn1_get_tag(&c, v_end, &cl, SEQ) || mbedtls_asn1_get_tag(&c, v_end, &il, MBEDTLS_ASN1_OID))
                return PFX_BAD_FORMAT;
            if (OID_IS(c, il, OID_X509_CERT)) {
                c += il;
                if (mbedtls_asn1_get_tag(&c, v_end, &dl, CTX0) ||
                    mbedtls_asn1_get_tag(&c, v_end, &dl, MBEDTLS_ASN1_OCTET_STRING))
                    return PFX_BAD_FORMAT;
                if (mbedtls_x509_crt_parse_der(x->certs, c, dl)) return PFX_BAD_FORMAT;
            }                                           /* (an SDSI certificate: skipped) */
        }                                               /* (CRL, secret and nested bags: skipped) */
        p = bag_end;
    }
    return PFX_OK;
}

/* The MAC over the AuthenticatedSafe (@data): HMAC with PKCS #12's key derivation */
static int check_mac(Pfx *x, unsigned char *p, const unsigned char *end, const unsigned char *data, size_t n)
{
    size_t l;
    mbedtls_asn1_buf alg, params;
    unsigned char *digest, *salt;
    size_t digest_len, salt_len;
    int iterations = 1;
    if (mbedtls_asn1_get_tag(&p, end, &l, SEQ)) return PFX_BAD_FORMAT;
    end = p + l;
    if (mbedtls_asn1_get_tag(&p, end, &l, SEQ)) return PFX_BAD_FORMAT;    /* DigestInfo */
    if (mbedtls_asn1_get_alg(&p, p + l, &alg, &params)) return PFX_BAD_FORMAT;
    if (mbedtls_asn1_get_tag(&p, end, &digest_len, MBEDTLS_ASN1_OCTET_STRING)) return PFX_BAD_FORMAT;
    digest = p;
    p += digest_len;
    if (mbedtls_asn1_get_tag(&p, end, &salt_len, MBEDTLS_ASN1_OCTET_STRING)) return PFX_BAD_FORMAT;
    salt = p;
    p += salt_len;
    if (p < end && mbedtls_asn1_get_int(&p, end, &iterations)) return PFX_BAD_FORMAT;
    mbedtls_md_type_t md;
    if (mbedtls_oid_get_md_alg(&alg, &md)) return PFX_UNSUPPORTED;      /* (PBMAC1: RFC 9579's) */
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(md);
    size_t ml = info ? mbedtls_md_get_size(info) : 0;
    if (!ml || ml != digest_len || iterations < 1) return PFX_UNSUPPORTED;
    /* the password as a BMPString: UTF-16 big-endian with its terminator (from UTF-8, the BMP) */
    unsigned char *bmp = mbedtls_calloc(1, 2 * x->pw_len + 2);
    if (!bmp) return PFX_NO_MEMORY;
    size_t k = 0;
    for (const unsigned char *s = (const unsigned char *)x->pw; *s;) {
        unsigned int c = *s++;
        if (c >= 0xE0 && s[0] && s[1]) { c = (c & 0x0F) << 12 | (s[0] & 0x3Fu) << 6 | (s[1] & 0x3Fu); s += 2; }
        else if (c >= 0xC0 && s[0]) { c = (c & 0x1F) << 6 | (s[0] & 0x3Fu); s++; }
        bmp[k++] = (unsigned char)(c >> 8);
        bmp[k++] = (unsigned char)c;
    }
    k += 2;
    unsigned char key[MBEDTLS_MD_MAX_SIZE], mac[MBEDTLS_MD_MAX_SIZE];
    int r = mbedtls_pkcs12_derivation(key, ml, bmp, k, salt, salt_len, md, MBEDTLS_PKCS12_DERIVE_MAC_KEY, iterations);
    mbedtls_platform_zeroize(bmp, 2 * x->pw_len + 2);
    mbedtls_free(bmp);
    if (!r) r = mbedtls_md_hmac(info, key, ml, data, n, mac);
    mbedtls_platform_zeroize(key, sizeof(key));
    if (r) return PFX_UNSUPPORTED;
    unsigned char diff = 0;
    for (size_t i = 0; i < ml; i++) diff |= (unsigned char)(mac[i] ^ digest[i]);
    return diff ? PFX_BAD_PASSWORD : PFX_OK;
}

static int parse(Pfx *x, const unsigned char *der, size_t len)
{
    unsigned char *p = (unsigned char *)der, *end = p + len, *auth;
    size_t l, auth_len;
    int version, r;
    if (mbedtls_asn1_get_tag(&p, end, &l, SEQ)) return PFX_BAD_FORMAT;
    end = p + l;
    if (mbedtls_asn1_get_int(&p, end, &version) || version != 3) return PFX_BAD_FORMAT;
    /* authSafe: ContentInfo { data, [0] OCTET STRING } (signedData: public-key integrity, not supported) */
    if (mbedtls_asn1_get_tag(&p, end, &l, SEQ)) return PFX_BAD_FORMAT;
    unsigned char *ci_end = p + l;
    if (mbedtls_asn1_get_tag(&p, ci_end, &l, MBEDTLS_ASN1_OID)) return PFX_BAD_FORMAT;
    if (!OID_IS(p, l, OID_DATA)) return PFX_UNSUPPORTED;
    p += l;
    if (mbedtls_asn1_get_tag(&p, ci_end, &l, CTX0) ||
        mbedtls_asn1_get_tag(&p, ci_end, &auth_len, MBEDTLS_ASN1_OCTET_STRING))
        return PFX_BAD_FORMAT;
    auth = p;
    p = ci_end;
    if (p < end && (r = check_mac(x, p, end, auth, auth_len)) != PFX_OK) return r;
    /* the AuthenticatedSafe */
    unsigned char *q = auth, *a_end = auth + auth_len;
    if (mbedtls_asn1_get_tag(&q, a_end, &l, SEQ)) return PFX_BAD_FORMAT;
    a_end = q + l;
    while (q < a_end) {
        if (mbedtls_asn1_get_tag(&q, a_end, &l, SEQ)) return PFX_BAD_FORMAT;
        unsigned char *c_end = q + l, *oid;
        size_t ol;
        if (mbedtls_asn1_get_tag(&q, c_end, &ol, MBEDTLS_ASN1_OID)) return PFX_BAD_FORMAT;
        oid = q;
        q += ol;
        if (mbedtls_asn1_get_tag(&q, c_end, &l, CTX0)) return PFX_BAD_FORMAT;
        if (OID_IS(oid, ol, OID_DATA)) {
            if (mbedtls_asn1_get_tag(&q, c_end, &l, MBEDTLS_ASN1_OCTET_STRING)) return PFX_BAD_FORMAT;
            if ((r = safe_contents(x, q, q + l)) != PFX_OK) return r;
        } else if (OID_IS(oid, ol, OID_ENCRYPTED_DATA)) {
            /* EncryptedData { version, EncryptedContentInfo { data, algorithm, [0] IMPLICIT encrypted } } */
            int v;
            mbedtls_asn1_buf alg, params;
            if (mbedtls_asn1_get_tag(&q, c_end, &l, SEQ) || mbedtls_asn1_get_int(&q, c_end, &v) ||
                mbedtls_asn1_get_tag(&q, c_end, &l, SEQ) || mbedtls_asn1_get_tag(&q, c_end, &ol, MBEDTLS_ASN1_OID))
                return PFX_BAD_FORMAT;
            q += ol;
            if (mbedtls_asn1_get_alg(&q, c_end, &alg, &params) ||
                mbedtls_asn1_get_tag(&q, c_end, &l, MBEDTLS_ASN1_CONTEXT_SPECIFIC | 0))
                return PFX_BAD_FORMAT;
            unsigned char *plain = mbedtls_calloc(1, l ? l : 1);
            if (!plain) return PFX_NO_MEMORY;
            size_t pl = 0;
            r = decrypt(x, &alg, &params, q, l, plain, &pl);
            if (r == PFX_OK) r = safe_contents(x, plain, plain + pl);
            mbedtls_platform_zeroize(plain, l);
            mbedtls_free(plain);
            if (r != PFX_OK) return r;
        } else {
            return PFX_UNSUPPORTED;                     /* (envelopedData: public-key privacy) */
        }
        q = c_end;
    }
    return PFX_OK;
}

int PfxParse(const unsigned char *der, size_t len, const char *password, mbedtls_x509_crt *chain,
             mbedtls_pk_context *key, int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
    mbedtls_x509_crt certs;
    mbedtls_x509_crt_init(&certs);
    Pfx x = { password ? password : "", password ? strlen(password) : 0, &certs, key, false, f_rng, p_rng };
    int r = parse(&x, der, len);
    const mbedtls_x509_crt *leaf = NULL;
    if (r == PFX_OK) {
        r = PFX_NO_KEY;
        for (const mbedtls_x509_crt *c = &certs; x.have_key && c && c->raw.len; c = c->next)
            if (!mbedtls_pk_check_pair(&c->pk, key, f_rng, p_rng)) { leaf = c; r = PFX_OK; break; }
    }
    if (r == PFX_OK && mbedtls_x509_crt_parse_der(chain, leaf->raw.p, leaf->raw.len)) r = PFX_NO_MEMORY;
    for (const mbedtls_x509_crt *c = &certs; r == PFX_OK && c && c->raw.len; c = c->next)
        if (c != leaf && mbedtls_x509_crt_parse_der(chain, c->raw.p, c->raw.len)) r = PFX_NO_MEMORY;
    mbedtls_x509_crt_free(&certs);
    if (r != PFX_OK) {
        mbedtls_x509_crt_free(chain);
        mbedtls_x509_crt_init(chain);
        mbedtls_pk_free(key);
        mbedtls_pk_init(key);
    }
    return r;
}

const char *PfxErrorText(int err)
{
    switch (err) {
    case PFX_OK:           return "OK";
    case PFX_BAD_FORMAT:   return "Not a valid PFX (PKCS #12) file";
    case PFX_BAD_PASSWORD: return "The specified network password is not correct";
    case PFX_UNSUPPORTED:  return "The PFX file uses an algorithm NovaOS does not support (RC2, PBMAC1)";
    case PFX_NO_KEY:       return "The PFX file has no private key, or no certificate for its key";
    case PFX_NO_MEMORY:    return "Out of memory";
    default:               return "The PFX file could not be read";
    }
}
