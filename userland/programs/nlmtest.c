/* nlmtest.exe — the Network List Manager (netprofm.dll's
 * NetworkListManager), as programs ask it whether they are online and on
 * which network (run as administrator, on QEMU's network):
 *
 *   - connectivity: IsConnected, IsConnectedToInternet and GetConnectivity
 *     (IPv4 Internet through QEMU's gateway);
 *   - the networks: the one the PC is on (GetNetworks CONNECTED) with the
 *     name, category and id the firewall keeps under NetworkList\Profiles,
 *     every known network (ALL, DISCONNECTED), GetNetwork by id, an
 *     unknown id refused;
 *   - the connection: the adapter, whose GetAdapterId is the GUID
 *     iphlpapi's GetAdaptersInfo names it by, and its network;
 *   - the cost: INetworkCostManager and INetworkConnectionCost say
 *     unrestricted, the data plan names the adapter;
 *   - changes: SetCategory(Private) on the network (the registry and the
 *     firewall's profile follow), SetName, each reported to an advised
 *     INetworkEvents sink as NetworkPropertyChanged; Domain refused;
 *     Unadvise.
 * Everything it changes is put back. */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <netlistmgr.h>

#define NETLIST_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkList\\Profiles"

static int passed, failed;

static void check(const char *what, int ok)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) passed++; else failed++;
}

static void checkf(int ok, const char *fmt, DWORD v)
{
    char m[200];
    snprintf(m, sizeof(m), fmt, (unsigned long)v);
    check(m, ok);
}

static void guid_text(const GUID *g, char *out)
{
    WCHAR w[40];
    StringFromGUID2(g, w, 40);
    int i = 0;
    for (; w[i]; i++) out[i] = (char)w[i];
    out[i] = 0;
}

/* The 64-bit view, which a 32-bit program's netprofm uses too */
static BOOL reg_get(const WCHAR *path, const WCHAR *name, void *out, DWORD n)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return FALSE;
    LSTATUS e = RegQueryValueExW(k, name, NULL, NULL, (BYTE *)out, &n);
    RegCloseKey(k);
    return e == ERROR_SUCCESS;
}

/* NetworkList\Profiles\{id}'s value (a DWORD, or text) */
static BOOL profile_dword(const GUID *id, const WCHAR *name, DWORD *out)
{
    WCHAR path[200], g[40];
    StringFromGUID2(id, g, 40);
    swprintf(path, 200, L"%ls\\%ls", NETLIST_KEY, g);
    return reg_get(path, name, out, sizeof(*out));
}
static BOOL profile_text(const GUID *id, const WCHAR *name, WCHAR *out, DWORD cap)
{
    WCHAR path[200], g[40];
    StringFromGUID2(id, g, 40);
    swprintf(path, 200, L"%ls\\%ls", NETLIST_KEY, g);
    memset(out, 0, cap * sizeof(WCHAR));
    return reg_get(path, name, out, (cap - 1) * sizeof(WCHAR));
}

