/*
 * cng.c — NovaOS's cng.sys: the kernel-mode BCrypt calls drivers make
 *
 * Windows' kernel cryptography (cng.sys) is Microsoft's and is never
 * shipped; drivers that check a signature (Intel's Wi-Fi driver verifies
 * an ECDSA P-384 signature over SHA-384) import these from it, so NovaOS
 * exports them from the kernel, over the Mbed TLS the kernel already
 * links (third_party/mbedtls: the code-integrity checks and TLS use it).
 *
 *   algorithms   hashes SHA1, SHA256, SHA384, SHA512; signatures
 *                ECDSA_P256, ECDSA_P384 (ECCPUBLICBLOB keys) and RSA
 *                (RSAPUBLICBLOB keys, PKCS #1 v1.5 or PSS padding)
 *   properties   ObjectLength, HashDigestLength
 *   hashing      BCryptCreateHash (no HMAC secret), BCryptHashData,
 *                BCryptFinishHash, BCryptDestroyHash
 *   keys         BCryptImportKeyPair (public blobs), BCryptVerifySignature,
 *                BCryptDestroyKey
 *
 * Signing, encryption, key generation and private keys are not here yet:
 * an algorithm or blob not listed is NT_STATUS_NOT_SUPPORTED.  A hash object
 * the caller passes in is not used (NovaOS keeps the state itself).
 */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS              /* (a curve's size: mbedtls_ecp_group.pbits) */
#include "nt.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "mbedtls/md.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/rsa.h"
#include "mbedtls/bignum.h"

#define ST_INVALID_SIGNATURE  ((NTSTATUS)0xC000A000)
#define ST_INVALID_HANDLE     ((NTSTATUS)0xC0000008)
#define ST_BUFFER_TOO_SMALL   ((NTSTATUS)0xC0000023)

#define M_ALG  0x67414E43u                 /* 'CNAg' */
#define M_HASH 0x68414E43u
#define M_KEY  0x6B414E43u

enum { A_HASH = 1, A_ECDSA, A_RSA };

typedef struct {
    UINT32            magic;
    int               kind;
    mbedtls_md_type_t md;                  /* A_HASH */
    mbedtls_ecp_group_id curve;            /* A_ECDSA */
    UINT32            key_bytes;
} CngAlg;

typedef struct {
    UINT32               magic;
    mbedtls_md_context_t ctx;
    UINT32               size;
} CngHash;

typedef struct {
    UINT32               magic;
    int                  kind;
    mbedtls_ecp_group    grp;              /* A_ECDSA */
    mbedtls_ecp_point    q;
    mbedtls_rsa_context  rsa;              /* A_RSA */
} CngKey;

static bool wide_is(const NT_WCHAR *w, const char *a)
{
    if (!w) return false;
    for (; *a; a++, w++) if (*w != (NT_WCHAR)*a) return false;
    return *w == 0;
}

static mbedtls_md_type_t md_named(const NT_WCHAR *w)
{
    if (wide_is(w, "SHA1")) return MBEDTLS_MD_SHA1;
    if (wide_is(w, "SHA256")) return MBEDTLS_MD_SHA256;
    if (wide_is(w, "SHA384")) return MBEDTLS_MD_SHA384;
    if (wide_is(w, "SHA512")) return MBEDTLS_MD_SHA512;
    return MBEDTLS_MD_NONE;
}

NT_API NTSTATUS nt_BCryptOpenAlgorithmProvider(CngAlg **out, const NT_WCHAR *alg, const NT_WCHAR *impl, UINT32 flags)
{
    (void)impl;
    if (!out || !alg) return STATUS_INVALID_PARAMETER;
    CngAlg a = { M_ALG, 0, MBEDTLS_MD_NONE, MBEDTLS_ECP_DP_NONE, 0 };
    if ((a.md = md_named(alg)) != MBEDTLS_MD_NONE) a.kind = A_HASH;
    else if (wide_is(alg, "ECDSA_P256")) { a.kind = A_ECDSA; a.curve = MBEDTLS_ECP_DP_SECP256R1; a.key_bytes = 32; }
    else if (wide_is(alg, "ECDSA_P384")) { a.kind = A_ECDSA; a.curve = MBEDTLS_ECP_DP_SECP384R1; a.key_bytes = 48; }
    else if (wide_is(alg, "RSA")) a.kind = A_RSA;
    if (!a.kind || (flags & 0x8 /* BCRYPT_ALG_HANDLE_HMAC_FLAG */)) {
        char name[32];
        int i = 0;
        for (; alg[i] && i < 31; i++) name[i] = alg[i] < 0x80 ? (char)alg[i] : '?';
        name[i] = 0;
        kprintf("[CNG] %s%s: not supported\n", name, flags & 0x8 ? " (HMAC)" : "");
        return NT_STATUS_NOT_SUPPORTED;
    }
    CngAlg *p = kzalloc(sizeof(*p));
    if (!p) return STATUS_INSUFFICIENT_RESOURCES;
    *p = a;
    *out = p;
    return STATUS_SUCCESS;
}

