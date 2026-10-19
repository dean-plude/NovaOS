/*
 * wlantest.exe — Wi-Fi through the Native Wifi API (wlanapi.dll), as a
 * Windows program reaches it
 *
 * Runs in the devices self-test boot "wifi": NovaOS's WDI test miniport
 * (tools/drivertest/wditest.c) on QEMU's edu device, whose radio hears
 * three access points: NovaNet (WPA2-Personal, 2.4 GHz channel 6, signal
 * 90%), NovaNet-5G (WPA3-Personal, 5 GHz channel 36, 72%) and NovaGuest
 * (open, 2.4 GHz channel 11, 35%).  "wlantest" checks:
 *  - WlanOpenHandle (a wrong client version refused), WlanEnumInterfaces
 *    (the adapter, its description, disconnected, its GUID from its MAC);
 *  - WlanRegisterNotification, then WlanScan finishing with
 *    wlan_notification_acm_scan_complete;
 *  - WlanGetAvailableNetworkList: the three networks with their
 *    authentication and cipher, signal and PHY types;
 *    WlanGetNetworkBssList: the three access points (BSSID, RSSI, channel
 *    centre frequency) and one filtered by SSID;
 *  - WlanSetProfile with NovaNet's profile XML, WlanGetProfileList,
 *    WlanGetProfile, and the network list naming the profile;
 *  - WlanConnect with a temporary profile and a wrong passphrase:
 *    connection_attempt_fail and connection_complete with the key mismatch
 *    reason; then with the saved profile: connection_complete, success;
 *    WlanQueryInterface's state, current connection (SSID, BSSID,
 *    WPA2-Personal with CCMP, the profile) and channel; WlanDisconnect;
 *  - WlanConnect to the open network by SSID alone (discovery), then
 *    WlanDisconnect; WlanDeleteProfile; WlanCloseHandle.
 * "wlantest settings": the test joins NovaNet in Settings' Wi-Fi page as a
 * person would (the selftest types it); wlantest waits for the adapter to
 * be connected, checks the connection through WlanQueryInterface, then
 * disconnects it.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>

typedef struct { ULONG uSSIDLength; UCHAR ucSSID[32]; } DOT11_SSID_;
typedef struct { GUID InterfaceGuid; WCHAR strInterfaceDescription[256]; DWORD isState; } IF_INFO_;
typedef struct { DWORD dwNumberOfItems, dwIndex; IF_INFO_ InterfaceInfo[1]; } IF_LIST_;
typedef struct {
    WCHAR strProfileName[256];
    DOT11_SSID_ dot11Ssid;
    DWORD dot11BssType;
    ULONG uNumberOfBssids;
    BOOL bNetworkConnectable;
    DWORD wlanNotConnectableReason;
    ULONG uNumberOfPhyTypes;
    DWORD dot11PhyTypes[8];
    BOOL bMorePhyTypes;
    ULONG wlanSignalQuality;
    BOOL bSecurityEnabled;
    DWORD dot11DefaultAuthAlgorithm, dot11DefaultCipherAlgorithm;
    DWORD dwFlags, dwReserved;
} NETWORK_;
typedef struct { DWORD dwNumberOfItems, dwIndex; NETWORK_ Network[1]; } NETWORK_LIST_;
typedef struct {
    DOT11_SSID_ dot11Ssid;
    ULONG uPhyId;
    UCHAR dot11Bssid[6];
    DWORD dot11BssType, dot11BssPhyType;
    LONG lRssi;
    ULONG uLinkQuality;
    BOOLEAN bInRegDomain;
    USHORT usBeaconPeriod;
    ULONGLONG ullTimestamp, ullHostTimestamp;
    USHORT usCapabilityInformation;
    ULONG ulChCenterFrequency;
    struct { ULONG uRateSetLength; USHORT usRateSet[126]; } wlanRateSet;
    ULONG ulIeOffset, ulIeSize;
} BSS_ENTRY_;
typedef struct { DWORD dwTotalSize, dwNumberOfItems; BSS_ENTRY_ wlanBssEntries[1]; } BSS_LIST_;
typedef struct {
    DWORD wlanConnectionMode;
    LPCWSTR strProfile;
    DOT11_SSID_ *pDot11Ssid;
    void *pDesiredBssidList;
    DWORD dot11BssType, dwFlags;
} CONNECT_PARAMS_;
typedef struct {
    DWORD isState, wlanConnectionMode;
    WCHAR strProfileName[256];
    struct {
        DOT11_SSID_ dot11Ssid;
        DWORD dot11BssType;
        UCHAR dot11Bssid[6];
        DWORD dot11PhyType;
        ULONG uDot11PhyIndex, wlanSignalQuality, ulRxRate, ulTxRate;
    } assoc;
    struct { BOOL bSecurityEnabled, bOneXEnabled; DWORD dot11AuthAlgorithm, dot11CipherAlgorithm; } sec;
} CONNECTION_ATTRIBUTES_;
typedef struct { DWORD NotificationSource, NotificationCode; GUID InterfaceGuid; DWORD dwDataSize; PVOID pData; } NOTIFY_;
typedef struct {
    DWORD wlanConnectionMode;
    WCHAR strProfileName[256];
    DOT11_SSID_ dot11Ssid;
    DWORD dot11BssType;
    BOOL bSecurityEnabled;
    DWORD wlanReasonCode, dwFlags;
    WCHAR strProfileXml[1];
} CONNECTION_NOTIFY_;
typedef struct { WCHAR strProfileName[256]; DWORD dwFlags; } PROFILE_INFO_;
typedef struct { DWORD dwNumberOfItems, dwIndex; PROFILE_INFO_ ProfileInfo[1]; } PROFILE_LIST_;
typedef void (WINAPI *NOTIFY_FN_)(NOTIFY_ *, PVOID);

__declspec(dllimport) DWORD WINAPI WlanOpenHandle(DWORD, PVOID, PDWORD, PHANDLE);
__declspec(dllimport) DWORD WINAPI WlanCloseHandle(HANDLE, PVOID);
__declspec(dllimport) DWORD WINAPI WlanEnumInterfaces(HANDLE, PVOID, IF_LIST_ **);
__declspec(dllimport) DWORD WINAPI WlanRegisterNotification(HANDLE, DWORD, BOOL, NOTIFY_FN_, PVOID, PVOID, PDWORD);
__declspec(dllimport) DWORD WINAPI WlanScan(HANDLE, const GUID *, const DOT11_SSID_ *, const void *, PVOID);
__declspec(dllimport) DWORD WINAPI WlanGetAvailableNetworkList(HANDLE, const GUID *, DWORD, PVOID, NETWORK_LIST_ **);
__declspec(dllimport) DWORD WINAPI WlanGetNetworkBssList(HANDLE, const GUID *, const DOT11_SSID_ *, DWORD, BOOL, PVOID,
                                                         BSS_LIST_ **);
__declspec(dllimport) DWORD WINAPI WlanSetProfile(HANDLE, const GUID *, DWORD, LPCWSTR, LPCWSTR, BOOL, PVOID, DWORD *);
__declspec(dllimport) DWORD WINAPI WlanGetProfile(HANDLE, const GUID *, LPCWSTR, PVOID, LPWSTR *, DWORD *, DWORD *);
__declspec(dllimport) DWORD WINAPI WlanDeleteProfile(HANDLE, const GUID *, LPCWSTR, PVOID);
__declspec(dllimport) DWORD WINAPI WlanGetProfileList(HANDLE, const GUID *, PVOID, PROFILE_LIST_ **);
__declspec(dllimport) DWORD WINAPI WlanConnect(HANDLE, const GUID *, const CONNECT_PARAMS_ *, PVOID);
__declspec(dllimport) DWORD WINAPI WlanDisconnect(HANDLE, const GUID *, PVOID);
__declspec(dllimport) DWORD WINAPI WlanQueryInterface(HANDLE, const GUID *, DWORD, PVOID, PDWORD, PVOID *, PDWORD);
__declspec(dllimport) DWORD WINAPI WlanReasonCodeToString(DWORD, DWORD, PWCHAR, PVOID);
__declspec(dllimport) VOID  WINAPI WlanFreeMemory(PVOID);

#ifndef ERROR_NOT_FOUND
#define ERROR_NOT_FOUND 1168
#endif
#define ACM 8
#define SCAN_COMPLETE 7
#define CONNECTION_COMPLETE 10
#define ATTEMPT_FAIL 11
#define DISCONNECTED 21
#define KEY_MISMATCH 0x2800D

static int g_pass, g_fail;

static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("%s: ", ok ? "pass" : "FAIL");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    if (ok) g_pass++; else g_fail++;
}

/* The notifications so far (the callback runs on wlanapi's thread) */
static CRITICAL_SECTION g_cs;
static HANDLE g_event;
static DWORD g_codes[64], g_reasons[64];
static int g_ncodes;

