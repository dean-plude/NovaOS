## Programs see the network adapter

iphlpapi's adapter tables were empty, the answer Windows gives on a
machine without IP configured, so Steam asserted "CalcUnIPThisBox -
GetAdaptersAddresses returned 232" when it looked for the computer's own
address, and Chromium's network watchers found no adapter.
`GetAdaptersAddresses` now lists the adapter the way Windows does: its
`{GUID}` name, "Ethernet", the driver's description, MAC, link state,
IPv6 and IPv4 addresses with their prefix lengths, DNS servers and, when
asked, the gateway and subnet prefixes, all inside the caller's buffer
(`IP_ADAPTER_ADDRESSES_LH`, 448 bytes, 376 in 32-bit programs), with the
size query and the IPv4- and IPv6-only views.  `GetAdaptersInfo`,
`GetIpAddrTable` and `GetNumberOfInterfaces` report the same adapter, and
`GetNetworkParams` now names the DNS servers too.  The kernel hands the
adapter's configuration to iphlpapi through `NtNovaSockCtl` (operation
16).  `adaptertest` (network suite, 64- and 32-bit) checks the tables on
QEMU's network.

Steam's login window still does not open: its interface waits on Steam
Input before the login step (docs/compatibility.md, Steam).
