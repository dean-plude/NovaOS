/*
 * kbdmoutest.exe — USB keyboards and mice through the HID class driver and
 * the keyboard and mouse class drivers (kbdhid, mouhid), as on Windows
 *
 * Runs in the devices self-test boot "devmgr" (QEMU's USB keyboard and
 * tablet on an xHCI controller).  At "[HID] kbdmou step 1" the test plugs
 * in a second USB keyboard and a second USB tablet, then:
 *  - the tree: each HID\VID_0627&PID_0001 keyboard collection started on
 *    kbdhid (class Keyboard), each tablet's on mouhid (class Mouse), and
 *    each one's USB device on HidUsb;
 *  - Raw Input lists none of them as a HID device (RIM_TYPEHID): their
 *    keys and motion are the keyboard's and the mouse's;
 *  - "[HID] kbdmou step 2": the test moves the new tablet to the middle of
 *    the screen's width, a quarter of its height, and the pointer must go
 *    there;
 *  - "[HID] kbdmou step 3": the test types "nova" and Enter on the new
 *    keyboard (QEMU types on the newest), and the line must come;
 *  - "[HID] kbdmou step 4": the test unplugs both, and their collections
 *    must leave.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

typedef DWORD CONFIGRET;
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW(PULONG len, LPCWSTR filter, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_ListW(LPCWSTR filter, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Parent(PDWORD parent, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW(DWORD dn, ULONG prop, PULONG type, PVOID buf, PULONG len, ULONG flags);

#define CM_DRP_SERVICE   0x05
#define CM_DRP_CLASS     0x08
#define DN_STARTED       0x08

static int g_pass, g_fail;

static void check(BOOL ok, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("%s: ", ok ? "PASS" : "FAIL");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    if (ok) g_pass++; else g_fail++;
}

static void step(int n, const char *what)
{
    printf("[HID] kbdmou step %d: %s\n", n, what);
    fflush(stdout);
}

static void prop(DWORD dn, ULONG which, WCHAR *out, ULONG cap)
{
    ULONG type, len = cap * sizeof(WCHAR);
    if (CM_Get_DevNode_Registry_PropertyW(dn, which, &type, out, &len, 0)) out[0] = 0;
}

/* The started QEMU (0627:0001) HID collections of setup class @cls: how
 * many, and how many of them run on @service with their USB device on HidUsb */
static int collections(const WCHAR *cls, const WCHAR *service, int *right)
{
    ULONG len = 0;
    int n = 0;
    *right = 0;
    if (CM_Get_Device_ID_List_SizeW(&len, L"HID", 0x101) || len < 2) return 0;
    WCHAR *list = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (list && !CM_Get_Device_ID_ListW(L"HID", list, len, 0x101))
        for (WCHAR *id = list; *id; id += lstrlenW(id) + 1) {
            DWORD dn, parent;
            ULONG status = 0, problem = 0;
            WCHAR s[64], p[64];
            if (_wcsnicmp(id, L"HID\\VID_0627&PID_0001\\", 22) || CM_Locate_DevNodeW(&dn, id, 0)) continue;
            if (CM_Get_DevNode_Status(&status, &problem, dn, 0) || !(status & DN_STARTED)) continue;
            prop(dn, CM_DRP_CLASS, s, 64);
            if (lstrcmpiW(s, cls)) continue;
            n++;
            prop(dn, CM_DRP_SERVICE, s, 64);
            p[0] = 0;
            if (!CM_Get_Parent(&parent, dn, 0)) prop(parent, CM_DRP_SERVICE, p, 64);
            if (!lstrcmpiW(s, service) && !lstrcmpiW(p, L"HidUsb")) (*right)++;
            else printf("  %ls: driver %ls, its USB device's %ls\n", id, s, p);
        }
    if (list) HeapFree(GetProcessHeap(), 0, list);
    return n;
}

/* Raw Input's HID devices from QEMU (0627:0001) */
static int raw_hid_qemu(void)
{
    UINT n = 0;
    if (GetRawInputDeviceList(NULL, &n, sizeof(RAWINPUTDEVICELIST)) || !n) return -1;
    RAWINPUTDEVICELIST l[64];
    if (n > 64) n = 64;
    n = GetRawInputDeviceList(l, &n, sizeof(l[0]));
    int qemu = 0;
    for (UINT i = 0; i < n && n != (UINT)-1; i++) {
        if (l[i].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info = { sizeof(info) };
        UINT size = sizeof(info);
        if (GetRawInputDeviceInfoW(l[i].hDevice, RIDI_DEVICEINFO, &info, &size) != (UINT)-1 &&
            info.hid.dwVendorId == 0x0627 && info.hid.dwProductId == 0x0001)
            qemu++;
    }
    return qemu;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int kbd = 0, mouse = 0, kbd_ok = 0, mouse_ok = 0;
    collections(L"Keyboard", L"kbdhid", &kbd_ok);
    collections(L"Mouse", L"mouhid", &mouse_ok);
    check(kbd_ok >= 1 && mouse_ok >= 1, "QEMU's USB keyboard on kbdhid (%d) and tablet on mouhid (%d) at boot", kbd_ok, mouse_ok);

    step(1, "plug in a USB keyboard and a USB tablet");
    for (int i = 0; i < 300; i++) {
        kbd = collections(L"Keyboard", L"kbdhid", &kbd_ok);
        mouse = collections(L"Mouse", L"mouhid", &mouse_ok);
        if (kbd_ok >= 2 && mouse_ok >= 2) break;
        Sleep(100);
    }
    check(kbd == 2 && kbd_ok == 2, "two keyboard collections, each on kbdhid under HidUsb (%d of %d)", kbd_ok, kbd);
    check(mouse == 2 && mouse_ok == 2, "two mouse collections, each on mouhid under HidUsb (%d of %d)", mouse_ok, mouse);
    int raw = raw_hid_qemu();
    check(raw == 0, "Raw Input lists none of them as a HID device (%d)", raw);

    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    POINT want = { w / 2, h / 4 }, at = { -1, -1 };
    step(2, "move the tablet");
    for (int i = 0; i < 100; i++) {
        GetCursorPos(&at);
        if (labs(at.x - want.x) <= 4 && labs(at.y - want.y) <= 4) break;
        Sleep(100);
    }
    check(labs(at.x - want.x) <= 4 && labs(at.y - want.y) <= 4, "the pointer follows the tablet to %ld,%ld (at %ld,%ld)",
          (long)want.x, (long)want.y, (long)at.x, (long)at.y);

    step(3, "type nova");
    char line[64] = "";
    if (!fgets(line, sizeof(line), stdin)) line[0] = 0;
    line[strcspn(line, "\r\n")] = 0;
    check(!strcmp(line, "nova"), "the keyboard types \"nova\" (\"%s\")", line);

    step(4, "unplug them");
    for (int i = 0; i < 300; i++) {
        kbd = collections(L"Keyboard", L"kbdhid", &kbd_ok);
        mouse = collections(L"Mouse", L"mouhid", &mouse_ok);
        if (kbd == 1 && mouse == 1) break;
        Sleep(100);
    }
    check(kbd == 1 && mouse == 1, "their collections leave, the boot's stay (%d keyboard, %d mouse)", kbd, mouse);

    printf("kbdmoutest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