static void WINAPI on_notify(NOTIFY_ *d, PVOID ctx)
{
    EnterCriticalSection(&g_cs);
    if (d->NotificationSource == ACM && g_ncodes < 64 && ctx == (PVOID)0x1234) {
        g_codes[g_ncodes] = d->NotificationCode;
        g_reasons[g_ncodes] = d->dwDataSize >= sizeof(CONNECTION_NOTIFY_) - sizeof(WCHAR) && d->pData ?
                              ((CONNECTION_NOTIFY_ *)d->pData)->wlanReasonCode : 0;
        g_ncodes++;
    }
    LeaveCriticalSection(&g_cs);
    SetEvent(g_event);
}

/* Wait (at most @ms) for notification @code after the first @from; its reason code, or -1 */
static long wait_for(int from, DWORD code, DWORD ms)
{
    DWORD start = GetTickCount();
    for (;;) {
        EnterCriticalSection(&g_cs);
        for (int i = from; i < g_ncodes; i++)
            if (g_codes[i] == code) { long r = (long)g_reasons[i]; LeaveCriticalSection(&g_cs); return r; }
        LeaveCriticalSection(&g_cs);
        DWORD gone = GetTickCount() - start;
        if (gone >= ms) return -1;
        WaitForSingleObject(g_event, ms - gone < 250 ? ms - gone : 250);
    }
}

