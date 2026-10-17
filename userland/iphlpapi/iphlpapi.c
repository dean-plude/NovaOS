/*
 * iphlpapi.dll — IP helper: the host's network parameters and tables.
 *
 * GetNetworkParams gives the computer's name (no DNS domain) and its DNS
 * servers.  The adapter tables (GetAdaptersAddresses, GetAdaptersInfo,
 * GetIpAddrTable) list the network adapter with its addresses, gateway and
 * DNS servers, from the kernel (NtNovaSockCtl op 16); without an adapter
 * they are empty, as Windows answers on a machine without IP configured.
 * The TCP connection tables are real (the kernel's sockets, with the
 * process that owns each): a local server finds which process is calling
 * it this way.
 */
#include <windows.h>
#include <winternl.h>

#define IPHLPAPI __declspec(dllexport)
#define ERROR_NO_DATA_ 232
#define ERROR_BUFFER_OVERFLOW_ 111
#ifndef NO_ERROR
#define NO_ERROR 0
#endif
#define ERROR_INVALID_DATA_ 13
#define AF_INET_  2
#define AF_INET6_ 23

typedef struct IP_ADDR_STRING_ { struct IP_ADDR_STRING_ *Next; char IpAddress[16], IpMask[16]; DWORD Context; } IP_ADDR_STRING_;
typedef struct {
    char HostName[132], DomainName[132];
    IP_ADDR_STRING_ *CurrentDnsServer;
    IP_ADDR_STRING_ DnsServerList;
    UINT NodeType;
    char ScopeId[260];
    UINT EnableRouting, EnableProxy, EnableDns;
} FIXED_INFO_;

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);

/* The adapter, as the kernel reports it (kernel/um/um_socket.c NetAdapterInfo) */
typedef struct {
    DWORD flags;                        /* bit 0 configured, 1 link, 2 present, 3 IPv4 by DHCP */
    DWORD if_index;
    BYTE  mac[6], ip6_count, dns_count;
    DWORD ip, mask, gw;                 /* network byte order */
    BYTE  ip6[4][16];
    BYTE  ip6_preferred[4];
    BYTE  dns_v6[3];
    BYTE  dns[3][16];
    char  adapter[64];
    BYTE  ip6_dhcp[4];                  /* ip6[i] leased from a DHCPv6 server */
    DWORD ip6_valid[4], ip6_pref[4];    /* lifetimes left, s */
    DWORD dhcp6_iaid;
    BYTE  dhcp6_server[16];             /* (zero: no lease) */
    BYTE  dhcp6_duid_len, dhcp6_duid[20];
    char  dns_suffix[128];              /* the search list: "a.example,b.example" */
} KAdapter;

/* FALSE: no adapter */
static BOOL adapter(KAdapter *a)
{
    memset(a, 0, sizeof(*a));
    return NtNovaSockCtl(0, 16, 0, a) == (LONG_PTR)sizeof(*a) && (a->flags & 4);
}

static char *put_dec(char *o, unsigned v)
{
    if (v >= 10) o = put_dec(o, v / 10);
    *o++ = (char)('0' + v % 10);
    return o;
}

static void ip_text(DWORD be, char out[16])
{
    const BYTE *b = (const BYTE *)&be;
    char *o = out;
    for (int i = 0; i < 4; i++) {
        o = put_dec(o, b[i]);
        *o++ = i < 3 ? '.' : 0;
    }
}

IPHLPAPI DWORD WINAPI GetNetworkParams(FIXED_INFO_ *info, PULONG size)
{
    if (!size) return ERROR_INVALID_PARAMETER;
    KAdapter a;
    DWORD dns[3], n4 = 0;
    if (adapter(&a))
        for (int i = 0; i < a.dns_count; i++)
            if (!a.dns_v6[i]) memcpy(&dns[n4++], a.dns[i], 4);
    ULONG need = sizeof(FIXED_INFO_) + (n4 > 1 ? n4 - 1 : 0) * sizeof(IP_ADDR_STRING_);
    if (!info || *size < need) { *size = need; return ERROR_BUFFER_OVERFLOW_; }
    memset(info, 0, need);
    DWORD n = sizeof(info->HostName);
    if (!GetComputerNameA(info->HostName, &n)) lstrcpyA(info->HostName, "NOVA-PC");
    info->NodeType = 1;                                  /* BROADCAST */
    IP_ADDR_STRING_ *more = (IP_ADDR_STRING_ *)(info + 1);
    for (DWORD i = 0; i < n4; i++) {
        IP_ADDR_STRING_ *e = i ? &more[i - 1] : &info->DnsServerList;
        ip_text(dns[i], e->IpAddress);
        lstrcpyA(e->IpMask, "255.255.255.255");
        if (i + 1 < n4) e->Next = &more[i];
    }
    if (n4) info->CurrentDnsServer = &info->DnsServerList;
    return NO_ERROR;
}

/* -----------------------------------------------------------------------
 * GetAdaptersAddresses: IP_ADAPTER_ADDRESSES (the LH layout) and the
 * lists it points to, all in the caller's buffer
 * ----------------------------------------------------------------------- */
typedef struct { USHORT family, port; DWORD addr; BYTE zero[8]; } Sin;
typedef struct { USHORT family, port; DWORD flowinfo; BYTE addr[16]; DWORD scope; } Sin6;
typedef struct { void *lpSockaddr; INT iSockaddrLength; } SockAddr;

