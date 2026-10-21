/* firewalltest.exe — the Windows Filtering Platform and the Windows
 * Firewall, as programs and installers use them (run as administrator):
 *
 *   - fwpuclnt's management API over 127.0.0.1: a dynamic session, a
 *     sublayer, block and permit filters at ALE_AUTH_CONNECT (the heavier
 *     filter decides; connect() fails with WSAEACCES), filters by id and
 *     key, enumeration, an aborted transaction, an application-id
 *     condition (FwpmGetAppIdFromFileName0), UDP sends, and the session's
 *     filters going when it closes;
 *   - hnetcfg's INetFwPolicy2 on QEMU's network: the network starts out
 *     Public, where an inbound connection the test makes from the host
 *     ("firewalltest: knock N": a connection to TCP port 8190+N and a
 *     datagram to UDP port 8290+N, which tools/selftest.py forwards from
 *     host ports 18190+N and 18290+N) is dropped until an INetFwRule
 *     allows the port; a live rule switched off, a rule for another
 *     profile, the network made Private (NetworkList's Category in the
 *     registry, as Settings and Set-NetConnectionProfile do), the Private
 *     profile switched off; an outbound block rule refusing a connection to
 *     the test's web server (10.0.2.2:18080);
 *   - what persists: the rule in FirewallPolicy\FirewallRules in Windows'
 *     "v2.31|Action=Allow|..." form and the profiles' EnableFirewall.
 * Everything it changes is put back. */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <fwpmu.h>
#include <netfw.h>

#define LOOP_PORT   47811
#define LOOP_PORT2  47812
#define UDP_PORT    47813
#define KNOCK_TCP   8190                /* + N (1-8): each knock its own ports, so */
#define KNOCK_UDP   8290                /* QEMU's retries of an earlier one go nowhere */
#define RULES_KEY   L"SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\FirewallRules"
#define PUBLIC_KEY  L"SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\PublicProfile"
#define NETLIST_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkList\\Profiles"

static int passed, failed;

static void check(const char *what, int ok)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) passed++; else failed++;
}

static void checkf(int ok, const char *fmt, DWORD v)
{
    char m[160];
    snprintf(m, sizeof(m), fmt, (unsigned long)v);
    check(m, ok);
}

/* ---- sockets ---- */
static SOCKET listener(int type, unsigned short port, int any)
{
    SOCKET s = socket(AF_INET, type, type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP);
    if (s == INVALID_SOCKET) return s;
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = any ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) || (type == SOCK_STREAM && listen(s, 4))) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

/* connect() to addr:port: 0 or the WSA error */
static int try_connect(unsigned long addr_host, unsigned short port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return WSAGetLastError();
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(addr_host);
    int r = connect(s, (struct sockaddr *)&a, sizeof(a)) ? WSAGetLastError() : 0;
    closesocket(s);
    return r;
}

static int try_sendto(unsigned short port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int r = sendto(s, "x", 1, 0, (struct sockaddr *)&a, sizeof(a)) == 1 ? 0 : WSAGetLastError();
    closesocket(s);
    return r;
}

/* Waits up to @ms for a connection on @l carrying "knock N\n" (an older
 * knock QEMU still delivers is skipped): 1 if it came */
static int knocked_tcp(SOCKET l, int n, int ms)
{
    char want[16];
    snprintf(want, sizeof(want), "knock %d", n);
    DWORD end = GetTickCount() + (DWORD)ms;
    while ((int)(end - GetTickCount()) > 0) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(l, &rd);
        struct timeval tv = { 0, 200000 };
        if (select(0, &rd, NULL, NULL, &tv) <= 0) continue;
        SOCKET c = accept(l, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        char buf[64];
        int got = 0;
        DWORD rend = GetTickCount() + 3000;
        while (got < (int)sizeof(buf) - 1 && (int)(rend - GetTickCount()) > 0 && !memchr(buf, '\n', got)) {
            FD_ZERO(&rd);
            FD_SET(c, &rd);
            tv.tv_sec = 0; tv.tv_usec = 200000;
            if (select(0, &rd, NULL, NULL, &tv) <= 0) continue;
            int k = recv(c, buf + got, (int)sizeof(buf) - 1 - got, 0);
            if (k <= 0) break;
            got += k;
        }
        buf[got] = 0;
        closesocket(c);
        if (strstr(buf, want)) return 1;
    }
    return 0;
}

