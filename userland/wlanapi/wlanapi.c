/*
 * wlanapi.dll — the Native Wifi API: Wi-Fi adapters, the networks they
 * hear, connecting to one and the profiles that remember them
 *
 * On Windows this DLL talks to the WLAN AutoConfig service (wlansvc) by
 * RPC.  NovaOS's Wi-Fi is its kernel's WDI upper edge (kernel/nt/wdi.c:
 * the scans, the association, WPA2's 4-way handshake and WPA3's SAE, with
 * the vendor's WDI driver below), which this DLL asks through
 * NtNovaSockCtl (userland/include/novawlan.h, kernel/um/um_wlan.c).
 *
 *   - handles: WlanOpenHandle (client version 1 or 2), WlanCloseHandle;
 *   - adapters: WlanEnumInterfaces, WlanQueryInterface (its state, the
 *     current connection, channel, RSSI, auto-configuration, BSS type);
 *   - networks: WlanScan, WlanGetAvailableNetworkList (one entry per
 *     network, from the last scan; a scan first if there was none),
 *     WlanGetNetworkBssList (one per access point);
 *   - connecting: WlanConnect (a saved profile, a temporary profile's XML
 *     or, for an open network, discovery by SSID), WlanDisconnect;
 *   - profiles: WlanSetProfile, WlanGetProfile, WlanDeleteProfile,
 *     WlanGetProfileList, kept in the registry (all users' under HKLM,
 *     WLAN_PROFILE_USER ones under HKCU: Software\NovaOS\Wlan\Profiles);
 *     a profile's SSID and its sharedKey's keyMaterial (the passphrase,
 *     protected false) are what NovaOS reads from it;
 *   - notifications: WlanRegisterNotification, with the AutoConfig ones
 *     (WLAN_NOTIFICATION_SOURCE_ACM) a program waits for: scan_complete
 *     or scan_fail, connection_start, connection_complete (its reason
 *     code says whether it worked), connection_attempt_fail,
 *     disconnecting, disconnected;
 *   - WlanAllocateMemory, WlanFreeMemory, WlanReasonCodeToString.
 *
 * As on Windows, WlanScan and WlanConnect return at once and the work
 * goes on in a thread of this DLL, which notifies when it ends.  An
 * interface's GUID is made from its MAC address, so it stays the same
 * from boot to boot.
 */
#include <windows.h>
#include <winternl.h>
#include <winreg.h>
#include "../include/novawlan.h"

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#define WLAPI __declspec(dllexport)

#ifndef ERROR_NOT_FOUND
#define ERROR_NOT_FOUND           1168
#endif
#ifndef ERROR_INVALID_HANDLE
#define ERROR_INVALID_HANDLE      6
#endif
#ifndef ERROR_BAD_PROFILE
#define ERROR_BAD_PROFILE         1206
#endif
#ifndef ERROR_ALREADY_EXISTS
#define ERROR_ALREADY_EXISTS      183
#endif
#ifndef ERROR_INVALID_STATE
#define ERROR_INVALID_STATE       5023
#endif
#ifndef ERROR_BUSY
#define ERROR_BUSY                170
#endif
#ifndef ERROR_REMOTE_SESSION_LIMIT_EXCEEDED
#define ERROR_REMOTE_SESSION_LIMIT_EXCEEDED 1220
#endif
#ifndef ERROR_NDIS_DOT11_POWER_STATE_INVALID
#define ERROR_NDIS_DOT11_POWER_STATE_INVALID 0x80342002
#endif

/* ---- Windows' structures (wlanapi.h, windot11.h), laid out as there ---- */

typedef struct { ULONG uSSIDLength; UCHAR ucSSID[32]; } DOT11_SSID_;

typedef struct {
    GUID  InterfaceGuid;
    WCHAR strInterfaceDescription[256];
    DWORD isState;                                      /* WLAN_INTERFACE_STATE */
} IF_INFO_;
typedef struct { DWORD dwNumberOfItems, dwIndex; IF_INFO_ InterfaceInfo[1]; } IF_LIST_;

typedef struct {
    WCHAR       strProfileName[256];
    DOT11_SSID_ dot11Ssid;
    DWORD       dot11BssType;
    ULONG       uNumberOfBssids;
    BOOL        bNetworkConnectable;
    DWORD       wlanNotConnectableReason;
    ULONG       uNumberOfPhyTypes;
    DWORD       dot11PhyTypes[8];
    BOOL        bMorePhyTypes;
    ULONG       wlanSignalQuality;
    BOOL        bSecurityEnabled;
    DWORD       dot11DefaultAuthAlgorithm, dot11DefaultCipherAlgorithm;
    DWORD       dwFlags, dwReserved;
} NETWORK_;
typedef struct { DWORD dwNumberOfItems, dwIndex; NETWORK_ Network[1]; } NETWORK_LIST_;

typedef struct {
    DOT11_SSID_ dot11Ssid;
    ULONG       uPhyId;
    UCHAR       dot11Bssid[6];
    DWORD       dot11BssType;
    DWORD       dot11BssPhyType;
    LONG        lRssi;
    ULONG       uLinkQuality;
    BOOLEAN     bInRegDomain;
    USHORT      usBeaconPeriod;
    ULONGLONG   ullTimestamp, ullHostTimestamp;
    USHORT      usCapabilityInformation;
    ULONG       ulChCenterFrequency;
    struct { ULONG uRateSetLength; USHORT usRateSet[126]; } wlanRateSet;
    ULONG       ulIeOffset, ulIeSize;
} BSS_ENTRY_;
typedef struct { DWORD dwTotalSize, dwNumberOfItems; BSS_ENTRY_ wlanBssEntries[1]; } BSS_LIST_;

