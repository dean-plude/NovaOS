/*
 * bttest.exe — a Bluetooth radio, the devices in its range and a
 * Bluetooth mouse, through Bluetooth's Windows API (bthprops.cpl) as a
 * Windows program reaches them
 *
 * Runs in the devices self-test boot "bluetooth": a USB Bluetooth radio
 * (tools/btpeer.py, 1209:000d "NovaOS Test Bluetooth Radio") behind a
 * QEMU usb-redir device, run by NovaOS's bthusb and its host stack
 * (kernel/nt/bthusb.c, bthport.c).  In its range are "NovaOS Test Mouse"
 * (4E:4F:56:41:00:01: Secure Simple Pairing, a HID service, PnP
 * Information 1209:000e) and "NovaOS Test Headset" (4E:4F:56:41:00:02:
 * legacy pairing with PIN 0000, Hands-Free and Audio Sink services).
 * "bttest" checks:
 *  - BluetoothFindFirstRadio and BluetoothGetRadioInfo: the radio's
 *    address 4E:4F:56:41:00:00, its name and manufacturer; it is
 *    connectable, and BluetoothEnableDiscovery switches discoverable on
 *    and off;
 *  - BluetoothFindFirstDevice with an inquiry: both devices, with their
 *    names and classes of device, unpaired;
 *  - BluetoothAuthenticateDeviceEx pairing each (the mouse "Just Works",
 *    the headset by PIN), then BluetoothGetDeviceInfo saying so;
 *  - BluetoothSetServiceState switching the mouse's HID service on
 *    (BluetoothEnumerateInstalledServices lists it), and refusing the
 *    headset's Hands-Free one (no driver yet);
 *  - the mouse's HID collection under hidbth (a HID interface of
 *    1209:000e turned on, which mouhid runs), and, at "[BT] step: move", the test moving the mouse
 *    through tools/btpeer.py: the cursor must move.
 * "bttest after" (after a restart): the radio again, both devices
 * remembered as paired without an inquiry, the HID service still on, the
 * mouse back (reconnected with its stored link key) and moving.
 * "bttest remove": BluetoothRemoveDevice forgets both; the device list
 * no longer has them paired and the mouse's HID collection is gone.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>

__declspec(dllimport) DWORD WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags);
__declspec(dllimport) DWORD WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags);

typedef struct { DWORD dwSize; } BLUETOOTH_FIND_RADIO_PARAMS_;
typedef struct {
    DWORD dwSize;
    ULONGLONG address;
    WCHAR szName[248];
    ULONG ulClassofDevice;
    USHORT lmpSubversion, manufacturer;
} BLUETOOTH_RADIO_INFO_;
typedef struct {
    DWORD dwSize;
    ULONGLONG Address;
    ULONG ulClassofDevice;
    BOOL fConnected, fRemembered, fAuthenticated;
    SYSTEMTIME stLastSeen, stLastUsed;
    WCHAR szName[248];
} BLUETOOTH_DEVICE_INFO_;
typedef struct {
    DWORD dwSize;
    BOOL fReturnAuthenticated, fReturnRemembered, fReturnUnknown, fReturnConnected, fIssueInquiry;
    UCHAR cTimeoutMultiplier;
    HANDLE hRadio;
} BLUETOOTH_DEVICE_SEARCH_PARAMS_;

__declspec(dllimport) HANDLE WINAPI BluetoothFindFirstRadio(const BLUETOOTH_FIND_RADIO_PARAMS_ *, HANDLE *);
__declspec(dllimport) BOOL WINAPI BluetoothFindRadioClose(HANDLE);
__declspec(dllimport) DWORD WINAPI BluetoothGetRadioInfo(HANDLE, BLUETOOTH_RADIO_INFO_ *);
__declspec(dllimport) BOOL WINAPI BluetoothIsConnectable(HANDLE);
__declspec(dllimport) BOOL WINAPI BluetoothIsDiscoverable(HANDLE);
__declspec(dllimport) BOOL WINAPI BluetoothEnableDiscovery(HANDLE, BOOL);
__declspec(dllimport) HANDLE WINAPI BluetoothFindFirstDevice(const BLUETOOTH_DEVICE_SEARCH_PARAMS_ *, BLUETOOTH_DEVICE_INFO_ *);
__declspec(dllimport) BOOL WINAPI BluetoothFindNextDevice(HANDLE, BLUETOOTH_DEVICE_INFO_ *);
__declspec(dllimport) BOOL WINAPI BluetoothFindDeviceClose(HANDLE);
__declspec(dllimport) DWORD WINAPI BluetoothGetDeviceInfo(HANDLE, BLUETOOTH_DEVICE_INFO_ *);
__declspec(dllimport) DWORD WINAPI BluetoothAuthenticateDeviceEx(HWND, HANDLE, BLUETOOTH_DEVICE_INFO_ *, void *, int);
__declspec(dllimport) DWORD WINAPI BluetoothSetServiceState(HANDLE, const BLUETOOTH_DEVICE_INFO_ *, const GUID *, DWORD);
__declspec(dllimport) DWORD WINAPI BluetoothEnumerateInstalledServices(HANDLE, const BLUETOOTH_DEVICE_INFO_ *, DWORD *, GUID *);
__declspec(dllimport) DWORD WINAPI BluetoothRemoveDevice(const ULONGLONG *);

#define MOUSE   0x4E4F56410001ULL
#define HEADSET 0x4E4F56410002ULL
static const GUID g_hid = { 0x00001124, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
static const GUID g_hfp = { 0x0000111E, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };

static int g_pass, g_fail;

static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("%s: ", ok ? "pass" : "FAIL");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fflush(stdout);
    if (ok) g_pass++; else g_fail++;
}

static HANDLE radio(void)
{
    BLUETOOTH_FIND_RADIO_PARAMS_ p = { sizeof(p) };
    HANDLE r = NULL, f = NULL;
    for (int i = 0; i < 100 && !f; i++) {                       /* (the radio comes up after boot) */
        f = BluetoothFindFirstRadio(&p, &r);
        if (!f) Sleep(200);
    }
    check(f != NULL, "BluetoothFindFirstRadio: a radio");
    if (f) BluetoothFindRadioClose(f);
    return r;
}

