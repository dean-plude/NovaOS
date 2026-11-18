## Wi-Fi: WPA3-Enterprise

Tenth part of roadmap step 28.1, item 5 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)).  NovaOS signed in to WPA2-Enterprise
networks with PEAP and EAP-TLS, but refused a WPA3-Enterprise one: an
802.1X-SHA256 network, or 802.1X with management frame protection
required, was "security NovaOS does not support yet".

- **Choosing it.**  A network offering 802.1X-SHA256 (AKM 00-0f-ac:5)
  and able to protect management frames connects with it, as SAE is
  taken over PSK; 802.1X with management frame protection required is
  WPA3-Enterprise too.  The connect asks the driver for
  `WDI_AUTH_ALGO_WPA3_ENT` (11) with MFP on, and the station's RSN
  element says MFP capable (and required when the access point is).
- **The handshake.**  The sign-in (PEAP or EAP-TLS) is unchanged and
  gives the PMK; the 4-way handshake then uses key descriptor version 3:
  the PTK from KDF-SHA256 and AES-128-CMAC MICs (`kernel/nt/wpa.c`), and
  message 3 must bring the IGTK, which the driver gets with the pairwise
  and group keys.
- **What programs see.**  `netsh wlan show networks` and Settings list
  the network as WPA3-Enterprise and ask for a user name and password as
  for WPA2-Enterprise; `wlanapi.dll` gives `DOT11_AUTH_ALGO_WPA3_ENT`
  (11), connectable, with 802.1X on.

The `wifi` boot's access point driver (`wditest.sys`) gains a fifth
network, NovaLab: 802.1X-SHA256 with MFP required, the same RADIUS
server.  alice signs in with PEAP, the driver checks the version 3
handshake, the management group key, DHCP and ping, and rekeys both
group keys.  `tests/host/wpa_handshake.c` runs the version 3 handshake
between the kernel's supplicant and the driver's authenticator on the
host, checks its PTK against Python's KDF-SHA256 and that an SAE-style
version 0 frame is refused.  Not yet: the 192-bit mode (AKM 12,
GCMP-256, SHA-384), PEAP's cryptobinding; the real card needs the T14.