typedef struct {
    DWORD        wlanConnectionMode;
    LPCWSTR      strProfile;
    DOT11_SSID_ *pDot11Ssid;
    void        *pDesiredBssidList;
    DWORD        dot11BssType;
    DWORD        dwFlags;
} CONNECT_PARAMS_;

typedef struct {
    DWORD isState, wlanConnectionMode;
    WCHAR strProfileName[256];
    struct {
        DOT11_SSID_ dot11Ssid;
        DWORD dot11BssType;
        UCHAR dot11Bssid[6];
        DWORD dot11PhyType;
        ULONG uDot11PhyIndex;
        ULONG wlanSignalQuality;
        ULONG ulRxRate, ulTxRate;
    } wlanAssociationAttributes;
    struct {
        BOOL  bSecurityEnabled, bOneXEnabled;
        DWORD dot11AuthAlgorithm, dot11CipherAlgorithm;
    } wlanSecurityAttributes;
} CONNECTION_ATTRIBUTES_;

typedef struct {
    DWORD NotificationSource, NotificationCode;
    GUID  InterfaceGuid;
    DWORD dwDataSize;
    PVOID pData;
} NOTIFICATION_DATA_;
typedef void (WINAPI *NOTIFY_FN_)(NOTIFICATION_DATA_ *, PVOID);

typedef struct {                                        /* WLAN_CONNECTION_NOTIFICATION_DATA */
    DWORD       wlanConnectionMode;
    WCHAR       strProfileName[256];
    DOT11_SSID_ dot11Ssid;
    DWORD       dot11BssType;
    BOOL        bSecurityEnabled;
    DWORD       wlanReasonCode;
    DWORD       dwFlags;
    WCHAR       strProfileXml[1];
} CONNECTION_NOTIFY_;

typedef struct { WCHAR strProfileName[256]; DWORD dwFlags; } PROFILE_INFO_;
typedef struct { DWORD dwNumberOfItems, dwIndex; PROFILE_INFO_ ProfileInfo[1]; } PROFILE_LIST_;

_Static_assert(sizeof(IF_INFO_) == 532 && sizeof(NETWORK_) == 628 && sizeof(BSS_ENTRY_) == 360, "wlanapi.h layouts");
_Static_assert(sizeof(CONNECTION_ATTRIBUTES_) == 604, "WLAN_CONNECTION_ATTRIBUTES");

/* WLAN_INTERFACE_STATE */
#define IF_CONNECTED     1
#define IF_DISCONNECTING 3
#define IF_DISCONNECTED  4
#define IF_ASSOCIATING   5
/* DOT11_AUTH_ALGORITHM, DOT11_CIPHER_ALGORITHM, DOT11_PHY_TYPE */
#define AUTH_OPEN      1
#define AUTH_RSNA_PSK  7
#define AUTH_WPA3_SAE  9
#define CIPHER_NONE    0
#define CIPHER_CCMP    4
static const DWORD g_phy[] = { 6 /* erp: 802.11g */, 7 /* ht: n */, 8 /* vht: ac */, 10 /* he: ax */ };
/* WLAN_CONNECTION_MODE */
#define MODE_PROFILE   0
#define MODE_TEMPORARY 1
#define MODE_SECURE    2
#define MODE_UNSECURE  3
/* WLAN_INTF_OPCODE */
#define OP_AUTOCONF_ENABLED  1
#define OP_BSS_TYPE          5
#define OP_INTERFACE_STATE   6
#define OP_CURRENT_CONNECTION 7
#define OP_CHANNEL_NUMBER    8
#define OP_RSSI              0x10000102
/* the AutoConfig notifications (WLAN_NOTIFICATION_ACM) */
#define SOURCE_ACM               0x00000008
#define SOURCE_ALL               0x0000FFFF
#define ACM_SCAN_COMPLETE        7
#define ACM_SCAN_FAIL            8
#define ACM_CONNECTION_START     9
#define ACM_CONNECTION_COMPLETE  10
#define ACM_CONNECTION_ATTEMPT_FAIL 11
#define ACM_DISCONNECTING        20
#define ACM_DISCONNECTED         21
/* WLAN_REASON_CODE */
#define REASON_SUCCESS               0
#define REASON_UNKNOWN               0x00010001
#define REASON_NETWORK_NOT_COMPATIBLE 0x00020001
#define REASON_CONNECT_CALL_FAIL     0x00028009
#define REASON_SCAN_CALL_FAIL        0x0002800A
#define REASON_NETWORK_NOT_AVAILABLE 0x0002800B
#define REASON_KEY_MISMATCH          0x0002800D
#define REASON_PROFILE_MISSING       0x00080001   /* (WLAN_REASON_CODE_PROFILE_BASE + 1) */
#define WLAN_PROFILE_USER            0x00000002
#define AVAILABLE_CONNECTED          0x00000001
#define AVAILABLE_HAS_PROFILE        0x00000002

/* ---- handles and notifications ---- */

#define MAX_HANDLES 16
#define HANDLE_TAG  0x4E4C5741u                         /* (handles are 1-based slots, tagged) */
typedef struct { BOOL used; NOTIFY_FN_ fn; PVOID ctx; DWORD sources; } Client;
static Client g_clients[MAX_HANDLES];
static CRITICAL_SECTION g_cs;
static LONG g_cs_ready;

static void cs_enter(void)
{
    if (InterlockedCompareExchange(&g_cs_ready, 1, 0) == 0) { InitializeCriticalSection(&g_cs); g_cs_ready = 2; }
    while (g_cs_ready != 2) Sleep(0);
    EnterCriticalSection(&g_cs);
}
static void cs_leave(void) { LeaveCriticalSection(&g_cs); }