static void radio_checks(HANDLE r, int toggle)
{
    BLUETOOTH_RADIO_INFO_ ri;
    memset(&ri, 0, sizeof(ri));
    ri.dwSize = sizeof(ri);
    DWORD e = BluetoothGetRadioInfo(r, &ri);
    check(!e && ri.address == 0x4E4F56410000ULL, "BluetoothGetRadioInfo: address %012llx (error %lu)", ri.address, e);
    check(!e && ri.szName[0] && ri.manufacturer == 0xFFFF, "the radio's name \"%ls\", manufacturer %04x", ri.szName,
          ri.manufacturer);
    check(BluetoothIsConnectable(r), "BluetoothIsConnectable");
    if (!toggle) return;
    check(!BluetoothIsDiscoverable(r), "not discoverable at first");
    check(BluetoothEnableDiscovery(r, TRUE) && BluetoothIsDiscoverable(r), "BluetoothEnableDiscovery(TRUE): discoverable");
    check(BluetoothEnableDiscovery(r, FALSE) && !BluetoothIsDiscoverable(r) && BluetoothIsConnectable(r),
          "BluetoothEnableDiscovery(FALSE): not discoverable, still connectable");
}

/* The devices a search returns: which of ours, paired or not */
static int search(HANDLE r, BOOL inquiry, BOOL unknown, BLUETOOTH_DEVICE_INFO_ *mouse, BLUETOOTH_DEVICE_INFO_ *headset)
{
    BLUETOOTH_DEVICE_SEARCH_PARAMS_ p;
    memset(&p, 0, sizeof(p));
    p.dwSize = sizeof(p);
    p.fReturnAuthenticated = p.fReturnRemembered = TRUE;
    p.fReturnUnknown = unknown;
    p.fIssueInquiry = inquiry;
    p.cTimeoutMultiplier = 2;
    p.hRadio = r;
    BLUETOOTH_DEVICE_INFO_ d;
    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    int n = 0;
    mouse->dwSize = headset->dwSize = 0;
    HANDLE f = BluetoothFindFirstDevice(&p, &d);
    for (BOOL more = f != NULL; more; more = BluetoothFindNextDevice(f, &d)) {
        printf("  %012llx \"%ls\" class %06lx%s%s\n", d.Address, d.szName, d.ulClassofDevice,
               d.fAuthenticated ? " paired" : "", d.fConnected ? " connected" : "");
        if (d.Address == MOUSE) *mouse = d;
        if (d.Address == HEADSET) *headset = d;
        n++;
    }
    if (f) BluetoothFindDeviceClose(f);
    return n;
}

/* The mouse's HID collection (1209:000e): its HID interface is on */
static BOOL hid_mouse(void)
{
    static GUID hid = { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };
    ULONG len = 0;
    if (CM_Get_Device_Interface_List_SizeW(&len, &hid, NULL, 0) || !len) return FALSE;
    WCHAR *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (len + 1) * sizeof(WCHAR));
    BOOL found = FALSE;
    if (list && !CM_Get_Device_Interface_ListW(&hid, NULL, list, len, 0))
        for (WCHAR *p = list; *p && !found; p += wcslen(p) + 1) {
            _wcsupr(p);
            found = wcsstr(p, L"VID_1209&PID_000E") != NULL;
        }
    if (list) HeapFree(GetProcessHeap(), 0, list);
    return found;
}

