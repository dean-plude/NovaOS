## Network security: the Network List Manager

Part of the roadmap's network security item (part 2,
[design](design/network-security.md)).  Programs ask Windows whether they
are online, and on which kind of network, through the Network List
Manager (`INetworkListManager`, `netprofm.dll`): browsers and chat
programs check `IsConnectedToInternet` before they go online, Office and
store clients ask `INetworkCostManager` whether the connection is
metered, and installers set a network Private with `INetwork::SetCategory`.
NovaOS had no `netprofm.dll`, so `CoCreateInstance` of the class failed.

- **The class.**  `netprofm.dll` serves `NetworkListManager`
  (`{DCB00C01-...}`, registered in process, both threading models):
  `INetworkListManager`, `INetworkCostManager` and
  `IConnectionPointContainer`.
- **What it says.**  Connectivity comes from the adapter: IPv4 with a
  default gateway is `IPV4_INTERNET` (without one `IPV4_SUBNET`), an IPv6
  address from a router or DHCPv6 is `IPV6_INTERNET`.  The networks are
  the firewall's own list under NetworkList's `Profiles` (name,
  description, category); the one the PC is on now is the firewall's.
  The one connection is the adapter, by the GUID `iphlpapi` names it
  with.  A wired network costs nothing: unrestricted, the data plan
  unknown (`SetSimulatedProfileInfo` changes that, as on Windows).
- **Changes.**  `SetCategory` on the network the PC is on goes through
  the firewall, so the Private or Public profile is in force at once;
  `SetName`, `SetDescription` and other networks' categories are written
  where Windows keeps them.  All need an administrator.
- **Events.**  Advising a sink starts a thread that looks once a second
  and calls `INetworkListManagerEvents::ConnectivityChanged`,
  `INetworkEvents` (networks added and deleted, their connectivity, name,
  description, category and connection) and
  `INetworkConnectionEvents`.

`nlmtest` (network suite, 64- and 32-bit) checks all of it on QEMU's
network.  Still to do: IDispatch (scripts that bind late), a second
adapter, an "Unidentified network" for an adapter without a gateway, and
the networks' creation and connection times.
