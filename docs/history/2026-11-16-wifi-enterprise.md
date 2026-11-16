## Wi-Fi: WPA2-Enterprise networks sign in with PEAP

Sixth part of roadmap step 28.1, item 5 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)).  NovaOS joined open, WPA2-Personal and
WPA3-Personal networks.  Now it signs in to WPA2-Enterprise networks, the
kind offices and universities run, with a user name and password, the way
Windows does by default: PEAP with MS-CHAPv2 inside it.

- **`netsh wlan connect name=SSID user=NAME key=PASSWORD`** on a network
  whose RSN element offers 802.1X (AKM 00-0f-ac:1) with CCMP signs in,
  then runs the 4-way handshake.  Without `user=` netsh says the network
  is WPA2-Enterprise; Settings' Wi-Fi page says to sign in from the
  Terminal.
- **The EAP peer** (`kernel/nt/eap.c`): EAP over EAPOL (802.1X), the
  identity, then PEAP version 0 ([MS-PEAP]): a TLS 1.2 tunnel to the
  authentication server, its fragments acknowledged and reassembled; inside,
  the inner identity, EAP-MSCHAPv2 (RFC 2759: the NT-Response from the
  password, and the server's own response checked, so a server that does
  not know the password is refused too) and the Result TLV.  On
  EAP-Success the tunnel's keying material ("client EAP encryption") gives
  the MSK, and its first half is the PMK the 4-way handshake starts from.
  MD4, which Mbed TLS 3 dropped, is RFC 1320's; SHA-1 and DES (now on in
  the kernel's Mbed TLS) are Mbed TLS's.
- **The server's certificate must be trusted** before any password goes
  out: the tunnel is an EAP session of `net/tls.c` (TLS 1.2 over memory,
  Mbed TLS) that checks the chain against the trusted roots, as Windows
  wants by default.  An untrusted one stops the sign-in with "has a
  certificate from a root that is not trusted"; `certutil -addstore root`
  trusts a company's CA.
- **Clear errors**: a wrong password or unknown user (MS-CHAPv2's E=691)
  is "The network security key isn't correct".
- **`wlanapi.dll`** reports such a network with its RSNA authentication
  (6), not connectable through the API yet.

The `wifi` boot's radio hears a fourth access point, `NovaCorp`, and
`wditest.sys` plays its authentication server too
(`tools/drivertest/eapap.h`, written from the same documents apart from
NovaOS's peer): Mbed TLS's TLS server compiled into the driver
(`tools/drivertest/mbedtls/`: its configuration and the few C library
headers a driver lacks), a test certificate from fixed seeds
("radius.novacorp.test" under "NovaCorp Test Root",
`tools/drivertest/mksys.py`), one user and its MS-CHAPv2 check.  The test
signs in before the root is trusted (refused), trusts it, signs in with a
wrong password (refused), then the right one, and gets DHCP, pings and a
group rekey over the connection.  A host test
(`tests/tools/test_eap_peap.py`) checks both sides' MD4 and MS-CHAPv2
against RFC 1320's and RFC 2759's vectors and runs whole sign-ins
between them: the same MSK at both ends, a wrong password, an unknown user,
a `DOMAIN\user` name and an untrusted server.  Not yet: EAP-TLS (a client
certificate), WPA3-Enterprise, Enterprise profiles in `wlanapi.dll` and
Settings, and PEAP's cryptobinding; the real card needs the T14.
