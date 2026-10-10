## Network settings can be edited

Settings > Network's Ethernet card has an **Edit** button.  It opens the
IP settings: **Automatic (DHCP)** or **Manual** with an IP address, subnet
mask and gateway, and a preferred and alternate DNS server (with DHCP,
typed DNS servers replace the ones DHCP hands out).  **Save** checks the
values (a real subnet mask, an address that is neither the subnet's own nor
its broadcast address, a gateway inside the subnet) and applies them at
once: a manual address stops DHCP and sets the interface's address, going
back to DHCP starts it again, and manual DNS servers stay in place when a
DHCP lease or a router advertisement brings others.  The Terminal's
`netsh interface ipv4 set address`, `set dnsservers`, `add dnsservers` and
`show config` do the same with Windows' syntax.  The settings are kept
where Windows keeps them, in `HKLM\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters`
(`EnableDHCP`, `IPAddress`, `SubnetMask`, `DefaultGateway`, `NameServer`),
and are put back after the registry loads at boot.  The network self-test
suite sets a static address and DNS server with `netsh`, pings QEMU's
gateway on it and goes back to DHCP (`tests/selftest/network4/070-netsh.py`).
