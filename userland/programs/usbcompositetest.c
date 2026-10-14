/*
 * usbcompositetest.exe — a composite USB device, one function per driver,
 * as Windows' usbccgp.sys splits one
 *
 * Runs in the devices self-test boot "composite": tools/compositepeer.py
 * (1209:000A) has a boot keyboard on interface 0 and two vendor-defined
 * HID functions on interfaces 1 and 2 (usage page FF00, usages 1 and 2;
 * each echoes an output report as its own input report 1 and keeps its own
 * feature report).  "usbcompositetest" checks, through cfgmgr32 and
 * setupapi as Device Manager reads them:
 *  - the device's node: USB\VID_1209&PID_000A, compatible ID
 *    USB\COMPOSITE, "USB Composite Device", started;
 *  - a child per interface, USB\VID_1209&PID_000A&MI_nn with
 *    USB\VID_1209&PID_000A&REV_0114&MI_nn first and USB\Class_03&...
 *    compatible IDs, each started: interface 0 by NovaOS's keyboard
 *    driver (usbhid) with a HID keyboard under it, interfaces 1 and 2 by
 *    the HID class driver's USB minidriver (HidUsb) with a
 *    HID\VID_1209&PID_000A&MI_nn collection under each;
 *  - both collections in the HID interface list, each path naming its
 *    interface; through hid.dll the right usage on each, reports echoed
 *    on the function written to only, and a feature report per function;
 *  - with "unplug": at "[USB] composite step 1" the test unplugs the
 *    device: a read fails, both interfaces leave the list and the
 *    device's node is no longer present.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <hidsdi.h>

typedef DWORD CONFIGRET;
__declspec(dllimport) CONFIGRET WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Child(PDWORD c, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Sibling(PDWORD s, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_IDW(DWORD dn, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW(DWORD dn, ULONG prop, PULONG type, PVOID buf, PULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW(PULONG len, LPCWSTR filter, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_ListW(LPCWSTR filter, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags);

#define CM_DRP_DEVICEDESC   0x01
#define CM_DRP_HARDWAREID   0x02
#define CM_DRP_COMPATIBLEIDS 0x03
#define CM_DRP_SERVICE      0x05
#define CM_DRP_CLASS        0x08
#define DN_STARTED          0x00000008

#define VID 0x1209
#define PID 0x000A
#define REPORT_LEN 9
#define NFN 3

static int g_pass, g_fail;
static const char *g_name = "usbcompositetest";

static void check(int ok, const char *what, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, what);
    vsnprintf(buf, sizeof(buf), what, ap);
    va_end(ap);
    printf("%s %s\n", ok ? "ok  " : "FAIL", buf);
    fflush(stdout);
    if (ok) g_pass++; else g_fail++;
}

/* A node's property as a string (a list's first entry), or "" */
static void prop(DWORD dn, ULONG which, WCHAR *out, ULONG cap)
{
    ULONG type = 0, len = cap * sizeof(WCHAR);
    out[0] = 0;
    if (CM_Get_DevNode_Registry_PropertyW(dn, which, &type, out, &len, 0)) out[0] = 0;
    out[cap - 1] = 0;
}

/* Whether REG_MULTI_SZ property @which of @dn holds @want (@first: as its first entry) */
static BOOL list_has(DWORD dn, ULONG which, const WCHAR *want, BOOL first)
{
    WCHAR buf[512];
    ULONG type = 0, len = sizeof(buf);
    memset(buf, 0, sizeof(buf));
    if (CM_Get_DevNode_Registry_PropertyW(dn, which, &type, buf, &len, 0)) return FALSE;
    for (WCHAR *p = buf; *p && p < buf + 510; p += lstrlenW(p) + 1) {
        if (!lstrcmpiW(p, want)) return TRUE;
        if (first) return FALSE;
    }
    return FALSE;
}

static BOOL started(DWORD dn, ULONG *problem)
{
    ULONG status = 0;
    *problem = 0;
    return !CM_Get_DevNode_Status(&status, problem, dn, 0) && (status & DN_STARTED) && !*problem;
}