static Client *client(HANDLE h)
{
    UINT_PTR v = (UINT_PTR)h;
    if ((v >> 8) != (HANDLE_TAG & 0xFFFFFF) || (v & 0xFF) == 0 || (v & 0xFF) > MAX_HANDLES) return NULL;
    Client *c = &g_clients[(v & 0xFF) - 1];
    return c->used ? c : NULL;
}

/* Tell every client that asked for @code's source (a thread of this DLL calls) */
static void notify(const GUID *g, DWORD code, void *data, DWORD size)
{
    NOTIFY_FN_ fn[MAX_HANDLES];
    PVOID ctx[MAX_HANDLES];
    int n = 0;
    cs_enter();
    for (int i = 0; i < MAX_HANDLES; i++)
        if (g_clients[i].used && g_clients[i].fn && (g_clients[i].sources & SOURCE_ACM)) {
            fn[n] = g_clients[i].fn;
            ctx[n++] = g_clients[i].ctx;
        }
    cs_leave();
    for (int i = 0; i < n; i++) {
        NOTIFICATION_DATA_ d = { SOURCE_ACM, code, *g, size, data };
        fn[i](&d, ctx[i]);
    }
}

WLAPI PVOID WINAPI WlanAllocateMemory(DWORD n)
{
    void *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1);
    if (!p) SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return p;
}

WLAPI VOID WINAPI WlanFreeMemory(PVOID p)
{
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

WLAPI DWORD WINAPI WlanOpenHandle(DWORD version, PVOID reserved, PDWORD negotiated, PHANDLE out)
{
    if (reserved || !negotiated || !out || version < 1 || version > 2) return ERROR_INVALID_PARAMETER;
    cs_enter();
    for (int i = 0; i < MAX_HANDLES; i++)
        if (!g_clients[i].used) {
            memset(&g_clients[i], 0, sizeof(g_clients[i]));
            g_clients[i].used = TRUE;
            cs_leave();
            *negotiated = version;
            *out = (HANDLE)(UINT_PTR)(((HANDLE_TAG & 0xFFFFFF) << 8) | (UINT_PTR)(i + 1));
            return ERROR_SUCCESS;
        }
    cs_leave();
    return ERROR_REMOTE_SESSION_LIMIT_EXCEEDED;
}

WLAPI DWORD WINAPI WlanCloseHandle(HANDLE h, PVOID reserved)
{
    if (reserved) return ERROR_INVALID_PARAMETER;
    cs_enter();
    Client *c = client(h);
    if (c) c->used = FALSE;
    cs_leave();
    return c ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

WLAPI DWORD WINAPI WlanRegisterNotification(HANDLE h, DWORD sources, BOOL ignore_dup, NOTIFY_FN_ fn, PVOID ctx,
                                            PVOID reserved, PDWORD previous)
{
    (void)ignore_dup;
    if (reserved || (sources && !fn)) return ERROR_INVALID_PARAMETER;
    cs_enter();
    Client *c = client(h);
    if (c) {
        if (previous) *previous = c->sources;
        c->sources = sources & SOURCE_ALL;
        c->fn = sources ? fn : NULL;
        c->ctx = ctx;
    }
    cs_leave();
    return c ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

/* ---- adapters ---- */

static int adapter_count(void)
{
    LONG_PTR n = NtNovaSockCtl(0, NOVAWLAN_IF, 0, NULL);
    return n > 0 ? (int)n : 0;
}

static BOOL adapter(int i, NovaWlanIf *o)
{
    return NtNovaSockCtl(0, NOVAWLAN_IF, (ULONG_PTR)i, o) == (LONG_PTR)sizeof(*o);
}

/* {4E4F5641-574C-4E00-8000-<MAC>}: the interface's GUID */
static void guid_of(const NovaWlanIf *o, GUID *g)
{
    g->Data1 = 0x4E4F5641; g->Data2 = 0x574C; g->Data3 = 0x4E00;
    g->Data4[0] = 0x80; g->Data4[1] = 0x00;
    memcpy(g->Data4 + 2, o->mac, 6);
}

/* The adapter @g names (its index, and its details into @o), or -1 */
static int find_adapter(const GUID *g, NovaWlanIf *o)
{
    if (!g) return -1;
    int n = adapter_count();
    for (int i = 0; i < n; i++) {
        GUID x;
        if (!adapter(i, o)) continue;
        guid_of(o, &x);
        if (!memcmp(&x, g, sizeof(x))) return i;
    }
    return -1;
}

static void wide(WCHAR *out, int cap, const char *s)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap)) out[0] = 0;
    out[cap - 1] = 0;
}

static void ssid_of(const NovaWlanBss *b, DOT11_SSID_ *s)
{
    memset(s, 0, sizeof(*s));
    s->uSSIDLength = b->ssid_len > 32 ? 32 : b->ssid_len;
    memcpy(s->ucSSID, b->ssid, s->uSSIDLength);
}

static DWORD auth_of(const NovaWlanBss *b)
{
    return b->security == NOVAWLAN_WPA3 ? AUTH_WPA3_SAE : b->security == NOVAWLAN_WPA2 ? AUTH_RSNA_PSK :
           b->security == NOVAWLAN_OPEN ? AUTH_OPEN : AUTH_RSNA_PSK;
}

WLAPI DWORD WINAPI WlanEnumInterfaces(HANDLE h, PVOID reserved, IF_LIST_ **out)
{
    if (reserved || !out) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    int n = adapter_count();
    IF_LIST_ *l = WlanAllocateMemory(sizeof(IF_LIST_) + (n ? n - 1 : 0) * sizeof(IF_INFO_));
    if (!l) return ERROR_NOT_ENOUGH_MEMORY;
    for (int i = 0; i < n; i++) {
        NovaWlanIf o;
        if (!adapter(i, &o)) continue;
        IF_INFO_ *f = &l->InterfaceInfo[l->dwNumberOfItems++];
        guid_of(&o, &f->InterfaceGuid);
        wide(f->strInterfaceDescription, 256, o.description);
        f->isState = o.connected ? IF_CONNECTED : IF_DISCONNECTED;
    }
    *out = l;
    return ERROR_SUCCESS;
}

