/*
 * vhidtest.exe — a third-party KMDF HID minidriver, Microsoft's vhidmini2
 * sample, through the HID class driver as Windows programs see it
 *
 * Runs in the devices self-test boot "vhidmini".  The sample's virtual
 * device is root-enumerated: nothing finds it, a program registers it.
 *   - "vhidtest create" does what devcon install does before it installs
 *     the driver: SetupDiCreateDeviceInfo (class HIDClass, a generated
 *     ROOT\HIDCLASS\NNNN), SetupDiSetDeviceRegistryProperty with the
 *     hardware ID root\VHidMini, SetupDiCallClassInstaller(DIF_REGISTERDEVICE).
 *     The test then installs vhidmini.inf on it with pnputil: mshidkmdf (the
 *     pass-through minidriver MsHidKmdf.inf names) is the function driver
 *     and vhidmini.sys, on KMDF, a lower filter under it.
 *   - "vhidtest" (32- and 64-bit) checks the device node (started, its
 *     filter in LowerFilters), the HID collection the class driver made
 *     for it (setupapi's and cfgmgr32's GUID_DEVINTERFACE_HID lists),
 *     HidD_GetAttributes (DEED:FEED version 0101), the sample's strings,
 *     HidP_GetCaps (usage FF00:1, input 2, output 8, feature 10 bytes), the
 *     feature report (the device's attributes), an output report read back
 *     by HidD_GetInputReport, a WriteFile output report coming back as the
 *     input report the sample's timer completes (every 5 s), the sample's
 *     SET_ATTRIBUTES control code through HidD_SetFeature, and Raw Input
 *     listing the collection as a RIM_TYPEHID device;
 *   - "vhidtest gone": after pnputil /remove-device the device node, its
 *     Enum key and its HID collection are gone.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <hidsdi.h>

typedef DWORD CONFIGRET;
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DWORD dn, ULONG flags);
__declspec(dllimport) HANDLE WINAPI SetupDiGetClassDevsW(const GUID *cls, LPCWSTR enumerator, HWND parent, DWORD flags);
__declspec(dllimport) HANDLE WINAPI SetupDiCreateDeviceInfoList(const GUID *cls, HWND parent);
__declspec(dllimport) BOOL WINAPI SetupDiCreateDeviceInfoW(HANDLE set, LPCWSTR name, const GUID *cls, LPCWSTR desc, HWND parent,
                                                           DWORD flags, PVOID dev);
__declspec(dllimport) BOOL WINAPI SetupDiSetDeviceRegistryPropertyW(HANDLE set, PVOID dev, DWORD prop, const BYTE *buf, DWORD size);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceRegistryPropertyW(HANDLE set, PVOID dev, DWORD prop, PDWORD type, PBYTE buf,
                                                                    DWORD n, PDWORD need);
__declspec(dllimport) BOOL WINAPI SetupDiCallClassInstaller(DWORD dif, HANDLE set, PVOID dev);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceInstanceIdW(HANDLE set, PVOID dev, LPWSTR id, DWORD n, PDWORD need);
__declspec(dllimport) BOOL WINAPI SetupDiEnumDeviceInfo(HANDLE set, DWORD i, PVOID info);
__declspec(dllimport) BOOL WINAPI SetupDiEnumDeviceInterfaces(HANDLE set, PVOID dev, const GUID *cls, DWORD i, PVOID info);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HANDLE set, PVOID iface, PVOID detail, DWORD n, PDWORD need, PVOID dev);
__declspec(dllimport) BOOL WINAPI SetupDiDestroyDeviceInfoList(HANDLE set);
typedef struct { DWORD cbSize; GUID InterfaceClassGuid; DWORD Flags; ULONG_PTR Reserved; } SP_DEVICE_INTERFACE_DATA;
typedef struct { DWORD cbSize; GUID ClassGuid; DWORD DevInst; ULONG_PTR Reserved; } SP_DEVINFO_DATA;
typedef struct { DWORD cbSize; WCHAR DevicePath[1]; } SP_DEVICE_INTERFACE_DETAIL_DATA_W;
#define DIGCF_PRESENT         0x02
#define DIGCF_DEVICEINTERFACE 0x10
#define DICD_GENERATE_ID      0x01
#define DIF_REGISTERDEVICE    0x19
#define SPDRP_HARDWAREID      0x01
#define SPDRP_SERVICE         0x04

static const GUID GUID_DEVCLASS_HIDCLASS_ = { 0x745a17a0, 0x74d3, 0x11d0, { 0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda } };

#define VID 0xDEED
#define PID 0xFEED
#define HWID L"root\\VHidMini"

static int g_pass, g_fail;
static const char *g_name = "vhidtest";

static void check(int ok, const char *what, ...)
{
    char buf[300];
    va_list ap;
    va_start(ap, what);
    vsnprintf(buf, sizeof(buf), what, ap);
    va_end(ap);
    printf("%s %s\n", ok ? "ok  " : "FAIL", buf);
    fflush(stdout);
    if (ok) g_pass++; else g_fail++;
}

static int done(void)
{
    printf("%s: %d passed, %d failed\n", g_name, g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/* The registered device: its instance ID (into @id), or FALSE */
