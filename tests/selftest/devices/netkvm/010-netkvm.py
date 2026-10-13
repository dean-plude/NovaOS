# netkvm: a real Windows network driver on NovaOS's own NDIS host (step
# 30.9).  The boot has QEMU's virtio-net card (PCI 1af4:1000) on its
# user-mode network, which NovaOS's built-in virtio-net driver runs first,
# and virtio-win's signed netkvm package (tools/drivertest/netkvm.py) in
# C:\Tests\NetKVM.  "pnputil /add-driver netkvm.inf /install" installs it
# on the card: the built-in driver steps aside, the PnP manager loads
# netkvm.sys, which registers as an NDIS 6 miniport with ndis.sys
# (kernel/nt/ndis.c), and the running adapter becomes lwIP's network
# adapter.  DHCP, a ping and a download then go through netkvm, and after a
# restart netkvm starts on the card at boot.
DOC = ('virtio-win\'s signed netkvm.sys installed with `pnputil /add-driver /install` on QEMU\'s virtio-net card, '
       'the built-in driver stepping aside: netkvm runs on NovaOS\'s own NDIS host, then DHCP, ping and a '
       '32 MB download over it, and netkvm starting at boot')
NET_ID = '"PCI\\VEN_1AF4&DEV_1000"'
NDIS = 'Windows network adapter (NDIS)'


def _lease(nova):
    """Wait for the DHCP lease the NDIS adapter got (DHCP follows its link-up by a moment)"""
    log = open(nova.serial_path, 'rb').read().decode('latin-1')
    if '[NET] DHCP: address' not in log[log.rfind('[NET] Adapter: ' + NDIS):]:
        nova.sr.wait('[NET] DHCP: address', 90)


def _first_lease(nova):
    """The built-in driver's lease, before netkvm takes the card over"""
    if '[NET] DHCP: address' not in open(nova.serial_path, 'rb').read().decode('latin-1'):
        nova.sr.wait('[NET] DHCP: address', 90)


TESTS = [
    Test('netkvm built-in first', 'ipconfig', [r'Virtio network adapter', r'IPv4 Address[ .]*: 10\.0\.2\.15'],
         builtin=True, before=_first_lease),
    Test('netkvm install', r'pnputil /add-driver C:\Tests\NetKVM\netkvm.inf /install',
         [r'Driver package installed on device: PCI\\VEN_1AF4&DEV_1000',
          r'\[PCI\] [0-9a-f:.]+: virtio-net steps aside for an installed Windows driver',
          r'\[NDIS\] netkvm: NDIS 6\.\d+ miniport registered',
          r'\[NDIS\] netkvm: running, MAC [0-9a-f:]+',
          r'\[PNP\] PCI\\VEN_1AF4&DEV_1000\S*: started \(driver netkvm\)',
          r'\[NET\] Adapter: Windows network adapter \(NDIS\)'], timeout=180),
    Test('netkvm devices', 'pnputil /enum-devices /deviceid ' + NET_ID, [r'Status: +Started', r'Driver Name: +netkvm']),
    Test('netkvm dhcp', 'ipconfig', [r'Windows network adapter \(NDIS\)', r'IPv4 Address[ .]*: 10\.0\.2\.15'],
         builtin=True, before=_lease),
    Test('netkvm ping', 'ping 10.0.2.2', [r'Received = 4, Lost = 0'], builtin=True),
    Test('netkvm download', 'dltest -n 4 -t 10 10.0.2.2 18080 33554432', [r'dltest: PASS'], timeout=600),
    Test('netkvm start at boot', 'shutdown /r /t 0',
         [r'\[PNP\] PCI\\VEN_1AF4&DEV_1000\S*: started \(driver netkvm\)', r'\[NDIS\] netkvm: running'], reboot=True),
    Test('netkvm after boot', 'ping 10.0.2.2', [r'Received = 4, Lost = 0'], builtin=True, before=_lease),
]
