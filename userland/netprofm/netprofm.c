/*
 * netprofm.dll — the Network List Manager: the NetworkListManager class
 * (INetworkListManager, INetworkCostManager, IConnectionPointContainer),
 * which programs ask whether the PC is connected and to the Internet, which
 * networks it knows and of what category (Public, Private), and which tell
 * them through events when that changes.
 *
 * Where it comes from:
 *   - connectivity: the network adapter as the kernel reports it
 *     (NtNovaSockCtl op 16, as iphlpapi asks): IPv4 with a default gateway
 *     is IPV4_INTERNET, without one IPV4_SUBNET; an IPv6 address that is
 *     not link-local (from a router's advertisement or DHCPv6) is
 *     IPV6_INTERNET, link-local alone IPV6_NOTRAFFIC.  Windows also probes
 *     a web server (NCSI) before it says Internet; NovaOS takes the router
 *     at its word.
 *   - networks: Windows' own list, HKLM\SOFTWARE\Microsoft\Windows NT\
 *     CurrentVersion\NetworkList\Profiles\{GUID} (ProfileName,
 *     Description, Category), which the kernel's firewall keeps
 *     (kernel/net/firewall.c): a network is known by its gateway's MAC, a
 *     new one is Public.  The one the adapter is on now is the kernel's
 *     (NtNovaSockCtl op 23, NOVAFWC_NETWORK_GET).
 *   - the connection: the adapter, by the GUID iphlpapi names it with
 *     ({4E4F5641-0000-4000-8000-<MAC>}), as its connection and adapter id.
 *
 * SetCategory on the network the PC is on goes through the firewall
 * (NOVAFWC_NETWORK_SET), so the profile in force changes at once; on
 * another network, SetName and SetDescription, it writes the registry
 * value.  All need an administrator (E_ACCESSDENIED), as on Windows.
 * The 32-bit DLL reads and writes the same (64-bit) registry view: on
 * Windows it is only a client of the one network list service.
 *
 * Events: advising a sink starts a thread that looks once a second and
 * calls INetworkListManagerEvents::ConnectivityChanged, INetworkEvents
 * (NetworkAdded, NetworkDeleted, NetworkConnectivityChanged,
 * NetworkPropertyChanged for the name, description, category and
 * connection) and INetworkConnectionEvents::
 * NetworkConnectionConnectivityChanged on that thread (Windows calls
 * through COM's marshaling, into the advising apartment).
 * INetworkCostManagerEvents sinks are accepted; the cost never changes
 * (a wired network is unrestricted), so they are never called.
 *
 * Not here yet: IDispatch (no type library: scripts that bind late cannot
 * use these objects), get__NewEnum, a second adapter, the "Unidentified
 * network" of an adapter with no gateway, the networks' creation and
 * connection times (zeros), Wi-Fi's own cost and data plans.
 */
#include <windows.h>
#include <oleauto.h>
#include <netlistmgr.h>
#include <winternl.h>
#include "../include/novafw.h"

#define NLMAPI __declspec(dllexport)
#ifndef offsetof
#define offsetof(t, f) __builtin_offsetof(t, f)
#endif

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

static volatile LONG g_objects, g_locks;

static void *zalloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void zfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

static const WCHAR PROFILES[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkList\\Profiles";
#define E_NOT_FOUND HRESULT_FROM_WIN32(ERROR_NOT_FOUND)

/* ---------------------------------------------------------------------------
 * The adapter (kernel/um/um_socket.c NetAdapterInfo, as iphlpapi reads it)
 * ------------------------------------------------------------------------- */
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
    BYTE  ip6_dhcp[4];
    DWORD ip6_valid[4], ip6_pref[4];
    DWORD dhcp6_iaid;
    BYTE  dhcp6_server[16];
    BYTE  dhcp6_duid_len, dhcp6_duid[20];
    char  dns_suffix[128];
} KAdapter;

static BOOL adapter(KAdapter *a)
{
    memset(a, 0, sizeof(*a));
    return NtNovaSockCtl(0, 16, 0, a) == (LONG_PTR)sizeof(*a) && (a->flags & 4);
}

static NLM_CONNECTIVITY adapter_connectivity(const KAdapter *a)
{
    if (!(a->flags & 2)) return NLM_CONNECTIVITY_DISCONNECTED;          /* no link */
    int c = 0;
    if ((a->flags & 1) && a->ip) c |= a->gw ? NLM_CONNECTIVITY_IPV4_INTERNET : NLM_CONNECTIVITY_IPV4_SUBNET;
    else c |= NLM_CONNECTIVITY_IPV4_NOTRAFFIC;
    int global6 = 0, any6 = 0;
    for (int i = 0; i < a->ip6_count && i < 4; i++) {
        any6 = 1;
        if (!(a->ip6[i][0] == 0xFE && (a->ip6[i][1] & 0xC0) == 0x80)) global6 = 1;
    }
    if (global6) c |= NLM_CONNECTIVITY_IPV6_INTERNET;
    else if (any6) c |= NLM_CONNECTIVITY_IPV6_NOTRAFFIC;
    return (NLM_CONNECTIVITY)c;
}

/* {4E4F5641-0000-4000-8000-<MAC>}: iphlpapi's AdapterName */
static void adapter_id(const KAdapter *a, GUID *g)
{
    g->Data1 = 0x4E4F5641;
    g->Data2 = 0;
    g->Data3 = 0x4000;
    g->Data4[0] = 0x80;
    g->Data4[1] = 0x00;
    memcpy(&g->Data4[2], a->mac, 6);
}

static BOOL connected(NLM_CONNECTIVITY c)
{
    return (c & (NLM_CONNECTIVITY_IPV4_SUBNET | NLM_CONNECTIVITY_IPV4_LOCALNETWORK | NLM_CONNECTIVITY_IPV4_INTERNET |
                 NLM_CONNECTIVITY_IPV6_SUBNET | NLM_CONNECTIVITY_IPV6_LOCALNETWORK | NLM_CONNECTIVITY_IPV6_INTERNET)) != 0;
}
static BOOL internet(NLM_CONNECTIVITY c)
{
    return (c & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET)) != 0;
}