static int knocked_udp(SOCKET u, int n, int ms)
{
    char want[16];
    snprintf(want, sizeof(want), "knock %d", n);
    DWORD end = GetTickCount() + (DWORD)ms;
    while ((int)(end - GetTickCount()) > 0) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(u, &rd);
        struct timeval tv = { 0, 200000 };
        if (select(0, &rd, NULL, NULL, &tv) <= 0) continue;
        char buf[64];
        int k = recv(u, buf, (int)sizeof(buf) - 1, 0);
        if (k <= 0) continue;
        buf[k] = 0;
        if (strstr(buf, want)) return 1;
    }
    return 0;
}

static int g_knock;
/* Asks the test for a knock and says whether it got through */
static int knock(int *udp_ok)
{
    int n = ++g_knock;
    SOCKET tl = listener(SOCK_STREAM, (unsigned short)(KNOCK_TCP + n), 1);
    SOCKET ul = listener(SOCK_DGRAM, (unsigned short)(KNOCK_UDP + n), 1);
    if (tl == INVALID_SOCKET || ul == INVALID_SOCKET) printf("firewalltest: cannot listen for knock %d\n", n);
    printf("firewalltest: knock %d\n", n);
    fflush(stdout);
    int t = tl != INVALID_SOCKET && knocked_tcp(tl, n, 7000);
    *udp_ok = ul != INVALID_SOCKET && knocked_udp(ul, n, t ? 4000 : 1000);
    closesocket(tl);
    closesocket(ul);
    return t;
}

/* ---- the filtering platform ---- */
static const GUID SUBLAYER_KEY = { 0x5e3c1a40, 0x7d4f, 0x4c2b, { 0x9a, 0x11, 0x46, 0x0e, 0x8b, 0x21, 0x5f, 0x01 } };
static const GUID FILTER_KEY   = { 0x5e3c1a40, 0x7d4f, 0x4c2b, { 0x9a, 0x11, 0x46, 0x0e, 0x8b, 0x21, 0x5f, 0x02 } };
static const GUID TXN_KEY      = { 0x5e3c1a40, 0x7d4f, 0x4c2b, { 0x9a, 0x11, 0x46, 0x0e, 0x8b, 0x21, 0x5f, 0x03 } };

static DWORD add_filter(HANDLE e, const GUID *key, const GUID *layer, FWP_ACTION_TYPE action, UINT8 weight,
                        FWPM_FILTER_CONDITION0 *conds, UINT32 n, UINT64 *id)
{
    FWPM_FILTER0 f;
    memset(&f, 0, sizeof(f));
    if (key) f.filterKey = *key;
    f.displayData.name = L"firewalltest";
    f.layerKey = *layer;
    f.subLayerKey = SUBLAYER_KEY;
    f.weight.type = FWP_UINT8;
    f.weight.uint8 = weight;
    f.numFilterConditions = n;
    f.filterCondition = conds;
    f.action.type = action;
    return FwpmFilterAdd0(e, &f, NULL, id);
}

static void port_cond(FWPM_FILTER_CONDITION0 *c, const GUID *field, UINT16 port)
{
    memset(c, 0, sizeof(*c));
    c->fieldKey = *field;
    c->matchType = FWP_MATCH_EQUAL;
    c->conditionValue.type = FWP_UINT16;
    c->conditionValue.uint16 = port;
}

