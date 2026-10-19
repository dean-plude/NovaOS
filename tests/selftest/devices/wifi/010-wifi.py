# wifi: NovaOS's WDI command layer (step 28.1, kernel/nt/wdi.c).  QEMU has no
# Wi-Fi card and Intel's own driver stops at its hardware probe on a
# stand-in (the ax211 boot), so QEMU's edu device (-device edu, PCI
# 1234:11e8) gets wditest.sys (tools/drivertest/wditest.c, in C:\Tests\Wifi),
# a WDI miniport as Intel's Netwtw14.sys is one, whose radio hears three
# access points.  With test signing on, "pnputil /add-driver wditest.inf
# /install" installs it: it registers with ndis.sys as a WDI miniport, the
# upper edge allocates and opens its adapter, asks its capabilities
# (OID_WDI_GET_ADAPTER_CAPABILITIES: MAC address, firmware, radio, bands)
# and creates its station port (OID_WDI_TASK_CREATE_PORT, finished by the
# create-port-complete indication).  "netsh wlan show interfaces" lists the
# adapter; "netsh wlan show networks mode=bssid" scans (OID_WDI_TASK_SCAN:
# the driver pends it, indicates BSS entries with each access point's
# beacon, completes it and indicates the scan's end) and lists the networks
# by SSID with their security from the beacons' RSN elements (WPA2-Personal,
# WPA3-Personal with SAE, open), signal, band and channel.  The data path
# (the driver's TAL TX/RX handlers, from MiniportWdiTalTxRxInitialize) is
# initialized and started at bring-up.  "netsh wlan connect name=NovaGuest"
# joins the open network (OID_WDI_TASK_CONNECT: the driver indicates the
# access point as its peer and the association's result); the adapter
# becomes the network stack's, and DHCP and ping run over Wi-Fi: lwIP's
# Ethernet frames go out as 802.11 MSDUs (TxDataSend, TxDequeueIndication,
# TxTransferCompleteIndication) to the router the driver plays behind the
# access point, and its answers come back as 802.11 frames
# (RxInorderDataIndication, RxGetMpdus, RxReturnFrames).  Disconnecting
# gives the stack its Ethernet card back.  NovaNet is WPA2-Personal:
# without a key netsh says so, and "netsh wlan connect name=NovaNet key=..." runs the 4-way
# handshake (kernel/nt/wpa.c) with the access point the driver plays: EAPOL
# exempted from encryption first (OID_WDI_SET_PRIVACY_EXEMPTION_LIST), the
# EAPOL-Key frames up and down the data path, then the pairwise and group
# keys to the driver (OID_WDI_SET_ADD_CIPHER_KEYS), which checks they are
# the ones it derived.  A wrong passphrase stops at message 2 ("The network
# security key isn't correct").  With the right one DHCP and ping run over
# the secured connection, the access point's group key handshake replaces
# the group key mid-ping, and netsh shows WPA2-Personal with CCMP.
# NovaNet-5G is WPA3-Personal: "netsh wlan connect name=NovaNet-5G key=..."
# runs SAE (kernel/nt/sae.c) through the driver as Windows does, the
# driver asking for the station's commit and passing up the access
# point's commit and confirm (NDIS_STATUS_WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED),
# NovaOS answering with OID_WDI_SET_SAE_AUTH_PARAMS; the driver checks
# each against its own SAE (tools/drivertest/saeap.h).  A wrong password
# fails at the station's confirm ("The network security key isn't
# correct"); the right one associates, the 4-way handshake runs WPA3's
# way from SAE's PMK and brings the management frame protection key
# (IGTK) with the group key, both checked by the driver, and DHCP, ping
# and a rekey of both group keys run over it.
# Disabling the device deletes the port, stops the data path and closes the
# adapter, and the interface is gone.
DOC = ('a WDI Wi-Fi miniport (wditest.sys) on QEMU\'s edu device, installed with `pnputil /add-driver /install`: '
       'NovaOS\'s WDI command layer reads its capabilities and creates its station port, `netsh wlan show interfaces` '
       'lists it and `netsh wlan show networks mode=bssid` scans (OID_WDI_TASK_SCAN and its BSS entry indications) '
       'and lists three networks (WPA2-Personal, WPA3-Personal, open) with signal, band and channel; '
       '`netsh wlan connect name=NovaGuest` joins the open one, and DHCP and ping run over the WDI data path (802.11 '
       'frames through the driver\'s TAL TX/RX handlers); disconnecting gives the Ethernet card back; '
       '`netsh wlan connect name=NovaNet key=...` joins the WPA2-Personal one (EAPOL exempted, the 4-way handshake '
       'over the data path, the pairwise and group keys given to the driver with OID_WDI_SET_ADD_CIPHER_KEYS and '
       'checked by it), a wrong passphrase is refused at message 2, and DHCP, ping and a group key handshake run over '
       'the secured connection; `netsh wlan connect name=NovaNet-5G key=...` joins the WPA3-Personal one (SAE through '
       'the driver: its SAE_AUTH_PARAMS_NEEDED indications answered with OID_WDI_SET_SAE_AUTH_PARAMS, then the WPA3 '
       '4-way handshake with management frame protection\'s IGTK), a wrong password is refused at SAE\'s confirm, and '
       'DHCP, ping and a rekey run over it; disabling the device deletes the port, stops the data path and closes the '
       'adapter')