static BOOL prefix(const WCHAR *s, const WCHAR *p)
{
    return !_wcsnicmp(s, p, wcslen(p));
}

/* The composite device's node (its instance ID into @id), or 0 */
static DWORD find_parent(WCHAR *id, int cap)
{
    ULONG len = 0;
    if (CM_Get_Device_ID_List_SizeW(&len, NULL, 0) || len < 2) return 0;
    WCHAR *list = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    DWORD dn = 0;
    if (list && !CM_Get_Device_ID_ListW(NULL, list, len, 0))
        for (WCHAR *p = list; *p && !dn; p += lstrlenW(p) + 1)
            if (prefix(p, L"USB\\VID_1209&PID_000A\\") && !CM_Locate_DevNodeW(&dn, p, 0)) lstrcpynW(id, p, cap);
            else dn = 0;
    HeapFree(GetProcessHeap(), 0, list);
    return dn;
}

/* The HID interface list's paths for the device's functions: by usage (1 or 2) */
static int hid_paths(WCHAR paths[3][300])
{
    GUID g;
    ULONG len = 0;
    int n = 0;
    paths[1][0] = paths[2][0] = 0;
    HidD_GetHidGuid(&g);
    if (CM_Get_Device_Interface_List_SizeW(&len, &g, NULL, 0) || len < 2) return 0;
    WCHAR *list = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (list && !CM_Get_Device_Interface_ListW(&g, NULL, list, len, 0))
        for (WCHAR *p = list; *p; p += lstrlenW(p) + 1) {
            HANDLE h = CreateFileW(p, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (h == INVALID_HANDLE_VALUE) continue;
            HIDD_ATTRIBUTES a = { sizeof(a) };
            PHIDP_PREPARSED_DATA pp;
            HIDP_CAPS caps;
            if (HidD_GetAttributes(h, &a) && a.VendorID == VID && a.ProductID == PID && HidD_GetPreparsedData(h, &pp)) {
                if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 0xFF00 &&
                    (caps.Usage == 1 || caps.Usage == 2)) {
                    lstrcpynW(paths[caps.Usage], p, 300);
                    n++;
                }
                HidD_FreePreparsedData(pp);
            }
            CloseHandle(h);
        }
    HeapFree(GetProcessHeap(), 0, list);
    return n;
}

/* Whether @s holds @sub, ignoring case */
static BOOL has_ci(const WCHAR *s, const WCHAR *sub)
{
    size_t n = wcslen(sub);
    for (; *s; s++) if (!_wcsnicmp(s, sub, n)) return TRUE;
    return FALSE;
}