static void wfp_tests(void)
{
    FWPM_SESSION0 session;
    memset(&session, 0, sizeof(session));
    session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    HANDLE e = NULL;
    DWORD r = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &e);
    checkf(r == 0 && e, "FwpmEngineOpen0 (a dynamic session): %lu", r);
    if (r) return;

    FWPM_SUBLAYER0 sl;
    memset(&sl, 0, sizeof(sl));
    sl.subLayerKey = SUBLAYER_KEY;
    sl.displayData.name = L"firewalltest";
    sl.weight = 0x100;
    r = FwpmSubLayerAdd0(e, &sl, NULL);
    checkf(r == 0, "FwpmSubLayerAdd0: %lu", r);
    r = FwpmSubLayerAdd0(e, &sl, NULL);
    checkf(r == FWP_E_ALREADY_EXISTS, "the same sublayer again: FWP_E_ALREADY_EXISTS (0x%08lx)", r);
    FWPM_SUBLAYER0 *got = NULL;
    r = FwpmSubLayerGetByKey0(e, &SUBLAYER_KEY, &got);
    check("FwpmSubLayerGetByKey0 reads it back (weight 0x100)", r == 0 && got && got->weight == 0x100);
    if (got) FwpmFreeMemory0((void **)&got);

    SOCKET l = listener(SOCK_STREAM, LOOP_PORT, 0), l2 = listener(SOCK_STREAM, LOOP_PORT2, 0);
    check("listening on 127.0.0.1", l != INVALID_SOCKET && l2 != INVALID_SOCKET);
    check("connect() before any filter", try_connect(INADDR_LOOPBACK, LOOP_PORT) == 0);

    FWPM_FILTER_CONDITION0 c[2];
    port_cond(&c[0], &FWPM_CONDITION_IP_REMOTE_PORT, LOOP_PORT);
    UINT64 block_id = 0, permit_id = 0;
    r = add_filter(e, &FILTER_KEY, &FWPM_LAYER_ALE_AUTH_CONNECT_V4, FWP_ACTION_BLOCK, 4, c, 1, &block_id);
    checkf(r == 0 && block_id, "FwpmFilterAdd0: block connections to the port (ALE_AUTH_CONNECT_V4): %lu", r);
    int err = try_connect(INADDR_LOOPBACK, LOOP_PORT);
    checkf(err == WSAEACCES, "connect() refused by the filter: WSAEACCES (%lu)", (DWORD)err);
    check("another port is not blocked", try_connect(INADDR_LOOPBACK, LOOP_PORT2) == 0);

    r = add_filter(e, NULL, &FWPM_LAYER_ALE_AUTH_CONNECT_V4, FWP_ACTION_PERMIT, 9, c, 1, &permit_id);
    checkf(r == 0 && permit_id, "a heavier permit filter for the same port: %lu", r);
    check("connect() allowed again (the heavier filter decides)", try_connect(INADDR_LOOPBACK, LOOP_PORT) == 0);
    r = FwpmFilterDeleteById0(e, permit_id);
    checkf(r == 0, "FwpmFilterDeleteById0: %lu", r);
    check("blocked again once it is gone", try_connect(INADDR_LOOPBACK, LOOP_PORT) == WSAEACCES);
    FWPM_FILTER0 *f = NULL;
    r = FwpmFilterGetById0(e, permit_id, &f);
    checkf(r == FWP_E_FILTER_NOT_FOUND, "FwpmFilterGetById0 of the deleted filter: FWP_E_FILTER_NOT_FOUND (0x%08lx)", r);
    r = FwpmFilterGetByKey0(e, &FILTER_KEY, &f);
    check("FwpmFilterGetByKey0: the block filter, its layer, action and condition",
          r == 0 && f && f->filterId == block_id && IsEqualGUID(&f->layerKey, &FWPM_LAYER_ALE_AUTH_CONNECT_V4) &&
          f->action.type == FWP_ACTION_BLOCK && f->numFilterConditions == 1 &&
          f->filterCondition[0].conditionValue.uint16 == LOOP_PORT);
    if (f) FwpmFreeMemory0((void **)&f);

    /* enumeration */
    HANDLE eh = NULL;
    FWPM_FILTER_ENUM_TEMPLATE0 t;
    memset(&t, 0, sizeof(t));
    t.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    t.actionMask = 0xFFFFFFFF;
    r = FwpmFilterCreateEnumHandle0(e, &t, &eh);
    int found = 0;
    if (!r) {
        FWPM_FILTER0 **list = NULL;
        UINT32 n = 0;
        while (!FwpmFilterEnum0(e, eh, 8, &list, &n) && n) {
            for (UINT32 i = 0; i < n; i++) if (list[i]->filterId == block_id) found = 1;
            FwpmFreeMemory0((void **)&list);
        }
        FwpmFilterDestroyEnumHandle0(e, eh);
    }
    check("FwpmFilterEnum0 lists the filter at its layer", found);

    /* an aborted transaction leaves nothing behind */
    r = FwpmTransactionBegin0(e, 0);
    DWORD r2 = add_filter(e, &TXN_KEY, &FWPM_LAYER_ALE_AUTH_CONNECT_V4, FWP_ACTION_BLOCK, 2, c, 1, NULL);
    DWORD r3 = FwpmTransactionAbort0(e);
    f = NULL;
    DWORD r4 = FwpmFilterGetByKey0(e, &TXN_KEY, &f);
    if (f) FwpmFreeMemory0((void **)&f);
    check("a filter added in an aborted transaction is not there", !r && !r2 && !r3 && r4 == FWP_E_FILTER_NOT_FOUND);

    /* an application's filter: this program, by its application id */
    WCHAR self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    FWP_BYTE_BLOB *app = NULL;
    r = FwpmGetAppIdFromFileName0(self, &app);
    check("FwpmGetAppIdFromFileName0 of this program", r == 0 && app && app->size > 8);
    if (app) {
        port_cond(&c[0], &FWPM_CONDITION_IP_REMOTE_PORT, LOOP_PORT2);
        memset(&c[1], 0, sizeof(c[1]));
        c[1].fieldKey = FWPM_CONDITION_ALE_APP_ID;
        c[1].matchType = FWP_MATCH_EQUAL;
        c[1].conditionValue.type = FWP_BYTE_BLOB_TYPE;
        c[1].conditionValue.byteBlob = app;
        UINT64 app_id = 0;
        r = add_filter(e, NULL, &FWPM_LAYER_ALE_AUTH_CONNECT_V4, FWP_ACTION_BLOCK, 4, c, 2, &app_id);
        check("a block filter for this program's connections to a port", r == 0);
        check("... refuses this program", try_connect(INADDR_LOOPBACK, LOOP_PORT2) == WSAEACCES);
        FwpmFilterDeleteById0(e, app_id);
        /* another program's id: not this one */
        FWP_BYTE_BLOB other = { 0 };
        UINT8 name[] = { '\\', 0, 'x', 0, '.', 0, 'e', 0, 'x', 0, 'e', 0, 0, 0 };
        other.size = sizeof(name);
        other.data = name;
        c[1].conditionValue.byteBlob = &other;
        r = add_filter(e, NULL, &FWPM_LAYER_ALE_AUTH_CONNECT_V4, FWP_ACTION_BLOCK, 4, c, 2, &app_id);
        check("... and one for another program leaves it alone", r == 0 && try_connect(INADDR_LOOPBACK, LOOP_PORT2) == 0);
        FwpmFilterDeleteById0(e, app_id);
        FwpmFreeMemory0((void **)&app);
    }

    /* UDP: a send to a blocked port */
    port_cond(&c[0], &FWPM_CONDITION_IP_REMOTE_PORT, UDP_PORT);
    memset(&c[1], 0, sizeof(c[1]));
    c[1].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
    c[1].matchType = FWP_MATCH_EQUAL;
    c[1].conditionValue.type = FWP_UINT8;
    c[1].conditionValue.uint8 = IPPROTO_UDP;
    UINT64 udp_id = 0;
    check("sendto() before the UDP filter", try_sendto(UDP_PORT) == 0);
    r = add_filter(e, NULL, &FWPM_LAYER_ALE_AUTH_CONNECT_V4, FWP_ACTION_BLOCK, 4, c, 2, &udp_id);
    err = try_sendto(UDP_PORT);
    check("a UDP block filter: sendto() fails with WSAEACCES", r == 0 && err == WSAEACCES);

    /* the dynamic session's filters go with it */
    r = FwpmEngineClose0(e);
    checkf(r == 0, "FwpmEngineClose0: %lu", r);
    check("its filters went with it: connect() and sendto() work again",
          try_connect(INADDR_LOOPBACK, LOOP_PORT) == 0 && try_sendto(UDP_PORT) == 0);
    e = NULL;
    FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, NULL, &e);
    got = NULL;
    r = FwpmSubLayerGetByKey0(e, &SUBLAYER_KEY, &got);
    check("... and so did its sublayer", r == FWP_E_SUBLAYER_NOT_FOUND || r == FWP_E_NOT_FOUND);
    if (got) FwpmFreeMemory0((void **)&got);
    FwpmEngineClose0(e);
    closesocket(l);
    closesocket(l2);
}