/* ---- scans (the last one's access points, per adapter) ---- */

typedef struct { GUID g; BOOL valid; int n; NovaWlanBss bss[NOVAWLAN_MAX_BSS]; } ScanCache;
static ScanCache g_scan[4];

static ScanCache *cache_of(const GUID *g, BOOL make)
{
    for (int i = 0; i < 4; i++) if (g_scan[i].valid && !memcmp(&g_scan[i].g, g, sizeof(*g))) return &g_scan[i];
    if (!make) return NULL;
    for (int i = 0; i < 4; i++) if (!g_scan[i].valid) { g_scan[i].g = *g; return &g_scan[i]; }
    g_scan[0].g = *g;
    return &g_scan[0];
}

/* Scan with adapter @i (@g) now: the cache gets the access points; FALSE if it failed */
static BOOL scan_now(int i, const GUID *g)
{
    NovaWlanBss *b = HeapAlloc(GetProcessHeap(), 0, sizeof(NovaWlanBss) * NOVAWLAN_MAX_BSS);
    if (!b) return FALSE;
    LONG_PTR n = NtNovaSockCtl(0, NOVAWLAN_SCAN, (ULONG_PTR)(i | (NOVAWLAN_MAX_BSS << 8)), b);
    if (n >= 0) {
        cs_enter();
        ScanCache *c = cache_of(g, TRUE);
        c->n = (int)n;
        memcpy(c->bss, b, (size_t)n * sizeof(*b));
        c->valid = TRUE;
        cs_leave();
    }
    HeapFree(GetProcessHeap(), 0, b);
    return n >= 0;
}

/* A copy of adapter @g's last scan (scanning first if it has none); its count, or -1 */
static int last_scan(int i, const GUID *g, NovaWlanBss *out)
{
    cs_enter();
    ScanCache *c = cache_of(g, FALSE);
    int n = c ? c->n : -1;
    if (c) memcpy(out, c->bss, (size_t)n * sizeof(*out));
    cs_leave();
    if (n >= 0) return n;
    if (!scan_now(i, g)) return -1;
    return last_scan(i, g, out);
}

typedef struct { int i; GUID g; } ScanJob;

static DWORD WINAPI scan_thread(void *p)
{
    ScanJob j = *(ScanJob *)p;
    HeapFree(GetProcessHeap(), 0, p);
    if (scan_now(j.i, &j.g)) notify(&j.g, ACM_SCAN_COMPLETE, NULL, 0);
    else {
        DWORD reason = REASON_SCAN_CALL_FAIL;
        notify(&j.g, ACM_SCAN_FAIL, &reason, sizeof(reason));
    }
    return 0;
}

WLAPI DWORD WINAPI WlanScan(HANDLE h, const GUID *g, const DOT11_SSID_ *ssid, const void *ie, PVOID reserved)
{
    (void)ssid; (void)ie;
    if (reserved || !g) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    NovaWlanIf o;
    int i = find_adapter(g, &o);
    if (i < 0) return ERROR_NOT_FOUND;
    ScanJob *j = HeapAlloc(GetProcessHeap(), 0, sizeof(*j));
    if (!j) return ERROR_NOT_ENOUGH_MEMORY;
    j->i = i; j->g = *g;
    HANDLE t = CreateThread(NULL, 0, scan_thread, j, 0, NULL);
    if (!t) { HeapFree(GetProcessHeap(), 0, j); return ERROR_NOT_ENOUGH_MEMORY; }
    CloseHandle(t);
    return ERROR_SUCCESS;
}

/* ---- profiles (the registry: Software\NovaOS\Wlan\Profiles\NAME, value "Xml") ---- */

#define PROFILES L"Software\\NovaOS\\Wlan\\Profiles"

static BOOL starts(const char *s, const char *with)
{
    while (*with) if (*s++ != *with++) return FALSE;
    return TRUE;
}

/* The text of the first <@tag> after @from in @xml (UTF-8), or FALSE */
static BOOL xml_tag(const char *xml, const char *from, const char *tag, char *out, int cap)
{
    const char *p = xml;
    if (from) {
        for (; *p; p++) if (starts(p, from)) break;
        if (!*p) return FALSE;
    }
    char open[40];
    int tl = lstrlenA(tag);
    if (tl > 36) return FALSE;
    open[0] = '<';
    memcpy(open + 1, tag, tl);
    open[tl + 1] = '>';
    open[tl + 2] = 0;
    for (; *p; p++) if (starts(p, open)) break;
    if (!*p) return FALSE;
    p += tl + 2;
    int n = 0;
    while (*p && *p != '<' && n < cap - 1) out[n++] = *p++;
    out[n] = 0;
    /* (the entities XML allows in a name or a key) */
    char *s = out, *d = out;
    while (*s) {
        static const struct { const char *e; char c; } ent[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
                                                                  { "&quot;", '"' }, { "&apos;", '\'' } };
        int k = 0;
        for (; k < 5; k++) if (starts(s, ent[k].e)) break;
        if (k < 5) { *d++ = ent[k].c; s += strlen(ent[k].e); }
        else *d++ = *s++;
    }
    *d = 0;
    return *p == '<';
}

