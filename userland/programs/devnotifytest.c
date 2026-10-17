/* devnotifytest.exe — devices arriving and leaving, as Windows programs
 * hear of it (the devices suite's "pnp" boot, after pnptest):
 *
 *   the PlugPlay service is running and has written the device tree into
 *   HKLM\SYSTEM\CurrentControlSet\Enum; then, at "[PNP] step 1" the test
 *   plugs in a USB keyboard and a FAT-formatted USB stick, at "[PNP] step
 *   2" it takes them out, and each must reach
 *   - a message-only window registered (RegisterDeviceNotificationW) for
 *     GUID_DEVINTERFACE_USB_DEVICE: DBT_DEVICEARRIVAL and
 *     DBT_DEVICEREMOVECOMPLETE with DEV_BROADCAST_DEVICEINTERFACE_W;
 *   - a hidden top-level window registered (RegisterDeviceNotificationA)
 *     for all interface classes: the keyboard's GUID_DEVINTERFACE_KEYBOARD
 *     with an ANSI name, the stick's drive letter as DEV_BROADCAST_VOLUME,
 *     and DBT_DEVNODES_CHANGED;
 *   - CM_Register_Notification callbacks: the keyboard interface class,
 *     and every device instance (enumerated, started, removed);
 *   and the keyboard's Enum key, with its service.
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <winsvc.h>
#include <dbt.h>
#include <novapnp.h>

__declspec(dllimport) DWORD WINAPI CM_Register_Notification(PVOID filter, PVOID ctx, PVOID callback, HANDLE *out);
__declspec(dllimport) DWORD WINAPI CM_Unregister_Notification(HANDLE h);

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

static const GUID g_usb = NOVA_IF_USB_DEVICE, g_kbd = NOVA_IF_KEYBOARD, g_hid = NOVA_IF_HID;

/* What arrived (1) and left (2), by who heard */
static volatile LONG msg_usb_kbd, msg_usb_stick, all_kbd, vol_arrival, vol_removal, devnodes, cm_kbd, cm_enum, cm_started, cm_removed;
static volatile LONG bad_name;
static volatile LONG handle_removal, bad_handle;
static WCHAR keyboard_device_path[256];
static HANDLE usb_handle, handle_reg;

