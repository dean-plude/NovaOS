## Windows' driver signing policy

Step 30.7 of the Windows driver model (roadmap Phase 30,
[design/windows-drivers.md](../design/windows-drivers.md)): the interim
"test signing or nothing" gate of step 30.1 is replaced by the policy
Windows 11 enforces, at load and at install, with nothing bypassed.

- **Code integrity at load** (`kernel/nt/ci.c`, with the kernel's Mbed
  TLS): the image's Authenticode digest is recomputed and must match an
  embedded signature or a catalog in `CatRoot` that lists it.  Without
  test signing the signer must chain to a Microsoft product root
  (`kernel/nt/ci_roots.c`, the public Root Certificate Authority 2010 and
  2001 certificates, written by `tools/gen_ci_roots.py`) through a
  certificate for drivers (WHQL, attestation, Windows components); with
  it any intact signature loads; unsigned or changed images never do.
  Refusals are Windows' 577 (`STATUS_INVALID_IMAGE_HASH`) with the reason
  in the kernel log.  Checked against Red Hat's WHQL-signed `vioser.sys`
  and its catalog from the Microsoft Update Catalog: both signatures are
  accepted as Microsoft's, and the package's Red Hat-signed test program
  is not.
- **Catalogs at install**: wintrust verifies catalogs
  (`WinVerifyTrust` with `WTD_CHOICE_CATALOG`; the driver action does not
  expire certificates) and installs them in `CatRoot`
  (`CryptCATAdminAddCatalog`, `CryptCATAdminRemoveCatalog`).  setupapi
  checks every staged file against its package's catalog: a file the
  catalog does not list refuses the package with
  `ERROR_FILE_HASH_NOT_IN_CATALOG`; an intact catalog becomes `oemN.cat`
  and the package's `SignerName` (`pnputil /enum-drivers` shows it); a
  package without a trusted signature is staged as unsigned.  Packages
  downloaded from the Update Catalog (step 30.6) pass the same check.
- **Test-signed test drivers**: `tools/drivertest/mksys.py` signs
  `wdmtest.sys` with a test certificate and makes an unsigned, a tampered
  and two catalog-signed packages (`tools/authenticode/catalog.py` writes
  catalogs); the devices suite's "wdm" boot checks each case before and
  after test signing is turned on.
- Still to come: the desktop's "Test Mode" mark, and problem code 52 on a
  device once Plug and Play installs drivers on devices (step 30.3).