/* A profile's name, SSID and passphrase (@key "" for an open network); FALSE if it is not one NovaOS reads */
static BOOL parse_profile(LPCWSTR xml, char *name, char *ssid, char *key)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, xml, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return FALSE;
    char *x = HeapAlloc(GetProcessHeap(), 0, n);
    if (!x) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, xml, -1, x, n, NULL, NULL);
    BOOL ok = xml_tag(x, NULL, "name", name, 257) && xml_tag(x, "<SSIDConfig>", "name", ssid, 33);
    char prot[8];
    key[0] = 0;
    if (ok && xml_tag(x, "<sharedKey>", "protected", prot, sizeof(prot)) && lstrcmpiA(prot, "false")) ok = FALSE;
    if (ok) xml_tag(x, "<sharedKey>", "keyMaterial", key, 65);
    HeapFree(GetProcessHeap(), 0, x);
    return ok && ssid[0];
}

/* Profile @name's XML (WlanAllocateMemory'd) and whether it is a user's, or NULL */
static WCHAR *load_profile(LPCWSTR name, BOOL *user)
{
    WCHAR path[300];
    if (lstrlenW(name) > 255) return NULL;
    lstrcpyW(path, PROFILES L"\\");
    lstrcatW(path, name);
    for (int pass = 0; pass < 2; pass++) {
        HKEY k;
        if (RegOpenKeyExW(pass ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, path, 0, KEY_READ, &k)) continue;
        DWORD type, size = 0;
        WCHAR *x = NULL;
        if (!RegQueryValueExW(k, L"Xml", NULL, &type, NULL, &size) && type == REG_SZ &&
            (x = WlanAllocateMemory(size + sizeof(WCHAR))) &&
            RegQueryValueExW(k, L"Xml", NULL, &type, (BYTE *)x, &size)) { WlanFreeMemory(x); x = NULL; }
        RegCloseKey(k);
        if (x) { if (user) *user = !pass; return x; }
    }
    return NULL;
}

/* The saved profile whose SSID is @ssid (its name into @out), or FALSE */
static BOOL profile_for(const char *ssid, WCHAR *out)
{
    for (int pass = 0; pass < 2; pass++) {
        HKEY k;
        if (RegOpenKeyExW(pass ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, PROFILES, 0, KEY_READ, &k)) continue;
        WCHAR name[260];
        for (DWORD i = 0;; i++) {
            DWORD len = 260;
            if (RegEnumKeyExW(k, i, name, &len, NULL, NULL, NULL, NULL)) break;
            WCHAR *x = load_profile(name, NULL);
            char pn[257], ps[33], pk[65];
            BOOL hit = x && parse_profile(x, pn, ps, pk) && !lstrcmpA(ps, ssid);
            if (x) WlanFreeMemory(x);
            memset(pk, 0, sizeof(pk));
            if (hit) { lstrcpyW(out, name); RegCloseKey(k); return TRUE; }
        }
        RegCloseKey(k);
    }
    return FALSE;
}

WLAPI DWORD WINAPI WlanSetProfile(HANDLE h, const GUID *g, DWORD flags, LPCWSTR xml, LPCWSTR security,
                                  BOOL overwrite, PVOID reserved, DWORD *reason)
{
    (void)security;
    if (reserved || !g || !xml || !reason) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    NovaWlanIf o;
    if (find_adapter(g, &o) < 0) return ERROR_NOT_FOUND;
    char name[257], ssid[33], key[65];
    *reason = REASON_SUCCESS;
    if (!parse_profile(xml, name, ssid, key)) { *reason = REASON_NETWORK_NOT_COMPATIBLE; return ERROR_BAD_PROFILE; }
    memset(key, 0, sizeof(key));
    WCHAR wname[257], path[300];
    wide(wname, 257, name);
    lstrcpyW(path, PROFILES L"\\");
    lstrcatW(path, wname);
    HKEY root = (flags & WLAN_PROFILE_USER) ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE, k;
    DWORD disp;
    if (RegCreateKeyExW(root, path, 0, NULL, 0, KEY_WRITE, NULL, &k, &disp)) return ERROR_ACCESS_DENIED;
    if (disp == REG_OPENED_EXISTING_KEY && !overwrite) { RegCloseKey(k); return ERROR_ALREADY_EXISTS; }
    LSTATUS r = RegSetValueExW(k, L"Xml", 0, REG_SZ, (const BYTE *)xml, (lstrlenW(xml) + 1) * sizeof(WCHAR));
    RegCloseKey(k);
    return r ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
}

WLAPI DWORD WINAPI WlanGetProfile(HANDLE h, const GUID *g, LPCWSTR name, PVOID reserved, LPWSTR *xml, DWORD *flags,
                                  DWORD *access)
{
    if (reserved || !g || !name || !xml) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    BOOL user;
    WCHAR *x = load_profile(name, &user);
    if (!x) return ERROR_NOT_FOUND;
    *xml = x;
    if (flags) *flags = user ? WLAN_PROFILE_USER : 0;
    if (access) *access = 0x00020000 | 0x00000001 | 0x00000002;   /* (read, execute, write: WLAN_ALL_ACCESS's parts) */
    return ERROR_SUCCESS;
}

WLAPI DWORD WINAPI WlanDeleteProfile(HANDLE h, const GUID *g, LPCWSTR name, PVOID reserved)
{
    if (reserved || !g || !name || lstrlenW(name) > 255) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    WCHAR path[300];
    lstrcpyW(path, PROFILES L"\\");
    lstrcatW(path, name);
    BOOL gone = !RegDeleteTreeW(HKEY_CURRENT_USER, path);
    gone |= !RegDeleteTreeW(HKEY_LOCAL_MACHINE, path);
    return gone ? ERROR_SUCCESS : ERROR_NOT_FOUND;
}

