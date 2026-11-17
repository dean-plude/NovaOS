## Wi-Fi: imported certificates survive a restart, and Settings signs in to Enterprise networks

Eighth part of roadmap step 28.1, item 5 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)).  A trusted root added with `certutil
-addstore root` and a certificate imported with `certutil -importpfx`
lasted until NovaOS restarted, so a WPA2-Enterprise network had to be set
up again after every restart, and Settings could join one only with a
certificate.

- **Kept in the registry.**  Imported roots go where Windows keeps the
  machine's, `HKLM\SOFTWARE\Microsoft\SystemCertificates\ROOT\Certificates\<SHA-1
  thumbprint>`, value `Blob` in Windows' serialized form (one
  `CERT_CERT_PROP_ID` property with the DER).  The personal store's
  certificates and private keys go under `HKLM\SECURITY\NovaOS\Personal`
  (the chain's DER and the key's, written with Mbed TLS's
  `MBEDTLS_PK_WRITE_C`, now on in the kernel), in import order, so
  "the newest certificate" stays the same one.  At boot
  `NetRestoreCertificates` (`kernel/net/net.c`) puts them back once the
  registry is loaded and logs how many.  `certutil` no longer says
  "Lasts until reboot".
- **Private keys stay private.**  No program may open `HKLM\SECURITY`
  (`um_registry.c` refuses it as it refuses the SAM; on Windows only
  SYSTEM may), and `C:\Windows\System32\config`, which holds the hive
  file, now has Windows' DACL: SYSTEM and Administrators only, where
  before every user could read it (and the SAM's password hashes in it).
- **Settings' Wi-Fi page** shows two boxes for a WPA2-Enterprise network,
  User name and Password: both filled sign in with PEAP, a user name alone
  with that user's certificate, both empty with the newest one (EAP-TLS).
  Tab or a click moves between them; the notes say what is missing.

The `wifi` boot gains two tests: `wlantest settings enterprise`, which
waits while the test chooses NovaCorp in Settings and types alice's user
name and password, then checks the WPA2-Enterprise connection through
`wlanapi.dll`; and a restart after which the boot log says the root and
both certificates were restored, `certutil -store my` lists them and
alice signs in with EAP-TLS again.  The host test
(`tests/tools/test_eap_peap.py`) writes each PKCS #12 file's chain and
key as the registry keeps them and reads them back.  Not yet:
WPA3-Enterprise, Enterprise profiles in `wlanapi.dll`, `certutil
-delstore`, and PEAP's cryptobinding; the real card needs the T14.
