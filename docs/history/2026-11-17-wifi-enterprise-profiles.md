## Wi-Fi: Enterprise profiles in wlanapi.dll, and certutil -delstore

Ninth part of roadmap step 28.1, item 5 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)).  `netsh wlan` and Settings could sign in to
a WPA2-Enterprise network, but a Windows program could not:
`wlanapi.dll` read only a profile's passphrase and called such networks
not connectable.  And nothing removed a certificate once imported.

- **Enterprise profiles.**  A profile with `useOneX` true is read for its
  `OneX` `EapHostConfig`'s outer method, PEAP (25, MS-CHAPv2 inside) or
  EAP-TLS (13), as `netsh wlan export profile` writes it (the XML reader
  now takes namespace prefixes and attributes).  Other methods are
  refused as `ERROR_BAD_PROFILE`.  `WlanGetAvailableNetworkList` lists
  WPA2-Enterprise networks as connectable, and the current connection
  says 802.1X is on.
- **The sign-in.**  `WlanSetProfileEapXmlUserData` takes the
  `EapHostUserCredentials` XML (its `Username`, `Password` and
  `LogonDomain`) for a saved 802.1X profile and keeps it sealed with
  `CryptProtectData` under the user's
  `HKCU\Software\NovaOS\Wlan\EapUserData\NAME` (with
  `WLAN_SET_EAPHOST_DATA_ALL_USERS`, under HKLM with the machine's key);
  `WlanDeleteProfile` removes it with the profile.  `WlanConnect` hands
  the method, user name and password to the kernel (op 20's request
  grew `user` and `eap` fields); without a sign-in EAP-TLS uses the
  newest certificate and PEAP fails with
  `ONEX_UNABLE_TO_IDENTIFY_USER`.  Failures map to the ONEX reasons
  (no identity, EAP failure), with texts in `WlanReasonCodeToString`.
- **`certutil -delstore root|my NAME`** (NAME: the common name or the
  SHA-1 thumbprint) stops trusting an imported root, building the root
  list again without it (the old list stays for handshakes under way;
  built-in roots cannot be removed), or takes a personal certificate and
  its key away (marked removed while NovaOS runs, as an EAP session may
  still hold it), and removes them from the registry: the root's
  thumbprint key, and the personal store's keys written again in order.

The `wifi` boot's `wlantest` gains the Enterprise part: NovaCorp's PEAP
profile refused without a sign-in, `WlanSetProfileEapXmlUserData`
refusing a missing or WPA2-Personal profile and then signing alice in,
and an EAP-TLS profile signing in with her certificate.  After the
restart, `certutil -delstore` removes mallory's certificate and the
NovaCorp root, alice's sign-in is refused as untrusted, and another
restart restores only alice's certificate.  Not yet: WPA3-Enterprise,
PEAP's cryptobinding, and removing a root's copy under
`System32\CertStore` that NetSurf reads; the real card needs the T14.
