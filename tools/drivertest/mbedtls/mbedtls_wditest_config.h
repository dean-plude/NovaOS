/*
 * mbedtls_wditest_config.h — Mbed TLS (third_party/mbedtls) as wditest.sys
 * builds it (tools/drivertest/mksys.py): the TLS 1.2 server its
 * WPA2-Enterprise network's authentication server runs PEAP with
 * (tools/drivertest/eapap.h), one cipher suite (ECDHE-RSA with
 * AES-128-GCM), and DES for MS-CHAPv2.  A Windows kernel driver has no C
 * library: memory comes from the pool (wditest_calloc, wditest_free) and
 * the string functions from ntoskrnl.exe (the headers beside this one).
 */
#pragma once

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_CALLOC_MACRO   wditest_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO     wditest_free
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO wditest_snprintf
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_DEPRECATED_REMOVED

#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_DES_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECDH_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_REMOVE_INFO
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_IN_CONTENT_LEN  4096
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096

/* (clang for x86_64-pc-windows-msvc is _MSC_VER: no <sal.h> annotations here) */
#define MBEDTLS_CHECK_RETURN

/* (the functions named above: in tools/drivertest/wditest.c) */
#include <stddef.h>
void *wditest_calloc(size_t n, size_t size);
void  wditest_free(void *p);
int   wditest_snprintf(char *s, size_t n, const char *fmt, ...);
