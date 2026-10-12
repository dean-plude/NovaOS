/*
 * ci.h — code integrity: whether a kernel-mode driver image is signed as
 * Windows 11 requires (ci.c)
 */
#pragma once

#include "../include/types.h"

#define STATUS_INVALID_IMAGE_HASH  ((NTSTATUS)0xC0000428)

/* Where installed catalogs live (setupapi copies a package's .cat there
 * as oemN.cat with CryptCATAdminAddCatalog, as on Windows) */
#define CI_CATROOT "C:\\Windows\\System32\\CatRoot\\{F750E6C3-38EE-11D1-85E5-00C04FC295EE}"

typedef struct {
    char signer[96];            /* the signing certificate's common name */
    char catalog[64];           /* the catalog that signed it, "" for an embedded signature */
    bool microsoft;             /* a Microsoft driver signature (else a test signature) */
} CiResult;

/* Check @file (@size bytes, the driver @image's file) against the signing
 * policy: an embedded Authenticode signature, else an installed catalog
 * listing the image's hash.  Without test signing the signature must be
 * Microsoft's (a chain to a Microsoft product root, by a certificate for
 * drivers); with it any intact signature will do.  An unsigned image is
 * refused either way.  STATUS_SUCCESS, or STATUS_INVALID_IMAGE_HASH with
 * the reason logged. */
NTSTATUS CiValidateImage(const char *image, const UINT8 *file, UINT32 size, bool test_signing, CiResult *res);

/* Supplied by the loader: the installed catalogs, one by one (@i from 0),
 * each valid until CiCatalogDone(@i); false when there are no more */
bool CiCatalog(int i, const UINT8 **data, UINT32 *size, char *name, int cap);
void CiCatalogDone(int i);
