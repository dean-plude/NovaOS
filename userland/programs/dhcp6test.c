/* dhcp6test.exe — the stateful DHCPv6 lease (RFC 8415) as programs see it
 * through iphlpapi's GetAdaptersAddresses, on the network suite's IPv6
 * network (tools/v6peer.py leases fd00:6e6f:7661::100 with the search list
 * nova6.test, example.test): the address with Dhcp prefix and suffix
 * origins, finite lifetimes and a /128 on-link prefix, beside the SLAAC
 * one; the adapter's DHCPv6 server, client DUID (DUID-LL) and IAID; its
 * DNS suffix and suffix list; and the DHCPv6 DNS server. */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#ifndef NO_ERROR
#define NO_ERROR 0
#endif
#define AF_INET6 23

typedef struct { USHORT family, port; DWORD flowinfo; BYTE addr[16]; DWORD scope; } Sin6;
typedef struct { void *sa; INT len; } SockAddr;
typedef struct Unicast_ { ULONGLONG head; struct Unicast_ *Next; SockAddr Address; int po, so, dad;
                          ULONG valid, preferred, lease; UINT8 prefix_len; } Unicast;
typedef struct Addr_ { ULONGLONG head; struct Addr_ *Next; SockAddr Address; } Addr;
typedef struct Suffix_ { struct Suffix_ *Next; WCHAR String[256]; } Suffix;
typedef struct Adapter_ {
    union { ULONGLONG Alignment; struct { ULONG Length; DWORD IfIndex; }; };
    struct Adapter_ *Next;
    char *AdapterName;
    Unicast *FirstUnicastAddress;
    Addr *FirstAnycastAddress, *FirstMulticastAddress, *FirstDnsServerAddress;
    WCHAR *DnsSuffix, *Description, *FriendlyName;
    BYTE PhysicalAddress[8];
    ULONG PhysicalAddressLength, Flags, Mtu, IfType;
    int OperStatus;
    DWORD Ipv6IfIndex;
    ULONG ZoneIndices[16];
    void *FirstPrefix;
    ULONG64 TransmitLinkSpeed, ReceiveLinkSpeed;
    Addr *FirstWinsServerAddress, *FirstGatewayAddress;
    ULONG Ipv4Metric, Ipv6Metric;
    ULONG64 Luid;
    SockAddr Dhcpv4Server;
    ULONG CompartmentId;
    GUID NetworkGuid;
    int ConnectionType, TunnelType;
    SockAddr Dhcpv6Server;
    BYTE Dhcpv6ClientDuid[130];
    ULONG Dhcpv6ClientDuidLength, Dhcpv6Iaid;
    Suffix *FirstDnsSuffix;
} Adapter;

typedef ULONG (WINAPI *GetAdaptersAddresses_t)(ULONG, ULONG, PVOID, PVOID, PULONG);

static const BYTE LEASED[16] = { 0xfd, 0x00, 0x6e, 0x6f, 0x76, 0x61, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x00 };
static const BYTE DNS2[16]   = { 0xfd, 0x00, 0x6e, 0x6f, 0x76, 0x61, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x02 };

static int passed, failed;

static void check(const char *what, int ok)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) passed++; else failed++;
}

static const Sin6 *v6(const SockAddr *a)
{
    const Sin6 *s = a->sa;
    return s && a->len == (INT)sizeof(Sin6) && s->family == AF_INET6 ? s : NULL;
}

static __declspec(align(8)) BYTE buf[64 * 1024];

int main(void)
{
    GetAdaptersAddresses_t gaa = (GetAdaptersAddresses_t)GetProcAddress(LoadLibraryA("iphlpapi.dll"), "GetAdaptersAddresses");
    ULONG size = sizeof(buf);
    Adapter *a = (Adapter *)buf;
    ULONG r = gaa ? gaa(AF_INET6, 0, NULL, buf, &size) : 1;
    check("GetAdaptersAddresses(AF_INET6) lists the adapter", r == NO_ERROR && a->Length == sizeof(Adapter));
    if (r != NO_ERROR) { printf("dhcp6test: %d passed, %d failed\n", passed, failed); return 1; }

    const Unicast *lease = NULL;
    int slaac = 0;
    for (const Unicast *u = a->FirstUnicastAddress; u; u = u->Next) {
        const Sin6 *s = v6(&u->Address);
        if (!s) continue;
        printf("  address %02x%02x:...:%02x%02x origin %d/%d, valid %lu s, preferred %lu s, /%u\n", s->addr[0], s->addr[1],
               s->addr[14], s->addr[15], u->po, u->so, u->valid, u->preferred, u->prefix_len);
        if (!memcmp(s->addr, LEASED, 16)) lease = u;
        else if (s->addr[0] == 0xfd && u->po == 4 && u->so == 4) slaac = 1;
    }
    check("the leased address fd00:6e6f:7661::100", lease != NULL);
    check("... with Dhcp prefix and suffix origins", lease && lease->po == 3 && lease->so == 3);
    check("... finite lifetimes (preferred <= valid) and a /128 on-link prefix",
          lease && lease->valid && lease->valid != 0xFFFFFFFF && lease->preferred <= lease->valid &&
          lease->lease == lease->valid && lease->prefix_len == 128);
    check("the SLAAC address beside it (RouterAdvertisement / LinkLayerAddress)", slaac);

    const Sin6 *server = v6(&a->Dhcpv6Server);
    check("Dhcpv6Server: the server's link-local address fe80::1, scoped",
          server && server->addr[0] == 0xfe && server->addr[1] == 0x80 && server->addr[15] == 1 && server->scope);
    check("Dhcpv6ClientDuid: a DUID-LL (type 3, Ethernet) of the adapter's MAC",
          a->Dhcpv6ClientDuidLength == 10 && a->Dhcpv6ClientDuid[1] == 3 && a->Dhcpv6ClientDuid[3] == 1 &&
          !memcmp(a->Dhcpv6ClientDuid + 4, a->PhysicalAddress, 6));
    check("Dhcpv6Iaid: set", a->Dhcpv6Iaid != 0);
    check("Flags: IP_ADAPTER_IPV6_MANAGE_ADDRESS_CONFIG", (a->Flags & 0x200) != 0);
    check("DnsSuffix: nova6.test", a->DnsSuffix && !wcscmp(a->DnsSuffix, L"nova6.test"));
    const Suffix *x = a->FirstDnsSuffix;
    check("FirstDnsSuffix: nova6.test, then example.test",
          x && !wcscmp(x->String, L"nova6.test") && x->Next && !wcscmp(x->Next->String, L"example.test") && !x->Next->Next);
    int dns = 0;
    for (const Addr *d = a->FirstDnsServerAddress; d; d = d->Next) {
        const Sin6 *s = v6(&d->Address);
        if (s && !memcmp(s->addr, DNS2, 16)) dns = 1;
    }
    check("the DHCPv6 DNS server fd00:6e6f:7661::2", dns);

    printf("dhcp6test: %d passed, %d failed\n", passed, failed);
    return failed != 0;
}
