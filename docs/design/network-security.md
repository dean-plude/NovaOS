# Network security: the filtering engine and the firewall

NovaOS protects its network the way Windows 11 does: a filtering engine
shaped like the Windows Filtering Platform (WFP) sits beside the network
stack, and the Windows Firewall is a set of policies on that engine.
Programs, installers and administrators use Windows' own interfaces to
both, and the state lives where Windows keeps it in the registry, so a
program that opens a port through `INetFwPolicy2`, a script that runs
`netsh advfirewall`, and an administrator who reads `FirewallRules` all
see the same thing.

## Where it sits

```
 programs        fwpuclnt.dll (FwpmEngineOpen0, FwpmFilterAdd0 ...)
                 hnetcfg.dll  (INetFwPolicy2, INetFwRules, INetFwRule3)
                 netsh advfirewall, Settings > Firewall
                         |  NtNovaSockCtl op 22 (WFP) / op 23 (firewall)
 kernel          kernel/net/wfp.c       the engine: layers, sublayers, filters
                 kernel/net/firewall.c  the firewall: profiles, rules, networks
                         |  classify
 network stack   kernel/net/sock.c      ALE connect / listen / recv-accept
                 lwIP hooks             inbound transport (LWIP_HOOK_IP4/6_INPUT)
                 netif output wrap      outbound transport
```

On Windows the engine is split between the Base Filtering Engine service
(user mode, which `fwpuclnt.dll` reaches by RPC) and the kernel's
`netio.sys`.  NovaOS keeps both halves in the kernel beside lwIP, and
`fwpuclnt.dll` and `hnetcfg.dll` reach it through one system call
(`userland/include/novafw.h` is the request layout).

## The engine (kernel/net/wfp.c)

- **Layers:** `ALE_AUTH_CONNECT`, `ALE_AUTH_RECV_ACCEPT`,
  `ALE_AUTH_LISTEN`, `INBOUND_TRANSPORT` and `OUTBOUND_TRANSPORT`, each
  for IPv4 and IPv6, with Windows' layer GUIDs.
- **Conditions:** protocol, local and remote address (an address, an IPv4
  address and mask, an IPv6 prefix), local and remote port (a number or a
  range), the application id (`FwpmGetAppIdFromFileName0`'s blob: the
  program's path, lower case, as `\device\harddiskvolumeN\...`), the
  `FLAGS` field (`IS_LOOPBACK`), the interface index and LUID.
  Conditions on the same field are OR'd and on different fields AND'd, as
  in WFP.
- **Arbitration:** sublayers are evaluated from the heaviest; inside each
  the heaviest matching filter decides.  Block beats permit across
  sublayers, except a hard permit (`FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT`).
  Weights may be empty (the engine picks), `UINT8` (placed in the top
  bits) or `UINT64`.  The firewall is the built-in `MPSSVC_WF` sublayer
  (weight 0x7FFE) under `FWPM_SUBLAYER_UNIVERSAL` (0x8000).
- **Objects:** providers, sublayers and filters, with transactions
  (begin, commit, abort), dynamic sessions (their objects go when the
  session closes or the process exits) and persistent objects, stored as
  binary values under
  `HKLM\SYSTEM\CurrentControlSet\Services\BFE\Parameters\Policy\Persistent`
  and restored at boot.  Changing the engine needs an administrator.
- **Where packets meet it:** a TCP `connect` and the first datagram a UDP
  socket sends to a new peer go through `ALE_AUTH_CONNECT` (refused:
  `WSAEACCES`); `listen` through `ALE_AUTH_LISTEN`; an inbound SYN to a
  listening socket and the first datagram from a new peer through
  `ALE_AUTH_RECV_ACCEPT` (refused: dropped, as Windows' stealth mode
  does).  Each UDP socket remembers its last few peers, so replies come
  back; a multicast or broadcast send opens a three-second window for
  unicast replies (what `DisableUnicastResponsesToMulticastBroadcast`
  turns off).  The transport layers see every packet, but only when a
  filter is there.  The kernel's own lwIP sockets (DHCP, DHCPv6, DNS) are
  not filtered.

## The firewall (kernel/net/firewall.c)

- **Profiles:** Domain, Private and Public, each with
  `EnableFirewall`, `DefaultInboundAction` (block),
  `DefaultOutboundAction` (allow), `DoNotAllowExceptions` (block all
  inbound), `DisableNotifications` and
  `DisableUnicastResponsesToMulticastBroadcast`, under
  `...\SharedAccess\Parameters\FirewallPolicy\{Domain,Standard,Public}Profile`.
- **Rules:** one `REG_SZ` per rule in `FirewallPolicy\FirewallRules`, in
  Windows' own text form (`v2.31|Action=Allow|Active=TRUE|Dir=In|
  Protocol=6|LPort=8080|App=...|Name=...|`): direction, action, protocol,
  local and remote ports (lists and ranges), local and remote addresses
  (lists, ranges, `LocalSubnet`, `DefaultGateway`), the program, the
  service, profiles, group and edge traversal.  A rule written there by
  hand or by a script is read at boot.
- **Decision:** loopback is never filtered.  Inbound: block-all, then any
  matching block rule, then any matching allow rule, then the profile's
  default; outbound the same without block-all.
- **Networks:** the network list is Windows' `NetworkList` keys
  (`Profiles\{guid}` with `ProfileName` and `Category`, and
  `Signatures\Unmanaged\<gateway MAC>`).  A network is known by its
  gateway's MAC address; a new one is Public, and the desktop asks once
  whether the PC may be discoverable there (Yes: Private).
  `NewNetworkWindowOff` under `...\Control\Network` turns the question
  off.  Changing `Category` in the registry (as `Set-NetConnectionProfile`
  does) takes effect within a second.

## The interfaces

- `fwpuclnt.dll`: engine sessions and options, transactions, providers,
  sublayers and filters (add, delete by id and key, get, enumerate),
  `FwpmGetAppIdFromFileName0`, `FwpmFreeMemory0`.
- `hnetcfg.dll`: the `NetFwPolicy2` and `NetFwRule` classes
  (`HNetCfg.FwPolicy2`, `HNetCfg.FWRule`): each profile's state, default
  actions and block-all, the current profile, rules (add, remove, item,
  enumerate, rule groups), restoring the defaults.  A rule taken from the
  collection is live: setting a property changes the rule in force.
- `netsh advfirewall`: `firewall add|delete|show|set rule`, `show
  allprofiles|currentprofile|...`, `set ... state on|off`, `set ...
  firewallpolicy`, `reset`, answering in Windows' words.
- Settings > Firewall: the network and its type, each profile on or off,
  the rules with a switch each.

## Not here yet

Callouts (a kernel-mode callout driver's `FwpsCalloutRegister`, and
`FwpmCalloutAdd0`), IPsec and connection security rules, SmartScreen-style
reputation checks, the "allow this app?" notification when a program
first listens, ICMP rules (types and codes), the legacy `INetFwMgr` API,
IDispatch for `hnetcfg`'s objects (so scripts that bind late, such as
PowerShell's `New-Object -ComObject`, cannot use them yet),
`INetworkListManager` (`netprofm.dll`), service-restricted rules and the
`NetSecurity` PowerShell module.  Checks on real hardware (the reference
laptop's Wi-Fi network as Public, then Private) wait on the ThinkPad.

## Tests

`firewalltest` (network suite, `tests/selftest/network4/075-firewalltest.py`)
exercises the engine over loopback and the firewall on QEMU's network,
with inbound connections made from the host through port forwarding;
the same file checks `netsh advfirewall`.
