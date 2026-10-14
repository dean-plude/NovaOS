/*
 * hidclasstest.exe — a HID collection the kernel's HID class driver runs,
 * through setupapi and hid.dll as a Windows program opens one
 *
 * Runs in the devices self-test boot "hid": a vendor-defined USB HID
 * device (tools/padpeer.py --kind vendor, 1209:0008) that NovaOS's own
 * drivers leave alone, so the HID class driver (kernel/nt/hidclass.c)
 * runs it on its USB minidriver (hidusb.c).  The device has one
 * collection (usage page FF00, usage 1) with input report 1, output report
 * 2 and feature report 3 of 8 bytes; it sends output reports back as
 * input report 1 and keeps the feature report.  "hidclasstest" checks:
 *  - setupapi's and cfgmgr32's GUID_DEVINTERFACE_HID lists holding its
 *    interface, and CreateFile on the path;
 *  - HidD_GetAttributes, the manufacturer, product, serial number and
 *    indexed strings, HidD_GetPreparsedData and HidP_GetCaps, and
 *    IOCTL_HID_GET_COLLECTION_INFORMATION through DeviceIoControl;
 *  - a feature report set and read back (HidD_SetFeature, GetFeature);
 *  - output reports by WriteFile and HidD_SetOutputReport, each read back
 *    by ReadFile as the input report the device echoes, on two handles
 *    at once; HidD_GetInputReport;
 *  - the input queue: HidD_Get/SetNumInputBuffers, HidD_FlushQueue;
 *  - with "unplug": at "[HID] step 1" the test unplugs the device; a
 *    read then fails and the interface leaves the lists.
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
__declspec(dllimport) HANDLE WINAPI SetupDiGetClassDevsW(const GUID *cls, LPCWSTR enumerator, HWND parent, DWORD flags);
__declspec(dllimport) BOOL WINAPI SetupDiEnumDeviceInterfaces(HANDLE set, PVOID dev, const GUID *cls, DWORD i, PVOID info);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HANDLE set, PVOID iface, PVOID detail, DWORD n, PDWORD need, PVOID dev);
__declspec(dllimport) BOOL WINAPI SetupDiDestroyDeviceInfoList(HANDLE set);
WINBASEAPI BOOL WINAPI DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_n, LPVOID out, DWORD out_n,
                                       LPDWORD ret, LPOVERLAPPED ov);
typedef struct { DWORD cbSize; GUID InterfaceClassGuid; DWORD Flags; ULONG_PTR Reserved; } SP_DEVICE_INTERFACE_DATA;
typedef struct { DWORD cbSize; GUID ClassGuid; DWORD DevInst; ULONG_PTR Reserved; } SP_DEVINFO_DATA;
typedef struct { DWORD cbSize; WCHAR DevicePath[1]; } SP_DEVICE_INTERFACE_DETAIL_DATA_W;
#define DIGCF_PRESENT         0x02
#define DIGCF_DEVICEINTERFACE 0x10

/* IOCTL_HID_GET_COLLECTION_INFORMATION and what it fills */
#define IOCTL_HID_GET_COLLECTION_INFORMATION ((0x0B << 16) | (106 << 2) | 0)
typedef struct {
    ULONG   DescriptorSize;
    BOOLEAN Polled;
    UCHAR   Reserved1[1];
    USHORT  VendorID, ProductID, VersionNumber;
} HID_COLLECTION_INFORMATION;

#define VID 0x1209
#define PID 0x0008
#define REPORT_LEN 9

static int g_pass, g_fail;
static const char *g_name = "hidclasstest";

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

/* The test device's interface path through setupapi (into @path), or FALSE */
static BOOL find_device(WCHAR *path, int cap)
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

/* Whether cfgmgr32's HID interface list holds @path */
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