static int seen(int from, DWORD code)
{
    EnterCriticalSection(&g_cs);
    int hit = 0;
    for (int i = from; i < g_ncodes; i++) if (g_codes[i] == code) hit = 1;
    LeaveCriticalSection(&g_cs);
    return hit;
}

static int now(void) { EnterCriticalSection(&g_cs); int n = g_ncodes; LeaveCriticalSection(&g_cs); return n; }

static const NETWORK_ *network(const NETWORK_LIST_ *l, const char *ssid)
{
    for (DWORD i = 0; l && i < l->dwNumberOfItems; i++)
        if (l->Network[i].dot11Ssid.uSSIDLength == strlen(ssid) && !memcmp(l->Network[i].dot11Ssid.ucSSID, ssid, strlen(ssid)))
            return &l->Network[i];
    return NULL;
}

static DOT11_SSID_ ssid_of(const char *s)
{
    DOT11_SSID_ d;
    memset(&d, 0, sizeof(d));
    d.uSSIDLength = (ULONG)strlen(s);
    memcpy(d.ucSSID, s, d.uSSIDLength);
    return d;
}

static void connected_to(HANDLE h, const GUID *g, const char *ssid, const UCHAR *bssid, const wchar_t *profile)
{
    DWORD size = 0, type = 0;
    DWORD *state = NULL;
    DWORD e = WlanQueryInterface(h, g, 6, NULL, &size, (PVOID *)&state, &type);
    check(!e && size == 4 && state && *state == 1, "WlanQueryInterface(interface_state): connected (%lu, error %lu)",
          state ? *state : 99, e);
    if (state) WlanFreeMemory(state);
    CONNECTION_ATTRIBUTES_ *a = NULL;
    e = WlanQueryInterface(h, g, 7, NULL, &size, (PVOID *)&a, &type);
    check(!e && a && size == sizeof(*a) && a->isState == 1 && a->assoc.dot11Ssid.uSSIDLength == strlen(ssid) &&
          !memcmp(a->assoc.dot11Ssid.ucSSID, ssid, strlen(ssid)) && !memcmp(a->assoc.dot11Bssid, bssid, 6),
          "WlanQueryInterface(current_connection): %s at %02x:%02x:%02x:%02x:%02x:%02x (error %lu)", ssid,
          a ? a->assoc.dot11Bssid[0] : 0, a ? a->assoc.dot11Bssid[1] : 0, a ? a->assoc.dot11Bssid[2] : 0,
          a ? a->assoc.dot11Bssid[3] : 0, a ? a->assoc.dot11Bssid[4] : 0, a ? a->assoc.dot11Bssid[5] : 0, e);
    if (a) {
        check(a->sec.bSecurityEnabled && a->sec.dot11AuthAlgorithm == 7 && a->sec.dot11CipherAlgorithm == 4 &&
              a->assoc.dot11PhyType == 7 && a->assoc.wlanSignalQuality == 90,
              "... WPA2-Personal (auth %lu) with CCMP (%lu), 802.11n, signal %lu", a->sec.dot11AuthAlgorithm,
              a->sec.dot11CipherAlgorithm, a->assoc.wlanSignalQuality);
        if (profile) check(!wcscmp(a->strProfileName, profile) && a->wlanConnectionMode == 0, "... profile \"%ls\"",
                           a->strProfileName);
        WlanFreeMemory(a);
    }
    DWORD *ch = NULL;
    e = WlanQueryInterface(h, g, 8, NULL, &size, (PVOID *)&ch, &type);
    check(!e && ch && *ch == 6, "WlanQueryInterface(channel_number): %lu", ch ? *ch : 0);
    if (ch) WlanFreeMemory(ch);
}

