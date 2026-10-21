## Network security: a filtering engine and the Windows Firewall

NovaOS filters its network traffic the Windows 11 way
([design](design/network-security.md)).

- **Engine:** a kernel engine shaped like the Windows Filtering Platform
  beside lwIP: the ALE connect, accept and listen layers and the inbound
  and outbound transport layers (IPv4 and IPv6), sublayers and weighted
  block and permit filters (hard permits too), conditions on protocol,
  addresses, ports, the program, loopback and the interface,
  transactions, and dynamic and persistent objects.  A refused `connect`
  fails with `WSAEACCES`; refused inbound connections and datagrams are
  dropped.
- **Firewall:** Domain, Private and Public profiles and rules, kept in
  `FirewallPolicy` as Windows keeps them.  A new network is Public
  (inbound blocked unless a rule allows it), known again by its gateway,
  and a small prompt asks whether it may be Private.
- **New DLLs:** `fwpuclnt.dll` (the WFP management API) and
  `hnetcfg.dll` (`INetFwPolicy2`, `INetFwRules`, `INetFwRule3`).
- **Terminal and Settings:** `netsh advfirewall` (rules, profile state,
  policy, reset) and a Settings > Firewall page.
- **Test:** `firewalltest` (network suite, 64- and 32-bit), with
  connections from the host through QEMU's port forwarding.