static LRESULT CALLBACK msg_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_DEVICECHANGE &&
        (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEQUERYREMOVE || wp == DBT_DEVICEQUERYREMOVEFAILED ||
         wp == DBT_DEVICEREMOVEPENDING || wp == DBT_DEVICEREMOVECOMPLETE)) {
        DEV_BROADCAST_HDR *hd = (DEV_BROADCAST_HDR *)lp;
        LONG v = wp == DBT_DEVICEARRIVAL ? 1 : 2;
        if (hd && hd->dbch_devicetype == DBT_DEVTYP_HANDLE) {
            DEV_BROADCAST_HANDLE *d = (DEV_BROADCAST_HANDLE *)lp;
            if (d->dbch_handle != usb_handle || d->dbch_hdevnotify != handle_reg ||
                d->dbch_size < sizeof(*d)) bad_handle = 1;
            if (wp == DBT_DEVICEREMOVECOMPLETE) handle_removal++;
            return TRUE;
        }
        DEV_BROADCAST_DEVICEINTERFACE_W *d = (DEV_BROADCAST_DEVICEINTERFACE_W *)lp;
        if (d && d->dbcc_devicetype == DBT_DEVTYP_DEVICEINTERFACE && !memcmp(&d->dbcc_classguid, &g_usb, sizeof(GUID))) {
            printf("usb device %s: %ls\n", v == 1 ? "arrival" : "removal", d->dbcc_name);
            if (wcsstr(d->dbcc_name, L"\\\\?\\USB#VID_0627&PID_0001#")) {
                msg_usb_kbd |= v;
            }
            if (wcsstr(d->dbcc_name, L"\\\\?\\USB#VID_46F4&PID_0001#")) msg_usb_stick |= v;
            if (!wcsstr(d->dbcc_name, L"#{a5dcbf10-6530-11d2-901f-00c04fb951ed}")) bad_name = 1;
        } else {
            bad_name = 1;                          /* (registered for USB devices only) */
        }
        return TRUE;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static LRESULT CALLBACK top_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_DEVICECHANGE) {
        DEV_BROADCAST_HDR *hd = (DEV_BROADCAST_HDR *)lp;
        LONG v = wp == DBT_DEVICEARRIVAL ? 1 : wp == DBT_DEVICEREMOVECOMPLETE ? 2 : 0;
        if (wp == DBT_DEVNODES_CHANGED) devnodes++;
        else if (hd && hd->dbch_devicetype == DBT_DEVTYP_VOLUME) {
            DEV_BROADCAST_VOLUME *vol = (DEV_BROADCAST_VOLUME *)lp;
            printf("volume %s: mask 0x%lx\n", v == 1 ? "arrival" : "removal", (unsigned long)vol->dbcv_unitmask);
            if (vol->dbcv_unitmask) { if (v == 1) vol_arrival++; else vol_removal++; }
        } else if (hd && hd->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            DEV_BROADCAST_DEVICEINTERFACE_A *d = (DEV_BROADCAST_DEVICEINTERFACE_A *)lp;
            if (!memcmp(&d->dbcc_classguid, &g_kbd, sizeof(GUID)) && strstr(d->dbcc_name, "\\\\?\\HID#VID_0627&PID_0001#")) {
                printf("keyboard interface %s: %s\n", v == 1 ? "arrival" : "removal", d->dbcc_name);
                all_kbd |= v;
            }
            if (v == 1 && !memcmp(&d->dbcc_classguid, &g_hid, sizeof(GUID)) &&
                strstr(d->dbcc_name, "\\\\?\\HID#VID_0627&PID_0001#")) {
                int i = 0;
                for (; d->dbcc_name[i] && i < (int)(sizeof(keyboard_device_path) / sizeof(keyboard_device_path[0])) - 1; i++)
                    keyboard_device_path[i] = (unsigned char)d->dbcc_name[i];
                keyboard_device_path[i] = 0;
            }
        }
        return TRUE;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static DWORD CALLBACK cm_iface(HANDLE n, PVOID ctx, DWORD action, PVOID data, DWORD size)
{
    (void)n; (void)size;
    const WCHAR *link = (const WCHAR *)((BYTE *)data + 24);
    if (ctx == (PVOID)1 && wcsstr(link, L"HID#VID_0627&PID_0001#")) cm_kbd |= action == 0 ? 1 : action == 1 ? 2 : 0;
    return 0;
}

static DWORD CALLBACK cm_inst(HANDLE n, PVOID ctx, DWORD action, PVOID data, DWORD size)
{
    (void)n; (void)ctx; (void)size;
    const WCHAR *id = (const WCHAR *)((BYTE *)data + 8);
    if (!wcsncmp(id, L"USBSTOR\\Disk", 12)) {
        if (action == 6) cm_enum++;
        if (action == 7) cm_started++;
        if (action == 8) cm_removed++;
    }
    return 0;
}

static void pump_until(volatile LONG *a, LONG av, volatile LONG *b, LONG bv, DWORD ms)
{
    DWORD start = GetTickCount();
    MSG m;
    while (GetTickCount() - start < ms) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if ((*a & av) == av && (*b & bv) == bv && GetTickCount() - start > 3000) return;   /* (and the rest a moment longer) */
        Sleep(20);
    }
}

static BOOL reg_str(const char *path, const char *name, char *out, DWORD cap)
{
    HKEY k;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k)) return FALSE;
    DWORD type = 0, n = cap;
    LONG e = RegQueryValueExA(k, name, NULL, &type, (BYTE *)out, &n);
    RegCloseKey(k);
    return !e && type == REG_SZ;
}