static BOOL find_node(WCHAR *id, int cap, BOOL present_only)
{
    HANDLE set = SetupDiGetClassDevsW(&GUID_DEVCLASS_HIDCLASS_, L"ROOT", NULL, present_only ? DIGCF_PRESENT : 0);
    if (set == INVALID_HANDLE_VALUE) return FALSE;
    BOOL found = FALSE;
    for (DWORD i = 0; !found; i++) {
        SP_DEVINFO_DATA d = { sizeof(d) };
        if (!SetupDiEnumDeviceInfo(set, i, &d)) break;
        WCHAR hw[200];
        memset(hw, 0, sizeof(hw));
        if (!SetupDiGetDeviceRegistryPropertyW(set, &d, SPDRP_HARDWAREID, NULL, (PBYTE)hw, sizeof(hw) - 4, NULL)) continue;
        if (_wcsicmp(hw, HWID)) continue;
        found = SetupDiGetDeviceInstanceIdW(set, &d, id, cap, NULL);
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

/* "vhidtest create": a root-enumerated device with hardware ID root\VHidMini */
static int create(void)
{
    g_name = "vhidtest create";
    WCHAR id[200];
    if (find_node(id, 200, FALSE)) {
        printf("vhidtest: %ls is registered already\n", id);
        return done();
    }
    HANDLE set = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_HIDCLASS_, NULL);
    check(set != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList");
    if (set == INVALID_HANDLE_VALUE) return done();
    SP_DEVINFO_DATA d = { sizeof(d) };
    BOOL ok = SetupDiCreateDeviceInfoW(set, L"HIDClass", &GUID_DEVCLASS_HIDCLASS_, NULL, NULL, DICD_GENERATE_ID, &d);
    check(ok, "SetupDiCreateDeviceInfo (class HIDClass, a generated ID) (error %lx)", ok ? 0 : GetLastError());
    static const WCHAR hw[] = HWID L"\0";
    ok = ok && SetupDiSetDeviceRegistryPropertyW(set, &d, SPDRP_HARDWAREID, (const BYTE *)hw, sizeof(hw));
    check(ok, "SetupDiSetDeviceRegistryProperty(SPDRP_HARDWAREID, %ls)", HWID);
    ok = ok && SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &d);
    check(ok, "SetupDiCallClassInstaller(DIF_REGISTERDEVICE) (error %lx)", ok ? 0 : GetLastError());
    if (ok && SetupDiGetDeviceInstanceIdW(set, &d, id, 200, NULL)) printf("vhidtest: registered %ls\n", id);
    SetupDiDestroyDeviceInfoList(set);
    DWORD dn;
    ULONG status = 0, problem = 0;
    ok = ok && find_node(id, 200, TRUE) && !CM_Locate_DevNodeW(&dn, id, 0) && !CM_Get_DevNode_Status(&status, &problem, dn, 0);
    check(ok && problem == 28, "the device node is present with no driver yet (problem %lu)", problem);
    return done();
}

/* The sample's HID collection: its interface path through setupapi */
static BOOL find_collection(WCHAR *path, int cap)
{
    GUID g;
    HidD_GetHidGuid(&g);
    HANDLE set = SetupDiGetClassDevsW(&g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return FALSE;
    BOOL found = FALSE;
    for (DWORD i = 0; !found; i++) {
        SP_DEVICE_INTERFACE_DATA d = { sizeof(d) };
        if (!SetupDiEnumDeviceInterfaces(set, NULL, &g, i, &d)) break;
        union { SP_DEVICE_INTERFACE_DETAIL_DATA_W d; char b[600]; } u;
        u.d.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &d, &u.d, sizeof(u), NULL, NULL)) continue;
        HANDLE h = CreateFileW(u.d.DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES a = { sizeof(a) };
        if (HidD_GetAttributes(h, &a) && a.VendorID == VID && a.ProductID == PID) {
            lstrcpynW(path, u.d.DevicePath, cap);
            found = TRUE;
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

static BOOL cm_lists(const WCHAR *path)
{
    GUID g;
    ULONG len = 0;
    HidD_GetHidGuid(&g);
    if (CM_Get_Device_Interface_List_SizeW(&len, &g, NULL, 0) || len < 2) return FALSE;
    WCHAR *list = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    BOOL found = FALSE;
    if (list && !CM_Get_Device_Interface_ListW(&g, NULL, list, len, 0))
        for (WCHAR *p = list; *p; p += lstrlenW(p) + 1)
            if (!lstrcmpiW(p, path)) found = TRUE;
    HeapFree(GetProcessHeap(), 0, list);
    return found;
}

/* A REG_MULTI_SZ value of the device's Enum key holds @want */
static BOOL enum_multi_has(const WCHAR *id, const WCHAR *name, const WCHAR *want)
{
    WCHAR k[300], v[256];
    HKEY h;
    swprintf(k, 300, L"SYSTEM\\CurrentControlSet\\Enum\\%ls", id);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &h)) return FALSE;
    DWORD type = 0, n = sizeof(v) - 2 * sizeof(WCHAR);
    memset(v, 0, sizeof(v));
    LONG e = RegQueryValueExW(h, name, NULL, &type, (BYTE *)v, &n);
    RegCloseKey(h);
    if (e || type != REG_MULTI_SZ) return FALSE;
    for (WCHAR *p = v; *p; p += wcslen(p) + 1) if (!_wcsicmp(p, want)) return TRUE;
    return FALSE;
}

static BOOL enum_key_exists(const WCHAR *id)
{
    WCHAR k[300];
    HKEY h;
    swprintf(k, 300, L"SYSTEM\\CurrentControlSet\\Enum\\%ls", id);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &h)) return FALSE;
    RegCloseKey(h);
    return TRUE;
}