/* ---- the firewall ---- */
/* Every network's, as Set-NetConnectionProfile would (in the 64-bit view:
 * NetworkList is not shared with WOW6432Node) */
static int category_set(DWORD cat)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NETLIST_KEY, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, &k)) return 0;
    int n = 0;
    WCHAR name[64];
    for (DWORD i = 0;; i++) {
        DWORD len = 64;
        if (RegEnumKeyExW(k, i, name, &len, NULL, NULL, NULL, NULL)) break;
        HKEY p;
        if (RegOpenKeyExW(k, name, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, &p)) continue;
        if (!RegSetValueExW(p, L"Category", 0, REG_DWORD, (const BYTE *)&cat, sizeof(cat))) n++;
        RegCloseKey(p);
    }
    RegCloseKey(k);
    return n;
}

static LONG wait_profile(INetFwPolicy2 *pol, LONG want)
{
    LONG cur = 0;
    for (int i = 0; i < 40; i++) {
        pol->lpVtbl->get_CurrentProfileTypes(pol, &cur);
        if (cur == want) break;
        Sleep(250);
    }
    return cur;
}

/* Does a FirewallRules value hold @a and @b? */
static int rule_stored(const WCHAR *a, const WCHAR *b)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, RULES_KEY, 0, KEY_READ, &k)) return 0;
    int found = 0;
    static WCHAR data[2048];
    WCHAR name[64];
    for (DWORD i = 0; !found; i++) {
        DWORD nl = 64, dl = sizeof(data) - 2, type;
        if (RegEnumValueW(k, i, name, &nl, NULL, &type, (BYTE *)data, &dl)) break;
        data[dl / 2] = 0;
        found = type == REG_SZ && wcsstr(data, a) && wcsstr(data, b);
    }
    RegCloseKey(k);
    return found;
}