WLAPI DWORD WINAPI WlanGetProfileList(HANDLE h, const GUID *g, PVOID reserved, PROFILE_LIST_ **out)
{
    if (reserved || !g || !out) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    PROFILE_LIST_ *l = WlanAllocateMemory(sizeof(PROFILE_LIST_) + 63 * sizeof(PROFILE_INFO_));
    if (!l) return ERROR_NOT_ENOUGH_MEMORY;
    for (int pass = 0; pass < 2; pass++) {
        HKEY k;
        if (RegOpenKeyExW(pass ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, PROFILES, 0, KEY_READ, &k)) continue;
        for (DWORD i = 0; l->dwNumberOfItems < 64; i++) {
            DWORD len = 256;
            PROFILE_INFO_ *p = &l->ProfileInfo[l->dwNumberOfItems];
            if (RegEnumKeyExW(k, i, p->strProfileName, &len, NULL, NULL, NULL, NULL)) break;
            p->dwFlags = pass ? 0 : WLAN_PROFILE_USER;
            l->dwNumberOfItems++;
        }
        RegCloseKey(k);
    }
    *out = l;
    return ERROR_SUCCESS;
}

/* ---- networks ---- */

WLAPI DWORD WINAPI WlanGetAvailableNetworkList(HANDLE h, const GUID *g, DWORD flags, PVOID reserved,
                                               NETWORK_LIST_ **out)
{
    (void)flags;
    if (reserved || !g || !out) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    NovaWlanIf o;
    int i = find_adapter(g, &o);
    if (i < 0) return ERROR_NOT_FOUND;
    NovaWlanBss *b = HeapAlloc(GetProcessHeap(), 0, sizeof(NovaWlanBss) * NOVAWLAN_MAX_BSS);
    if (!b) return ERROR_NOT_ENOUGH_MEMORY;
    int n = last_scan(i, g, b);
    if (n < 0) { HeapFree(GetProcessHeap(), 0, b); return ERROR_NDIS_DOT11_POWER_STATE_INVALID; }
    NETWORK_LIST_ *l = WlanAllocateMemory(sizeof(NETWORK_LIST_) + (n ? n - 1 : 0) * sizeof(NETWORK_));
    if (!l) { HeapFree(GetProcessHeap(), 0, b); return ERROR_NOT_ENOUGH_MEMORY; }
    /* one entry per network (SSID and security), its strongest access point's signal (the list is strongest first) */
    for (int k = 0; k < n; k++) {
        NETWORK_ *e = NULL;
        for (DWORD m = 0; m < l->dwNumberOfItems; m++) {
            NETWORK_ *x = &l->Network[m];
            if (x->dot11Ssid.uSSIDLength == b[k].ssid_len && !memcmp(x->dot11Ssid.ucSSID, b[k].ssid, b[k].ssid_len) &&
                x->dot11DefaultAuthAlgorithm == auth_of(&b[k])) { e = x; break; }
        }
        if (e) {
            e->uNumberOfBssids++;
            BOOL have = FALSE;
            for (ULONG p = 0; p < e->uNumberOfPhyTypes; p++) if (e->dot11PhyTypes[p] == g_phy[b[k].phy & 3]) have = TRUE;
            if (!have && e->uNumberOfPhyTypes < 8) e->dot11PhyTypes[e->uNumberOfPhyTypes++] = g_phy[b[k].phy & 3];
            continue;
        }
        e = &l->Network[l->dwNumberOfItems++];
        ssid_of(&b[k], &e->dot11Ssid);
        e->dot11BssType = 1;                                       /* (dot11_BSS_type_infrastructure) */
        e->uNumberOfBssids = 1;
        e->bNetworkConnectable = b[k].security != NOVAWLAN_OTHER;
        e->wlanNotConnectableReason = e->bNetworkConnectable ? REASON_SUCCESS : REASON_NETWORK_NOT_COMPATIBLE;
        e->uNumberOfPhyTypes = 1;
        e->dot11PhyTypes[0] = g_phy[b[k].phy & 3];
        e->wlanSignalQuality = b[k].quality;
        e->bSecurityEnabled = b[k].security != NOVAWLAN_OPEN;
        e->dot11DefaultAuthAlgorithm = auth_of(&b[k]);
        e->dot11DefaultCipherAlgorithm = b[k].security == NOVAWLAN_OPEN ? CIPHER_NONE : CIPHER_CCMP;
        if (o.connected && !lstrcmpA(o.bss.ssid, b[k].ssid)) e->dwFlags |= AVAILABLE_CONNECTED;
        if (profile_for(b[k].ssid, e->strProfileName)) e->dwFlags |= AVAILABLE_HAS_PROFILE;
    }
    HeapFree(GetProcessHeap(), 0, b);
    *out = l;
    return ERROR_SUCCESS;
}