static void mouse_checks(void)
{
    BOOL found = FALSE;
    for (int i = 0; i < 150 && !(found = hid_mouse()); i++) Sleep(200);
    check(found, "the Bluetooth mouse's HID collection is there (1209:000e)");
    POINT a, b;
    GetCursorPos(&a);
    printf("[BT] step: move\n");
    fflush(stdout);
    BOOL moved = FALSE;
    for (int i = 0; i < 150 && !moved; i++) {
        Sleep(100);
        GetCursorPos(&b);
        moved = b.x != a.x || b.y != a.y;
    }
    check(moved, "the Bluetooth mouse moved the cursor (%ld,%ld -> %ld,%ld)", a.x, a.y, b.x, b.y);
}

static BOOL hid_installed(HANDLE r, const BLUETOOTH_DEVICE_INFO_ *d)
{
    GUID g[8];
    DWORD n = 8;
    if (BluetoothEnumerateInstalledServices(r, d, &n, g)) return FALSE;
    for (DWORD i = 0; i < n; i++) if (!memcmp(&g[i], &g_hid, sizeof(GUID))) return TRUE;
    return FALSE;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    BLUETOOTH_DEVICE_INFO_ mouse, headset;
    HANDLE r = radio();
    if (!r) { printf("bttest: %d passed, %d failed\n", g_pass, g_fail); return 1; }
    if (!strcmp(mode, "remove")) {
        ULONGLONG a = MOUSE, b = HEADSET;
        check(!BluetoothRemoveDevice(&a) && !BluetoothRemoveDevice(&b), "BluetoothRemoveDevice: both forgotten");
        search(r, FALSE, FALSE, &mouse, &headset);
        check(!mouse.dwSize && !headset.dwSize, "neither is listed as paired any more");
        BOOL gone = FALSE;
        for (int i = 0; i < 100 && !(gone = !hid_mouse()); i++) Sleep(200);
        check(gone, "the Bluetooth mouse's HID collection is gone");
        check(BluetoothRemoveDevice(&a) != 0, "removing it again fails");
        printf("bttest remove: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail != 0;
    }
    if (!strcmp(mode, "after")) {
        radio_checks(r, 0);
        search(r, FALSE, FALSE, &mouse, &headset);
        check(mouse.dwSize && mouse.fAuthenticated && headset.dwSize && headset.fAuthenticated,
              "both remembered as paired, with no inquiry");
        check(mouse.dwSize && !wcscmp(mouse.szName, L"NovaOS Test Mouse"), "the mouse's name was kept");
        check(mouse.dwSize && hid_installed(r, &mouse), "the mouse's HID service is still on");
        mouse_checks();
        printf("bttest after: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail != 0;
    }
    radio_checks(r, 1);
    int n = search(r, TRUE, TRUE, &mouse, &headset);
    check(n >= 2 && mouse.dwSize && headset.dwSize, "an inquiry found both devices (%d listed)", n);
    check(mouse.dwSize && !wcscmp(mouse.szName, L"NovaOS Test Mouse") && mouse.ulClassofDevice == 0x002580 && !mouse.fAuthenticated,
          "the mouse: its name, class of device 002580, not paired");
    check(headset.dwSize && !wcscmp(headset.szName, L"NovaOS Test Headset") && headset.ulClassofDevice == 0x240404,
          "the headset: its name, class of device 240404");
    if (!mouse.dwSize || !headset.dwSize) { printf("bttest: %d passed, %d failed\n", g_pass, g_fail); return 1; }
    DWORD e = BluetoothAuthenticateDeviceEx(NULL, r, &mouse, NULL, 0);
    check(!e && mouse.fAuthenticated, "BluetoothAuthenticateDeviceEx: the mouse paired (Just Works) (error %lu)", e);
    e = BluetoothAuthenticateDeviceEx(NULL, r, &headset, NULL, 0);
    check(!e, "BluetoothAuthenticateDeviceEx: the headset paired (PIN 0000) (error %lu)", e);
    BLUETOOTH_DEVICE_INFO_ d;
    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    d.Address = MOUSE;
    e = BluetoothGetDeviceInfo(r, &d);
    check(!e && d.fAuthenticated && d.fRemembered, "BluetoothGetDeviceInfo: the mouse is paired");
    check(!hid_installed(r, &mouse), "no service on before it is switched on");
    e = BluetoothSetServiceState(r, &mouse, &g_hid, 1);
    check(!e, "BluetoothSetServiceState: the mouse's HID service on (error %lu)", e);
    check(hid_installed(r, &mouse), "BluetoothEnumerateInstalledServices lists HID");
    e = BluetoothSetServiceState(r, &headset, &g_hfp, 1);
    check(e != 0, "the headset's Hands-Free service is refused for now (error %lu)", e);
    mouse_checks();
    printf("bttest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