static const UCHAR NOVANET[6] = { 0x02, 0x4e, 0x6f, 0x76, 0x61, 0x01 };
static const wchar_t PROFILE[] =
    L"<?xml version=\"1.0\"?>\r\n"
    L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\r\n"
    L"  <name>NovaNet</name>\r\n"
    L"  <SSIDConfig><SSID><name>NovaNet</name></SSID></SSIDConfig>\r\n"
    L"  <connectionType>ESS</connectionType><connectionMode>auto</connectionMode>\r\n"
    L"  <MSM><security>\r\n"
    L"    <authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption>"
    L"<useOneX>false</useOneX></authEncryption>\r\n"
    L"    <sharedKey><keyType>passPhrase</keyType><protected>false</protected>"
    L"<keyMaterial>nova-wpa2-passphrase</keyMaterial></sharedKey>\r\n"
    L"  </security></MSM>\r\n"
    L"</WLANProfile>\r\n";
static const wchar_t WRONG[] =
    L"<WLANProfile><name>NovaNet wrong</name><SSIDConfig><SSID><name>NovaNet</name></SSID></SSIDConfig>"
    L"<MSM><security><sharedKey><keyType>passPhrase</keyType><protected>false</protected>"
    L"<keyMaterial>not-the-passphrase</keyMaterial></sharedKey></security></MSM></WLANProfile>";