/* ---------------------------------------------------------------------------
 * A look at everything: the adapter and the known networks
 * ------------------------------------------------------------------------- */
#define MAX_NETS 32
typedef struct {
    GUID  id;
    WCHAR name[64], desc[64];
    DWORD category;
    BOOL  current;                      /* the network the adapter is on now */
} NetInfo;

typedef struct {
    BOOL  have_adapter;
    GUID  adapter;
    NLM_CONNECTIVITY conn;              /* the adapter's */
    int   n;
    NetInfo net[MAX_NETS];
} Snap;

static void reg_text(HKEY k, const WCHAR *name, WCHAR *out, DWORD cap)
{
    DWORD type = 0, n = (cap - 1) * sizeof(WCHAR);
    out[0] = 0;
    if (RegQueryValueExW(k, name, NULL, &type, (BYTE *)out, &n) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ))
        out[n / sizeof(WCHAR)] = 0;
    else
        out[0] = 0;
}

static void snapshot(Snap *s)
{
    memset(s, 0, sizeof(*s));
    KAdapter a;
    if (adapter(&a)) {
        s->have_adapter = TRUE;
        adapter_id(&a, &s->adapter);
        s->conn = adapter_connectivity(&a);
    }
    NovaFwNetwork cur;
    memset(&cur, 0, sizeof(cur));
    GUID cur_id;
    BOOL have_cur = FALSE;
    if (NtNovaSockCtl(0, NOVAFW_FW, NOVAFWC_NETWORK_GET, &cur) == 0 && cur.connected && cur.guid[0]) {
        WCHAR w[40];
        int i = 0;
        for (; cur.guid[i] && i < 39; i++) w[i] = (WCHAR)(unsigned char)cur.guid[i];
        w[i] = 0;
        have_cur = SUCCEEDED(CLSIDFromString(w, &cur_id));
    }
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, PROFILES, 0, KEY_READ | KEY_WOW64_64KEY, &root) != ERROR_SUCCESS) return;
    for (DWORD i = 0; s->n < MAX_NETS; i++) {
        WCHAR sub[64];
        DWORD n = 64;
        if (RegEnumKeyExW(root, i, sub, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        NetInfo *ni = &s->net[s->n];
        if (FAILED(CLSIDFromString(sub, &ni->id))) continue;
        HKEY k;
        if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) continue;
        reg_text(k, L"ProfileName", ni->name, 64);
        reg_text(k, L"Description", ni->desc, 64);
        DWORD cat = 0, type = 0, cb = sizeof(cat);
        if (RegQueryValueExW(k, L"Category", NULL, &type, (BYTE *)&cat, &cb) != ERROR_SUCCESS || type != REG_DWORD || cat > 2)
            cat = 0;
        ni->category = cat;
        RegCloseKey(k);
        ni->current = have_cur && IsEqualGUID(&ni->id, &cur_id) && connected(s->conn);
        s->n++;
    }
    RegCloseKey(root);
}

static const NetInfo *snap_find(const Snap *s, const GUID *id)
{
    for (int i = 0; i < s->n; i++) if (IsEqualGUID(&s->net[i].id, id)) return &s->net[i];
    return NULL;
}
static const NetInfo *snap_current(const Snap *s)
{
    for (int i = 0; i < s->n; i++) if (s->net[i].current) return &s->net[i];
    return NULL;
}
static NLM_CONNECTIVITY net_conn(const Snap *s, const NetInfo *ni)
{
    return ni && ni->current ? s->conn : NLM_CONNECTIVITY_DISCONNECTED;
}

/* ---------------------------------------------------------------------------
 * IDispatch: no type library yet
 * ------------------------------------------------------------------------- */
