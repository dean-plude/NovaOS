/*
 * pfx.h — reading a PKCS #12 file (.pfx / .p12, RFC 7292): the
 * certificate with its private key that "certutil -importpfx" puts in the
 * personal store, as Windows' PFXImportCertStore reads one.
 *
 * What Windows and OpenSSL write is understood: the bags in plain data or
 * in encrypted data (PBES2 with PBKDF2 and AES, Windows' default since
 * 2017 and OpenSSL 3's; or PKCS #12's own pbeWithSHAAnd3-KeyTripleDES-CBC,
 * the older "TripleDES-SHA1"), the key as a keyBag or a
 * pkcs8ShroudedKeyBag, X.509 certificates in certBags, and the
 * HMAC-SHA-1/SHA-256 integrity MAC (a wrong password fails there).  RC2
 * (OpenSSL 1.x's default for the certificates) is not: Mbed TLS has none.
 *
 * Mbed TLS does the cryptography; this only walks the ASN.1.  It is
 * written here rather than vendored: Mbed TLS has PKCS #12's key
 * derivation and ciphers but not the file's structure.
 */
#pragma once

#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"

#define PFX_OK            0
#define PFX_BAD_FORMAT   (-1)   /* not a PKCS #12 file, or one cut short */
#define PFX_BAD_PASSWORD (-2)   /* the MAC (or a decryption) failed: the password is wrong */
#define PFX_UNSUPPORTED  (-3)   /* an algorithm NovaOS does not have (RC2, PBMAC1, public-key privacy) */
#define PFX_NO_KEY       (-4)   /* no private key, or no certificate for it */
#define PFX_NO_MEMORY    (-5)

/* Read @der (@len bytes) with @password (UTF-8, may be ""): *@chain gets the
 * certificate that goes with the key first, then the file's other
 * certificates (its chain); *@key the private key.  Both must have been
 * initialised; on failure they are left empty.  @f_rng for Mbed TLS's
 * key checks. */
int PfxParse(const unsigned char *der, size_t len, const char *password, mbedtls_x509_crt *chain,
             mbedtls_pk_context *key, int (*f_rng)(void *, unsigned char *, size_t), void *p_rng);
const char *PfxErrorText(int err);
