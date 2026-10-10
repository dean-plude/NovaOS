# adaptertest: iphlpapi's adapter tables on QEMU's user network
# (GetAdaptersAddresses with gateways and prefixes, the IPv6-only view,
# GetAdaptersInfo, GetIpAddrTable, GetNetworkParams' DNS servers); Steam
# reads the computer's own address this way before it connects
DOC = '`adaptertest` (GetAdaptersAddresses / GetAdaptersInfo / GetIpAddrTable / GetNetworkParams: the adapter, its address, gateway and DNS server), 64- and 32-bit'
TESTS = [
    Test('adaptertest', 'adaptertest', [r'adaptertest: \d+ passed, 0 failed']),
    Test('adaptertest x86', r'C:\Programs\x86\adaptertest.exe', [r'adaptertest: \d+ passed, 0 failed']),
]