static HRESULT STDMETHODCALLTYPE disp_count(void *t, UINT *n) { (void)t; if (n) *n = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE disp_info(void *t, UINT i, LCID l, ITypeInfo **out) { (void)t; (void)i; (void)l; if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE disp_ids(void *t, REFIID r, LPOLESTR *names, UINT n, LCID l, DISPID *ids)
{ (void)t; (void)r; (void)names; (void)n; (void)l; (void)ids; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE disp_invoke(void *t, DISPID id, REFIID r, LCID l, WORD f, DISPPARAMS *p, VARIANT *res, EXCEPINFO *ei, UINT *ae)
{ (void)t; (void)id; (void)r; (void)l; (void)f; (void)p; (void)res; (void)ei; (void)ae; return E_NOTIMPL; }
#define DISPATCH_SLOTS (void *)disp_count, (void *)disp_info, (void *)disp_ids, (void *)disp_invoke

static HRESULT put_bool(VARIANT_BOOL *out, BOOL v) { if (!out) return E_POINTER; *out = v ? VARIANT_TRUE : VARIANT_FALSE; return S_OK; }

/* The cost every connection has: a wired network's (or the simulated one) */
static volatile LONG g_sim_on;
static NLM_SIMULATED_PROFILE_INFO g_sim;

static DWORD current_cost(void) { return g_sim_on ? (DWORD)g_sim.cost : NLM_CONNECTION_COST_UNRESTRICTED; }
static void dataplan(NLM_DATAPLAN_STATUS *st, const GUID *adapter)
{
    memset(st, 0, sizeof(*st));
    if (adapter) st->InterfaceGuid = *adapter;
    st->UsageData.UsageInMegabytes = g_sim_on ? g_sim.UsageInMegabytes : NLM_UNKNOWN_DATAPLAN_STATUS;
    st->DataLimitInMegabytes = g_sim_on ? g_sim.DataLimitInMegabytes : NLM_UNKNOWN_DATAPLAN_STATUS;
    st->InboundBandwidthInKbps = st->OutboundBandwidthInKbps = NLM_UNKNOWN_DATAPLAN_STATUS;
    st->MaxTransferSizeInMegabytes = NLM_UNKNOWN_DATAPLAN_STATUS;
}

/* ---------------------------------------------------------------------------
 * Network: INetwork, by its id (each call looks again, so it stays live)
 * ------------------------------------------------------------------------- */
typedef struct { INetwork iface; LONG refs; GUID id; } Network;
typedef struct { INetworkConnection iface; INetworkConnectionCost cost; LONG refs; GUID id; } Connection;

static HRESULT network_new(const GUID *id, INetwork **out);
static HRESULT connection_new(const GUID *id, INetworkConnection **out);
static HRESULT enum_new(BOOL connections, const GUID *ids, int n, void **out);

#define NET(t) ((Network *)(t))

static HRESULT STDMETHODCALLTYPE net_qi(INetwork *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_INetwork)) {
        *ppv = t;
        InterlockedIncrement(&NET(t)->refs);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE net_addref(INetwork *t) { return (ULONG)InterlockedIncrement(&NET(t)->refs); }
static ULONG STDMETHODCALLTYPE net_release(INetwork *t)
{
    LONG n = InterlockedDecrement(&NET(t)->refs);
    if (!n) { zfree(t); InterlockedDecrement(&g_objects); }
    return (ULONG)n;
}

/* This network as it is now (E_NOT_FOUND: it has been deleted) */
static HRESULT net_look(INetwork *t, Snap **s, const NetInfo **ni)
{
    *s = zalloc(sizeof(Snap));
    if (!*s) return E_OUTOFMEMORY;
    snapshot(*s);
    *ni = snap_find(*s, &NET(t)->id);
    if (!*ni) { zfree(*s); return E_NOT_FOUND; }
    return S_OK;
}

static HRESULT text_get(INetwork *t, BSTR *out, BOOL desc)
{
    if (!out) return E_POINTER;
    *out = NULL;
    Snap *s; const NetInfo *ni;
    HRESULT hr = net_look(t, &s, &ni);
    if (FAILED(hr)) return hr;
    *out = SysAllocString(desc ? ni->desc : ni->name);
    zfree(s);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT reg_write(const GUID *id, const WCHAR *value, DWORD type, const void *data, DWORD n)
{
    WCHAR path[160], g[40];
    lstrcpyW(path, PROFILES);
    lstrcatW(path, L"\\");
    if (!StringFromGUID2(id, g, 40)) return E_FAIL;
    lstrcatW(path, g);
    HKEY k;
    LSTATUS e = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &k);
    if (e == ERROR_SUCCESS) {
        e = RegSetValueExW(k, value, 0, type, (const BYTE *)data, n);
        RegCloseKey(k);
    }
    return e == ERROR_SUCCESS ? S_OK : e == ERROR_FILE_NOT_FOUND ? E_NOT_FOUND : HRESULT_FROM_WIN32(e);
}

static HRESULT text_set(INetwork *t, BSTR v, BOOL desc)
{
    if (!v || !v[0]) return E_INVALIDARG;
    if (lstrlenW(v) > 63) return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    Snap *s; const NetInfo *ni;
    HRESULT hr = net_look(t, &s, &ni);
    if (FAILED(hr)) return hr;
    zfree(s);
    return reg_write(&NET(t)->id, desc ? L"Description" : L"ProfileName", REG_SZ, v, (DWORD)(lstrlenW(v) + 1) * sizeof(WCHAR));
}

static HRESULT STDMETHODCALLTYPE net_get_name(INetwork *t, BSTR *out) { return text_get(t, out, FALSE); }
static HRESULT STDMETHODCALLTYPE net_set_name(INetwork *t, BSTR v) { return text_set(t, v, FALSE); }
static HRESULT STDMETHODCALLTYPE net_get_desc(INetwork *t, BSTR *out) { return text_get(t, out, TRUE); }
static HRESULT STDMETHODCALLTYPE net_set_desc(INetwork *t, BSTR v) { return text_set(t, v, TRUE); }
static HRESULT STDMETHODCALLTYPE net_get_id(INetwork *t, GUID *out)
{
    if (!out) return E_POINTER;
    *out = NET(t)->id;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE net_domain(INetwork *t, NLM_DOMAIN_TYPE *out)
{
    if (!out) return E_POINTER;
    Snap *s; const NetInfo *ni;
    HRESULT hr = net_look(t, &s, &ni);
    if (FAILED(hr)) return hr;
    *out = ni->category == 2 ? NLM_DOMAIN_TYPE_DOMAIN_AUTHENTICATED : NLM_DOMAIN_TYPE_NON_DOMAIN_NETWORK;
    zfree(s);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE net_connections(INetwork *t, IEnumNetworkConnections **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    Snap *s; const NetInfo *ni;
    HRESULT hr = net_look(t, &s, &ni);
    if (FAILED(hr)) return hr;
    hr = enum_new(TRUE, &s->adapter, ni->current && s->have_adapter ? 1 : 0, (void **)out);
    zfree(s);
    return hr;
}
static HRESULT STDMETHODCALLTYPE net_times(INetwork *t, DWORD *cl, DWORD *ch, DWORD *nl, DWORD *nh)
{
    if (!cl || !ch || !nl || !nh) return E_POINTER;
    Snap *s; const NetInfo *ni;
    HRESULT hr = net_look(t, &s, &ni);
    if (FAILED(hr)) return hr;
    zfree(s);
    *cl = *ch = *nl = *nh = 0;                  /* (the firewall keeps no dates yet) */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE net_conn_get(INetwork *t, NLM_CONNECTIVITY *out)
{
    if (!out) return E_POINTER;
    Snap *s; const NetInfo *ni;
    HRESULT hr = net_look(t, &s, &ni);
    if (FAILED(hr)) return hr;
    *out = net_conn(s, ni);
    zfree(s);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE net_is_internet(INetwork *t, VARIANT_BOOL *out)
{
    NLM_CONNECTIVITY c;
    HRESULT hr = net_conn_get(t, &c);
    return FAILED(hr) ? hr : put_bool(out, internet(c));
}
static HRESULT STDMETHODCALLTYPE net_is_connected(INetwork *t, VARIANT_BOOL *out)
{
    NLM_CONNECTIVITY c;
    HRESULT hr = net_conn_get(t, &c);
    return FAILED(hr) ? hr : put_bool(out, connected(c));
}
static HRESULT STDMETHODCALLTYPE net_get_category(INetwork *t, NLM_NETWORK_CATEGORY *out)
{
    if (!out) return E_POINTER;
    Snap *s; const NetInfo *ni;
    HRESULT hr = net_look(t, &s, &ni);
    if (FAILED(hr)) return hr;
    *out = (NLM_NETWORK_CATEGORY)ni->category;
    zfree(s);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE net_set_category(INetwork *t, NLM_NETWORK_CATEGORY cat)
{
    /* Domain-authenticated is the domain controller's to say */
    if (cat != NLM_NETWORK_CATEGORY_PUBLIC && cat != NLM_NETWORK_CATEGORY_PRIVATE) return E_INVALIDARG;
    Snap *s; const NetInfo *ni;
    HRESULT hr = net_look(t, &s, &ni);
    if (FAILED(hr)) return hr;
    BOOL current = ni->current;
    zfree(s);
    if (current) {
        /* the firewall's profile follows at once */
        NovaFwNetwork n;
        memset(&n, 0, sizeof(n));
        n.category = (int)cat;
        UINT32 r = (UINT32)(ULONG_PTR)NtNovaSockCtl(0, NOVAFW_FW, NOVAFWC_NETWORK_SET, &n);
        if (r == 0) return S_OK;
        if (r == NOVAWFP_E_ACCESS_DENIED) return E_ACCESSDENIED;
        /* (it went away meanwhile: write the registry as for any network) */
    }
    DWORD v = (DWORD)cat;
    return reg_write(&NET(t)->id, L"Category", REG_DWORD, &v, sizeof(v));
}

static const INetworkVtbl g_net_vtbl = {
    net_qi, net_addref, net_release, DISPATCH_SLOTS,
    net_get_name, net_set_name, net_get_desc, net_set_desc, net_get_id, net_domain,
    net_connections, net_times, net_is_internet, net_is_connected, net_conn_get,
    net_get_category, net_set_category,
};

static HRESULT network_new(const GUID *id, INetwork **out)
{
    Network *n = zalloc(sizeof(*n));
    if (!n) return E_OUTOFMEMORY;
    n->iface.lpVtbl = &g_net_vtbl;
    n->refs = 1;
    n->id = *id;
    InterlockedIncrement(&g_objects);
    *out = &n->iface;
    return S_OK;
}

/* ---------------------------------------------------------------------------
 * Connection: INetworkConnection and INetworkConnectionCost, the adapter
 * ------------------------------------------------------------------------- */
#define CONN(t) ((Connection *)(t))
#define CONN_OF_COST(t) ((Connection *)((char *)(t) - offsetof(Connection, cost)))

static HRESULT STDMETHODCALLTYPE conn_qi(INetworkConnection *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_INetworkConnection))
        *ppv = t;
    else if (IsEqualIID(riid, &IID_INetworkConnectionCost))
        *ppv = &CONN(t)->cost;
    else { *ppv = NULL; return E_NOINTERFACE; }
    InterlockedIncrement(&CONN(t)->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE conn_addref(INetworkConnection *t) { return (ULONG)InterlockedIncrement(&CONN(t)->refs); }
static ULONG STDMETHODCALLTYPE conn_release(INetworkConnection *t)
{
    LONG n = InterlockedDecrement(&CONN(t)->refs);
    if (!n) { zfree(t); InterlockedDecrement(&g_objects); }
    return (ULONG)n;
}

/* The adapter's connectivity if it is still this connection */
static HRESULT conn_look(INetworkConnection *t, Snap **s)
{
    *s = zalloc(sizeof(Snap));
    if (!*s) return E_OUTOFMEMORY;
    snapshot(*s);
    if (!(*s)->have_adapter || !IsEqualGUID(&(*s)->adapter, &CONN(t)->id)) { zfree(*s); return E_NOT_FOUND; }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE conn_network(INetworkConnection *t, INetwork **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    Snap *s;
    HRESULT hr = conn_look(t, &s);
    if (FAILED(hr)) return hr;
    const NetInfo *ni = snap_current(s);
    hr = ni ? network_new(&ni->id, out) : E_NOT_FOUND;
    zfree(s);
    return hr;
}
static HRESULT STDMETHODCALLTYPE conn_connectivity(INetworkConnection *t, NLM_CONNECTIVITY *out)
{
    if (!out) return E_POINTER;
    Snap *s;
    HRESULT hr = conn_look(t, &s);
    if (FAILED(hr)) { *out = NLM_CONNECTIVITY_DISCONNECTED; return hr == E_NOT_FOUND ? S_OK : hr; }
    *out = s->conn;
    zfree(s);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE conn_is_internet(INetworkConnection *t, VARIANT_BOOL *out)
{
    NLM_CONNECTIVITY c;
    HRESULT hr = conn_connectivity(t, &c);
    return FAILED(hr) ? hr : put_bool(out, internet(c));
}
static HRESULT STDMETHODCALLTYPE conn_is_connected(INetworkConnection *t, VARIANT_BOOL *out)
{
    NLM_CONNECTIVITY c;
    HRESULT hr = conn_connectivity(t, &c);
    return FAILED(hr) ? hr : put_bool(out, connected(c));
}
static HRESULT STDMETHODCALLTYPE conn_id(INetworkConnection *t, GUID *out)
{
    if (!out) return E_POINTER;
    *out = CONN(t)->id;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE conn_domain(INetworkConnection *t, NLM_DOMAIN_TYPE *out)
{
    if (!out) return E_POINTER;
    Snap *s;
    HRESULT hr = conn_look(t, &s);
    if (FAILED(hr)) return hr;
    const NetInfo *ni = snap_current(s);
    *out = ni && ni->category == 2 ? NLM_DOMAIN_TYPE_DOMAIN_AUTHENTICATED : NLM_DOMAIN_TYPE_NON_DOMAIN_NETWORK;
    zfree(s);
    return S_OK;
}

static const INetworkConnectionVtbl g_conn_vtbl = {
    conn_qi, conn_addref, conn_release, DISPATCH_SLOTS,
    conn_network, conn_is_internet, conn_is_connected, conn_connectivity, conn_id, conn_id, conn_domain,
};

static HRESULT STDMETHODCALLTYPE ccost_qi(INetworkConnectionCost *t, REFIID riid, void **ppv)
{ return conn_qi(&CONN_OF_COST(t)->iface, riid, ppv); }
static ULONG STDMETHODCALLTYPE ccost_addref(INetworkConnectionCost *t) { return conn_addref(&CONN_OF_COST(t)->iface); }
static ULONG STDMETHODCALLTYPE ccost_release(INetworkConnectionCost *t) { return conn_release(&CONN_OF_COST(t)->iface); }
static HRESULT STDMETHODCALLTYPE ccost_cost(INetworkConnectionCost *t, DWORD *out)
{
    (void)t;
    if (!out) return E_POINTER;
    *out = current_cost();
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ccost_plan(INetworkConnectionCost *t, NLM_DATAPLAN_STATUS *out)
{
    if (!out) return E_POINTER;
    dataplan(out, &CONN_OF_COST(t)->id);
    return S_OK;
}
static const INetworkConnectionCostVtbl g_ccost_vtbl = { ccost_qi, ccost_addref, ccost_release, ccost_cost, ccost_plan };

static HRESULT connection_new(const GUID *id, INetworkConnection **out)
{
    Connection *c = zalloc(sizeof(*c));
    if (!c) return E_OUTOFMEMORY;
    c->iface.lpVtbl = &g_conn_vtbl;
    c->cost.lpVtbl = &g_ccost_vtbl;
    c->refs = 1;
    c->id = *id;
    InterlockedIncrement(&g_objects);
    *out = &c->iface;
    return S_OK;
}

/* ---------------------------------------------------------------------------
 * The enumerators: IEnumNetworks and IEnumNetworkConnections over ids taken
 * when they were made
 * ------------------------------------------------------------------------- */
typedef struct {
    IEnumNetworks iface;                /* (IEnumNetworkConnections has the same slots) */
    LONG  refs;
    BOOL  connections;
    int   n, pos;
    GUID  ids[MAX_NETS];
} Enum;
#define ENUM(t) ((Enum *)(t))

static HRESULT STDMETHODCALLTYPE en_qi(IEnumNetworks *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) ||
        IsEqualIID(riid, ENUM(t)->connections ? &IID_IEnumNetworkConnections : &IID_IEnumNetworks)) {
        *ppv = t;
        InterlockedIncrement(&ENUM(t)->refs);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE en_addref(IEnumNetworks *t) { return (ULONG)InterlockedIncrement(&ENUM(t)->refs); }
static ULONG STDMETHODCALLTYPE en_release(IEnumNetworks *t)
{
    LONG n = InterlockedDecrement(&ENUM(t)->refs);
    if (!n) { zfree(t); InterlockedDecrement(&g_objects); }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE en_newenum(IEnumNetworks *t, IEnumVARIANT **out)
{ (void)t; if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE en_next(IEnumNetworks *t, ULONG want, INetwork **out, ULONG *got)
{
    Enum *e = ENUM(t);
    if (!out) return E_POINTER;
    if (want > 1 && !got) return E_INVALIDARG;
    ULONG n = 0;
    while (n < want && e->pos < e->n) {
        HRESULT hr = e->connections ? connection_new(&e->ids[e->pos], (INetworkConnection **)&out[n])
                                    : network_new(&e->ids[e->pos], &out[n]);
        if (FAILED(hr)) {
            while (n) { n--; ((IUnknown *)out[n])->lpVtbl->Release((IUnknown *)out[n]); out[n] = NULL; }
            if (got) *got = 0;
            return hr;
        }
        e->pos++;
        n++;
    }
    if (got) *got = n;
    return n == want ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE en_skip(IEnumNetworks *t, ULONG n)
{
    Enum *e = ENUM(t);
    ULONG left = (ULONG)(e->n - e->pos);
    e->pos += (int)(n < left ? n : left);
    return n <= left ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE en_reset(IEnumNetworks *t) { ENUM(t)->pos = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE en_clone(IEnumNetworks *t, IEnumNetworks **out)
{
    if (!out) return E_POINTER;
    Enum *e = ENUM(t);
    HRESULT hr = enum_new(e->connections, e->ids, e->n, (void **)out);
    if (SUCCEEDED(hr)) ENUM(*out)->pos = e->pos;
    return hr;
}

static const IEnumNetworksVtbl g_enum_vtbl = {
    en_qi, en_addref, en_release, DISPATCH_SLOTS,
    en_newenum, en_next, en_skip, en_reset, en_clone,
};

static HRESULT enum_new(BOOL connections, const GUID *ids, int n, void **out)
{
    Enum *e = zalloc(sizeof(*e));
    if (!e) { *out = NULL; return E_OUTOFMEMORY; }
    e->iface.lpVtbl = &g_enum_vtbl;
    e->refs = 1;
    e->connections = connections;
    e->n = n < MAX_NETS ? n : MAX_NETS;
    if (e->n) memcpy(e->ids, ids, (size_t)e->n * sizeof(GUID));
    InterlockedIncrement(&g_objects);
    *out = &e->iface;
    return S_OK;
}

/* ---------------------------------------------------------------------------
 * Events: the sinks advised on a manager and the thread that watches
 * ------------------------------------------------------------------------- */
enum { EV_LIST, EV_NETWORK, EV_CONNECTION, EV_COST, EV_KINDS };
static const IID *const g_event_iids[EV_KINDS] = {
    &IID_INetworkListManagerEvents, &IID_INetworkEvents, &IID_INetworkConnectionEvents, &IID_INetworkCostManagerEvents,
};

#define MAX_SINKS 32
typedef struct { IUnknown *sink; int kind; DWORD cookie; } Sink;

/* Kept apart from the manager, so the thread can outlive it by a tick */
typedef struct {
    LONG   refs;
    CRITICAL_SECTION cs;
    Sink   sinks[MAX_SINKS];
    DWORD  next_cookie;
    HANDLE stop, thread;
    BOOL   watching;                    /* (a thread has been started) */
} Hub;

static void hub_release(Hub *h)
{
    if (InterlockedDecrement(&h->refs)) return;
    for (int i = 0; i < MAX_SINKS; i++) if (h->sinks[i].sink) h->sinks[i].sink->lpVtbl->Release(h->sinks[i].sink);
    if (h->stop) CloseHandle(h->stop);
    if (h->thread) CloseHandle(h->thread);
    DeleteCriticalSection(&h->cs);
    zfree(h);
}

/* The sinks of one kind, each with a reference of its own (released by the caller) */
static int hub_sinks(Hub *h, int kind, IUnknown **out)
{
    int n = 0;
    EnterCriticalSection(&h->cs);
    for (int i = 0; i < MAX_SINKS; i++)
        if (h->sinks[i].sink && h->sinks[i].kind == kind) {
            out[n] = h->sinks[i].sink;
            out[n]->lpVtbl->AddRef(out[n]);
            n++;
        }
    LeaveCriticalSection(&h->cs);
    return n;
}

static void fire(Hub *h, const Snap *old, const Snap *now)
{
    IUnknown *s[MAX_SINKS];
    int n;
    if (old->conn != now->conn) {
        n = hub_sinks(h, EV_LIST, s);
        for (int i = 0; i < n; i++) {
            ((INetworkListManagerEvents *)s[i])->lpVtbl->ConnectivityChanged((INetworkListManagerEvents *)s[i], now->conn);
            s[i]->lpVtbl->Release(s[i]);
        }
        if (now->have_adapter) {
            n = hub_sinks(h, EV_CONNECTION, s);
            for (int i = 0; i < n; i++) {
                INetworkConnectionEvents *e = (INetworkConnectionEvents *)s[i];
                e->lpVtbl->NetworkConnectionConnectivityChanged(e, now->adapter, now->conn);
                s[i]->lpVtbl->Release(s[i]);
            }
        }
    }
    n = hub_sinks(h, EV_NETWORK, s);
    if (!n) return;
    for (int i = 0; i < now->n; i++) {
        const NetInfo *a = &now->net[i], *b = snap_find(old, &a->id);
        for (int j = 0; j < n; j++) {
            INetworkEvents *e = (INetworkEvents *)s[j];
            if (!b) { e->lpVtbl->NetworkAdded(e, a->id); continue; }
            NLM_CONNECTIVITY ca = net_conn(now, a), cb = net_conn(old, b);
            if (ca != cb) e->lpVtbl->NetworkConnectivityChanged(e, a->id, ca);
            int f = (lstrcmpW(a->name, b->name) ? NLM_NETWORK_PROPERTY_CHANGE_NAME : 0) |
                    (lstrcmpW(a->desc, b->desc) ? NLM_NETWORK_PROPERTY_CHANGE_DESCRIPTION : 0) |
                    (a->category != b->category ? NLM_NETWORK_PROPERTY_CHANGE_CATEGORY_VALUE : 0) |
                    (a->current != b->current ? NLM_NETWORK_PROPERTY_CHANGE_CONNECTION : 0);
            if (f) e->lpVtbl->NetworkPropertyChanged(e, a->id, (NLM_NETWORK_PROPERTY_CHANGE)f);
        }
    }
    for (int i = 0; i < old->n; i++)
        if (!snap_find(now, &old->net[i].id))
            for (int j = 0; j < n; j++) ((INetworkEvents *)s[j])->lpVtbl->NetworkDeleted((INetworkEvents *)s[j], old->net[i].id);
    for (int j = 0; j < n; j++) s[j]->lpVtbl->Release(s[j]);
}

static DWORD WINAPI watch(void *arg)
{
    Hub *h = arg;
    Snap *a = zalloc(sizeof(Snap)), *b = zalloc(sizeof(Snap));
    if (a && b) {
        snapshot(a);
        while (WaitForSingleObject(h->stop, 1000) == WAIT_TIMEOUT) {
            snapshot(b);
            fire(h, a, b);
            Snap *t = a; a = b; b = t;
        }
    }
    zfree(a); zfree(b);
    hub_release(h);
    return 0;
}

/* ---------------------------------------------------------------------------
 * The manager: INetworkListManager, INetworkCostManager,
 * IConnectionPointContainer and its four connection points
 * ------------------------------------------------------------------------- */
typedef struct Manager Manager;
typedef struct { IConnectionPoint iface; Manager *m; int kind; } Point;
struct Manager {
    INetworkListManager iface;
    INetworkCostManager cost;
    IConnectionPointContainer cpc;
    Point points[EV_KINDS];
    LONG  refs;
    Hub  *hub;
};
#define MGR(t) ((Manager *)(t))
#define MGR_OF(t, field) ((Manager *)((char *)(t) - offsetof(Manager, field)))

static HRESULT STDMETHODCALLTYPE mgr_qi(INetworkListManager *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    Manager *m = MGR(t);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_INetworkListManager))
        *ppv = &m->iface;
    else if (IsEqualIID(riid, &IID_INetworkCostManager))
        *ppv = &m->cost;
    else if (IsEqualIID(riid, &IID_IConnectionPointContainer))
        *ppv = &m->cpc;
    else { *ppv = NULL; return E_NOINTERFACE; }
    InterlockedIncrement(&m->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE mgr_addref(INetworkListManager *t) { return (ULONG)InterlockedIncrement(&MGR(t)->refs); }
static ULONG STDMETHODCALLTYPE mgr_release(INetworkListManager *t)
{
    Manager *m = MGR(t);
    LONG n = InterlockedDecrement(&m->refs);
    if (n) return (ULONG)n;
    Hub *h = m->hub;
    /* the sinks go now, as on Windows when the last reference goes; a
     * watching thread stops at its next look and lets go of the hub */
    EnterCriticalSection(&h->cs);
    Sink gone[MAX_SINKS];
    memcpy(gone, h->sinks, sizeof(gone));
    memset(h->sinks, 0, sizeof(h->sinks));
    if (h->stop) SetEvent(h->stop);
    LeaveCriticalSection(&h->cs);
    for (int i = 0; i < MAX_SINKS; i++) if (gone[i].sink) gone[i].sink->lpVtbl->Release(gone[i].sink);
    hub_release(h);
    zfree(m);
    InterlockedDecrement(&g_objects);
    return 0;
}

static HRESULT STDMETHODCALLTYPE mgr_networks(INetworkListManager *t, NLM_ENUM_NETWORK flags, IEnumNetworks **out)
{
    (void)t;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(flags & NLM_ENUM_NETWORK_ALL) || (flags & ~NLM_ENUM_NETWORK_ALL)) return E_INVALIDARG;
    Snap *s = zalloc(sizeof(Snap));
    if (!s) return E_OUTOFMEMORY;
    snapshot(s);
    GUID ids[MAX_NETS];
    int n = 0;
    for (int i = 0; i < s->n; i++)
        if ((flags & (s->net[i].current ? NLM_ENUM_NETWORK_CONNECTED : NLM_ENUM_NETWORK_DISCONNECTED)))
            ids[n++] = s->net[i].id;
    zfree(s);
    return enum_new(FALSE, ids, n, (void **)out);
}
static HRESULT STDMETHODCALLTYPE mgr_network(INetworkListManager *t, GUID id, INetwork **out)
{
    (void)t;
    if (!out) return E_POINTER;
    *out = NULL;
    Snap *s = zalloc(sizeof(Snap));
    if (!s) return E_OUTOFMEMORY;
    snapshot(s);
    HRESULT hr = snap_find(s, &id) ? network_new(&id, out) : E_NOT_FOUND;
    zfree(s);
    return hr;
}
static HRESULT STDMETHODCALLTYPE mgr_connections(INetworkListManager *t, IEnumNetworkConnections **out)
{
    (void)t;
    if (!out) return E_POINTER;
    *out = NULL;
    Snap *s = zalloc(sizeof(Snap));
    if (!s) return E_OUTOFMEMORY;
    snapshot(s);
    HRESULT hr = enum_new(TRUE, &s->adapter, s->have_adapter && connected(s->conn) ? 1 : 0, (void **)out);
    zfree(s);
    return hr;
}
static HRESULT STDMETHODCALLTYPE mgr_connection(INetworkListManager *t, GUID id, INetworkConnection **out)
{
    (void)t;
    if (!out) return E_POINTER;
    *out = NULL;
    KAdapter a;
    GUID g;
    if (!adapter(&a)) return E_NOT_FOUND;
    adapter_id(&a, &g);
    return IsEqualGUID(&g, &id) ? connection_new(&id, out) : E_NOT_FOUND;
}
static HRESULT STDMETHODCALLTYPE mgr_connectivity(INetworkListManager *t, NLM_CONNECTIVITY *out)
{
    (void)t;
    if (!out) return E_POINTER;
    KAdapter a;
    *out = adapter(&a) ? adapter_connectivity(&a) : NLM_CONNECTIVITY_DISCONNECTED;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mgr_is_internet(INetworkListManager *t, VARIANT_BOOL *out)
{
    NLM_CONNECTIVITY c;
    mgr_connectivity(t, &c);
    return put_bool(out, internet(c));
}
static HRESULT STDMETHODCALLTYPE mgr_is_connected(INetworkListManager *t, VARIANT_BOOL *out)
{
    NLM_CONNECTIVITY c;
    mgr_connectivity(t, &c);
    return put_bool(out, connected(c));
}
static HRESULT STDMETHODCALLTYPE mgr_simulate(INetworkListManager *t, NLM_SIMULATED_PROFILE_INFO *info)
{
    (void)t;
    if (!info) return E_POINTER;
    g_sim = *info;
    InterlockedExchange(&g_sim_on, 1);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mgr_unsimulate(INetworkListManager *t) { (void)t; InterlockedExchange(&g_sim_on, 0); return S_OK; }

static const INetworkListManagerVtbl g_mgr_vtbl = {
    mgr_qi, mgr_addref, mgr_release, DISPATCH_SLOTS,
    mgr_networks, mgr_network, mgr_connections, mgr_connection, mgr_is_internet, mgr_is_connected,
    mgr_connectivity, mgr_simulate, mgr_unsimulate,
};

/* INetworkCostManager */
static HRESULT STDMETHODCALLTYPE cost_qi(INetworkCostManager *t, REFIID riid, void **ppv) { return mgr_qi(&MGR_OF(t, cost)->iface, riid, ppv); }
static ULONG STDMETHODCALLTYPE cost_addref(INetworkCostManager *t) { return mgr_addref(&MGR_OF(t, cost)->iface); }
static ULONG STDMETHODCALLTYPE cost_release(INetworkCostManager *t) { return mgr_release(&MGR_OF(t, cost)->iface); }
static HRESULT STDMETHODCALLTYPE cost_get(INetworkCostManager *t, DWORD *cost, NLM_SOCKADDR *dest)
{
    (void)t; (void)dest;                     /* (one adapter: every destination goes through it) */
    if (!cost) return E_POINTER;
    *cost = current_cost();
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cost_plan(INetworkCostManager *t, NLM_DATAPLAN_STATUS *st, NLM_SOCKADDR *dest)
{
    (void)t; (void)dest;
    if (!st) return E_POINTER;
    KAdapter a;
    GUID g;
    BOOL have = adapter(&a);
    if (have) adapter_id(&a, &g);
    dataplan(st, have ? &g : NULL);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cost_dests(INetworkCostManager *t, UINT32 n, NLM_SOCKADDR *list, VARIANT_BOOL append)
{
    (void)t; (void)append;
    if (n > NLM_MAX_ADDRESS_LIST_SIZE) return E_INVALIDARG;
    if (n && !list) return E_POINTER;
    return S_OK;
}
static const INetworkCostManagerVtbl g_cost_vtbl = { cost_qi, cost_addref, cost_release, cost_get, cost_plan, cost_dests };

/* IConnectionPointContainer */
static HRESULT STDMETHODCALLTYPE cpc_qi(IConnectionPointContainer *t, REFIID riid, void **ppv) { return mgr_qi(&MGR_OF(t, cpc)->iface, riid, ppv); }
static ULONG STDMETHODCALLTYPE cpc_addref(IConnectionPointContainer *t) { return mgr_addref(&MGR_OF(t, cpc)->iface); }
static ULONG STDMETHODCALLTYPE cpc_release(IConnectionPointContainer *t) { return mgr_release(&MGR_OF(t, cpc)->iface); }
static HRESULT STDMETHODCALLTYPE cpc_enum(IConnectionPointContainer *t, IEnumConnectionPoints **out)
{ (void)t; if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE cpc_find(IConnectionPointContainer *t, REFIID iid, IConnectionPoint **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    Manager *m = MGR_OF(t, cpc);
    for (int k = 0; k < EV_KINDS; k++)
        if (IsEqualIID(iid, g_event_iids[k])) {
            *out = &m->points[k].iface;
            InterlockedIncrement(&m->refs);
            return S_OK;
        }
    return CONNECT_E_NOCONNECTION;
}
static const IConnectionPointContainerVtbl g_cpc_vtbl = { cpc_qi, cpc_addref, cpc_release, cpc_enum, cpc_find };

/* IConnectionPoint (each holds the manager through its own references) */
#define POINT(t) ((Point *)(t))
static HRESULT STDMETHODCALLTYPE cp_qi(IConnectionPoint *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IConnectionPoint)) {
        *ppv = t;
        InterlockedIncrement(&POINT(t)->m->refs);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cp_addref(IConnectionPoint *t) { return mgr_addref(&POINT(t)->m->iface); }
static ULONG STDMETHODCALLTYPE cp_release(IConnectionPoint *t) { return mgr_release(&POINT(t)->m->iface); }
static HRESULT STDMETHODCALLTYPE cp_iid(IConnectionPoint *t, IID *out)
{
    if (!out) return E_POINTER;
    *out = *g_event_iids[POINT(t)->kind];
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cp_container(IConnectionPoint *t, IConnectionPointContainer **out)
{
    if (!out) return E_POINTER;
    *out = &POINT(t)->m->cpc;
    InterlockedIncrement(&POINT(t)->m->refs);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cp_advise(IConnectionPoint *t, IUnknown *sink, DWORD *cookie)
{
    if (!sink || !cookie) return E_POINTER;
    *cookie = 0;
    Point *p = POINT(t);
    Hub *h = p->m->hub;
    IUnknown *s = NULL;
    if (FAILED(sink->lpVtbl->QueryInterface(sink, g_event_iids[p->kind], (void **)&s)) || !s)
        return CONNECT_E_CANNOTCONNECT;
    HRESULT hr = CONNECT_E_ADVISELIMIT;
    EnterCriticalSection(&h->cs);
    for (int i = 0; i < MAX_SINKS; i++)
        if (!h->sinks[i].sink) {
            h->sinks[i].sink = s;
            h->sinks[i].kind = p->kind;
            h->sinks[i].cookie = *cookie = ++h->next_cookie;
            s = NULL;
            hr = S_OK;
            break;
        }
    if (SUCCEEDED(hr) && !h->watching) {
        h->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
        InterlockedIncrement(&h->refs);                     /* (the thread's) */
        h->thread = h->stop ? CreateThread(NULL, 0, watch, h, 0, NULL) : NULL;
        if (h->thread) h->watching = TRUE;
        else InterlockedDecrement(&h->refs);
    }
    LeaveCriticalSection(&h->cs);
    if (s) s->lpVtbl->Release(s);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cp_unadvise(IConnectionPoint *t, DWORD cookie)
{
    Hub *h = POINT(t)->m->hub;
    IUnknown *s = NULL;
    EnterCriticalSection(&h->cs);
    for (int i = 0; i < MAX_SINKS; i++)
        if (h->sinks[i].sink && h->sinks[i].cookie == cookie && h->sinks[i].kind == POINT(t)->kind) {
            s = h->sinks[i].sink;
            h->sinks[i].sink = NULL;
            break;
        }
    LeaveCriticalSection(&h->cs);
    if (!s) return CONNECT_E_NOCONNECTION;
    s->lpVtbl->Release(s);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cp_enum(IConnectionPoint *t, IEnumConnections **out)
{ (void)t; if (out) *out = NULL; return E_NOTIMPL; }
static const IConnectionPointVtbl g_cp_vtbl = { cp_qi, cp_addref, cp_release, cp_iid, cp_container, cp_advise, cp_unadvise, cp_enum };

static HRESULT manager_new(INetworkListManager **out)
{
    Manager *m = zalloc(sizeof(*m));
    Hub *h = zalloc(sizeof(*h));
    if (!m || !h) { zfree(m); zfree(h); return E_OUTOFMEMORY; }
    InitializeCriticalSection(&h->cs);
    h->refs = 1;
    m->hub = h;
    m->iface.lpVtbl = &g_mgr_vtbl;
    m->cost.lpVtbl = &g_cost_vtbl;
    m->cpc.lpVtbl = &g_cpc_vtbl;
    for (int k = 0; k < EV_KINDS; k++) {
        m->points[k].iface.lpVtbl = &g_cp_vtbl;
        m->points[k].m = m;
        m->points[k].kind = k;
    }
    m->refs = 1;
    InterlockedIncrement(&g_objects);
    *out = &m->iface;
    return S_OK;
}

/* ---------------------------------------------------------------------------
 * The class factory
 * ------------------------------------------------------------------------- */
static HRESULT STDMETHODCALLTYPE cf_qi(IClassFactory *cf, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory) ? cf : NULL;
    return *ppv ? S_OK : E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cf_addref(IClassFactory *cf)  { (void)cf; return 2; }
static ULONG STDMETHODCALLTYPE cf_release(IClassFactory *cf) { (void)cf; return 1; }
static HRESULT STDMETHODCALLTYPE cf_create(IClassFactory *cf, IUnknown *outer, REFIID riid, void **ppv)
{
    (void)cf;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    INetworkListManager *m;
    HRESULT hr = manager_new(&m);
    if (FAILED(hr)) return hr;
    hr = m->lpVtbl->QueryInterface(m, riid, ppv);
    m->lpVtbl->Release(m);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cf_lock(IClassFactory *cf, BOOL lock)
{
    (void)cf;
    if (lock) InterlockedIncrement(&g_locks); else InterlockedDecrement(&g_locks);
    return S_OK;
}
static const IClassFactoryVtbl g_cf_vtbl = { cf_qi, cf_addref, cf_release, cf_create, cf_lock };
static IClassFactory g_factory = { &g_cf_vtbl };

NLMAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!IsEqualCLSID(clsid, &CLSID_NetworkListManager)) return CLASS_E_CLASSNOTAVAILABLE;
    return cf_qi(&g_factory, riid, ppv);
}

NLMAPI HRESULT WINAPI DllCanUnloadNow(void) { return g_objects || g_locks ? S_FALSE : S_OK; }
