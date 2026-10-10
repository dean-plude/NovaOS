/* adaptertest.exe — iphlpapi's adapter tables: GetAdaptersAddresses,
 * GetAdaptersInfo, GetIpAddrTable and GetNetworkParams list the network
 * adapter with its MAC, its IPv4 address and mask, gateway and DNS server
 * (on QEMU's user network: 10.0.2.15/24, 10.0.2.2, 10.0.2.3).  Steam
 * finds the computer's own address this way before it connects (it
 * asserted "GetAdaptersAddresses returned 232" while the tables were
 * empty); Chromium watches the adapters to know it is online.  The
 * structures are Windows' (IP_ADAPTER_ADDRESSES_LH: 448 bytes, 376 in
 * 32-bit), checked by size and offsets. */
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <windows.h>

#ifndef NO_ERROR
#define NO_ERROR 0
#endif
#define AF_INET  2
#define AF_INET6 23
#define GAA_FLAG_INCLUDE_PREFIX   0x10
#define GAA_FLAG_INCLUDE_GATEWAYS 0x80

typedef struct { USHORT family, port; BYTE addr[4]; BYTE zero[8]; } Sin;
typedef struct { void *sa; INT len; } SockAddr;
typedef struct Unicast_ { ULONGLONG head; struct Unicast_ *Next; SockAddr Address; int po, so, dad;
                          ULONG valid, preferred, lease; UINT8 prefix_len; } Unicast;
typedef struct Addr_ { ULONGLONG head; struct Addr_ *Next; SockAddr Address; } Addr;
typedef struct Prefix_ { ULONGLONG head; struct Prefix_ *Next; SockAddr Address; ULONG PrefixLength; } Prefix;
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
    Prefix *FirstPrefix;
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
    void *FirstDnsSuffix;
} Adapter;
typedef struct IpStr_ { struct IpStr_ *Next; char IpAddress[16], IpMask[16]; DWORD Context; } IpStr;
typedef struct Info_ {
    struct Info_ *Next; DWORD ComboIndex; char AdapterName[260], Description[132];
    UINT AddressLength; BYTE Address[8]; DWORD Index; UINT Type, DhcpEnabled;
    IpStr *CurrentIpAddress; IpStr IpAddressList, GatewayList, DhcpServer;
    BOOL HaveWins; IpStr PrimaryWinsServer, SecondaryWinsServer; LONGLONG LeaseObtained, LeaseExpires;
} Info;
typedef struct {
    char HostName[132], DomainName[132]; IpStr *CurrentDnsServer; IpStr DnsServerList;
    UINT NodeType; char ScopeId[260]; UINT EnableRouting, EnableProxy, EnableDns;
} Fixed;
typedef struct { DWORD dwAddr, dwIndex, dwMask, dwBCastAddr, dwReasmSize; USHORT unused1, wType; } IpAddrRow;

typedef ULONG (WINAPI *GetAdaptersAddresses_t)(ULONG, ULONG, PVOID, PVOID, PULONG);
typedef DWORD (WINAPI *GetAdaptersInfo_t)(PVOID, PULONG);
typedef DWORD (WINAPI *GetIpAddrTable_t)(PVOID, PULONG, BOOL);
typedef DWORD (WINAPI *GetNetworkParams_t)(PVOID, PULONG);

static int passed, failed;

static void check(const char *what, int ok)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) passed++; else failed++;
}

static int is_ip(const SockAddr *a, BYTE b0, BYTE b1, BYTE b2, BYTE b3)
{
    const Sin *s = a->sa;
    return s && a->len == 16 && s->family == AF_INET &&
           s->addr[0] == b0 && s->addr[1] == b1 && s->addr[2] == b2 && s->addr[3] == b3;
}

static __declspec(align(8)) BYTE buf[64 * 1024];