/* The Raw Input device whose RIDI_DEVICEINFO is the sample's, or NULL */
static HANDLE raw_find(RID_DEVICE_INFO *ri)
{
    RAWINPUTDEVICELIST l[32];
    UINT n = 32;
    UINT got = GetRawInputDeviceList(l, &n, sizeof(l[0]));
    if (got == (UINT)-1) return NULL;
    for (UINT i = 0; i < got; i++) {
        if (l[i].dwType != RIM_TYPEHID) continue;
        UINT len = sizeof(*ri);
        memset(ri, 0, sizeof(*ri));
        ri->cbSize = sizeof(*ri);
        if (GetRawInputDeviceInfoW(l[i].hDevice, RIDI_DEVICEINFO, ri, &len) != sizeof(*ri)) continue;
        if (ri->hid.dwVendorId == VID && ri->hid.dwProductId == PID) return l[i].hDevice;
    }
    return NULL;
}

/* The device's attributes through the sample's feature report (ID 1) */
static BOOL get_feature(HANDLE h, USHORT *vid, USHORT *pid, USHORT *ver)
{
    UCHAR f[10];
    memset(f, 0, sizeof(f));
    f[0] = 1;
    if (!HidD_GetFeature(h, f, sizeof(f))) return FALSE;
    *vid = (USHORT)(f[1] | f[2] << 8);
    *pid = (USHORT)(f[3] | f[4] << 8);
    *ver = (USHORT)(f[5] | f[6] << 8);
    return TRUE;
}

