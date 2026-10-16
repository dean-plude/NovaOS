# the network suite's IPv6 boot, stateful DHCPv6 (RFC 8415): tools/v6peer.py's
# router advertisements carry the M flag and it leases fd00:6e6f:7661::100
# with a DNS server and the search list nova6.test, example.test.  The lease
# in ipconfig /all and GetAdaptersAddresses (dhcp6test); the short first
# lease, whose Renews the peer leaves unanswered, rebound at T2 and then
# renewed while ping -6 runs; ipconfig /release6 (the server confirms) and
# /renew6 (a new Solicit gets it back)
LEASED = r'(?i)fd00:6e6f:7661::100'
TESTS = [
    Test('DHCPv6 lease', 'ipconfig /all', [LEASED + r' \(DHCPv6\)',
                                           r'Connection-specific DNS Suffix[ .]*: nova6\.test',
                                           r'DHCPv6 IAID[ .]*: \d+',
                                           r'DHCPv6 Client DUID[ .]*: 00-03-00-01(-[0-9A-F]{2}){6}',
                                           r'(?i)DHCPv6 Server[ .]*: fe80::1',
                                           r'DNS Suffix Search List[ .]*: nova6\.test\s+example\.test'], builtin=True),
    Test('dhcp6test', 'dhcp6test', [r'dhcp6test: \d+ passed, 0 failed']),
    Test('dhcp6test x86', r'C:\Programs\x86\dhcp6test.exe', [r'dhcp6test: \d+ passed, 0 failed']),
    Test('DHCPv6 rebind and renew', 'ping -6 -n 12 nova6.test', [r'Received = 12, Lost = 0'], builtin=True,
         boot_expect=[r'\[NET\] DHCPv6: address ' + LEASED[4:] + r' from server (?i:fe80::1) \(T1 4 s, T2 8 s, valid 60 s\)',
                      r'\[NET\] DHCPv6: lease on ' + LEASED[4:] + r' rebound by server',
                      r'\[NET\] DHCPv6: lease on ' + LEASED[4:] + r' renewed \(valid 900 s\)']),
    Test('ipconfig /release6', 'ipconfig /release6', [r'Released the DHCPv6 lease \(the server confirmed\)',
                                                      r'release confirmed by server'], builtin=True),
    Test('ipconfig /renew6', 'ipconfig /renew6', [r'Renewed the DHCPv6 lease', r'IPv6 Address[ .]*: ' + LEASED[4:]],
         builtin=True),
]