WLAPI DWORD WINAPI WlanGetNetworkBssList(HANDLE h, const GUID *g, const DOT11_SSID_ *ssid, DWORD type, BOOL secure,
                                         PVOID reserved, BSS_LIST_ **out)
{
    if (reserved || !g || !out || (ssid && type != 1 && type != 3)) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    NovaWlanIf o;
    int i = find_adapter(g, &o);
    if (i < 0) return ERROR_NOT_FOUND;
    NovaWlanBss *b = HeapAlloc(GetProcessHeap(), 0, sizeof(NovaWlanBss) * NOVAWLAN_MAX_BSS);
    if (!b) return ERROR_NOT_ENOUGH_MEMORY;
    int n = last_scan(i, g, b);
    if (n < 0) { HeapFree(GetProcessHeap(), 0, b); return ERROR_NDIS_DOT11_POWER_STATE_INVALID; }
    DWORD size = sizeof(BSS_LIST_) + (n ? n - 1 : 0) * sizeof(BSS_ENTRY_);
    BSS_LIST_ *l = WlanAllocateMemory(size);
    if (!l) { HeapFree(GetProcessHeap(), 0, b); return ERROR_NOT_ENOUGH_MEMORY; }
    l->dwTotalSize = size;
    for (int k = 0; k < n; k++) {
        if (ssid && (ssid->uSSIDLength != b[k].ssid_len || memcmp(ssid->ucSSID, b[k].ssid, b[k].ssid_len) ||
                     secure != (b[k].security != NOVAWLAN_OPEN))) continue;
        BSS_ENTRY_ *e = &l->wlanBssEntries[l->dwNumberOfItems++];
        ssid_of(&b[k], &e->dot11Ssid);
        memcpy(e->dot11Bssid, b[k].bssid, 6);
        e->dot11BssType = 1;
        e->dot11BssPhyType = g_phy[b[k].phy & 3];
        e->lRssi = b[k].rssi;
        e->uLinkQuality = b[k].quality;
        e->bInRegDomain = TRUE;
        e->usBeaconPeriod = 100;
        e->usCapabilityInformation = 0x0001 | (b[k].privacy ? 0x0010 : 0);   /* (ESS, Privacy) */
        e->ulChCenterFrequency = b[k].freq_khz;
    }
    HeapFree(GetProcessHeap(), 0, b);
    *out = l;
    return ERROR_SUCCESS;
}

/* ---- connecting ---- */

typedef struct {
    int   i;
    GUID  g;
    DWORD mode;
    WCHAR profile[256];
    NovaWlanConnect c;
} ConnectJob;
static volatile LONG g_connecting;

static DWORD reason_of(LONG_PTR r)
{
    switch ((int)r) {
    case NOVAWLAN_OK:          return REASON_SUCCESS;
    case NOVAWLAN_NO_NETWORK:  return REASON_NETWORK_NOT_AVAILABLE;
    case NOVAWLAN_NEEDS_KEY:
    case NOVAWLAN_BAD_KEY:
    case NOVAWLAN_KEY_FORMAT:  return REASON_KEY_MISMATCH;
    case NOVAWLAN_UNSUPPORTED: return REASON_NETWORK_NOT_COMPATIBLE;
    case NOVAWLAN_FAILED:      return REASON_CONNECT_CALL_FAIL;
    }
    return REASON_UNKNOWN;
}

static DWORD WINAPI connect_thread(void *p)
{
    ConnectJob *j = p;
    CONNECTION_NOTIFY_ d;
    memset(&d, 0, sizeof(d));
    d.wlanConnectionMode = j->mode;
    lstrcpyW(d.strProfileName, j->profile);
    d.dot11Ssid.uSSIDLength = lstrlenA(j->c.ssid);
    memcpy(d.dot11Ssid.ucSSID, j->c.ssid, d.dot11Ssid.uSSIDLength);
    d.dot11BssType = 1;
    d.bSecurityEnabled = j->c.key[0] != 0;
    notify(&j->g, ACM_CONNECTION_START, &d, sizeof(d));
    LONG_PTR r = NtNovaSockCtl(0, NOVAWLAN_CONNECT, (ULONG_PTR)j->i, &j->c);
    d.wlanReasonCode = reason_of(r);
    if (r != NOVAWLAN_OK) notify(&j->g, ACM_CONNECTION_ATTEMPT_FAIL, &d, sizeof(d));
    notify(&j->g, ACM_CONNECTION_COMPLETE, &d, sizeof(d));
    memset(j, 0, sizeof(*j));                                   /* (the passphrase) */
    HeapFree(GetProcessHeap(), 0, j);
    InterlockedExchange(&g_connecting, 0);
    return 0;
}

WLAPI DWORD WINAPI WlanConnect(HANDLE h, const GUID *g, const CONNECT_PARAMS_ *p, PVOID reserved)
{
    if (reserved || !g || !p) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    if (p->dot11BssType != 1 && p->dot11BssType != 3) return ERROR_INVALID_PARAMETER;   /* (infrastructure or any) */
    NovaWlanIf o;
    int i = find_adapter(g, &o);
    if (i < 0) return ERROR_NOT_FOUND;
    ConnectJob *j = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*j));
    if (!j) return ERROR_NOT_ENOUGH_MEMORY;
    j->i = i; j->g = *g; j->mode = p->wlanConnectionMode;
    char name[257];
    DWORD err = ERROR_SUCCESS;
    switch (p->wlanConnectionMode) {
    case MODE_PROFILE: {
        WCHAR *x = p->strProfile ? load_profile(p->strProfile, NULL) : NULL;
        if (!x) { err = p->strProfile ? ERROR_NOT_FOUND : ERROR_INVALID_PARAMETER; break; }
        if (!parse_profile(x, name, j->c.ssid, j->c.key)) err = ERROR_BAD_PROFILE;
        WlanFreeMemory(x);
        lstrcpynW(j->profile, p->strProfile, 256);
        break;
    }
    case MODE_TEMPORARY:
        if (!p->strProfile) { err = ERROR_INVALID_PARAMETER; break; }
        if (!parse_profile(p->strProfile, name, j->c.ssid, j->c.key)) { err = ERROR_BAD_PROFILE; break; }
        wide(j->profile, 256, name);
        break;
    case MODE_SECURE:
    case MODE_UNSECURE:                                         /* (an open network, by its SSID alone) */
        if (!p->pDot11Ssid) err = ERROR_INVALID_PARAMETER;
        break;
    default:
        err = ERROR_INVALID_PARAMETER;
    }
    if (!err && p->pDot11Ssid) {
        if (p->pDot11Ssid->uSSIDLength > 32) err = ERROR_INVALID_PARAMETER;
        else {
            memset(j->c.ssid, 0, sizeof(j->c.ssid));
            memcpy(j->c.ssid, p->pDot11Ssid->ucSSID, p->pDot11Ssid->uSSIDLength);
        }
    }
    if (!err && InterlockedCompareExchange(&g_connecting, 1, 0)) err = ERROR_BUSY;
    if (err) {
        memset(j, 0, sizeof(*j));
        HeapFree(GetProcessHeap(), 0, j);
        return err;
    }
    HANDLE t = CreateThread(NULL, 0, connect_thread, j, 0, NULL);
    if (!t) {
        memset(j, 0, sizeof(*j));
        HeapFree(GetProcessHeap(), 0, j);
        InterlockedExchange(&g_connecting, 0);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    CloseHandle(t);
    return ERROR_SUCCESS;
}