static int settings_test(HANDLE h, const GUID *g)
{
    printf("wlantest: join NovaNet in Settings\n");
    fflush(stdout);
    int up = 0;
    for (int i = 0; i < 240 && !up; i++) {
        DWORD size, *state = NULL;
        if (!WlanQueryInterface(h, g, 6, NULL, &size, (PVOID *)&state, NULL) && state) up = *state == 1;
        if (state) WlanFreeMemory(state);
        if (!up) Sleep(500);
    }
    check(up, "Settings connected the adapter");
    printf("wlantest: connected through Settings\n");
    fflush(stdout);
    if (up) connected_to(h, g, "NovaNet", NOVANET, NULL);
    int from = now();
    DWORD e = WlanDisconnect(h, g, NULL);
    check(!e && wait_for(from, DISCONNECTED, 2000) >= 0, "WlanDisconnect (error %lu)", e);
    WlanCloseHandle(h, NULL);
    printf("wlantest settings: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}

int main(int argc, char **argv)
{
    InitializeCriticalSection(&g_cs);
    g_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE h = NULL;
    DWORD ver = 0, e;
    check(WlanOpenHandle(3, NULL, &ver, &h) == ERROR_INVALID_PARAMETER, "WlanOpenHandle: client version 3 refused");
    e = WlanOpenHandle(2, NULL, &ver, &h);
    check(!e && h && ver == 2, "WlanOpenHandle: version %lu (error %lu)", ver, e);
    if (e) { printf("wlantest: %d passed, %d failed\n", g_pass, g_fail); return 1; }

    IF_LIST_ *ifs = NULL;
    e = WlanEnumInterfaces(h, NULL, &ifs);
    check(!e && ifs && ifs->dwNumberOfItems == 1, "WlanEnumInterfaces: %lu interface(s) (error %lu)",
          ifs ? ifs->dwNumberOfItems : 0, e);
    if (e || !ifs || !ifs->dwNumberOfItems) { printf("wlantest: %d passed, %d failed\n", g_pass, g_fail); return 1; }
    GUID g = ifs->InterfaceInfo[0].InterfaceGuid;
    static const GUID want = { 0x4E4F5641, 0x574C, 0x4E00, { 0x80, 0x00, 0x02, 0x4e, 0x6f, 0x76, 0x61, 0x00 } };
    check(!wcscmp(ifs->InterfaceInfo[0].strInterfaceDescription, L"NovaOS WDI test Wi-Fi adapter") &&
          ifs->InterfaceInfo[0].isState == 4 && !memcmp(&g, &want, sizeof(g)),
          "... \"%ls\", disconnected (%lu), GUID from its MAC", ifs->InterfaceInfo[0].strInterfaceDescription,
          ifs->InterfaceInfo[0].isState);
    WlanFreeMemory(ifs);

    DWORD prev = 0;
    e = WlanRegisterNotification(h, ACM, FALSE, on_notify, (PVOID)0x1234, NULL, &prev);
    check(!e && prev == 0, "WlanRegisterNotification(ACM) (error %lu)", e);
    if (argc > 1 && !strcmp(argv[1], "settings")) return settings_test(h, &g);

    int from = now();
    e = WlanScan(h, &g, NULL, NULL, NULL);
    check(!e && wait_for(from, SCAN_COMPLETE, 20000) >= 0, "WlanScan: scan_complete (error %lu)", e);

    NETWORK_LIST_ *nl = NULL;
    e = WlanGetAvailableNetworkList(h, &g, 0, NULL, &nl);
    check(!e && nl && nl->dwNumberOfItems == 3, "WlanGetAvailableNetworkList: %lu networks (error %lu)",
          nl ? nl->dwNumberOfItems : 0, e);
    const NETWORK_ *n1 = network(nl, "NovaNet"), *n2 = network(nl, "NovaNet-5G"), *n3 = network(nl, "NovaGuest");
    check(n1 && n1->bSecurityEnabled && n1->dot11DefaultAuthAlgorithm == 7 && n1->dot11DefaultCipherAlgorithm == 4 &&
          n1->wlanSignalQuality == 90 && n1->uNumberOfBssids == 1 && n1->dot11PhyTypes[0] == 7 && n1->bNetworkConnectable &&
          n1->dot11BssType == 1 && !n1->dwFlags,
          "NovaNet: WPA2-Personal (7) with CCMP (4), signal 90, 802.11n, connectable, no profile yet");
    check(n2 && n2->dot11DefaultAuthAlgorithm == 9 && n2->dot11DefaultCipherAlgorithm == 4 && n2->wlanSignalQuality == 72 &&
          n2->dot11PhyTypes[0] == 10, "NovaNet-5G: WPA3-Personal (SAE, 9), signal 72, 802.11ax");
    check(n3 && !n3->bSecurityEnabled && n3->dot11DefaultAuthAlgorithm == 1 && n3->dot11DefaultCipherAlgorithm == 0 &&
          n3->wlanSignalQuality == 35, "NovaGuest: open (1), no cipher, signal 35");
    if (nl) WlanFreeMemory(nl);

    BSS_LIST_ *bl = NULL;
    e = WlanGetNetworkBssList(h, &g, NULL, 3, FALSE, NULL, &bl);
    check(!e && bl && bl->dwNumberOfItems == 3, "WlanGetNetworkBssList: %lu access points (error %lu)",
          bl ? bl->dwNumberOfItems : 0, e);
    int found = 0;
    for (DWORD i = 0; bl && i < bl->dwNumberOfItems; i++) {
        BSS_ENTRY_ *b = &bl->wlanBssEntries[i];
        if (b->dot11Bssid[5] == 2 && b->ulChCenterFrequency == 5180000 && b->uLinkQuality == 72 && b->lRssi < 0 &&
            (b->usCapabilityInformation & 0x10)) found++;
    }
    check(found == 1, "... NovaNet-5G's at 02:4e:6f:76:61:02, 5180 MHz (channel 36), Privacy");
    if (bl) WlanFreeMemory(bl);
    DOT11_SSID_ guest = ssid_of("NovaGuest");
    bl = NULL;
    e = WlanGetNetworkBssList(h, &g, &guest, 1, FALSE, NULL, &bl);
    check(!e && bl && bl->dwNumberOfItems == 1 && bl->wlanBssEntries[0].ulChCenterFrequency == 2462000,
          "WlanGetNetworkBssList(NovaGuest, unsecured): one, 2462 MHz");
    if (bl) WlanFreeMemory(bl);

    DWORD reason = 1;
    e = WlanSetProfile(h, &g, 0, PROFILE, NULL, TRUE, NULL, &reason);
    check(!e && !reason, "WlanSetProfile(NovaNet) (error %lu, reason %lx)", e, reason);
    PROFILE_LIST_ *pl = NULL;
    e = WlanGetProfileList(h, &g, NULL, &pl);
    int has = 0;
    for (DWORD i = 0; pl && i < pl->dwNumberOfItems; i++) if (!wcscmp(pl->ProfileInfo[i].strProfileName, L"NovaNet")) has = 1;
    check(!e && has, "WlanGetProfileList: NovaNet");
    if (pl) WlanFreeMemory(pl);
    LPWSTR xml = NULL;
    DWORD flags = 9;
    e = WlanGetProfile(h, &g, L"NovaNet", NULL, &xml, &flags, NULL);
    check(!e && xml && wcsstr(xml, L"<keyMaterial>nova-wpa2-passphrase</keyMaterial>") && flags == 0,
          "WlanGetProfile(NovaNet): its XML (error %lu)", e);
    if (xml) WlanFreeMemory(xml);
    nl = NULL;
    e = WlanGetAvailableNetworkList(h, &g, 0, NULL, &nl);
    n1 = network(nl, "NovaNet");
    check(!e && n1 && (n1->dwFlags & 2) && !wcscmp(n1->strProfileName, L"NovaNet"), "... NovaNet now has its profile");
    if (nl) WlanFreeMemory(nl);

    CONNECT_PARAMS_ p;
    memset(&p, 0, sizeof(p));
    p.wlanConnectionMode = 1;                                   /* (a temporary profile) */
    p.strProfile = WRONG;
    p.dot11BssType = 1;
    from = now();
    e = WlanConnect(h, &g, &p, NULL);
    long r = e ? -1 : wait_for(from, CONNECTION_COMPLETE, 60000);
    check(!e && r == KEY_MISMATCH && seen(from, ATTEMPT_FAIL),
          "WlanConnect(a wrong passphrase): connection_attempt_fail, complete with reason %lx (error %lu)", r, e);
    WCHAR text[128];
    check(!WlanReasonCodeToString(KEY_MISMATCH, 128, text, NULL) && wcsstr(text, L"key"), "WlanReasonCodeToString: \"%ls\"",
          text);

    p.wlanConnectionMode = 0;                                   /* (the saved profile) */
    p.strProfile = L"NovaNet";
    from = now();
    e = WlanConnect(h, &g, &p, NULL);
    r = e ? -1 : wait_for(from, CONNECTION_COMPLETE, 60000);
    check(!e && r == 0 && !seen(from, ATTEMPT_FAIL), "WlanConnect(profile NovaNet): connection_complete, success "
          "(reason %lx, error %lu)", r, e);
    if (!r) connected_to(h, &g, "NovaNet", NOVANET, L"NovaNet");
    from = now();
    e = WlanDisconnect(h, &g, NULL);
    check(!e && wait_for(from, DISCONNECTED, 2000) >= 0, "WlanDisconnect: disconnected (error %lu)", e);
    DWORD size, *state = NULL;
    e = WlanQueryInterface(h, &g, 6, NULL, &size, (PVOID *)&state, NULL);
    check(!e && state && *state == 4, "... and the interface says so (%lu)", state ? *state : 99);
    if (state) WlanFreeMemory(state);

    memset(&p, 0, sizeof(p));
    p.wlanConnectionMode = 3;                                   /* (discovery, unsecured) */
    p.pDot11Ssid = &guest;
    p.dot11BssType = 1;
    from = now();
    e = WlanConnect(h, &g, &p, NULL);
    r = e ? -1 : wait_for(from, CONNECTION_COMPLETE, 60000);
    check(!e && r == 0, "WlanConnect(NovaGuest by SSID): success (reason %lx, error %lu)", r, e);
    from = now();
    e = WlanDisconnect(h, &g, NULL);
    check(!e && wait_for(from, DISCONNECTED, 2000) >= 0, "WlanDisconnect (error %lu)", e);

    check(!WlanDeleteProfile(h, &g, L"NovaNet", NULL), "WlanDeleteProfile(NovaNet)");
    xml = NULL;
    check(WlanGetProfile(h, &g, L"NovaNet", NULL, &xml, NULL, NULL) == ERROR_NOT_FOUND, "... gone");
    check(!WlanCloseHandle(h, NULL), "WlanCloseHandle");
    check(WlanCloseHandle(h, NULL) == ERROR_INVALID_HANDLE, "... a second time: ERROR_INVALID_HANDLE");
    printf("wlantest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