EDU_ID = '"PCI\\VEN_1234&DEV_11E8"'
def _dhcp(nova):
    """wait for the lease: lwIP probes the offered address with ARP (ACD)
    for a few seconds after the driver's acknowledgement before it takes it"""
    lease = '[NET] DHCP: address 192.168.77.100, gateway 192.168.77.1, DNS 192.168.77.1'
    if lease not in open(nova.serial_path, 'rb').read().decode('latin-1'):
        nova.sr.wait(lease, 60)


def _leases(n):
    """the same for a later connection: the @n-th lease"""
    def wait(nova):
        lease = '[NET] DHCP: address 192.168.77.100, gateway 192.168.77.1, DNS 192.168.77.1'
        for _ in range(240):
            if open(nova.serial_path, 'rb').read().decode('latin-1').count(lease) >= n:
                return
            nova.sr.wait(lease, 0.25)
    return wait


_dhcp_again = _leases(2)        # (NovaNet's connection)


TESTS = [
    Test('wifi test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2, elevated=True),
    Test('wifi restart', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    Test('wifi no interface', 'netsh wlan show interfaces', [r'There is no wireless interface on the system\.'],
         builtin=True),
    Test('wifi install', r'pnputil /add-driver C:\Tests\Wifi\wditest.inf /install',
         [r'Driver package installed on device: PCI\\VEN_1234&DEV_11E8',
          r'\[NDIS\] WdiTest: WDI miniport registered \(NDIS 6\.50, WDI version 0x10108\)',
          r'\[DbgPrint\] wditest: adapter allocated',
          r'\[DbgPrint\] wditest: adapter open: radio on, 3 access points in range',
          r'\[NDIS\] WdiTest: WDI: open adapter completed \(0x00000000\)',
          r'\[DbgPrint\] wditest: data path initialized \(API revision 2\)',
          r'\[WDI\] WdiTest: data path initialized \(0x00000000\), TX, RX',
          r'\[DbgPrint\] wditest: adapter capabilities answered',
          r'\[WDI\] WdiTest: adapter capabilities: MAC 02:4e:6f:76:61:00, firmware wditest 1\.0, radio on/on, bands 2\.4 5',
          r'\[DbgPrint\] wditest: adapter configuration: 5 signal bars',
          r'\[WDI\] WdiTest: adapter configured \(0x00000000\)',
          r'\[DbgPrint\] wditest: data path started: message based, port queueing, 1 port\(s\), 1 peer\(s\)',
          r'\[WDI\] WdiTest: data path started \(0x00000000\): port queueing, 1 peer\(s\), 16 transfers at a time',
          r'\[DbgPrint\] wditest: create port task \d+: modes 0x1, NDIS port 0',
          r'\[DbgPrint\] wditest: station port 1 created',
          r'\[WDI\] WdiTest: station port 1 created \(MAC 02:4e:6f:76:61:00\)',
          r'\[DbgPrint\] wditest: data path port 1 added \(mode 0x1\)',
          r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver WdiTest\)']),
    Test('wifi interfaces', 'netsh wlan show interfaces',
         [r'There is 1 interface on the system:', r'Name +: Wi-Fi\n',
          r'Description +: NovaOS WDI test Wi-Fi adapter', r'Physical address +: 02:4e:6f:76:61:00',
          r'State +: disconnected', r'Radio status +: Hardware On\n +Software On'], builtin=True),
    Test('wifi scan', 'netsh wlan show networks mode=bssid',
         [r'\[DbgPrint\] wditest: scan task \d+ on port 1: wildcard BSSID, any SSID, type 3, dwell 20/110 ms, at most 4000 ms',
          r'\[DbgPrint\] wditest: scan complete: 3 BSS entries indicated',
          r'\[WDI\] WdiTest: scan on port 1: 0x00000000, 3 access point\(s\)',
          r'Interface name : Wi-Fi', r'There are 3 networks currently visible\.',
          r'SSID 1 : NovaNet\n +Network type +: Infrastructure\n +Authentication +: WPA2-Personal\n +Encryption +: CCMP\n'
          r' +BSSID 1 +: 02:4e:6f:76:61:01\n +Signal +: 90% *\n +Radio type +: 802\.11n\n +Band +: 2\.4 GHz\n'
          r' +Channel +: 6\n',
          r'SSID 2 : NovaNet-5G\n +Network type +: Infrastructure\n +Authentication +: WPA3-Personal\n'
          r' +Encryption +: CCMP\n +BSSID 1 +: 02:4e:6f:76:61:02\n +Signal +: 72% *\n +Radio type +: 802\.11ax\n'
          r' +Band +: 5 GHz\n +Channel +: 36\n',
          r'SSID 3 : NovaGuest\n +Network type +: Infrastructure\n +Authentication +: Open\n +Encryption +: None\n'
          r' +BSSID 1 +: 02:4e:6f:76:61:03\n +Signal +: 35% *\n +Radio type +: 802\.11n\n +Band +: 2\.4 GHz\n'
          r' +Channel +: 11\b'], builtin=True),
    Test('wifi networks', 'netsh wlan show networks',
         [r'There are 3 networks currently visible\.', r'SSID 2 : NovaNet-5G\n +Network type +: Infrastructure\n'
          r' +Authentication +: WPA3-Personal\n +Encryption +: CCMP\n'], builtin=True),
    Test('wifi secured', 'netsh wlan connect name=NovaNet',
         [r'The network "NovaNet" is secured: give its passphrase with key=PASSPHRASE\.'], builtin=True),
    Test('wifi short key', 'netsh wlan connect name=NovaNet key=short',
         [r'A WPA2 passphrase is 8 to 63 characters, or 64 hexadecimal digits; a WPA3 password 8 to 63 characters\.'],
         builtin=True),
    Test('wifi connect', 'netsh wlan connect name=NovaGuest',
         [r'\[DbgPrint\] wditest: connect task \d+ on port 1: SSID NovaGuest, auth 1, 1 BSS entry, first 02:4e:6f:76:61:03',
          r'\[WDI\] WdiTest: peer 1 on port 1: 02:4e:6f:76:61:03',
          r'\[DbgPrint\] wditest: associated with NovaGuest, peer 1',
          r'\[DbgPrint\] wditest: peer 1 on port 1 configured \(QoS 0\)',
          r'\[WDI\] WdiTest: connected to NovaGuest \(02:4e:6f:76:61:03\) on port 1, peer 1: open, no cipher, WMM off',
          r'Connection request was completed successfully\.',
          r'\[NET\] Adapter: NovaOS WDI test Wi-Fi adapter, MAC 02:4e:6f:76:61:00',
          r'\[DbgPrint\] wditest: DHCP discover: offering 192\.168\.77\.100',
          r'\[DbgPrint\] wditest: DHCP request: acknowledging 192\.168\.77\.100'], builtin=True),
    Test('wifi ping', 'ping 192.168.77.1',
         [r'\[DbgPrint\] wditest: ARP: 192\.168\.77\.1 is at the router',
          r'\[DbgPrint\] wditest: ping from 192\.168\.77\.100 to the router: answering',
          r'Reply from 192\.168\.77\.1', r'Received = 4, Lost = 0'], builtin=True, before=_dhcp),
    Test('wifi connected', 'netsh wlan show interfaces',
         [r'State +: connected\n +SSID +: NovaGuest\n +AP BSSID +: 02:4e:6f:76:61:03\n +Band +: 2\.4 GHz\n'
          r' +Channel +: 11\n +Network type +: Infrastructure\n +Radio type +: 802\.11n\n +Authentication +: Open\n'
          r' +Cipher +: None\n +Signal +: 35%', r'Frames sent/received +: [1-9]\d*/[1-9]\d*'], builtin=True),
    Test('wifi ipconfig', 'ipconfig',
         [r'Wireless LAN adapter Wi-Fi:', r'Physical Address\. \. : 02-4e-6f-76-61-00',
          r'IPv4 Address\. \. \. \. : 192\.168\.77\.100', r'Default Gateway \. \. : 192\.168\.77\.1'], builtin=True),
    Test('wifi disconnect', 'netsh wlan disconnect',
         [r'\[DbgPrint\] wditest: disconnect task \d+: from 02:4e:6f:76:61:03, reason 3',
          r'\[WDI\] WdiTest: peer 1 on port 1 deleted',
          r'\[DbgPrint\] wditest: peer 1 on port 1 deleted \([1-9]\d* frames sent, [1-9]\d* received\)',
          r'\[WDI\] WdiTest: disconnected from NovaGuest \(0x00000000\)',
          r'\[NET\] Adapter NovaOS WDI test Wi-Fi adapter gone: (back to|loopback)',
          r'Disconnection request was completed successfully for interface "Wi-Fi"\.'], builtin=True),
    Test('wifi wrong key', 'netsh wlan connect name=NovaNet key=not-the-passphrase',
         [r'\[DbgPrint\] wditest: privacy exemption: Ethertype 888e, action 2, packets 3',
          r'\[WPA\] WdiTest: EAPOL exempted from encryption \(0x00000000\)',
          r'\[DbgPrint\] wditest: connect to NovaNet: auth 7, ciphers 4/4, exclude unencrypted 1, EAPOL exempted yes',
          r'\[DbgPrint\] wditest: 4-way handshake message 1 \(ANonce\) to the station',
          r'\[WPA\] WdiTest: 4-way handshake message 1 \(the ANonce\): sending message 2',
          r'\[DbgPrint\] wditest: 4-way handshake message 2 refused: a wrong MIC \(not the passphrase\); no message 3',
          r'\[WPA\] WdiTest: the 4-way handshake did not finish: no message 3 after message 2',
          r'\[WDI\] WdiTest: disconnected from NovaNet',
          r'The network security key isn\'t correct: "NovaNet" did not accept it\.'], builtin=True),
    Test('wifi wpa2 connect', 'netsh wlan connect name=NovaNet key=nova-wpa2-passphrase',
         [r'\[DbgPrint\] wditest: connect task \d+ on port 1: SSID NovaNet, auth 7, 1 BSS entry, first 02:4e:6f:76:61:01',
          r'\[WDI\] WdiTest: connected to NovaNet \(02:4e:6f:76:61:01\) on port 1, peer 1: WPA2-Personal, CCMP, WMM off',
          r'\[WPA\] WdiTest: 4-way handshake message 1 \(the ANonce\): sending message 2',
          r'\[DbgPrint\] wditest: 4-way handshake message 2: MIC good, RSN element its association\'s; sending message 3',
          r'\[WPA\] WdiTest: 4-way handshake message 3 \(MIC good, group key\): sending message 4',
          r'\[DbgPrint\] wditest: 4-way handshake message 4: MIC good; the handshake is done',
          r'\[DbgPrint\] wditest: pairwise key for 02:4e:6f:76:61:01: the 4-way handshake\'s',
          r'\[DbgPrint\] wditest: group key 1: the access point\'s',
          r'\[WPA\] WdiTest: pairwise key and group key 1 installed \(0x00000000\)',
          r'\[WPA\] WdiTest: 4-way handshake done: the port is authorized',
          r'Connection request was completed successfully\.',
          r'\[NET\] Adapter: NovaOS WDI test Wi-Fi adapter, MAC 02:4e:6f:76:61:00',
          r'\[DbgPrint\] wditest: DHCP request: acknowledging 192\.168\.77\.100'], builtin=True),
    Test('wifi wpa2 ping', 'ping 192.168.77.1',
         [r'\[DbgPrint\] wditest: group key handshake: sending group key 2',
          r'\[WPA\] WdiTest: group key handshake message 1 \(a new group key\): sending message 2',
          r'\[DbgPrint\] wditest: group key handshake message 2: MIC good',
          r'\[DbgPrint\] wditest: group key 2: the access point\'s',
          r'\[WPA\] WdiTest: group key 2 installed \(0x00000000\)',
          r'Reply from 192\.168\.77\.1', r'Received = 4, Lost = 0'], builtin=True, before=_dhcp_again),
    Test('wifi wpa2 connected', 'netsh wlan show interfaces',
         [r'State +: connected\n +SSID +: NovaNet\n +AP BSSID +: 02:4e:6f:76:61:01\n +Band +: 2\.4 GHz\n'
          r' +Channel +: 6\n +Network type +: Infrastructure\n +Radio type +: 802\.11n\n'
          r' +Authentication +: WPA2-Personal\n +Cipher +: CCMP\n'], builtin=True),
    Test('wifi wpa2 disconnect', 'netsh wlan disconnect',
         [r'\[WDI\] WdiTest: disconnected from NovaNet \(0x00000000\)',
          r'Disconnection request was completed successfully for interface "Wi-Fi"\.'], builtin=True),
    Test('wifi wpa3 wrong password', 'netsh wlan connect name=NovaNet-5G key=not-the-password',
         [r'\[DbgPrint\] wditest: connect to NovaNet-5G: auth 9, ciphers 4/4, exclude unencrypted 1, MFP 1, '
          r'EAPOL exempted yes',
          r'\[DbgPrint\] wditest: SAE: asked the host for the station\'s commit',
          r'\[SAE\] WdiTest: commit \(group 19\) to the access point',
          r'\[DbgPrint\] wditest: SAE: the station\'s commit \(group 19\): good',
          r'\[DbgPrint\] wditest: SAE: the access point\'s commit to the host',
          r'\[SAE\] WdiTest: the access point\'s commit is good: confirm to it',
          r'\[DbgPrint\] wditest: SAE: the station\'s confirm: the station\'s confirm is wrong \(not the password\)',
          r'\[DbgPrint\] wditest: SAE with NovaNet-5G failed: not associated',
          r'\[SAE\] WdiTest: no confirm from the access point: the password is not NovaNet-5G\'s',
          r'The network security key isn\'t correct: "NovaNet-5G" did not accept it\.'], builtin=True),
    Test('wifi wpa3 connect', 'netsh wlan connect name=NovaNet-5G key=nova-wpa3-password',
         [r'\[DbgPrint\] wditest: connect task \d+ on port 1: SSID NovaNet-5G, auth 9, 1 BSS entry, first 02:4e:6f:76:61:02',
          r'\[DbgPrint\] wditest: SAE: the station\'s commit \(group 19\): good',
          r'\[DbgPrint\] wditest: SAE: the station\'s confirm: good \(the password\)',
          r'\[DbgPrint\] wditest: SAE: the access point\'s confirm to the host',
          r'\[SAE\] WdiTest: the access point\'s confirm is good: authenticated',
          r'\[DbgPrint\] wditest: SAE: the host says success: associating',
          r'\[WDI\] WdiTest: connected to NovaNet-5G \(02:4e:6f:76:61:02\) on port 1, peer 1: WPA3-Personal, CCMP, '
          r'WMM off',
          r'\[DbgPrint\] wditest: 4-way handshake message 1 \(ANonce\) to the station \(WPA3: from SAE\'s PMK\)',
          r'\[DbgPrint\] wditest: 4-way handshake message 2: MIC good, RSN element its association\'s; sending message 3',
          r'\[DbgPrint\] wditest: 4-way handshake message 4: MIC good; the handshake is done',
          r'\[DbgPrint\] wditest: pairwise key for 02:4e:6f:76:61:02: the 4-way handshake\'s',
          r'\[DbgPrint\] wditest: management group key \(IGTK\) 4: the access point\'s',
          r'\[WPA\] WdiTest: pairwise key and group key 1 and management group key 4 installed \(0x00000000\)',
          r'\[WPA\] WdiTest: 4-way handshake done: the port is authorized',
          r'Connection request was completed successfully\.'], builtin=True),
    Test('wifi wpa3 ping', 'ping 192.168.77.1',
         [r'\[DbgPrint\] wditest: group key handshake: sending group key 2',
          r'\[DbgPrint\] wditest: management group key \(IGTK\) 5: the access point\'s',
          r'\[WPA\] WdiTest: group key 2 and management group key 5 installed \(0x00000000\)',
          r'Reply from 192\.168\.77\.1', r'Received = 4, Lost = 0'], builtin=True, before=_leases(3)),
    Test('wifi wpa3 connected', 'netsh wlan show interfaces',
         [r'State +: connected\n +SSID +: NovaNet-5G\n +AP BSSID +: 02:4e:6f:76:61:02\n +Band +: 5 GHz\n'
          r' +Channel +: 36\n +Network type +: Infrastructure\n +Radio type +: 802\.11ax\n'
          r' +Authentication +: WPA3-Personal\n +Cipher +: CCMP\n'], builtin=True),
    Test('wifi wpa3 disconnect', 'netsh wlan disconnect',
         [r'\[WDI\] WdiTest: disconnected from NovaNet-5G \(0x00000000\)',
          r'Disconnection request was completed successfully for interface "Wi-Fi"\.'], builtin=True),
    Test('wifi disable', 'pnputil /disable-device /deviceid ' + EDU_ID,
         [r'Device disabled successfully', r'\[DbgPrint\] wditest: station port 1 deleted',
          r'\[DbgPrint\] wditest: data path port 1 deleted',
          r'\[WDI\] WdiTest: station port 1 deleted \(0x00000000\)', r'\[DbgPrint\] wditest: data path stopped',
          r'\[DbgPrint\] wditest: data path deinitialized', r'\[WDI\] WdiTest: data path stopped',
          r'\[DbgPrint\] wditest: adapter closed(?! \()',
          r'\[NDIS\] WdiTest: WDI: adapter closed \(0x00000000\)', r'\[DbgPrint\] wditest: adapter freed']),
    Test('wifi gone', 'netsh wlan show interfaces', [r'There is no wireless interface on the system\.'],
         builtin=True),
]