/* ---- an INetworkEvents sink: remembers which properties changed ---- */
typedef struct { INetworkEvents iface; LONG refs; volatile LONG flags; GUID id; HANDLE ev; } Sink;
static HRESULT STDMETHODCALLTYPE sk_qi(INetworkEvents *t, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_INetworkEvents)) {
        *ppv = t;
        InterlockedIncrement(&((Sink *)t)->refs);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE sk_addref(INetworkEvents *t) { return (ULONG)InterlockedIncrement(&((Sink *)t)->refs); }
static ULONG STDMETHODCALLTYPE sk_release(INetworkEvents *t) { return (ULONG)InterlockedDecrement(&((Sink *)t)->refs); }
static HRESULT STDMETHODCALLTYPE sk_added(INetworkEvents *t, GUID id) { (void)t; (void)id; return S_OK; }
static HRESULT STDMETHODCALLTYPE sk_deleted(INetworkEvents *t, GUID id) { (void)t; (void)id; return S_OK; }
static HRESULT STDMETHODCALLTYPE sk_conn(INetworkEvents *t, GUID id, NLM_CONNECTIVITY c) { (void)t; (void)id; (void)c; return S_OK; }
static HRESULT STDMETHODCALLTYPE sk_prop(INetworkEvents *t, GUID id, NLM_NETWORK_PROPERTY_CHANGE f)
{
    Sink *s = (Sink *)t;
    if (IsEqualGUID(&id, &s->id)) {
        InterlockedOr(&s->flags, (LONG)f);
        SetEvent(s->ev);
    }
    return S_OK;
}
static const INetworkEventsVtbl g_sink_vtbl = { sk_qi, sk_addref, sk_release, sk_added, sk_deleted, sk_conn, sk_prop };

/* Wait (up to 6 s) for the sink to hear of @flag */
static BOOL heard(Sink *s, LONG flag)
{
    for (int i = 0; i < 60; i++) {
        if (InterlockedAnd(&s->flags, ~flag) & flag) return TRUE;
        WaitForSingleObject(s->ev, 100);
    }
    return FALSE;
}

/* The first network of an enumeration, and how many it holds */
static int count_networks(INetworkListManager *m, NLM_ENUM_NETWORK flags, INetwork **first, int *connected_ones)
{
    IEnumNetworks *e = NULL;
    if (m->lpVtbl->GetNetworks(m, flags, &e) != S_OK || !e) return -1;
    int n = 0;
    if (connected_ones) *connected_ones = 0;
    INetwork *net;
    ULONG got;
    while (e->lpVtbl->Next(e, 1, &net, &got) == S_OK && got == 1) {
        VARIANT_BOOL c = VARIANT_FALSE;
        net->lpVtbl->get_IsConnected(net, &c);
        if (connected_ones && c) (*connected_ones)++;
        if (first && !*first) *first = net;
        else net->lpVtbl->Release(net);
        n++;
    }
    e->lpVtbl->Release(e);
    return n;
}

/* iphlpapi's name for the adapter ("{4E4F5641-...}") */
static BOOL adapter_name(char *out, int cap)
{
    typedef DWORD (WINAPI *GetAdaptersInfo_t)(PVOID, PULONG);
    HMODULE ip = LoadLibraryA("iphlpapi.dll");
    GetAdaptersInfo_t gai = ip ? (GetAdaptersInfo_t)GetProcAddress(ip, "GetAdaptersInfo") : NULL;
    static BYTE buf[8192];
    ULONG n = sizeof(buf);
    if (!gai || gai(buf, &n)) return FALSE;
    /* IP_ADAPTER_INFO: Next, ComboIndex, AdapterName[260] */
    const char *name = (const char *)(buf + sizeof(void *) + sizeof(DWORD));
    snprintf(out, (size_t)cap, "%s", name);
    return TRUE;
}

static void nlm_tests(void)
{
    INetworkListManager *m = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_NetworkListManager, NULL, CLSCTX_ALL, &IID_INetworkListManager, (void **)&m);
    checkf(hr == S_OK && m, "CoCreateInstance(NetworkListManager): 0x%08lx", (DWORD)hr);
    if (!m) return;

    /* the network is identified once ARP has found the gateway */
    INetwork *net = NULL;
    int n_conn = -1;
    for (int i = 0; i < 30 && !net; i++) {
        n_conn = count_networks(m, NLM_ENUM_NETWORK_CONNECTED, &net, NULL);
        if (!net) Sleep(500);
    }
    checkf(n_conn == 1 && net, "GetNetworks(CONNECTED): one network (%lu)", (DWORD)n_conn);

    VARIANT_BOOL on = VARIANT_FALSE, inet = VARIANT_FALSE;
    NLM_CONNECTIVITY c = NLM_CONNECTIVITY_DISCONNECTED;
    m->lpVtbl->get_IsConnected(m, &on);
    m->lpVtbl->get_IsConnectedToInternet(m, &inet);
    m->lpVtbl->GetConnectivity(m, &c);
    checkf(on == VARIANT_TRUE && inet == VARIANT_TRUE && (c & NLM_CONNECTIVITY_IPV4_INTERNET),
           "IsConnected, IsConnectedToInternet, GetConnectivity IPV4_INTERNET (0x%lx)", (DWORD)c);
    if (!net) { m->lpVtbl->Release(m); return; }

    GUID id;
    char idt[40];
    net->lpVtbl->GetNetworkId(net, &id);
    guid_text(&id, idt);
    printf("nlmtest: network %s\n", idt);
    BSTR name = NULL;
    WCHAR reg_name[64] = L"";
    DWORD reg_cat = 99;
    hr = net->lpVtbl->GetName(net, &name);
    profile_text(&id, L"ProfileName", reg_name, 64);
    profile_dword(&id, L"Category", &reg_cat);
    check("INetwork::GetName: the ProfileName under NetworkList\\Profiles\\{id}",
          hr == S_OK && name && reg_name[0] && !wcscmp(name, reg_name));
    NLM_NETWORK_CATEGORY cat0 = (NLM_NETWORK_CATEGORY)-1;
    net->lpVtbl->GetCategory(net, &cat0);
    checkf((DWORD)cat0 == reg_cat, "INetwork::GetCategory: the registry's Category (%lu)", (DWORD)cat0);
    NLM_CONNECTIVITY nc = 0;
    NLM_DOMAIN_TYPE dt = (NLM_DOMAIN_TYPE)-1;
    VARIANT_BOOL non = VARIANT_FALSE;
    net->lpVtbl->GetConnectivity(net, &nc);
    net->lpVtbl->GetDomainType(net, &dt);
    net->lpVtbl->get_IsConnected(net, &non);
    check("INetwork: connected, the manager's connectivity, not a domain network",
          non == VARIANT_TRUE && nc == c && dt == NLM_DOMAIN_TYPE_NON_DOMAIN_NETWORK);

    int connected_in_all = 0;
    int n_all = count_networks(m, NLM_ENUM_NETWORK_ALL, NULL, &connected_in_all);
    int n_dis_connected = 0;
    int n_dis = count_networks(m, NLM_ENUM_NETWORK_DISCONNECTED, NULL, &n_dis_connected);
    checkf(n_all >= 1 && connected_in_all == 1 && n_dis == n_all - 1 && n_dis_connected == 0,
           "GetNetworks(ALL) = the connected one + DISCONNECTED ones (%lu)", (DWORD)n_all);

    INetwork *again = NULL;
    hr = m->lpVtbl->GetNetwork(m, id, &again);
    GUID id2 = { 0 };
    if (again) again->lpVtbl->GetNetworkId(again, &id2);
    GUID bogus = { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    INetwork *none = NULL;
    HRESULT hr2 = m->lpVtbl->GetNetwork(m, bogus, &none);
    check("GetNetwork(id) finds it; an unknown id fails", hr == S_OK && IsEqualGUID(&id, &id2) && FAILED(hr2) && !none);
    if (again) again->lpVtbl->Release(again);

    /* the connection: the adapter */
    IEnumNetworkConnections *ec = NULL;
    INetworkConnection *conn = NULL;
    ULONG got = 0;
    if (m->lpVtbl->GetNetworkConnections(m, &ec) == S_OK && ec) {
        ec->lpVtbl->Next(ec, 1, &conn, &got);
        INetworkConnection *more = NULL;
        ULONG got2 = 0;
        hr = ec->lpVtbl->Next(ec, 1, &more, &got2);
        check("GetNetworkConnections: one connection", conn && got == 1 && hr == S_FALSE && got2 == 0);
        ec->lpVtbl->Release(ec);
    } else check("GetNetworkConnections", 0);
    char want[64] = "", have[40] = "";
    if (conn) {
        GUID ad, cid, nid = { 0 };
        conn->lpVtbl->GetAdapterId(conn, &ad);
        conn->lpVtbl->GetConnectionId(conn, &cid);
        guid_text(&ad, have);
        adapter_name(want, sizeof(want));
        printf("nlmtest: adapter %s (iphlpapi %s)\n", have, want);
        check("INetworkConnection::GetAdapterId: iphlpapi's AdapterName", want[0] && !_stricmp(have, want));
        INetwork *cn = NULL;
        conn->lpVtbl->GetNetwork(conn, &cn);
        if (cn) { cn->lpVtbl->GetNetworkId(cn, &nid); cn->lpVtbl->Release(cn); }
        VARIANT_BOOL ci = VARIANT_FALSE;
        conn->lpVtbl->get_IsConnectedToInternet(conn, &ci);
        check("INetworkConnection: on the network, to the Internet", IsEqualGUID(&nid, &id) && ci == VARIANT_TRUE);
        INetworkConnection *byid = NULL;
        hr = m->lpVtbl->GetNetworkConnection(m, cid, &byid);
        check("GetNetworkConnection(its id)", hr == S_OK && byid);
        if (byid) byid->lpVtbl->Release(byid);

        INetworkConnectionCost *cc = NULL;
        DWORD cost = 0;
        if (conn->lpVtbl->QueryInterface(conn, &IID_INetworkConnectionCost, (void **)&cc) == S_OK && cc) {
            cc->lpVtbl->GetCost(cc, &cost);
            cc->lpVtbl->Release(cc);
        }
        checkf(cost == NLM_CONNECTION_COST_UNRESTRICTED, "INetworkConnectionCost::GetCost: unrestricted (0x%lx)", cost);
    }

    /* the cost manager */
    INetworkCostManager *cm = NULL;
    DWORD cost = 0;
    NLM_DATAPLAN_STATUS plan;
    memset(&plan, 0, sizeof(plan));
    if (m->lpVtbl->QueryInterface(m, &IID_INetworkCostManager, (void **)&cm) == S_OK && cm) {
        cm->lpVtbl->GetCost(cm, &cost, NULL);
        cm->lpVtbl->GetDataPlanStatus(cm, &plan, NULL);
        cm->lpVtbl->Release(cm);
    }
    char plan_if[40];
    guid_text(&plan.InterfaceGuid, plan_if);
    check("INetworkCostManager: unrestricted, the data plan unknown, on the adapter",
          cost == NLM_CONNECTION_COST_UNRESTRICTED && plan.DataLimitInMegabytes == NLM_UNKNOWN_DATAPLAN_STATUS &&
          have[0] && !strcmp(plan_if, have));

    /* events */
    IConnectionPointContainer *cpc = NULL;
    IConnectionPoint *cp = NULL, *bad = NULL;
    Sink sink = { { &g_sink_vtbl }, 1, 0, id, CreateEventW(NULL, FALSE, FALSE, NULL) };
    DWORD cookie = 0;
    m->lpVtbl->QueryInterface(m, &IID_IConnectionPointContainer, (void **)&cpc);
    if (cpc) {
        cpc->lpVtbl->FindConnectionPoint(cpc, &IID_INetworkEvents, &cp);
        hr2 = cpc->lpVtbl->FindConnectionPoint(cpc, &IID_IUnknown, &bad);
        cpc->lpVtbl->Release(cpc);
    }
    hr = cp ? cp->lpVtbl->Advise(cp, (IUnknown *)&sink.iface, &cookie) : E_FAIL;
    check("IConnectionPointContainer: INetworkEvents advised; another IID has no point",
          hr == S_OK && cookie && hr2 == CONNECT_E_NOCONNECTION && !bad);

    /* changes */
    NLM_NETWORK_CATEGORY other = cat0 == NLM_NETWORK_CATEGORY_PRIVATE ? NLM_NETWORK_CATEGORY_PUBLIC : NLM_NETWORK_CATEGORY_PRIVATE;
    Sleep(1500);                                /* (the watcher's first look) */
    hr = net->lpVtbl->SetCategory(net, other);
    NLM_NETWORK_CATEGORY cat1 = (NLM_NETWORK_CATEGORY)-1;
    net->lpVtbl->GetCategory(net, &cat1);
    reg_cat = 99;
    profile_dword(&id, L"Category", &reg_cat);
    check("SetCategory: GetCategory and the registry follow", hr == S_OK && cat1 == other && reg_cat == (DWORD)other);
    check("INetworkEvents::NetworkPropertyChanged(CATEGORY_VALUE)", heard(&sink, NLM_NETWORK_PROPERTY_CHANGE_CATEGORY_VALUE));
    hr = net->lpVtbl->SetCategory(net, NLM_NETWORK_CATEGORY_DOMAIN_AUTHENTICATED);
    check("SetCategory(DOMAIN_AUTHENTICATED) refused", hr == E_INVALIDARG);

    BSTR nn = SysAllocString(L"nlmtest network");
    hr = net->lpVtbl->SetName(net, nn);
    BSTR now = NULL;
    net->lpVtbl->GetName(net, &now);
    check("SetName: GetName follows", hr == S_OK && now && !wcscmp(now, L"nlmtest network"));
    check("INetworkEvents::NetworkPropertyChanged(NAME)", heard(&sink, NLM_NETWORK_PROPERTY_CHANGE_NAME));
    SysFreeString(nn);
    SysFreeString(now);

    /* put back */
    HRESULT r1 = net->lpVtbl->SetName(net, name);
    HRESULT r2 = net->lpVtbl->SetCategory(net, cat0);
    reg_cat = 99;
    profile_dword(&id, L"Category", &reg_cat);
    check("name and category put back", r1 == S_OK && r2 == S_OK && reg_cat == (DWORD)cat0);
    heard(&sink, NLM_NETWORK_PROPERTY_CHANGE_CATEGORY_VALUE);

    if (cp) {
        hr = cp->lpVtbl->Unadvise(cp, cookie);
        hr2 = cp->lpVtbl->Unadvise(cp, cookie);
        check("Unadvise; again: CONNECT_E_NOCONNECTION", hr == S_OK && hr2 == CONNECT_E_NOCONNECTION);
        cp->lpVtbl->Release(cp);
    }
    check("the sink let go", sink.refs == 1);
    SysFreeString(name);
    if (conn) conn->lpVtbl->Release(conn);
    net->lpVtbl->Release(net);
    m->lpVtbl->Release(m);
    CloseHandle(sink.ev);
}

int main(void)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    nlm_tests();
    CoUninitialize();
    printf("nlmtest: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