int main(void)
{
    HMODULE ip = LoadLibraryA("iphlpapi.dll");
    GetAdaptersAddresses_t gaa = (GetAdaptersAddresses_t)GetProcAddress(ip, "GetAdaptersAddresses");
    GetAdaptersInfo_t gai = (GetAdaptersInfo_t)GetProcAddress(ip, "GetAdaptersInfo");
    GetIpAddrTable_t giat = (GetIpAddrTable_t)GetProcAddress(ip, "GetIpAddrTable");
    GetNetworkParams_t gnp = (GetNetworkParams_t)GetProcAddress(ip, "GetNetworkParams");
    if (!gaa || !gai || !giat || !gnp) { printf("adaptertest: %d passed, %d failed\n", passed, failed + 1); return 1; }

    check("IP_ADAPTER_ADDRESSES_LH is Windows' size and layout",
          sizeof(void *) == 8 ? sizeof(Adapter) == 448 && offsetof(Adapter, FirstGatewayAddress) == 208 &&
                                offsetof(Adapter, Dhcpv6Server) == 280 && sizeof(Unicast) == 64
                              : sizeof(Adapter) == 376 && offsetof(Adapter, FirstGatewayAddress) == 164 &&
                                offsetof(Adapter, Dhcpv6Server) == 220 && sizeof(Unicast) == 48);

    /* the size query */
    ULONG size = 0;
    ULONG r = gaa(0, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_PREFIX, NULL, NULL, &size);
    check("GetAdaptersAddresses size query: ERROR_BUFFER_OVERFLOW and a size", r == ERROR_BUFFER_OVERFLOW && size >= sizeof(Adapter));
    check("an unknown family is refused", gaa(99, 0, NULL, buf, &size) == ERROR_INVALID_PARAMETER);

    size = sizeof(buf);
    r = gaa(0, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_PREFIX, NULL, buf, &size);
    Adapter *a = (Adapter *)buf;
    check("GetAdaptersAddresses(AF_UNSPEC) lists an adapter", r == NO_ERROR && a->Length == sizeof(Adapter));
    if (r != NO_ERROR) { printf("adaptertest: %d passed, %d failed\n", passed, failed + 1); return 1; }
    printf("  %s \"%ls\" (%ls), index %lu\n", a->AdapterName, a->FriendlyName, a->Description, a->IfIndex);
    check("... Ethernet, up, with a 6-byte MAC and a {GUID} name",
          a->IfType == 6 && a->OperStatus == 1 && a->PhysicalAddressLength == 6 && a->IfIndex &&
          a->AdapterName && a->AdapterName[0] == '{' && a->FriendlyName && a->Description && a->DnsSuffix);
    int v4 = 0;
    for (Unicast *u = a->FirstUnicastAddress; u; u = u->Next)
        if (is_ip(&u->Address, 10, 0, 2, 15) && u->prefix_len == 24) v4 = 1;
    check("... its address 10.0.2.15/24", v4);
    int dns = 0;
    for (Addr *d = a->FirstDnsServerAddress; d; d = d->Next) dns |= is_ip(&d->Address, 10, 0, 2, 3);
    check("... the DNS server 10.0.2.3", dns);
    check("... the gateway 10.0.2.2", a->FirstGatewayAddress && is_ip(&a->FirstGatewayAddress->Address, 10, 0, 2, 2));
    check("... the subnet prefix 10.0.2.0/24",
          a->FirstPrefix && is_ip(&a->FirstPrefix->Address, 10, 0, 2, 0) && a->FirstPrefix->PrefixLength == 24);
    check("... the strings and lists sit in the caller's buffer",
          (BYTE *)a->AdapterName > buf && (BYTE *)a->AdapterName < buf + size &&
          (BYTE *)a->FirstUnicastAddress > buf && (BYTE *)a->FirstUnicastAddress < buf + size);

    size = sizeof(buf);
    r = gaa(AF_INET6, 0, NULL, buf, &size);
    int no_v4 = r == NO_ERROR;
    for (Unicast *u = r == NO_ERROR ? a->FirstUnicastAddress : NULL; u; u = u->Next)
        if (((Sin *)u->Address.sa)->family != AF_INET6) no_v4 = 0;
    check("GetAdaptersAddresses(AF_INET6) has no IPv4 address", no_v4 && !a->IfIndex);

    size = 0;
    check("GetAdaptersInfo size query", gai(NULL, &size) == ERROR_BUFFER_OVERFLOW &&
          size == (sizeof(void *) == 8 ? 704 : 648) && size == sizeof(Info));
    size = sizeof(buf);
    Info *in = (Info *)buf;
    check("GetAdaptersInfo: 10.0.2.15 / 255.255.255.0, gateway 10.0.2.2",
          gai(buf, &size) == NO_ERROR && in->Type == 6 && in->AddressLength == 6 &&
          !strcmp(in->IpAddressList.IpAddress, "10.0.2.15") && !strcmp(in->IpAddressList.IpMask, "255.255.255.0") &&
          !strcmp(in->GatewayList.IpAddress, "10.0.2.2"));

    size = sizeof(buf);
    IpAddrRow *row = (IpAddrRow *)(buf + 4);
    check("GetIpAddrTable: the address with its mask and index",
          giat(buf, &size, TRUE) == NO_ERROR && *(DWORD *)buf == 1 &&
          !memcmp(&row->dwAddr, "\x0a\x00\x02\x0f", 4) && !memcmp(&row->dwMask, "\xff\xff\xff\x00", 4) && row->dwIndex);

    size = sizeof(buf);
    Fixed *f = (Fixed *)buf;
    check("GetNetworkParams: the host name and the DNS server 10.0.2.3",
          gnp(buf, &size) == NO_ERROR && f->HostName[0] && f->CurrentDnsServer &&
          !strcmp(f->DnsServerList.IpAddress, "10.0.2.3"));

    printf("adaptertest: %d passed, %d failed\n", passed, failed);
    return failed != 0;
}