WLAPI DWORD WINAPI WlanDisconnect(HANDLE h, const GUID *g, PVOID reserved)
{
    if (reserved || !g) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    NovaWlanIf o;
    int i = find_adapter(g, &o);
    if (i < 0) return ERROR_NOT_FOUND;
    if (!o.connected) return ERROR_SUCCESS;
    CONNECTION_NOTIFY_ d;
    memset(&d, 0, sizeof(d));
    ssid_of(&o.bss, &d.dot11Ssid);
    d.dot11BssType = 1;
    d.bSecurityEnabled = o.bss.security != NOVAWLAN_OPEN;
    profile_for(o.bss.ssid, d.strProfileName);
    notify(g, ACM_DISCONNECTING, &d, sizeof(d));
    NtNovaSockCtl(0, NOVAWLAN_DISCONNECT, (ULONG_PTR)i, NULL);
    notify(g, ACM_DISCONNECTED, &d, sizeof(d));
    return ERROR_SUCCESS;
}

WLAPI DWORD WINAPI WlanQueryInterface(HANDLE h, const GUID *g, DWORD op, PVOID reserved, PDWORD size, PVOID *data,
                                      PDWORD type)
{
    if (reserved || !g || !size || !data) return ERROR_INVALID_PARAMETER;
    if (!client(h)) return ERROR_INVALID_HANDLE;
    NovaWlanIf o;
    if (find_adapter(g, &o) < 0) return ERROR_NOT_FOUND;
    DWORD v;
    switch (op) {
    case OP_AUTOCONF_ENABLED: v = TRUE; break;
    case OP_BSS_TYPE:         v = 1; break;
    case OP_INTERFACE_STATE:  v = o.connected ? IF_CONNECTED : IF_DISCONNECTED; break;
    case OP_CHANNEL_NUMBER:   if (!o.connected) return ERROR_INVALID_STATE; v = o.bss.channel; break;
    case OP_RSSI:             if (!o.connected) return ERROR_INVALID_STATE; v = (DWORD)o.bss.rssi; break;
    case OP_CURRENT_CONNECTION: {
        if (!o.connected) return ERROR_INVALID_STATE;
        CONNECTION_ATTRIBUTES_ *a = WlanAllocateMemory(sizeof(*a));
        if (!a) return ERROR_NOT_ENOUGH_MEMORY;
        a->isState = IF_CONNECTED;
        a->wlanConnectionMode = profile_for(o.bss.ssid, a->strProfileName) ? MODE_PROFILE : MODE_TEMPORARY;
        ssid_of(&o.bss, &a->wlanAssociationAttributes.dot11Ssid);
        a->wlanAssociationAttributes.dot11BssType = 1;
        memcpy(a->wlanAssociationAttributes.dot11Bssid, o.bss.bssid, 6);
        a->wlanAssociationAttributes.dot11PhyType = g_phy[o.bss.phy & 3];
        a->wlanAssociationAttributes.wlanSignalQuality = o.bss.quality;
        a->wlanAssociationAttributes.ulRxRate = a->wlanAssociationAttributes.ulTxRate = 144400;   /* (kbps: MCS 15, 20 MHz) */
        a->wlanSecurityAttributes.bSecurityEnabled = o.bss.security != NOVAWLAN_OPEN;
        a->wlanSecurityAttributes.dot11AuthAlgorithm = auth_of(&o.bss);
        a->wlanSecurityAttributes.dot11CipherAlgorithm = o.bss.security == NOVAWLAN_OPEN ? CIPHER_NONE : CIPHER_CCMP;
        *size = sizeof(*a);
        *data = a;
        if (type) *type = 0;                                    /* (wlan_opcode_value_type_query_only) */
        return ERROR_SUCCESS;
    }
    default:
        return ERROR_NOT_SUPPORTED;
    }
    DWORD *d = WlanAllocateMemory(sizeof(DWORD));
    if (!d) return ERROR_NOT_ENOUGH_MEMORY;
    *d = v;
    *size = sizeof(DWORD);
    *data = d;
    if (type) *type = 0;
    return ERROR_SUCCESS;
}

WLAPI DWORD WINAPI WlanReasonCodeToString(DWORD code, DWORD cap, PWCHAR buf, PVOID reserved)
{
    if (reserved || !buf || !cap) return ERROR_INVALID_PARAMETER;
    static const struct { DWORD code; const WCHAR *text; } t[] = {
        { REASON_SUCCESS,               L"The operation was successful." },
        { REASON_NETWORK_NOT_COMPATIBLE, L"The network is not compatible." },
        { REASON_CONNECT_CALL_FAIL,     L"The connection failed." },
        { REASON_SCAN_CALL_FAIL,        L"The scan failed." },
        { REASON_NETWORK_NOT_AVAILABLE, L"The network is not available." },
        { REASON_KEY_MISMATCH,          L"The network security key is not correct." },
        { REASON_PROFILE_MISSING,       L"The profile was not found." },
    };
    const WCHAR *s = L"An unknown reason.";
    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) if (t[i].code == code) s = t[i].text;
    lstrcpynW(buf, s, (int)cap);
    return ERROR_SUCCESS;
}