/* The sample's HIDMINI_CONTROL_CODE_SET_ATTRIBUTES control code: feature
 * report 1 carrying the control code and new attributes */
static BOOL set_attributes(HANDLE h, USHORT vid, USHORT pid, USHORT ver)
{
    UCHAR f[10] = { 1, 0, (UCHAR)vid, (UCHAR)(vid >> 8), (UCHAR)pid, (UCHAR)(pid >> 8), (UCHAR)ver, (UCHAR)(ver >> 8), 0, 0 };
    return HidD_SetFeature(h, f, sizeof(f));
}

static int full(void)
{
    WCHAR id[200], path[300];
    DWORD dn = 0;
    ULONG status = 0, problem = 0;
    BOOL ok = find_node(id, 200, TRUE);
    check(ok, "setupapi lists the root-enumerated device (HIDClass, %ls)", HWID);
    if (!ok) return done();
    printf("     %ls\n", id);
    ok = !CM_Locate_DevNodeW(&dn, id, 0) && !CM_Get_DevNode_Status(&status, &problem, dn, 0);
    check(ok && !problem, "the device is started (problem %lu)", problem);
    check(enum_multi_has(id, L"LowerFilters", L"vhidmini"), "its Enum key's LowerFilters names vhidmini");

    BOOL found = FALSE;
    for (int i = 0; i < 100 && !(found = find_collection(path, 300)); i++) Sleep(100);
    check(found, "setupapi lists its HID collection (DEED:FEED)");
    if (!found) return done();
    printf("     %ls\n", path);
    check(cm_lists(path), "CM_Get_Device_Interface_List lists the same path");
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    check(h != INVALID_HANDLE_VALUE, "CreateFile on the path (error %lu)", h == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    if (h == INVALID_HANDLE_VALUE) return done();

    HIDD_ATTRIBUTES a = { sizeof(a) };
    ok = HidD_GetAttributes(h, &a);
    check(ok && a.VendorID == VID && a.ProductID == PID && a.VersionNumber == 0x0101,
          "HidD_GetAttributes: %04x:%04x version %04x", a.VendorID, a.ProductID, a.VersionNumber);
    WCHAR s[128];
    memset(s, 0, sizeof(s));
    ok = HidD_GetManufacturerString(h, s, sizeof(s));
    check(ok && !wcscmp(s, L"UMDF Virtual hidmini device Manufacturer string"), "HidD_GetManufacturerString: %ls", s);
    memset(s, 0, sizeof(s));
    ok = HidD_GetProductString(h, s, sizeof(s));
    check(ok && !wcscmp(s, L"UMDF Virtual hidmini device Product string"), "HidD_GetProductString: %ls", s);
    memset(s, 0, sizeof(s));
    ok = HidD_GetSerialNumberString(h, s, sizeof(s));
    check(ok && !wcscmp(s, L"UMDF Virtual hidmini device Serial Number string"), "HidD_GetSerialNumberString: %ls", s);
    memset(s, 0, sizeof(s));
    ok = HidD_GetIndexedString(h, 5, s, sizeof(s));
    check(ok && !wcscmp(s, L"UMDF Virtual hidmini device"), "HidD_GetIndexedString(5): %ls", s);

    PHIDP_PREPARSED_DATA pp = NULL;
    HIDP_CAPS caps;
    memset(&caps, 0, sizeof(caps));
    check(HidD_GetPreparsedData(h, &pp) && HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS, "HidD_GetPreparsedData and HidP_GetCaps");
    check(caps.UsagePage == 0xFF00 && caps.Usage == 1 && caps.InputReportByteLength == 2 && caps.OutputReportByteLength == 8 &&
          caps.FeatureReportByteLength == 10, "caps: usage %04x:%04x, input %u, output %u, feature %u bytes", caps.UsagePage,
          caps.Usage, caps.InputReportByteLength, caps.OutputReportByteLength, caps.FeatureReportByteLength);
    if (pp) HidD_FreePreparsedData(pp);

    USHORT v = 0, p = 0, r = 0;
    ok = get_feature(h, &v, &p, &r);
    check(ok && v == VID && p == PID && r == 0x0101, "HidD_GetFeature(1): the device's attributes %04x:%04x version %04x", v, p, r);

    UCHAR out[8] = { 1, 0x42 }, in[2] = { 1, 0 };
    ok = HidD_SetOutputReport(h, out, sizeof(out));
    check(ok, "HidD_SetOutputReport(1, 0x42) (error %lu)", ok ? 0 : GetLastError());
    ok = HidD_GetInputReport(h, in, sizeof(in));
    check(ok && in[0] == 1 && in[1] == 0x42, "HidD_GetInputReport(1): %02x %02x", in[0], in[1]);

    /* WriteFile: the sample keeps the byte, and its timer completes the
     * next read with it (every 5 s) */
    HidD_FlushQueue(h);
    UCHAR w[8] = { 1, 0x5A };
    DWORD n = 0;
    ok = WriteFile(h, w, sizeof(w), &n, NULL) && n == sizeof(w);
    check(ok, "WriteFile: output report 1, 0x5a (error %lu)", ok ? 0 : GetLastError());
    BOOL echoed = FALSE;
    DWORD t0 = GetTickCount();
    for (int i = 0; i < 4 && !echoed; i++) {
        UCHAR rd[2] = { 0, 0 };
        n = 0;
        if (!ReadFile(h, rd, sizeof(rd), &n, NULL) || n != sizeof(rd)) break;
        echoed = rd[0] == 1 && rd[1] == 0x5A;
    }
    check(echoed, "ReadFile: input report 1, 0x5a from the sample's timer (%lu ms)", GetTickCount() - t0);

    ok = set_attributes(h, VID, 0xFEEE, 0x0202) && get_feature(h, &v, &p, &r);
    check(ok && v == VID && p == 0xFEEE && r == 0x0202, "HidD_SetFeature: SET_ATTRIBUTES control code, now %04x:%04x version %04x",
          v, p, r);
    ok = set_attributes(h, VID, PID, 0x0101) && get_feature(h, &v, &p, &r);
    check(ok && p == PID && r == 0x0101, "HidD_SetFeature: attributes back to %04x:%04x version %04x", v, p, r);
    CloseHandle(h);

    RID_DEVICE_INFO ri;
    HANDLE raw = NULL;
    for (int i = 0; i < 50 && !(raw = raw_find(&ri)); i++) Sleep(100);
    check(raw && ri.hid.usUsagePage == 0xFF00 && ri.hid.usUsage == 1 && ri.hid.dwVersionNumber == 0x0101,
          "Raw Input: a RIM_TYPEHID device %04x:%04x, usage %04x:%04x", raw ? ri.hid.dwVendorId : 0, raw ? ri.hid.dwProductId : 0,
          raw ? ri.hid.usUsagePage : 0, raw ? ri.hid.usUsage : 0);
    return done();
}

/* "vhidtest gone": after pnputil /remove-device */
static int gone(void)
{
    g_name = "vhidtest gone";
    WCHAR id[200], path[300];
    DWORD dn;
    check(!find_node(id, 200, FALSE), "setupapi lists no root-enumerated %ls device", HWID);
    check(CM_Locate_DevNodeW(&dn, L"ROOT\\HIDCLASS\\0000", 0) != 0, "ROOT\\HIDCLASS\\0000 is not present");
    check(!enum_key_exists(L"ROOT\\HIDCLASS\\0000"), "its Enum key is gone");
    check(!find_collection(path, 300), "no HID collection DEED:FEED");
    return done();
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "create")) return create();
    if (argc > 1 && !strcmp(argv[1], "gone")) return gone();
    return full();
}