/* The keyboard's USB device node's instance ID, or "" */
static void keyboard_instance(char *out, size_t cap)
{
    out[0] = 0;
    DWORD count = nova_pnp_count();
    for (DWORD id = count; id >= 1; id--) {
        NovaPnpNode n;
        if (nova_pnp_node(id, &n) && !strncmp(n.instance, "USB\\VID_0627&PID_0001\\", 22)) { snprintf(out, cap, "%s", n.instance); return; }
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* The service, and the Enum keys it wrote */
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    SC_HANDLE svc = scm ? OpenServiceW(scm, L"PlugPlay", SERVICE_QUERY_STATUS) : NULL;
    SERVICE_STATUS st;
    memset(&st, 0, sizeof(st));
    for (int i = 0; svc && i < 300 && !(QueryServiceStatus(svc, &st) && st.dwCurrentState == SERVICE_RUNNING); i++) Sleep(100);
    CHECK("the PlugPlay service is running", st.dwCurrentState == SERVICE_RUNNING);
    if (svc) CloseServiceHandle(svc);
    if (scm) CloseServiceHandle(scm);
    char v[128];
    for (int i = 0; i < 50 && !reg_str("SYSTEM\\CurrentControlSet\\Enum\\USB\\ROOT_HUB30\\4&1&0&0", "Service", v, sizeof(v)); i++) Sleep(100);
    CHECK("Enum has the root hub, its service", reg_str("SYSTEM\\CurrentControlSet\\Enum\\USB\\ROOT_HUB30\\4&1&0&0", "Service", v, sizeof(v)) && !strcmp(v, "usb"));
    CHECK("... and its class GUID", reg_str("SYSTEM\\CurrentControlSet\\Enum\\USB\\ROOT_HUB30\\4&1&0&0", "ClassGUID", v, sizeof(v)) &&
          !strcmp(v, "{36fc9e60-c465-11cf-8056-444553540000}"));

    /* The windows and registrations */
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = msg_proc;
    wc.lpszClassName = L"DevNotifyMsg";
    RegisterClassW(&wc);
    wc.lpfnWndProc = top_proc;
    wc.lpszClassName = L"DevNotifyTop";
    RegisterClassW(&wc);
    HWND msgw = CreateWindowExW(0, L"DevNotifyMsg", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    HWND top = CreateWindowExA(0, "DevNotifyTop", "devnotifytest", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL, NULL, NULL);
    CHECK("windows", msgw && top);

    DEV_BROADCAST_DEVICEINTERFACE_W f;
    memset(&f, 0, sizeof(f));
    f.dbcc_size = sizeof(f);
    f.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    f.dbcc_classguid = g_usb;
    HANDLE r1 = RegisterDeviceNotificationW(msgw, &f, DEVICE_NOTIFY_WINDOW_HANDLE);
    HANDLE r2 = RegisterDeviceNotificationA(top, &f, DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    CHECK("RegisterDeviceNotification", r1 && r2);
    DEV_BROADCAST_VOLUME bad;
    memset(&bad, 0, sizeof(bad));
    bad.dbcv_size = sizeof(bad);
    bad.dbcv_devicetype = DBT_DEVTYP_VOLUME;
    CHECK("a volume filter is refused", !RegisterDeviceNotificationW(msgw, &bad, 0));

    struct { DWORD cbSize, Flags, FilterType, Reserved; union { GUID g; WCHAR id[200]; } u; } cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.FilterType = 0;
    cf.u.g = g_kbd;
    HANDLE c1 = NULL, c2 = NULL;
    CHECK("CM_Register_Notification (interface class)", !CM_Register_Notification(&cf, (PVOID)1, cm_iface, &c1) && c1);
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.FilterType = 2;
    cf.Flags = 2;                                  /* CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES */
    CHECK("CM_Register_Notification (all instances)", !CM_Register_Notification(&cf, NULL, cm_inst, &c2) && c2);

    /* Plugged in */
    printf("[PNP] step 1: plug in a keyboard and a stick\n");
    pump_until(&msg_usb_kbd, 1, &vol_arrival, 1, 30000);
    pump_until(&msg_usb_stick, 1, &all_kbd, 1, 5000);
    pump_until(&cm_kbd, 1, &cm_started, 1, 5000);
    CHECK("USB device arrival: the keyboard", msg_usb_kbd & 1);
    CHECK("USB device arrival: the stick", msg_usb_stick & 1);
    CHECK("all classes: the keyboard interface, ANSI", all_kbd & 1);
    CHECK("volume arrival", vol_arrival == 1);
    CHECK("DBT_DEVNODES_CHANGED", devnodes > 0);
    CHECK("CM: keyboard interface arrival", cm_kbd & 1);
    CHECK("CM: disk enumerated and started", cm_enum == 1 && cm_started == 1);
    char inst[160], path[220];
    keyboard_instance(inst, sizeof(inst));
    snprintf(path, sizeof(path), "SYSTEM\\CurrentControlSet\\Enum\\%s", inst);
    for (int i = 0; i < 50 && !reg_str(path, "Service", v, sizeof(v)); i++) Sleep(100);
    CHECK("Enum has the keyboard, with its service", inst[0] && reg_str(path, "Service", v, sizeof(v)) && !strcmp(v, "HidUsb"));
    printf("keyboard: %s\n", inst);

    usb_handle = keyboard_device_path[0] ?
        CreateFileW(keyboard_device_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL) :
        INVALID_HANDLE_VALUE;
    if (usb_handle == INVALID_HANDLE_VALUE)
        printf("opening keyboard interface failed: %lu\n", (unsigned long)GetLastError());
    DEV_BROADCAST_HANDLE hf;
    memset(&hf, 0, sizeof(hf));
    hf.dbch_size = sizeof(hf);
    hf.dbch_devicetype = DBT_DEVTYP_HANDLE;
    hf.dbch_handle = usb_handle;
    handle_reg = usb_handle != INVALID_HANDLE_VALUE ?
        RegisterDeviceNotificationW(msgw, &hf, DEVICE_NOTIFY_WINDOW_HANDLE) : NULL;
    if (usb_handle != INVALID_HANDLE_VALUE && !handle_reg)
        printf("registering keyboard handle failed: %lu\n", (unsigned long)GetLastError());
    CHECK("register a USB device handle", usb_handle != INVALID_HANDLE_VALUE && handle_reg);

    /* Taken out */
    LONG before = devnodes;
    printf("[PNP] step 2: unplug them\n");
    pump_until(&msg_usb_kbd, 2, &vol_removal, 1, 30000);
    pump_until(&msg_usb_stick, 2, &all_kbd, 2, 5000);
    pump_until(&cm_kbd, 2, &cm_removed, 1, 5000);
    CHECK("USB device removal: the keyboard", msg_usb_kbd & 2);
    CHECK("USB device removal: the stick", msg_usb_stick & 2);
    CHECK("all classes: the keyboard interface removal", all_kbd & 2);
    CHECK("volume removal", vol_removal == 1);
    CHECK("DBT_DEVNODES_CHANGED again", devnodes > before);
    CHECK("CM: keyboard interface removal", cm_kbd & 2);
    CHECK("CM: disk removed", cm_removed == 1);
    CHECK("interface names", !bad_name);
    CHECK("handle removal notification", handle_removal == 1 && !bad_handle);

    CHECK("UnregisterDeviceNotification", UnregisterDeviceNotification(r1) && UnregisterDeviceNotification(r2));
    CHECK("unregister handle notification", handle_reg && UnregisterDeviceNotification(handle_reg));
    if (usb_handle != INVALID_HANDLE_VALUE) CloseHandle(usb_handle);
    CHECK("... not twice", !UnregisterDeviceNotification(r1));
    CHECK("CM_Unregister_Notification", !CM_Unregister_Notification(c1) && !CM_Unregister_Notification(c2));
    DestroyWindow(top);
    DestroyWindow(msgw);
    printf("devnotifytest: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
