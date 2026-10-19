## Wi-Fi: Settings' Wi-Fi page and wlanapi.dll

Fifth part of roadmap step 28.1.  NovaOS joined open, WPA2-Personal and
WPA3-Personal networks from the Terminal.  Now people can do it from
Settings, and Windows programs through the Native Wifi API.

- **Settings > Wi-Fi** shows the Wi-Fi card and its connection, lists the
  networks in range with their security, signal, band and channel (a
  scan runs when the page opens, and on Refresh or F5), and joins one:
  click it or press Down, type its password, Connect or Enter.  A wrong
  password says so; Disconnect leaves.  Search finds it as "Wi-Fi".
- **`wlanapi.dll`** (`userland/wlanapi`): `WlanOpenHandle`,
  `WlanEnumInterfaces`, `WlanScan`, `WlanGetAvailableNetworkList`,
  `WlanGetNetworkBssList`, `WlanConnect`, `WlanDisconnect`,
  `WlanQueryInterface`, `WlanSetProfile`, `WlanGetProfile`,
  `WlanDeleteProfile`, `WlanGetProfileList`, `WlanRegisterNotification`
  and the memory and reason-code helpers.  As on Windows, a scan and a
  connection return at once and finish with a notification
  (`scan_complete`, `connection_complete` with its reason code).
  Profiles are Windows' XML, kept in the registry; NovaOS reads their
  SSID and passphrase.  An interface's GUID comes from its MAC address.
- **The kernel side** (`kernel/um/um_wlan.c`): `NtNovaSockCtl` ops 18 to
  21 give user mode the adapters, a scan, a connection and a
  disconnection, over the WDI upper edge's `WlanScan` and `WlanConnect`
  (where Windows' `wlanapi.dll` asks the WLAN AutoConfig service).

The `wifi` boot runs `wlantest` against the test driver's three access
points (the lists, a profile, a wrong passphrase refused with the
key-mismatch reason, joining by profile and by SSID, the connection's
details) and joins the WPA2 network in Settings by typing, as a person
would.  Not yet: SAE's hash-to-element and other groups, Enterprise
networks; the real card needs the T14.