static HANDLE open_dev(const WCHAR *path)
{
    return CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL send_output(HANDLE h, UCHAR fill)
{
    UCHAR r[REPORT_LEN];
    DWORD n = 0;
    r[0] = 2;
    for (int i = 1; i < REPORT_LEN; i++) r[i] = (UCHAR)(fill + i - 1);
    return WriteFile(h, r, sizeof(r), &n, NULL) && n == sizeof(r);
}

static BOOL is_echo(const UCHAR *r, UCHAR fill)
{
    if (r[0] != 1) return FALSE;
    for (int i = 1; i < REPORT_LEN; i++) if (r[i] != (UCHAR)(fill + i - 1)) return FALSE;
    return TRUE;
}

static BOOL read_echo(HANDLE h, UCHAR fill)
{
    UCHAR r[REPORT_LEN];
    DWORD n = 0;
    memset(r, 0xEE, sizeof(r));
    return ReadFile(h, r, sizeof(r), &n, NULL) && n == sizeof(r) && is_echo(r, fill);
}

/* The tree: the device's node, a child per interface, a collection under each */
static void check_tree(void)
{
    WCHAR id[200], s[200];
    DWORD parent = find_parent(id, 200);
    ULONG problem;
    check(parent != 0, "cfgmgr32 lists the composite device: %ls", parent ? id : L"(none)");
    if (!parent) return;
    check(list_has(parent, CM_DRP_HARDWAREID, L"USB\\VID_1209&PID_000A&REV_0114", TRUE),
          "its first hardware ID is USB\\VID_1209&PID_000A&REV_0114");
    check(list_has(parent, CM_DRP_COMPATIBLEIDS, L"USB\\COMPOSITE", FALSE), "its compatible IDs hold USB\\COMPOSITE");
    prop(parent, CM_DRP_DEVICEDESC, s, 200);
    check(!lstrcmpW(s, L"USB Composite Device"), "its description: %ls", s);
    check(started(parent, &problem), "it is started (problem %lu)", problem);

    BOOL seen[NFN] = { 0 };
    DWORD c;
    int kids = 0;
    for (CONFIGRET cr = CM_Get_Child(&c, parent, 0); !cr; cr = CM_Get_Sibling(&c, c, 0)) {
        kids++;
        CM_Get_Device_IDW(c, id, 200, 0);
        int mi = -1;
        for (int k = 0; k < NFN; k++) {
            WCHAR want[64];
            _snwprintf(want, 64, L"USB\\VID_1209&PID_000A&MI_%02d\\", k);
            if (prefix(id, want)) mi = k;
        }
        check(mi >= 0, "child %ls is one of its interfaces", id);
        if (mi < 0) continue;
        seen[mi] = TRUE;
        WCHAR hw[80], cid[80];
        _snwprintf(hw, 80, L"USB\\VID_1209&PID_000A&REV_0114&MI_%02d", mi);
        check(list_has(c, CM_DRP_HARDWAREID, hw, TRUE), "interface %d: first hardware ID %ls", mi, hw);
        _snwprintf(hw, 80, L"USB\\VID_1209&PID_000A&MI_%02d", mi);
        check(list_has(c, CM_DRP_HARDWAREID, hw, FALSE), "interface %d: hardware ID %ls", mi, hw);
        _snwprintf(cid, 80, mi ? L"USB\\Class_03&SubClass_00&Prot_00" : L"USB\\Class_03&SubClass_01&Prot_01");
        check(list_has(c, CM_DRP_COMPATIBLEIDS, cid, TRUE), "interface %d: first compatible ID %ls", mi, cid);
        check(list_has(c, CM_DRP_COMPATIBLEIDS, L"USB\\Class_03", FALSE), "interface %d: compatible ID USB\\Class_03", mi);
        check(started(c, &problem), "interface %d is started (problem %lu)", mi, problem);
        prop(c, CM_DRP_SERVICE, s, 200);
        check(!lstrcmpiW(s, mi ? L"HidUsb" : L"usbhid"), "interface %d: its driver is %ls", mi, s);

        /* the collection under it */
        DWORD g;
        BOOL coll = !CM_Get_Child(&g, c, 0);
        check(coll, "interface %d has a child", mi);
        if (!coll) continue;
        CM_Get_Device_IDW(g, id, 200, 0);
        _snwprintf(hw, 80, L"HID\\VID_1209&PID_000A&MI_%02d", mi);
        check(prefix(id, hw), "interface %d: its child %ls", mi, id);
        if (mi) {
            _snwprintf(hw, 80, L"HID\\VID_1209&PID_000A&REV_0114&MI_%02d", mi);
            check(list_has(g, CM_DRP_HARDWAREID, hw, TRUE), "interface %d's collection: first hardware ID %ls", mi, hw);
            _snwprintf(hw, 80, L"HID_DEVICE_UP:FF00_U:%04X", mi);
            check(list_has(g, CM_DRP_HARDWAREID, hw, FALSE), "interface %d's collection: hardware ID %ls", mi, hw);
            check(started(g, &problem), "interface %d's collection is started (problem %lu)", mi, problem);
        } else {
            prop(g, CM_DRP_CLASS, s, 200);
            check(!lstrcmpiW(s, L"Keyboard"), "interface 0's child is a keyboard (class %ls)", s);
        }
    }
    check(kids == NFN && seen[0] && seen[1] && seen[2], "a child for each of the %d interfaces (%d)", NFN, kids);
}

int main(int argc, char **argv)
{
    BOOL unplug = argc > 1 && !strcmp(argv[1], "unplug");
    if (unplug) g_name = "usbcompositetest unplug";
    WCHAR paths[3][300], id[200];
    int n = 0;
    for (int i = 0; i < 200 && ((n = hid_paths(paths)) < 2 || !find_parent(id, 200)); i++) Sleep(100);
    check(n == 2, "the HID interface list holds both vendor functions (%d)", n);
    check_tree();
    if (n < 2) goto done;
    check(has_ci(paths[1], L"&mi_01"), "usage FF00:1 is interface 1's: %ls", paths[1]);
    check(has_ci(paths[2], L"&mi_02"), "usage FF00:2 is interface 2's: %ls", paths[2]);

    HANDLE h1 = open_dev(paths[1]), h2 = open_dev(paths[2]);
    check(h1 != INVALID_HANDLE_VALUE && h2 != INVALID_HANDLE_VALUE, "CreateFile on both (error %lu)", GetLastError());
    if (h1 == INVALID_HANDLE_VALUE || h2 == INVALID_HANDLE_VALUE) goto done;
    WCHAR s[128];
    BOOL ok = HidD_GetProductString(h1, s, sizeof(s));
    check(ok && !wcscmp(s, L"NovaOS Test Composite Device"), "HidD_GetProductString: %ls", ok ? s : L"(failed)");

    /* each function's reports are its own */
    check(send_output(h1, 0x10), "WriteFile: an output report to interface 1 (error %lu)", GetLastError());
    check(read_echo(h1, 0x10), "ReadFile: interface 1 echoes it");
    check(send_output(h2, 0x20), "WriteFile: an output report to interface 2 (error %lu)", GetLastError());
    check(read_echo(h2, 0x20), "ReadFile: interface 2 echoes it");
    UCHAR in[REPORT_LEN] = { 1 };
    check(HidD_GetInputReport(h1, in, sizeof(in)) && is_echo(in, 0x10), "HidD_GetInputReport: interface 1 kept its own");
    UCHAR f1[REPORT_LEN] = { 3, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8 };
    UCHAR f2[REPORT_LEN] = { 3, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8 };
    check(HidD_SetFeature(h1, f1, sizeof(f1)) && HidD_SetFeature(h2, f2, sizeof(f2)), "HidD_SetFeature on both");
    UCHAR g1[REPORT_LEN] = { 3 }, g2[REPORT_LEN] = { 3 };
    check(HidD_GetFeature(h1, g1, sizeof(g1)) && !memcmp(g1, f1, sizeof(f1)) &&
          HidD_GetFeature(h2, g2, sizeof(g2)) && !memcmp(g2, f2, sizeof(f2)),
          "HidD_GetFeature: each reads back its own (%02x, %02x)", g1[1], g2[1]);

    if (unplug) {
        printf("[USB] composite step 1: unplug the device\n");
        fflush(stdout);
        UCHAR r[REPORT_LEN];
        DWORD got = 0;
        ok = ReadFile(h1, r, sizeof(r), &got, NULL);
        check(!ok, "a read on interface 1 fails once the device is unplugged (error %lu)", ok ? 0 : GetLastError());
        WCHAR left[3][300];
        int still = 2;
        DWORD dn = 0;
        for (int i = 0; i < 100 && (still || dn); i++) {
            still = hid_paths(left);
            dn = find_parent(id, 200);
            if (still || dn) Sleep(100);
        }
        check(still == 0, "both interfaces left the HID interface list (%d left)", still);
        check(dn == 0, "the device's node is no longer present");
    }
    CloseHandle(h1);
    CloseHandle(h2);
done:
    printf("%s: %d passed, %d failed\n", g_name, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