static HANDLE open_dev(const WCHAR *path)
{
    return CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

/* An output report of @fill bytes, by WriteFile or HidD_SetOutputReport */
static BOOL send_output(HANDLE h, UCHAR fill, BOOL by_write)
{
    UCHAR r[REPORT_LEN];
    r[0] = 2;
    for (int i = 1; i < REPORT_LEN; i++) r[i] = (UCHAR)(fill + i - 1);
    if (!by_write) return HidD_SetOutputReport(h, r, sizeof(r));
    DWORD n = 0;
    return WriteFile(h, r, sizeof(r), &n, NULL) && n == sizeof(r);
}

/* Whether @r is input report 1 carrying what send_output(@fill) sent */
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

int main(int argc, char **argv)
{
    BOOL unplug = argc > 1 && !strcmp(argv[1], "unplug");
    if (unplug) g_name = "hidclasstest unplug";
    WCHAR path[300];
    BOOL found = FALSE;
    for (int i = 0; i < 100 && !(found = find_device(path, 300)); i++) Sleep(100);
    check(found, "setupapi lists the test device's HID interface (1209:0008)");
    if (!found) goto done;
    printf("     %ls\n", path);
    check(cm_lists(path), "CM_Get_Device_Interface_List lists the same path");

    HANDLE h = open_dev(path);
    check(h != INVALID_HANDLE_VALUE, "CreateFile on the path (error %lu)", h == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    if (h == INVALID_HANDLE_VALUE) goto done;

    /* what the device is */
    HIDD_ATTRIBUTES a = { sizeof(a) };
    BOOL ok = HidD_GetAttributes(h, &a);
    check(ok && a.VendorID == VID && a.ProductID == PID && a.VersionNumber == 0x0114,
          "HidD_GetAttributes: %04x:%04x version %04x", a.VendorID, a.ProductID, a.VersionNumber);
    WCHAR s[128];
    memset(s, 0, sizeof(s));
    ok = HidD_GetManufacturerString(h, s, sizeof(s));
    check(ok && !wcscmp(s, L"NovaOS"), "HidD_GetManufacturerString: %ls", s);
    memset(s, 0, sizeof(s));
    ok = HidD_GetProductString(h, s, sizeof(s));
    check(ok && !wcscmp(s, L"NovaOS Test HID Device"), "HidD_GetProductString: %ls", s);
    memset(s, 0, sizeof(s));
    ok = HidD_GetSerialNumberString(h, s, sizeof(s));
    check(ok && !wcscmp(s, L"NOVA0002"), "HidD_GetSerialNumberString: %ls", s);
    memset(s, 0, sizeof(s));
    ok = HidD_GetIndexedString(h, 2, s, sizeof(s));
    check(ok && !wcscmp(s, L"NovaOS Test HID Device"), "HidD_GetIndexedString(2): %ls", s);

    PHIDP_PREPARSED_DATA pp = NULL;
    HIDP_CAPS caps;
    memset(&caps, 0, sizeof(caps));
    check(HidD_GetPreparsedData(h, &pp) && HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS,
          "HidD_GetPreparsedData and HidP_GetCaps");
    check(caps.UsagePage == 0xFF00 && caps.Usage == 1, "caps: usage %04x:%04x", caps.UsagePage, caps.Usage);
    check(caps.InputReportByteLength == REPORT_LEN && caps.OutputReportByteLength == REPORT_LEN &&
          caps.FeatureReportByteLength == REPORT_LEN, "caps: report lengths in %u out %u feature %u",
          caps.InputReportByteLength, caps.OutputReportByteLength, caps.FeatureReportByteLength);
    check(caps.NumberInputValueCaps == 1 && caps.NumberOutputValueCaps == 1 && caps.NumberFeatureValueCaps == 1,
          "caps: one value each (input %u, output %u, feature %u)", caps.NumberInputValueCaps,
          caps.NumberOutputValueCaps, caps.NumberFeatureValueCaps);
    if (pp) HidD_FreePreparsedData(pp);

    HID_COLLECTION_INFORMATION ci;
    DWORD n = 0;
    memset(&ci, 0, sizeof(ci));
    ok = DeviceIoControl(h, IOCTL_HID_GET_COLLECTION_INFORMATION, NULL, 0, &ci, sizeof(ci), &n, NULL);
    check(ok && n == sizeof(ci) && ci.VendorID == VID && ci.ProductID == PID && ci.DescriptorSize > 0 && !ci.Polled,
          "IOCTL_HID_GET_COLLECTION_INFORMATION: %04x:%04x, preparsed data %lu bytes", ci.VendorID, ci.ProductID,
          ci.DescriptorSize);

    /* the feature report */
    UCHAR f[REPORT_LEN] = { 3, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7 }, g[REPORT_LEN];
    check(HidD_SetFeature(h, f, sizeof(f)), "HidD_SetFeature (error %lu)", GetLastError());
    memset(g, 0, sizeof(g));
    g[0] = 3;
    ok = HidD_GetFeature(h, g, sizeof(g));
    check(ok && !memcmp(f, g, sizeof(f)), "HidD_GetFeature reads it back (%02x %02x .. %02x)",
          g[0], g[1], g[8]);
    UCHAR small[4] = { 3 };
    check(!HidD_GetFeature(h, small, sizeof(small)), "HidD_GetFeature refuses a buffer shorter than the report");

    /* output reports and the input reports they come back as, on two handles */
    HANDLE h2 = open_dev(path);
    check(h2 != INVALID_HANDLE_VALUE, "a second handle");
    HidD_FlushQueue(h);                      /* (nothing from before this run) */
    if (h2 != INVALID_HANDLE_VALUE) HidD_FlushQueue(h2);
    check(send_output(h, 0x10, TRUE), "WriteFile: output report 2 (error %lu)", GetLastError());
    check(read_echo(h, 0x10), "ReadFile: the echo as input report 1");
    if (h2 != INVALID_HANDLE_VALUE) check(read_echo(h2, 0x10), "ReadFile on the second handle: the same report");
    check(send_output(h, 0x20, FALSE), "HidD_SetOutputReport (error %lu)", GetLastError());
    check(read_echo(h, 0x20), "ReadFile: its echo");
    if (h2 != INVALID_HANDLE_VALUE) check(read_echo(h2, 0x20), "ReadFile on the second handle: its echo");
    UCHAR in[REPORT_LEN] = { 1 };
    check(HidD_GetInputReport(h, in, sizeof(in)) && is_echo(in, 0x20), "HidD_GetInputReport: the last report");
    UCHAR bad[REPORT_LEN - 1] = { 2 };
    DWORD w = 0;
    check(!WriteFile(h, bad, sizeof(bad), &w, NULL), "WriteFile refuses a report of the wrong length");

    /* the input queue */
    ULONG nb = 0;
    ok = HidD_GetNumInputBuffers(h, &nb);
    check(ok && nb >= 2, "HidD_GetNumInputBuffers: %lu", nb);
    check(HidD_SetNumInputBuffers(h, 64) && HidD_GetNumInputBuffers(h, &nb) && nb == 64, "HidD_SetNumInputBuffers(64)");
    check(!HidD_SetNumInputBuffers(h, 1), "HidD_SetNumInputBuffers(1) is refused");
    send_output(h, 0x30, TRUE);
    send_output(h, 0x40, TRUE);
    Sleep(500);
    ok = HidD_FlushQueue(h);
    check(ok, "HidD_FlushQueue (error %lu)", ok ? 0 : GetLastError());
    send_output(h, 0x50, TRUE);
    check(read_echo(h, 0x50), "after the flush ReadFile gets the next report, not the flushed ones");
    if (h2 != INVALID_HANDLE_VALUE) {
        check(read_echo(h2, 0x30) && read_echo(h2, 0x40) && read_echo(h2, 0x50),
              "the second handle's queue kept its three reports");
        CloseHandle(h2);
    }

    if (unplug) {
        printf("[HID] step 1: unplug the device\n");
        fflush(stdout);
        UCHAR r[REPORT_LEN];
        DWORD got = 0;
        ok = ReadFile(h, r, sizeof(r), &got, NULL);
        check(!ok, "a read fails once the device is unplugged (error %lu)", ok ? 0 : GetLastError());
        WCHAR again[300];
        BOOL listed = TRUE;
        for (int i = 0; i < 100 && (listed = find_device(again, 300)); i++) Sleep(100);
        check(!listed, "setupapi no longer lists the device");
        check(!cm_lists(path), "nor does cfgmgr32");
        check(!HidD_GetAttributes(h, &a) || !HidD_GetFeature(h, g, sizeof(g)), "the old handle's requests fail");
    }
    CloseHandle(h);

done:
    printf("%s: %d passed, %d failed\n", g_name, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