NT_API NTSTATUS nt_BCryptCloseAlgorithmProvider(CngAlg *a, UINT32 flags)
{
    (void)flags;
    if (!a || a->magic != M_ALG) return ST_INVALID_HANDLE;
    a->magic = 0;
    kfree(a);
    return STATUS_SUCCESS;
}

NT_API NTSTATUS nt_BCryptGetProperty(void *h, const NT_WCHAR *prop, UINT8 *out, UINT32 cb, UINT32 *result, UINT32 flags)
{
    (void)flags;
    UINT32 magic = h ? *(UINT32 *)h : 0;
    UINT32 v;
    if (wide_is(prop, "ObjectLength") && magic == M_ALG && ((CngAlg *)h)->kind == A_HASH) v = sizeof(CngHash);
    else if (wide_is(prop, "HashDigestLength") && magic == M_ALG && ((CngAlg *)h)->kind == A_HASH)
        v = mbedtls_md_get_size(mbedtls_md_info_from_type(((CngAlg *)h)->md));
    else if (wide_is(prop, "HashDigestLength") && magic == M_HASH) v = ((CngHash *)h)->size;
    else if (magic != M_ALG && magic != M_HASH && magic != M_KEY) return ST_INVALID_HANDLE;
    else return NT_STATUS_NOT_SUPPORTED;
    if (result) *result = sizeof(v);
    if (!out) return STATUS_SUCCESS;
    if (cb < sizeof(v)) return ST_BUFFER_TOO_SMALL;
    memcpy(out, &v, sizeof(v));
    return STATUS_SUCCESS;
}

NT_API NTSTATUS nt_BCryptCreateHash(CngAlg *a, CngHash **out, UINT8 *obj, UINT32 cbobj, UINT8 *secret, UINT32 cbsecret,
                                    UINT32 flags)
{
    (void)obj; (void)cbobj; (void)flags;
    if (!a || a->magic != M_ALG || a->kind != A_HASH) return ST_INVALID_HANDLE;
    if (!out || (secret && cbsecret)) return NT_STATUS_NOT_SUPPORTED;      /* (no HMAC) */
    CngHash *h = kzalloc(sizeof(*h));
    if (!h) return STATUS_INSUFFICIENT_RESOURCES;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(a->md);
    mbedtls_md_init(&h->ctx);
    if (!info || mbedtls_md_setup(&h->ctx, info, 0) || mbedtls_md_starts(&h->ctx)) {
        mbedtls_md_free(&h->ctx);
        kfree(h);
        return NT_STATUS_NOT_SUPPORTED;
    }
    h->magic = M_HASH;
    h->size = mbedtls_md_get_size(info);
    *out = h;
    return STATUS_SUCCESS;
}

