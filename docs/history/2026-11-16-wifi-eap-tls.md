## Wi-Fi: WPA2-Enterprise networks sign in with a certificate (EAP-TLS)

Seventh part of roadmap step 28.1, item 5 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)).  NovaOS signed in to WPA2-Enterprise
networks with a user name and password (PEAP).  Now it also signs in with
a certificate, Windows' "smart card or other certificate" (EAP-TLS,
RFC 5216), which many companies use instead of passwords.

- **`certutil -p PASSWORD -importpfx FILE`** reads a PKCS #12 file
  (`.pfx`/`.p12`, RFC 7292; `kernel/net/pfx.c`) into a new personal store
  ("My"): the certificate that goes with the private key, the rest of its
  chain, and the key.  It reads what Windows and OpenSSL write: bags in
  the clear or encrypted with PBES2 (PBKDF2 with AES, Windows' default
  since 2017 and OpenSSL 3's) or PKCS #12's TripleDES-SHA1 (Mbed TLS's
  `MBEDTLS_PKCS12_C`, now on in the kernel), keys in keyBags or shrouded
  ones, and the HMAC-SHA-1/SHA-256 integrity MAC, so a wrong password is
  refused ("The specified network password is not correct").  RC2, which
  OpenSSL 1.x used for certificates, is not there: Mbed TLS has none.
  `certutil -store my` lists the store.  It lasts until NovaOS restarts,
  as `certutil -addstore root` does.
- **`netsh wlan connect name=SSID user=NAME`** with no `key=` signs in with
  NAME's certificate (its common name, or the name without a `DOMAIN\` or
  `@realm`); with neither, the newest certificate.  Settings' Wi-Fi page,
  which has no user name box yet, joins such a network the same way once
  a certificate is in the store.
- **The EAP peer** (`kernel/nt/eap.c`) asks for the method it can do: a
  Nak naming EAP-TLS when the server offers PEAP and the user has a
  certificate (and PEAP when the server offers EAP-TLS and the user has a
  password).  EAP-TLS runs the same TLS 1.2 handshake in the same
  fragments as PEAP's tunnel, now with the certificate and its key
  (`net/tls.c`'s EAP sessions take one from the personal store), the
  server's certificate still checked against the trusted roots; nothing
  runs inside, and the MSK comes from the same "client EAP encryption"
  keying material.
- **Clear errors**: a user with no certificate ("There is no certificate
  for that user in the personal store"), and a server that refuses the
  certificate with its TLS alert ("did not accept the certificate").

`wditest.sys`'s authentication server (`tools/drivertest/eapap.h`) does
EAP-TLS too, as FreeRADIUS does with its CA file: on the station's Nak it
starts EAP-TLS, asks for the certificate, checks it chains to "NovaCorp
Test Root", and sends its alert in a request before EAP-Failure when it
does not.  `tools/drivertest/mksys.py` makes two client certificates from
fixed seeds in PKCS #12 files beside the driver: `alice.pfx` (under the
test root, with the root as an exported chain has it) and `mallory.pfx`
(self-signed).  The `wifi` boot imports both (a wrong password refused
first), lists the store, is told a user without a certificate has none,
is refused with mallory's, signs in with alice's and pings over the
connection.  The host test (`tests/tools/test_eap_peap.py`) runs both
EAP-TLS sign-ins between the kernel's peer and the server, and reads
PKCS #12 files OpenSSL encrypts with AES-256 and with TripleDES, with the
right and a wrong password.  Not yet: WPA3-Enterprise, Enterprise
profiles in `wlanapi.dll` and Settings, PEAP's cryptobinding, and keeping
imported certificates across restarts; the real card needs the T14.