typedef struct Unicast_ {
    union { ULONGLONG Alignment; struct { ULONG Length; DWORD Flags; }; };
    struct Unicast_ *Next;
    SockAddr Address;
    int PrefixOrigin, SuffixOrigin, DadState;
    ULONG ValidLifetime, PreferredLifetime, LeaseLifetime;
    UINT8 OnLinkPrefixLength;
} Unicast;

/* the DNS server, gateway, anycast and multicast entries */
typedef struct Addr_ {
    union { ULONGLONG Alignment; struct { ULONG Length; DWORD Reserved; }; };
    struct Addr_ *Next;
    SockAddr Address;
} Addr;

typedef struct Prefix_ {
    union { ULONGLONG Alignment; struct { ULONG Length; DWORD Flags; }; };
    struct Prefix_ *Next;
    SockAddr Address;
    ULONG PrefixLength;
} Prefix;

typedef struct Adapter_ {
    union { ULONGLONG Alignment; struct { ULONG Length; DWORD IfIndex; }; };
    struct Adapter_ *Next;
    char *AdapterName;
    Unicast *FirstUnicastAddress;
    Addr *FirstAnycastAddress, *FirstMulticastAddress, *FirstDnsServerAddress;
    WCHAR *DnsSuffix, *Description, *FriendlyName;
    BYTE PhysicalAddress[8];
    ULONG PhysicalAddressLength;
    ULONG Flags;
    ULONG Mtu;
    ULONG IfType;
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

/* IP_ADAPTER_DNS_SUFFIX */
typedef struct Suffix_ {
    struct Suffix_ *Next;
    WCHAR String[256];
} Suffix;

#define AF_UNSPEC_ 0
#define GAA_FLAG_SKIP_UNICAST_      0x1
#define GAA_FLAG_SKIP_ANYCAST_      0x2
#define GAA_FLAG_SKIP_MULTICAST_    0x4
#define GAA_FLAG_SKIP_DNS_SERVER_   0x8
#define GAA_FLAG_INCLUDE_PREFIX_    0x10
#define GAA_FLAG_SKIP_FRIENDLY_NAME_ 0x20
#define GAA_FLAG_INCLUDE_GATEWAYS_  0x80

/* bump allocation in the caller's buffer (or just counting, when @base is NULL) */
typedef struct { BYTE *base; ULONG used; } Arena;
static void *take(Arena *ar, ULONG n)
{
    ULONG at = (ar->used + 7) & ~7u;
    ar->used = at + n;
    return ar->base ? memset(ar->base + at, 0, n) : NULL;
}
static WCHAR *wstr(Arena *ar, const char *s)
{
    ULONG n = (ULONG)lstrlenA(s) + 1;
    WCHAR *w = take(ar, n * sizeof(WCHAR));
    for (ULONG i = 0; w && i < n; i++) w[i] = (BYTE)s[i];
    return w;
}
static SockAddr sockaddr(Arena *ar, BOOL v6, const BYTE *a, DWORD scope)
{
    SockAddr r = { take(ar, v6 ? sizeof(Sin6) : sizeof(Sin)), v6 ? sizeof(Sin6) : sizeof(Sin) };
    if (r.lpSockaddr && v6) {
        Sin6 *s = r.lpSockaddr;
        s->family = AF_INET6_;
        memcpy(s->addr, a, 16);
        s->scope = scope;
    } else if (r.lpSockaddr) {
        Sin *s = r.lpSockaddr;
        s->family = AF_INET_;
        memcpy(&s->addr, a, 4);
    }
    return r;
}
static int mask_bits(DWORD be)
{
    int n = 0;
    for (const BYTE *b = (const BYTE *)&be, *e = b + 4; b < e; b++)
        for (int i = 7; i >= 0 && (*b >> i & 1); i--) n++;
    return n;
}
static BOOL link_local6(const BYTE *a) { return a[0] == 0xFE && (a[1] & 0xC0) == 0x80; }

/* The adapter's name: a GUID made from its MAC, as Windows names adapters by GUID */
static void adapter_guid(const KAdapter *k, char out[40])
{
    static const char hex[] = "0123456789ABCDEF";
    lstrcpyA(out, "{4E4F5641-0000-4000-8000-");
    char *o = out + lstrlenA(out);
    for (int i = 0; i < 6; i++) { *o++ = hex[k->mac[i] >> 4]; *o++ = hex[k->mac[i] & 15]; }
    *o++ = '}';
    *o = 0;
}

static void build_adapter(Arena *ar, const KAdapter *k, ULONG family, ULONG flags)
{
    BOOL want4 = family != AF_INET6_, want6 = family != AF_INET_;
    BOOL has4 = (k->flags & 1) && k->ip;
    Adapter *ad = take(ar, sizeof(Adapter));
    char guid[40];
    adapter_guid(k, guid);
    char *name = take(ar, (ULONG)lstrlenA(guid) + 1);
    if (name) lstrcpyA(name, guid);
    /* the DNS suffix: the first name of DHCPv6's domain search list */
    char first[128];
    lstrcpynA(first, k->dns_suffix, sizeof(first));
    for (char *c = first; *c; c++) if (*c == ',') { *c = 0; break; }
    BOOL lease6 = k->dhcp6_server[0] != 0;
    WCHAR *suffix = wstr(ar, first), *desc = wstr(ar, k->adapter[0] ? k->adapter : "Ethernet adapter");
    WCHAR *friendly = wstr(ar, (flags & GAA_FLAG_SKIP_FRIENDLY_NAME_) ? "" : "Ethernet");
    if (ad) {
        ad->Length = sizeof(Adapter);
        ad->IfIndex = want4 ? k->if_index : 0;
        ad->AdapterName = name;
        ad->DnsSuffix = suffix;
        ad->Description = desc;
        ad->FriendlyName = friendly;
        memcpy(ad->PhysicalAddress, k->mac, 6);
        ad->PhysicalAddressLength = 6;
        ad->Flags = 0x1 | 0x2 | ((k->flags & 8) ? 0x4 : 0) | 0x40 | 0x80 | 0x100 |  /* DDNS, suffix, DHCPv4, NetBIOS, IPv4, IPv6 */
                    (lease6 ? 0x200 : 0);                     /* IP_ADAPTER_IPV6_MANAGE_ADDRESS_CONFIG */
        ad->Mtu = 1500;
        ad->IfType = 6;                                       /* IF_TYPE_ETHERNET_CSMACD */
        ad->OperStatus = (k->flags & 2) ? 1 : 2;               /* IfOperStatusUp / Down */
        ad->Ipv6IfIndex = want6 ? k->if_index : 0;
        for (int i = 0; i < 16; i++) ad->ZoneIndices[i] = i < 3 ? k->if_index : (i == 14 || i == 15) ? 1 : 0;
        ad->TransmitLinkSpeed = ad->ReceiveLinkSpeed = (k->flags & 2) ? 1000000000ull : ~0ull;
        ad->Ipv4Metric = ad->Ipv6Metric = 25;
        ad->Luid = (6ull << 48) | ((ULONG64)k->if_index << 24);  /* IfType, NetLuidIndex */
        ad->CompartmentId = 1;
        ad->ConnectionType = 1;                               /* NET_IF_CONNECTION_DEDICATED */
        ad->TunnelType = 0;
    }
    /* addresses: IPv6 first, then IPv4 (Windows' order) */
    if (!(flags & GAA_FLAG_SKIP_UNICAST_)) {
        Unicast **link = ad ? &ad->FirstUnicastAddress : NULL;
        for (int i = 0; want6 && i < k->ip6_count; i++) {
            Unicast *u = take(ar, sizeof(Unicast));
            SockAddr sa = sockaddr(ar, TRUE, k->ip6[i], link_local6(k->ip6[i]) ? k->if_index : 0);
            if (!u) continue;
            u->Length = sizeof(Unicast);
            u->Address = sa;
            u->PrefixOrigin = link_local6(k->ip6[i]) ? 2 : k->ip6_dhcp[i] ? 3 : 4;   /* WellKnown / Dhcp / RouterAdvertisement */
            u->SuffixOrigin = k->ip6_dhcp[i] ? 3 : 4;                   /* Dhcp / LinkLayerAddress (EUI-64) */
            u->DadState = k->ip6_preferred[i] ? 4 : 1;                  /* Preferred / Tentative */
            u->ValidLifetime = u->LeaseLifetime = k->ip6_valid[i];
            u->PreferredLifetime = k->ip6_pref[i];
            u->OnLinkPrefixLength = k->ip6_dhcp[i] ? 128 : 64;          /* (an IA_NA address has no prefix: Windows says 128) */
            *link = u;
            link = &u->Next;
        }
        if (want4 && has4) {
            Unicast *u = take(ar, sizeof(Unicast));
            SockAddr sa = sockaddr(ar, FALSE, (const BYTE *)&k->ip, 0);
            if (u) {
                u->Length = sizeof(Unicast);
                u->Flags = 0x2;                                         /* IP_ADAPTER_ADDRESS_DNS_ELIGIBLE */
                u->Address = sa;
                u->PrefixOrigin = u->SuffixOrigin = (k->flags & 8) ? 3 : 1;    /* Dhcp / Manual */
                u->DadState = 4;
                u->ValidLifetime = u->PreferredLifetime = u->LeaseLifetime = 86400;
                u->OnLinkPrefixLength = (UINT8)mask_bits(k->mask);
                *link = u;
            }
        }
    }
    if (!(flags & GAA_FLAG_SKIP_DNS_SERVER_)) {
        Addr **link = ad ? &ad->FirstDnsServerAddress : NULL;
        for (int i = 0; i < k->dns_count; i++) {
            if (k->dns_v6[i] ? !want6 : !want4) continue;
            Addr *d = take(ar, sizeof(Addr));
            SockAddr sa = sockaddr(ar, k->dns_v6[i], k->dns[i], 0);
            if (!d) continue;
            d->Length = sizeof(Addr);
            d->Address = sa;
            *link = d;
            link = &d->Next;
        }
    }
    /* the DHCPv6 client (RFC 8415): server, DUID, IAID; the search list */
    if (want6 && lease6) {
        SockAddr sa = sockaddr(ar, TRUE, k->dhcp6_server, link_local6(k->dhcp6_server) ? k->if_index : 0);
        if (ad) ad->Dhcpv6Server = sa;
    }
    if (ad && k->dhcp6_duid_len && k->dhcp6_duid_len <= sizeof(k->dhcp6_duid)) {
        memcpy(ad->Dhcpv6ClientDuid, k->dhcp6_duid, k->dhcp6_duid_len);
        ad->Dhcpv6ClientDuidLength = k->dhcp6_duid_len;
        ad->Dhcpv6Iaid = k->dhcp6_iaid;
    }
    {
        Suffix **link = ad ? (Suffix **)&ad->FirstDnsSuffix : NULL;
        const char *n = k->dns_suffix;
        while (*n) {
            Suffix *x = take(ar, sizeof(Suffix));
            int len = 0;
            while (n[len] && n[len] != ',') len++;
            for (int i = 0; x && i < len && i < 255; i++) x->String[i] = (BYTE)n[i];
            if (x) { *link = x; link = &x->Next; }
            n += len;
            if (*n == ',') n++;
        }
    }
    if ((flags & GAA_FLAG_INCLUDE_GATEWAYS_) && want4 && has4 && k->gw) {
        Addr *g = take(ar, sizeof(Addr));
        SockAddr sa = sockaddr(ar, FALSE, (const BYTE *)&k->gw, 0);
        if (g) { g->Length = sizeof(Addr); g->Address = sa; ad->FirstGatewayAddress = g; }
    }
    if ((flags & GAA_FLAG_INCLUDE_PREFIX_) && want4 && has4) {
        /* the subnet, then the address itself (/32), as Windows lists them */
        DWORD net = k->ip & k->mask;
        Prefix *p = take(ar, sizeof(Prefix));
        SockAddr sa = sockaddr(ar, FALSE, (const BYTE *)&net, 0);
        Prefix *q = take(ar, sizeof(Prefix));
        SockAddr sb = sockaddr(ar, FALSE, (const BYTE *)&k->ip, 0);
        if (p && q) {
            p->Length = q->Length = sizeof(Prefix);
            p->Address = sa; p->PrefixLength = (ULONG)mask_bits(k->mask);
            q->Address = sb; q->PrefixLength = 32;
            p->Next = q;
            ad->FirstPrefix = p;
        }
    }
}

IPHLPAPI ULONG WINAPI GetAdaptersAddresses(ULONG family, ULONG flags, PVOID reserved, PVOID addrs, PULONG size)
{
    (void)reserved;
    if (!size || (family != AF_UNSPEC_ && family != AF_INET_ && family != AF_INET6_)) return ERROR_INVALID_PARAMETER;
    KAdapter k;
    if (!adapter(&k)) return ERROR_NO_DATA_;
    Arena count = { NULL, 0 };
    build_adapter(&count, &k, family, flags);
    if (!addrs || *size < count.used) { *size = count.used; return ERROR_BUFFER_OVERFLOW_; }
    if ((ULONG_PTR)addrs & 7) return ERROR_INVALID_PARAMETER;
    Arena ar = { addrs, 0 };
    build_adapter(&ar, &k, family, flags);
    return NO_ERROR;
}

/* -----------------------------------------------------------------------
 * GetAdaptersInfo (IPv4) and GetIpAddrTable
 * ----------------------------------------------------------------------- */
typedef struct Info_ {
    struct Info_ *Next;
    DWORD ComboIndex;
    char AdapterName[260], Description[132];
    UINT AddressLength;
    BYTE Address[8];
    DWORD Index;
    UINT Type, DhcpEnabled;
    IP_ADDR_STRING_ *CurrentIpAddress;
    IP_ADDR_STRING_ IpAddressList, GatewayList, DhcpServer;
    BOOL HaveWins;
    IP_ADDR_STRING_ PrimaryWinsServer, SecondaryWinsServer;
    LONGLONG LeaseObtained, LeaseExpires;                 /* time_t: 64-bit on both */
} Info;

IPHLPAPI DWORD WINAPI GetAdaptersInfo(PVOID info, PULONG size)
{
    if (!size) return ERROR_INVALID_PARAMETER;
    KAdapter k;
    if (!adapter(&k)) return ERROR_NO_DATA_;
    if (!info || *size < sizeof(Info)) { *size = sizeof(Info); return ERROR_BUFFER_OVERFLOW_; }
    Info *a = memset(info, 0, sizeof(Info));
    adapter_guid(&k, a->AdapterName);
    lstrcpynA(a->Description, k.adapter[0] ? k.adapter : "Ethernet adapter", sizeof(a->Description));
    a->ComboIndex = a->Index = k.if_index;
    a->AddressLength = 6;
    memcpy(a->Address, k.mac, 6);
    a->Type = 6;                                          /* MIB_IF_TYPE_ETHERNET */
    a->DhcpEnabled = (k.flags & 8) != 0;
    BOOL has4 = (k.flags & 1) && k.ip;
    ip_text(has4 ? k.ip : 0, a->IpAddressList.IpAddress);
    ip_text(has4 ? k.mask : 0, a->IpAddressList.IpMask);
    ip_text(has4 ? k.gw : 0, a->GatewayList.IpAddress);
    ip_text(has4 ? 0xFFFFFFFF : 0, a->GatewayList.IpMask);
    ip_text(has4 && (k.flags & 8) ? k.gw : 0, a->DhcpServer.IpAddress);   /* (the router: QEMU's DHCP server) */
    lstrcpyA(a->DhcpServer.IpMask, "255.255.255.255");
    lstrcpyA(a->PrimaryWinsServer.IpAddress, "0.0.0.0");
    lstrcpyA(a->SecondaryWinsServer.IpAddress, "0.0.0.0");
    return NO_ERROR;
}

typedef struct { DWORD dwAddr, dwIndex, dwMask, dwBCastAddr, dwReasmSize; USHORT unused1, wType; } IpAddrRow;

IPHLPAPI DWORD WINAPI GetIpAddrTable(PVOID t, PULONG size, BOOL order)
{
    (void)order;
    if (!size) return ERROR_INVALID_PARAMETER;
    KAdapter k;
    DWORD n = adapter(&k) && (k.flags & 1) && k.ip ? 1 : 0;
    ULONG need = sizeof(DWORD) + n * sizeof(IpAddrRow);
    if (!t || *size < need) { *size = need; return ERROR_INSUFFICIENT_BUFFER; }
    memset(t, 0, need);
    *(DWORD *)t = n;
    if (n) {
        IpAddrRow *r = (IpAddrRow *)((DWORD *)t + 1);
        r->dwAddr = k.ip;
        r->dwIndex = k.if_index;
        r->dwMask = k.mask;
        r->dwBCastAddr = 1;
        r->dwReasmSize = 65535;
        r->wType = (k.flags & 8) ? 0x0005 : 0x0001;          /* MIB_IPADDR_PRIMARY (| MIB_IPADDR_DYNAMIC) */
    }
    return NO_ERROR;
}

IPHLPAPI DWORD WINAPI GetIfEntry(PVOID row) { (void)row; return ERROR_INVALID_DATA_; }
IPHLPAPI DWORD WINAPI GetNumberOfInterfaces(PDWORD n) { KAdapter k; *n = adapter(&k) ? 1 : 0; return NO_ERROR; }

/* An empty MIB table: just its entry count */
static DWORD empty_table(PVOID table, PULONG size)
{
    if (!size) return ERROR_INVALID_PARAMETER;
    if (!table || *size < sizeof(DWORD)) { *size = sizeof(DWORD); return ERROR_INSUFFICIENT_BUFFER; }
    *(DWORD *)table = 0;
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI GetIpForwardTable(PVOID t, PULONG size, BOOL order) { (void)order; return empty_table(t, size); }
IPHLPAPI DWORD WINAPI GetUdpTable(PVOID t, PULONG size, BOOL order) { (void)order; return empty_table(t, size); }
IPHLPAPI DWORD WINAPI GetIfTable(PVOID t, PULONG size, BOOL order) { (void)order; return empty_table(t, size); }
IPHLPAPI DWORD WINAPI GetExtendedUdpTable(PVOID t, PDWORD size, BOOL order, ULONG af, int cls, ULONG r)
{ (void)order; (void)af; (void)cls; (void)r; return empty_table(t, size); }
/* -----------------------------------------------------------------------
 * The TCP connection tables (GetTcpTable, GetTcp6Table, GetExtendedTcpTable)
 * ----------------------------------------------------------------------- */
/* A row from the kernel (NtNovaSockCtl op 14): addresses as Winsock's,
 * ports in network order, a MIB_TCP_STATE and the owning process */
typedef struct { USHORT family, port; BYTE addr[16]; ULONG scope; } KAddr;
typedef struct { KAddr local, remote; DWORD state, owner; } KTcpRow;

#define MIB_TCP_STATE_LISTEN_ 2
enum { TCP_BASIC_LISTENER, TCP_BASIC_CONNECTIONS, TCP_BASIC_ALL, TCP_PID_LISTENER, TCP_PID_CONNECTIONS,
       TCP_PID_ALL, TCP_MODULE_LISTENER, TCP_MODULE_CONNECTIONS, TCP_MODULE_ALL };

/* The kernel's rows for one family (2 or 23), filtered by the class's
 * listener / connection choice, sorted when @order asks; HeapFree them */
static KTcpRow *tcp_rows(ULONG af, int which, BOOL order, DWORD *count)
{
    KTcpRow *all = NULL;
    LONG_PTR n = NtNovaSockCtl(0, 14, 0, NULL);
    for (int tries = 0; tries < 4 && n > 0; tries++) {
        HeapFree(GetProcessHeap(), 0, all);
        all = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(n + 8) * sizeof(KTcpRow));
        if (!all) { n = 0; break; }
        LONG_PTR got = NtNovaSockCtl(0, 14, (ULONG_PTR)(n + 8), all);
        if (got <= n + 8) { n = got; break; }
        n = got;                                    /* (more sockets came meanwhile) */
    }
    DWORD k = 0;
    for (LONG_PTR i = 0; i < n; i++) {
        KTcpRow *r = &all[i];
        BOOL listening = r->state == MIB_TCP_STATE_LISTEN_;
        if (r->local.family != af || (which == 0 && !listening) || (which == 1 && listening)) continue;
        all[k++] = *r;
    }
    /* by local address and port, then remote address and port */
    for (DWORD i = 1; order && i < k; i++)
        for (DWORD j = i; j > 0; j--) {
            KTcpRow *a = &all[j - 1], *b = &all[j];
            int c = memcmp(a->local.addr, b->local.addr, 16);
            if (!c) c = (int)((a->local.port >> 8 | a->local.port << 8) & 0xFFFF) - (int)((b->local.port >> 8 | b->local.port << 8) & 0xFFFF);
            if (!c) c = memcmp(a->remote.addr, b->remote.addr, 16);
            if (!c) c = (int)((a->remote.port >> 8 | a->remote.port << 8) & 0xFFFF) - (int)((b->remote.port >> 8 | b->remote.port << 8) & 0xFFFF);
            if (c <= 0) break;
            KTcpRow t = *a; *a = *b; *b = t;
        }
    *count = k;
    return all;
}

static DWORD tcp_table(PVOID t, PDWORD size, BOOL order, ULONG af, int cls)
{
    if (!size || (af != AF_INET_ && af != AF_INET6_) || cls < -1 || cls > TCP_MODULE_ALL) return ERROR_INVALID_PARAMETER;
    if (af == AF_INET6_ && cls >= 0 && cls <= TCP_BASIC_ALL) return ERROR_NOT_SUPPORTED;
    /* row sizes: MIB_TCPROW(_OWNER_PID/_OWNER_MODULE), MIB_TCP6ROW_OWNER_PID/_MODULE, MIB_TCP6ROW (cls -1) */
    static const DWORD v4[3] = { 20, 24, 160 }, v6[3] = { 52, 56, 192 };
    int kind = cls < 0 ? 0 : cls / 3;
    DWORD row = af == AF_INET_ ? v4[kind] : v6[kind], head = kind == 2 ? 8 : 4;
    DWORD n;
    KTcpRow *rows = tcp_rows(af, cls % 3, order, &n);
    DWORD need = head + n * row;
    if (!t || *size < need) {
        HeapFree(GetProcessHeap(), 0, rows);
        *size = need + 4 * row;                      /* (room for a few more) */
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memset(t, 0, need);
    *(DWORD *)t = n;
    for (DWORD i = 0; i < n; i++) {
        BYTE *o = (BYTE *)t + head + i * row;
        KTcpRow *r = &rows[i];
        DWORD *d = (DWORD *)o;
        if (af == AF_INET_) {                         /* state, local addr, port, remote addr, port[, pid] */
            d[0] = r->state;
            memcpy(&d[1], r->local.addr, 4); d[2] = r->local.port;
            memcpy(&d[3], r->remote.addr, 4); d[4] = r->remote.port;
            if (kind) d[5] = r->owner;
        } else if (kind) {                           /* MIB_TCP6ROW_OWNER_*: local, scope, port, remote, scope, port, state, pid */
            memcpy(o, r->local.addr, 16); d[4] = r->local.scope; d[5] = r->local.port;
            memcpy(o + 24, r->remote.addr, 16); d[10] = r->remote.scope; d[11] = r->remote.port;
            d[12] = r->state; d[13] = r->owner;
        } else {                                     /* MIB_TCP6ROW: state, local, scope, port, remote, scope, port */
            d[0] = r->state;
            memcpy(o + 4, r->local.addr, 16); d[5] = r->local.scope; d[6] = r->local.port;
            memcpy(o + 28, r->remote.addr, 16); d[11] = r->remote.scope; d[12] = r->remote.port;
        }
    }
    HeapFree(GetProcessHeap(), 0, rows);
    return NO_ERROR;
}

IPHLPAPI DWORD WINAPI GetExtendedTcpTable(PVOID t, PDWORD size, BOOL order, ULONG af, int cls, ULONG r)
{
    if (r) return ERROR_INVALID_PARAMETER;
    return tcp_table(t, size, order, af, cls);
}
IPHLPAPI DWORD WINAPI GetTcpTable(PVOID t, PULONG size, BOOL order) { return tcp_table(t, size, order, AF_INET_, TCP_BASIC_ALL); }
IPHLPAPI DWORD WINAPI GetTcp6Table(PVOID t, PULONG size, BOOL order) { return tcp_table(t, size, order, AF_INET6_, -1); }

IPHLPAPI DWORD WINAPI GetBestInterface(DWORD addr, PDWORD index) { (void)addr; (void)index; return ERROR_NO_DATA_; }
IPHLPAPI DWORD WINAPI GetBestRoute(DWORD dst, DWORD src, PVOID row) { (void)dst; (void)src; (void)row; return ERROR_NO_DATA_; }
IPHLPAPI DWORD WINAPI NotifyAddrChange(PHANDLE h, LPOVERLAPPED o) { (void)h; (void)o; return ERROR_NOT_SUPPORTED; }

/* Interface names and indexes: the one interface is "eth0", index 1 */
IPHLPAPI ULONG WINAPI if_nametoindex(const char *name) { return name && !lstrcmpA(name, "eth0") ? 1 : 0; }
IPHLPAPI char *WINAPI if_indextoname(ULONG index, char *name)
{
    if (index != 1 || !name) return 0;
    lstrcpyA(name, "eth0");
    return name;
}

/* -----------------------------------------------------------------------
 * The IP helper's newer calls (NET_LUID-based): one interface, eth0
 * ----------------------------------------------------------------------- */
typedef union { ULONG64 Value; } NET_LUID_;
IPHLPAPI DWORD WINAPI ConvertInterfaceIndexToLuid(ULONG index, NET_LUID_ *luid)
{
    if (!luid) return ERROR_INVALID_PARAMETER;
    if (index != 1) { luid->Value = 0; return ERROR_FILE_NOT_FOUND; }
    luid->Value = ((ULONG64)6 << 48) | ((ULONG64)1 << 24);       /* IfType 6 (Ethernet), NetLuidIndex 1 */
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI ConvertInterfaceLuidToIndex(const NET_LUID_ *luid, ULONG *index)
{
    if (!luid || !index) return ERROR_INVALID_PARAMETER;
    *index = 1;
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI ConvertInterfaceLuidToNameW(const NET_LUID_ *luid, LPWSTR name, SIZE_T n)
{
    if (!luid || !name) return ERROR_INVALID_PARAMETER;
    if (n < 10) return ERROR_NOT_ENOUGH_MEMORY;
    lstrcpyW(name, L"ethernet_1");
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI ConvertInterfaceLuidToNameA(const NET_LUID_ *luid, LPSTR name, SIZE_T n)
{
    if (!luid || !name) return ERROR_INVALID_PARAMETER;
    if (n < 11) return ERROR_NOT_ENOUGH_MEMORY;
    lstrcpyA(name, "ethernet_1");
    return NO_ERROR;
}
/* Change notifications: registered, never delivered */
IPHLPAPI DWORD WINAPI NotifyIpInterfaceChange(USHORT family, PVOID cb, PVOID ctx, BOOLEAN initial, HANDLE *h)
{
    (void)family; (void)cb; (void)ctx; (void)initial;
    if (!h) return ERROR_INVALID_PARAMETER;
    *h = (HANDLE)(ULONG_PTR)0x1F01;
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI NotifyUnicastIpAddressChange(USHORT family, PVOID cb, PVOID ctx, BOOLEAN initial, HANDLE *h)
{
    return NotifyIpInterfaceChange(family, cb, ctx, initial, h);
}
IPHLPAPI DWORD WINAPI NotifyRouteChange2(USHORT family, PVOID cb, PVOID ctx, BOOLEAN initial, HANDLE *h)
{
    return NotifyIpInterfaceChange(family, cb, ctx, initial, h);
}
IPHLPAPI DWORD WINAPI CancelMibChangeNotify2(HANDLE h) { (void)h; return NO_ERROR; }
IPHLPAPI DWORD WINAPI GetBestRoute2(PVOID luid, ULONG index, const void *src, const void *dst, ULONG opts, PVOID route, PVOID best)
{
    (void)luid; (void)index; (void)src; (void)dst; (void)opts; (void)route; (void)best;
    return 1168;                                              /* ERROR_NOT_FOUND */
}

/* MIB_IF_ROW2 (1352 bytes, x64 and x86 alike): the adapter, with its
 * counters (NtNovaSockCtl op 17), as Task Manager and Resource Monitor
 * read an interface's throughput */
#define IF_ROW2_SIZE 1352
static void row2_put(BYTE *r, ULONG off, ULONG64 v, int n) { memcpy(r + off, &v, (size_t)n); }

static BOOL if_row2(const KAdapter *k, BYTE *r)
{
    ULONG64 c[4] = { 0 };
    memset(r, 0, IF_ROW2_SIZE);
    NtNovaSockCtl(0, 17, 0, c);
    row2_put(r, 0, (6ull << 48) | ((ULONG64)k->if_index << 24), 8);   /* InterfaceLuid */
    row2_put(r, 8, k->if_index, 4);                                   /* InterfaceIndex */
    const char *alias = "Ethernet", *desc = k->adapter[0] ? k->adapter : "Ethernet adapter";
    for (int i = 0; alias[i]; i++) r[28 + 2 * i] = (BYTE)alias[i];    /* Alias */
    for (int i = 0; desc[i] && i < 256; i++) r[542 + 2 * i] = (BYTE)desc[i];   /* Description */
    row2_put(r, 1056, 6, 4);                                          /* PhysicalAddressLength */
    memcpy(r + 1060, k->mac, 6);                                      /* PhysicalAddress */
    memcpy(r + 1092, k->mac, 6);                                      /* PermanentPhysicalAddress */
    row2_put(r, 1124, 1500, 4);                                       /* Mtu */
    row2_put(r, 1128, 6, 4);                                          /* Type: IF_TYPE_ETHERNET_CSMACD */
    row2_put(r, 1144, 2, 4);                                          /* AccessType: NET_IF_ACCESS_BROADCAST */
    row2_put(r, 1148, 0, 4);                                          /* DirectionType: send and receive */
    r[1152] = 0x01;                                                   /* InterfaceAndOperStatusFlags: HardwareInterface */
    BOOL up = (k->flags & 2) != 0;
    row2_put(r, 1156, up ? 1 : 2, 4);                                 /* OperStatus: Up, Down */
    row2_put(r, 1160, 1, 4);                                          /* AdminStatus: Up */
    row2_put(r, 1164, up ? 1 : 2, 4);                                 /* MediaConnectState: Connected, Disconnected */
    row2_put(r, 1184, 1, 4);                                          /* ConnectionType: DEDICATED */
    ULONG64 speed = up ? 1000000000ull : 0;
    row2_put(r, 1192, speed, 8);                                      /* TransmitLinkSpeed */
    row2_put(r, 1200, speed, 8);                                      /* ReceiveLinkSpeed */
    row2_put(r, 1208, c[0], 8);                                       /* InOctets */
    row2_put(r, 1216, c[2], 8);                                       /* InUcastPkts */
    row2_put(r, 1256, c[0], 8);                                       /* InUcastOctets */
    row2_put(r, 1280, c[1], 8);                                       /* OutOctets */
    row2_put(r, 1288, c[3], 8);                                       /* OutUcastPkts */
    row2_put(r, 1320, c[1], 8);                                       /* OutUcastOctets */
    return TRUE;
}

/* GetIfEntry2: the row asked for by its InterfaceLuid or, with that zero,
 * its InterfaceIndex */
IPHLPAPI DWORD WINAPI GetIfEntry2(PVOID row)
{
    if (!row) return ERROR_INVALID_PARAMETER;
    KAdapter k;
    BYTE *r = row;
    ULONG64 luid;
    ULONG index;
    memcpy(&luid, r, 8);
    memcpy(&index, r + 8, 4);
    if (!adapter(&k)) return 2;                                      /* ERROR_FILE_NOT_FOUND */
    ULONG64 mine = (6ull << 48) | ((ULONG64)k.if_index << 24);
    if (luid ? luid != mine : index != k.if_index) return 2;
    if_row2(&k, r);
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI GetIfEntry2Ex(int level, PVOID row) { (void)level; return GetIfEntry2(row); }

/* MIB_IF_TABLE2: NumEntries, then (8-aligned) the adapter's row */
IPHLPAPI DWORD WINAPI GetIfTable2Ex(int level, PVOID *table)
{
    (void)level;
    if (!table) return ERROR_INVALID_PARAMETER;
    KAdapter k;
    BOOL have = adapter(&k);
    BYTE *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 8 + (have ? IF_ROW2_SIZE : 8));
    if (!t) return ERROR_NOT_ENOUGH_MEMORY;
    if (have && if_row2(&k, t + 8)) *(ULONG *)t = 1;
    *table = t;
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI GetIfTable2(PVOID *table) { return GetIfTable2Ex(0, table); }

/* The other MIB_*_TABLE2s: empty */
static DWORD empty_table2(PVOID *table)
{
    if (!table) return ERROR_INVALID_PARAMETER;
    ULONG *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 16);
    if (!t) return ERROR_NOT_ENOUGH_MEMORY;
    *table = t;                                       /* NumEntries = 0 */
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI GetIpInterfaceTable(USHORT family, PVOID *table) { (void)family; return empty_table2(table); }
IPHLPAPI DWORD WINAPI GetUnicastIpAddressTable(USHORT family, PVOID *table) { (void)family; return empty_table2(table); }
IPHLPAPI DWORD WINAPI GetIpForwardTable2(USHORT family, PVOID *table) { (void)family; return empty_table2(table); }
IPHLPAPI DWORD WINAPI GetIpNetTable2(USHORT family, PVOID *table) { (void)family; return empty_table2(table); }
IPHLPAPI DWORD WINAPI GetAnycastIpAddressTable(USHORT family, PVOID *table) { (void)family; return empty_table2(table); }
IPHLPAPI DWORD WINAPI GetMulticastIpAddressTable(USHORT family, PVOID *table) { (void)family; return empty_table2(table); }
IPHLPAPI VOID WINAPI FreeMibTable(PVOID table) { if (table) HeapFree(GetProcessHeap(), 0, table); }

/* The interface toward an address (sockaddr form): no route is known */
IPHLPAPI DWORD WINAPI GetBestInterfaceEx(const void *addr, PDWORD index) { (void)addr; (void)index; return ERROR_NO_DATA_; }

/* The interface's other names: "ethernet_1" (its NDIS-style name) and a
 * GUID of its own, both naming eth0's LUID */
static const GUID eth0_guid = { 0x4e6f7661, 0x0001, 0x4e6f, { 0x80, 0x00, 0x65, 0x74, 0x68, 0x30, 0x00, 0x01 } };
#define ETH0_LUID (((ULONG64)6 << 48) | ((ULONG64)1 << 24))
IPHLPAPI DWORD WINAPI ConvertInterfaceNameToLuidW(LPCWSTR name, NET_LUID_ *luid)
{
    if (!name || !luid) return ERROR_INVALID_PARAMETER;
    if (lstrcmpiW(name, L"ethernet_1")) { luid->Value = 0; return ERROR_INVALID_NAME; }
    luid->Value = ETH0_LUID;
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI ConvertInterfaceNameToLuidA(LPCSTR name, NET_LUID_ *luid)
{
    if (!name || !luid) return ERROR_INVALID_PARAMETER;
    if (lstrcmpiA(name, "ethernet_1")) { luid->Value = 0; return ERROR_INVALID_NAME; }
    luid->Value = ETH0_LUID;
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI ConvertInterfaceLuidToGuid(const NET_LUID_ *luid, GUID *guid)
{
    if (!luid || !guid) return ERROR_INVALID_PARAMETER;
    if (luid->Value != ETH0_LUID) { memset(guid, 0, sizeof(*guid)); return ERROR_FILE_NOT_FOUND; }
    *guid = eth0_guid;
    return NO_ERROR;
}
IPHLPAPI DWORD WINAPI ConvertInterfaceGuidToLuid(const GUID *guid, NET_LUID_ *luid)
{
    if (!luid || !guid) return ERROR_INVALID_PARAMETER;
    const BYTE *a = (const BYTE *)guid, *b = (const BYTE *)&eth0_guid;
    for (int i = 0; i < 16; i++)
        if (a[i] != b[i]) { luid->Value = 0; return ERROR_FILE_NOT_FOUND; }
    luid->Value = ETH0_LUID;
    return NO_ERROR;
}

/* The adapter list for DHCP's calls (IP_INTERFACE_INFO): empty, like
 * GetAdaptersInfo's; the kernel keeps the DHCP lease (dhcpcsvc), so there
 * is no adapter here to release or renew */
IPHLPAPI DWORD WINAPI GetInterfaceInfo(PVOID info, PULONG size) { (void)info; if (!size) return ERROR_INVALID_PARAMETER; return ERROR_NO_DATA_; }
IPHLPAPI DWORD WINAPI IpReleaseAddress(PVOID adapter) { return adapter ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER; }
IPHLPAPI DWORD WINAPI IpRenewAddress(PVOID adapter) { return adapter ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER; }
/* NotifyAddrChange never starts one, so there is no request to cancel */
IPHLPAPI BOOL WINAPI CancelIPChangeNotify(LPOVERLAPPED o) { (void)o; SetLastError(ERROR_NOT_FOUND); return FALSE; }