NT_API NTSTATUS nt_BCryptHashData(CngHash *h, const UINT8 *in, UINT32 cb, UINT32 flags)
{
    (void)flags;
    if (!h || h->magic != M_HASH) return ST_INVALID_HANDLE;
    return mbedtls_md_update(&h->ctx, in, cb) ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

NT_API NTSTATUS nt_BCryptFinishHash(CngHash *h, UINT8 *out, UINT32 cb, UINT32 flags)
{
    (void)flags;
    if (!h || h->magic != M_HASH) return ST_INVALID_HANDLE;
    if (cb != h->size) return STATUS_INVALID_PARAMETER;
    return mbedtls_md_finish(&h->ctx, out) ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

NT_API NTSTATUS nt_BCryptDestroyHash(CngHash *h)
{
    if (!h || h->magic != M_HASH) return ST_INVALID_HANDLE;
    mbedtls_md_free(&h->ctx);
    h->magic = 0;
    kfree(h);
    return STATUS_SUCCESS;
}

/* BCRYPT_ECCKEY_BLOB (magic, cbKey, X, Y) or BCRYPT_RSAKEY_BLOB (magic,
 * BitLength, cbPublicExp, cbModulus, cbPrime1, cbPrime2, exponent, modulus) */
NT_API NTSTATUS nt_BCryptImportKeyPair(CngAlg *a, void *import_key, const NT_WCHAR *type, CngKey **out, const UINT8 *in,
                                       UINT32 cb, UINT32 flags)
{
    (void)import_key; (void)flags;
    if (!a || a->magic != M_ALG) return ST_INVALID_HANDLE;
    if (!out || !in) return STATUS_INVALID_PARAMETER;
    CngKey *k = kzalloc(sizeof(*k));
    if (!k) return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS st = NT_STATUS_NOT_SUPPORTED;
    if (a->kind == A_ECDSA && wide_is(type, "ECCPUBLICBLOB") && cb >= 8) {
        UINT32 n = *(const UINT32 *)(in + 4);
        UINT8 pt[1 + 2 * 66];
        mbedtls_ecp_group_init(&k->grp);
        mbedtls_ecp_point_init(&k->q);
        st = STATUS_INVALID_PARAMETER;
        if (n == a->key_bytes && cb >= 8 + 2 * n) {
            pt[0] = 0x04;
            memcpy(pt + 1, in + 8, 2 * n);
            if (!mbedtls_ecp_group_load(&k->grp, a->curve) &&
                !mbedtls_ecp_point_read_binary(&k->grp, &k->q, pt, 1 + 2 * n) && !mbedtls_ecp_check_pubkey(&k->grp, &k->q))
                st = STATUS_SUCCESS;
        }
        if (st != STATUS_SUCCESS) { mbedtls_ecp_point_free(&k->q); mbedtls_ecp_group_free(&k->grp); }
        k->kind = A_ECDSA;
    } else if (a->kind == A_RSA && wide_is(type, "RSAPUBLICBLOB") && cb >= 24) {
        const UINT32 *h = (const UINT32 *)in;
        UINT32 ne = h[2], nn = h[3];
        mbedtls_rsa_init(&k->rsa);
        st = STATUS_INVALID_PARAMETER;
        if (h[0] == 0x31415352u /* 'RSA1' */ && cb >= 24 + ne + nn &&
            !mbedtls_rsa_import_raw(&k->rsa, in + 24 + ne, nn, NULL, 0, NULL, 0, NULL, 0, in + 24, ne) &&
            !mbedtls_rsa_complete(&k->rsa) && !mbedtls_rsa_check_pubkey(&k->rsa))
            st = STATUS_SUCCESS;
        if (st != STATUS_SUCCESS) mbedtls_rsa_free(&k->rsa);
        k->kind = A_RSA;
    }
    if (st != STATUS_SUCCESS) { kfree(k); return st; }
    k->magic = M_KEY;
    *out = k;
    return STATUS_SUCCESS;
}

/* (BCRYPT_PKCS1_PADDING_INFO, BCRYPT_PSS_PADDING_INFO) */
typedef struct { const NT_WCHAR *pszAlgId; UINT32 cbSalt; } CngPadding;
#define PAD_PKCS1 0x2
#define PAD_PSS   0x8

NT_API NTSTATUS nt_BCryptVerifySignature(CngKey *k, const CngPadding *pad, const UINT8 *hash, UINT32 cbhash,
                                         const UINT8 *sig, UINT32 cbsig, UINT32 flags)
{
    if (!k || k->magic != M_KEY) return ST_INVALID_HANDLE;
    if (!hash || !sig) return STATUS_INVALID_PARAMETER;
    int r = -1;
    if (k->kind == A_ECDSA) {                          /* (the signature is r || s, each the curve's size) */
        size_t n = (k->grp.pbits + 7) / 8;
        if (cbsig != 2 * n) return ST_INVALID_SIGNATURE;
        mbedtls_mpi mr, ms;
        mbedtls_mpi_init(&mr);
        mbedtls_mpi_init(&ms);
        if (!mbedtls_mpi_read_binary(&mr, sig, n) && !mbedtls_mpi_read_binary(&ms, sig + n, n))
            r = mbedtls_ecdsa_verify(&k->grp, hash, cbhash, &k->q, &mr, &ms);
        mbedtls_mpi_free(&mr);
        mbedtls_mpi_free(&ms);
    } else if (k->kind == A_RSA) {
        mbedtls_md_type_t md = pad ? md_named(pad->pszAlgId) : MBEDTLS_MD_NONE;
        if (cbsig != mbedtls_rsa_get_len(&k->rsa)) return ST_INVALID_SIGNATURE;
        if (flags & PAD_PSS) {
            mbedtls_rsa_set_padding(&k->rsa, MBEDTLS_RSA_PKCS_V21, md);
            r = mbedtls_rsa_rsassa_pss_verify_ext(&k->rsa, md, cbhash, hash, md, pad ? (int)pad->cbSalt : 0, sig);
        } else if (flags & PAD_PKCS1) {
            mbedtls_rsa_set_padding(&k->rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
            r = mbedtls_rsa_pkcs1_verify(&k->rsa, md, cbhash, hash, sig);
        } else {
            return NT_STATUS_NOT_SUPPORTED;
        }
    }
    return r ? ST_INVALID_SIGNATURE : STATUS_SUCCESS;
}

NT_API NTSTATUS nt_BCryptDestroyKey(CngKey *k)
{
    if (!k || k->magic != M_KEY) return ST_INVALID_HANDLE;
    if (k->kind == A_ECDSA) { mbedtls_ecp_point_free(&k->q); mbedtls_ecp_group_free(&k->grp); }
    else mbedtls_rsa_free(&k->rsa);
    k->magic = 0;
    kfree(k);
    return STATUS_SUCCESS;
}
