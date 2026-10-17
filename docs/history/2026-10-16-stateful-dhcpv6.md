## Stateful DHCPv6

NovaOS now gets an IPv6 address from a DHCPv6 server (RFC 8415) when a
router's advertisement sets the "managed" (M) flag, as Windows does.  lwIP
2.2.0's DHCPv6 client only did stateless DHCPv6, and no BSD or MIT client
fitted lwIP's single-threaded kernel model (dhcpcd and WIDE-DHCPv6 are
userspace daemons), so the stateful half is written into lwIP's own
BSD-licensed `dhcp6.c`, beside the stateless code it shares the parser and
DNS handling with:

- a Solicit (after a random delay of up to a second), collecting
  Advertises for the first retransmission timeout (one with Preference 255
  at once), then a Request for one IA_NA with the advertised address;
- the Reply's address goes on the interface with the server's valid and
  preferred lifetimes, and its DNS servers and domain search list are used;
- a Renew to that server at T1, a Rebind to any server at T2 (T1 and T2
  default to half and four fifths of the preferred lifetime when the server
  leaves them to the client), and a new Solicit when the valid lifetime
  ends or a server says the binding is gone or not on this link;
- a Decline when duplicate address detection finds the address in use;
- a Release on `ipconfig /release6` and before the machine shuts down or
  restarts; `ipconfig /renew6` renews (or solicits again); the link going
  down drops the lease, and the next advertisement with the M flag starts
  over;
- retransmission as section 15 says, with the constants of section 7.6.

An advertisement with only the "other configuration" (O) flag still gets
an Information-request (stateless DHCPv6).  When the leased address's
prefix is also advertised for SLAAC, the SLAAC address is made beside it
and router advertisements no longer touch the lease's lifetimes.  The
interface holds four IPv6 addresses now (link-local, two SLAAC, the lease).

The lease shows where DHCPv4's does: `ipconfig` gives the connection's DNS
suffix, and `ipconfig /all` marks the address "(DHCPv6)" and adds the
DHCPv6 IAID, client DUID (a DUID-LL of the MAC), server, the lease's time
left with T1 and T2, and the DNS suffix search list; `GetAdaptersAddresses`
reports the address with `Dhcp` origins, its real lifetimes and a /128
on-link prefix, the adapter's `Dhcpv6Server`, `Dhcpv6ClientDuid`,
`Dhcpv6Iaid`, `DnsSuffix` and `FirstDnsSuffix` list (SLAAC addresses now
say `IpPrefixOriginRouterAdvertisement` with their real lifetimes too);
Settings > Network shows the leased address, when the lease expires and
the search list, and says "Connected" on an IPv6-only network with a
lease.

The network suite's IPv6 peer (`tools/v6peer.py`) is now a stateful DHCPv6
server too: it leases `fd00:6e6f:7661::100` with a second DNS server and
the search list `nova6.test, example.test`, gives the first lease T1 4 s
and T2 8 s and leaves its Renews unanswered, so the client must Rebind;
the next Renew is answered with a long lease.  The new tests check
`ipconfig /all`, `dhcp6test` (64- and 32-bit), the rebind and renew while
`ping -6` runs, `ipconfig /release6` (the server confirms) and
`ipconfig /renew6`.