static INetFwRule *new_rule(const WCHAR *name, LONG dir, LONG proto, const WCHAR *lports, const WCHAR *rports,
                            NET_FW_ACTION action, LONG profiles)
{
    INetFwRule *r = NULL;
    if (CoCreateInstance(&CLSID_NetFwRule, NULL, CLSCTX_INPROC_SERVER, &IID_INetFwRule, (void **)&r) || !r) return NULL;
    BSTR b = SysAllocString(name);
    r->lpVtbl->put_Name(r, b);
    SysFreeString(b);
    r->lpVtbl->put_Protocol(r, proto);
    if (lports) { b = SysAllocString(lports); r->lpVtbl->put_LocalPorts(r, b); SysFreeString(b); }
    if (rports) { b = SysAllocString(rports); r->lpVtbl->put_RemotePorts(r, b); SysFreeString(b); }
    r->lpVtbl->put_Direction(r, (NET_FW_RULE_DIRECTION)dir);
    r->lpVtbl->put_Action(r, action);
    r->lpVtbl->put_Profiles(r, profiles);
    r->lpVtbl->put_Enabled(r, VARIANT_TRUE);
    return r;
}

static void firewall_tests(void)
{
    INetFwPolicy2 *pol = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_NetFwPolicy2, NULL, CLSCTX_INPROC_SERVER, &IID_INetFwPolicy2, (void **)&pol);
    checkf(hr == S_OK && pol, "CoCreateInstance(HNetCfg.FwPolicy2): 0x%08lx", (DWORD)hr);
    if (!pol) return;
    VARIANT_BOOL on = VARIANT_FALSE;
    pol->lpVtbl->get_FirewallEnabled(pol, NET_FW_PROFILE2_PUBLIC, &on);
    check("the firewall is on for the Public profile", on == VARIANT_TRUE);
    NET_FW_ACTION in = NET_FW_ACTION_ALLOW, out = NET_FW_ACTION_BLOCK;
    pol->lpVtbl->get_DefaultInboundAction(pol, NET_FW_PROFILE2_PUBLIC, &in);
    pol->lpVtbl->get_DefaultOutboundAction(pol, NET_FW_PROFILE2_PUBLIC, &out);
    check("Public: inbound blocked, outbound allowed by default", in == NET_FW_ACTION_BLOCK && out == NET_FW_ACTION_ALLOW);
    hr = pol->lpVtbl->get_FirewallEnabled(pol, NET_FW_PROFILE2_ALL, &on);
    check("a get for several profiles at once: E_INVALIDARG", hr == E_INVALIDARG);
    LONG cur = wait_profile(pol, NET_FW_PROFILE2_PUBLIC);
    checkf(cur == NET_FW_PROFILE2_PUBLIC, "QEMU's network is a new network: Public (%lu)", (DWORD)cur);

    INetFwRules *rules = NULL;
    pol->lpVtbl->get_Rules(pol, &rules);
    LONG before = -1;
    if (rules) rules->lpVtbl->get_Count(rules, &before);
    check("INetFwPolicy2::get_Rules and its Count", rules && before >= 0);
    if (!rules) { pol->lpVtbl->Release(pol); return; }

    int udp_ok;
    check("knock 1: Public, no rule: the connection is dropped", !knock(&udp_ok));
    check("knock 1: ... and so is the datagram", !udp_ok);

    INetFwRule *tcp_rule = new_rule(L"firewalltest inbound", NET_FW_RULE_DIR_IN, NET_FW_IP_PROTOCOL_TCP, L"8191-8198", NULL,
                                    NET_FW_ACTION_ALLOW, NET_FW_PROFILE2_ALL);
    INetFwRule *udp_rule = new_rule(L"firewalltest inbound UDP", NET_FW_RULE_DIR_IN, NET_FW_IP_PROTOCOL_UDP, L"8291-8298", NULL,
                                    NET_FW_ACTION_ALLOW, NET_FW_PROFILE2_ALL);
    hr = tcp_rule && udp_rule ? rules->lpVtbl->Add(rules, tcp_rule) : E_FAIL;
    HRESULT hr2 = udp_rule ? rules->lpVtbl->Add(rules, udp_rule) : E_FAIL;
    check("INetFwRules::Add: allow TCP 8191-8198 and UDP 8291-8298 in", hr == S_OK && hr2 == S_OK);
    LONG after = 0;
    rules->lpVtbl->get_Count(rules, &after);
    check("... Count is two more", after == before + 2);
    check("knock 2: allowed by the rules: the connection comes in", knock(&udp_ok));
    check("knock 2: ... and the datagram", udp_ok);
    check("the rule is stored in FirewallRules as Windows writes it",
          rule_stored(L"v2.31|Action=Allow|Active=TRUE|Dir=In|Protocol=6|", L"LPort=8191-8198|Name=firewalltest inbound|"));

    INetFwRule *item = NULL;
    BSTR nm = SysAllocString(L"firewalltest inbound");
    hr = rules->lpVtbl->Item(rules, nm, &item);
    BSTR ports = NULL;
    LONG proto = 0;
    if (item) { item->lpVtbl->get_LocalPorts(item, &ports); item->lpVtbl->get_Protocol(item, &proto); }
    check("INetFwRules::Item finds it by name (TCP, ports 8191-8198)", hr == S_OK && item && proto == 6 && ports && !wcscmp(ports, L"8191-8198"));
    SysFreeString(ports);
    if (item) {
        item->lpVtbl->put_Enabled(item, VARIANT_FALSE);
        check("knock 3: the rule switched off (a live rule): dropped again", !knock(&udp_ok));
        check("... and stored switched off", rule_stored(L"Active=FALSE|", L"Name=firewalltest inbound|"));
        item->lpVtbl->put_Enabled(item, VARIANT_TRUE);
        item->lpVtbl->put_Profiles(item, NET_FW_PROFILE2_PRIVATE);
        check("knock 4: a rule for Private networks only, on a Public one: dropped", !knock(&udp_ok));
    }

    /* the network made Private */
    int nets = category_set(1);
    cur = wait_profile(pol, NET_FW_PROFILE2_PRIVATE);
    check("the network made Private (NetworkList Category = 1): CurrentProfileTypes follows",
          nets > 0 && cur == NET_FW_PROFILE2_PRIVATE);
    check("knock 5: on Private, the Private rule lets it in", knock(&udp_ok));
    if (item) item->lpVtbl->put_Profiles(item, NET_FW_PROFILE2_PUBLIC);
    check("knock 6: the rule moved to Public: dropped on Private", !knock(&udp_ok));
    hr = pol->lpVtbl->put_FirewallEnabled(pol, NET_FW_PROFILE2_PRIVATE, VARIANT_FALSE);
    check("knock 7: the Private profile's firewall off: it comes in", hr == S_OK && knock(&udp_ok));
    pol->lpVtbl->put_FirewallEnabled(pol, NET_FW_PROFILE2_PRIVATE, VARIANT_TRUE);
    category_set(0);
    cur = wait_profile(pol, NET_FW_PROFILE2_PUBLIC);
    check("back to Public", cur == NET_FW_PROFILE2_PUBLIC);

    /* the profile's state persists where Windows keeps it */
    HKEY k;
    DWORD v = 99, len = sizeof(v);
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, PUBLIC_KEY, 0, KEY_READ, &k)) {
        RegQueryValueExW(k, L"EnableFirewall", NULL, NULL, (BYTE *)&v, &len);
        RegCloseKey(k);
    }
    check("PublicProfile\\EnableFirewall is 1 in the registry", v == 1);
    pol->lpVtbl->put_BlockAllInboundTraffic(pol, NET_FW_PROFILE2_PUBLIC, VARIANT_TRUE);
    if (item) item->lpVtbl->put_Profiles(item, NET_FW_PROFILE2_ALL);
    check("knock 8: Public blocking all inbound: dropped despite the rule", !knock(&udp_ok));
    pol->lpVtbl->put_BlockAllInboundTraffic(pol, NET_FW_PROFILE2_PUBLIC, VARIANT_FALSE);

    /* outbound */
    INetFwRule *ob = new_rule(L"firewalltest outbound", NET_FW_RULE_DIR_OUT, NET_FW_IP_PROTOCOL_TCP, NULL, L"18080",
                              NET_FW_ACTION_BLOCK, NET_FW_PROFILE2_ALL);
    check("the test's web server answers before an outbound rule", try_connect(0x0A000202, 18080) == 0);
    hr = ob ? rules->lpVtbl->Add(rules, ob) : E_FAIL;
    int err = try_connect(0x0A000202, 18080);
    checkf(hr == S_OK && err == WSAEACCES, "an outbound block rule: connect() fails with WSAEACCES (%lu)", (DWORD)err);
    check("... loopback is not filtered by the firewall", try_connect(INADDR_LOOPBACK, LOOP_PORT) != WSAEACCES);
    BSTR obn = SysAllocString(L"firewalltest outbound");
    hr = rules->lpVtbl->Remove(rules, obn);
    check("INetFwRules::Remove: connect() works again", hr == S_OK && try_connect(0x0A000202, 18080) == 0);
    SysFreeString(obn);

    /* tidy up */
    rules->lpVtbl->Remove(rules, nm);
    BSTR un = SysAllocString(L"firewalltest inbound UDP");
    rules->lpVtbl->Remove(rules, un);
    SysFreeString(un);
    LONG end = -1;
    rules->lpVtbl->get_Count(rules, &end);
    INetFwRule *gone = NULL;
    hr = rules->lpVtbl->Item(rules, nm, &gone);
    check("the rules removed: Count back, Item not found, nothing left in FirewallRules",
          end == before && hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && !rule_stored(L"Name=firewalltest", L"v2.31"));
    SysFreeString(nm);
    if (item) item->lpVtbl->Release(item);
    if (tcp_rule) tcp_rule->lpVtbl->Release(tcp_rule);
    if (udp_rule) udp_rule->lpVtbl->Release(udp_rule);
    if (ob) ob->lpVtbl->Release(ob);
    rules->lpVtbl->Release(rules);
    pol->lpVtbl->Release(pol);
}

int main(void)
{
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    wfp_tests();
    firewall_tests();
    CoUninitialize();
    WSACleanup();
    printf("firewalltest: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
