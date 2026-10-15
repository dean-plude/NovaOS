/* devenumtest.exe — the device tree as Windows programs walk it (the
 * devices suite's "pnp" boot, after pnptest and devnotifytest, with only
 * phantoms left on the xHCI controller): at "[PNP] step 1" the test plugs
 * in a USB keyboard, and once it is in the tree
 *   - cfgmgr32: CM_Locate_DevNode finds the root and the keyboard;
 *     CM_Get_Child/Sibling reach it from the root; CM_Get_Parent leads
 *     back up through the USB device, the root hub and the controller;
 *     CM_Get_DevNode_Status says it is started; its registry properties
 *     (class, hardware IDs, service) and DEVPKEY properties; the device
 *     ID lists filtered by enumerator, service, class and presence;
 *     a phantom found only with CM_LOCATE_DEVNODE_PHANTOM; the keyboard
 *     interface list; its Device Parameters key;
 *   - setupapi: SetupDiGetClassDevs by setup class (Keyboard), by
 *     enumerator (USB, all classes) and by interface class (keyboard),
 *     the interface path the same as cfgmgr32's, SetupDiOpenDeviceInfo
 *     and SetupDiOpenDeviceInterface by name, SetupDiOpenDevRegKey.
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <novapnp.h>

typedef DWORD CONFIGRET;
__declspec(dllimport) CONFIGRET WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Child(PDWORD c, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Sibling(PDWORD s, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Parent(PDWORD p, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Depth(PULONG d, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_IDW(DWORD dn, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_Size(PULONG len, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW(DWORD dn, ULONG prop, PULONG type, PVOID buf, PULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_PropertyW(DWORD dn, const void *key, PULONG type, PBYTE buf, PULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW(PULONG len, LPCWSTR filter, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_ListW(LPCWSTR filter, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Open_DevNode_Key(DWORD dn, DWORD sam, ULONG profile, DWORD disp, HKEY *key, ULONG flags);
__declspec(dllimport) HANDLE WINAPI SetupDiGetClassDevsW(const GUID *cls, LPCWSTR enumerator, HWND parent, DWORD flags);
__declspec(dllimport) HANDLE WINAPI SetupDiCreateDeviceInfoList(const GUID *cls, HWND parent);
__declspec(dllimport) BOOL WINAPI SetupDiEnumDeviceInfo(HANDLE set, DWORD i, PVOID info);
__declspec(dllimport) BOOL WINAPI SetupDiEnumDeviceInterfaces(HANDLE set, PVOID dev, const GUID *cls, DWORD i, PVOID info);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HANDLE set, PVOID iface, PVOID detail, DWORD n, PDWORD need, PVOID dev);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceRegistryPropertyW(HANDLE set, PVOID dev, DWORD prop, PDWORD type, PBYTE buf, DWORD n, PDWORD need);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceInstanceIdW(HANDLE set, PVOID dev, LPWSTR id, DWORD n, PDWORD need);
__declspec(dllimport) BOOL WINAPI SetupDiOpenDeviceInfoW(HANDLE set, LPCWSTR id, HWND parent, DWORD flags, PVOID dev);
__declspec(dllimport) BOOL WINAPI SetupDiOpenDeviceInterfaceW(HANDLE set, LPCWSTR path, DWORD flags, PVOID iface);
__declspec(dllimport) HKEY WINAPI SetupDiOpenDevRegKey(HANDLE set, PVOID dev, DWORD scope, DWORD profile, DWORD type, REGSAM sam);
__declspec(dllimport) BOOL WINAPI SetupDiDestroyDeviceInfoList(HANDLE set);

typedef struct { DWORD cbSize; GUID InterfaceClassGuid; DWORD Flags; ULONG_PTR Reserved; } SP_DEVICE_INTERFACE_DATA;
typedef struct { DWORD cbSize; GUID ClassGuid; DWORD DevInst; ULONG_PTR Reserved; } SP_DEVINFO_DATA;
typedef struct { DWORD cbSize; WCHAR DevicePath[256]; } DetailW;
typedef struct { GUID fmtid; ULONG pid; } DEVPROPKEY;

#define DIGCF_PRESENT         0x02
#define DIGCF_ALLCLASSES      0x04
#define DIGCF_DEVICEINTERFACE 0x10
#define CR_SUCCESS            0
#define CR_NO_SUCH_DEVNODE    0x0D
#define CR_NO_SUCH_VALUE      0x25
#define CM_DRP_HARDWAREID     0x02
#define CM_DRP_SERVICE        0x05
#define CM_DRP_CLASS          0x08
#define SPDRP_DEVICEDESC      0x00
#define SPDRP_CLASS           0x07
#define DN_STARTED            0x08
#define DICS_FLAG_GLOBAL      1
#define DIREG_DEV             1

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

static const GUID g_kbd_class = { 0x4d36e96b, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
static const GUID g_kbd_iface = NOVA_IF_KEYBOARD;

static BOOL in_list(const WCHAR *list, const WCHAR *what, BOOL prefix)
{
    for (; *list; list += wcslen(list) + 1)
        if (prefix ? !_wcsnicmp(list, what, wcslen(what)) : !_wcsicmp(list, what)) return TRUE;
    return FALSE;
}

/* Every entry of @list starts with @prefix */
static BOOL all_start(const WCHAR *list, const WCHAR *prefix)
{
    for (; *list; list += wcslen(list) + 1) if (_wcsnicmp(list, prefix, wcslen(prefix))) return FALSE;
    return TRUE;
}

static WCHAR g_list[32768];
static ULONG id_list(const WCHAR *filter, ULONG flags)
{
    ULONG len = 0;
    if (CM_Get_Device_ID_List_SizeW(&len, filter, flags) || len > 32768 ||
        CM_Get_Device_ID_ListW(filter, g_list, 32768, flags)) { g_list[0] = 0; return 0; }
    return len;
}

/* The keyboard's HID node, present and its driver started (0: not yet) */
static DWORD keyboard(WCHAR *id)
{
    id_list(L"HID", 0x101);                         /* (CM_GETIDLIST_FILTER_ENUMERATOR | _PRESENT) */
    for (WCHAR *s = g_list; *s; s += wcslen(s) + 1) {
        DWORD dn;
        ULONG status = 0, problem = 0;
        if (!_wcsnicmp(s, L"HID\\VID_0627&PID_0001\\", 22) && !CM_Locate_DevNodeW(&dn, s, 0) &&
            !CM_Get_DevNode_Status(&status, &problem, dn, 0) && (status & DN_STARTED)) { wcscpy(id, s); return dn; }
    }
    return 0;
}

/* Whether @target is reached from the root by CM_Get_Child/Sibling */
static BOOL reachable(DWORD from, DWORD target, int depth)
{
    if (from == target) return TRUE;
    if (depth > 16) return FALSE;
    DWORD c;
    for (CONFIGRET cr = CM_Get_Child(&c, from, 0); cr == CR_SUCCESS; cr = CM_Get_Sibling(&c, c, 0))
        if (reachable(c, target, depth + 1)) return TRUE;
    return FALSE;
}

static void cfgmgr(DWORD kbd, const WCHAR *kbd_id)
{
    DWORD root, dn, parent;
    WCHAR s[512];
    ULONG len, type, status, problem;
    CHECK("CM_Locate_DevNode(NULL) is the root", !CM_Locate_DevNodeW(&root, NULL, 0) && root == 1);
    CHECK("the root's ID", !CM_Get_Device_IDW(root, s, 512, 0) && !wcscmp(s, L"HTREE\\ROOT\\0"));
    CHECK("the root has no parent", CM_Get_Parent(&parent, root, 0) == CR_NO_SUCH_DEVNODE);
    CHECK("CM_Locate_DevNode finds the keyboard by its ID", !CM_Locate_DevNodeW(&dn, kbd_id, 0) && dn == kbd);
    CHECK("CM_Get_Device_ID_Size", !CM_Get_Device_ID_Size(&len, kbd, 0) && len == wcslen(kbd_id));
    CHECK("the keyboard is reached from the root by CM_Get_Child and CM_Get_Sibling", reachable(root, kbd, 0));
    ULONG depth = 0;
    CHECK("CM_Get_Depth: root, PCI bus, controller, root hub, USB device, keyboard",
          !CM_Get_Depth(&depth, kbd, 0) && depth == 5);
    CHECK("CM_Get_DevNode_Status: started, no problem",
          !CM_Get_DevNode_Status(&status, &problem, kbd, 0) && (status & DN_STARTED) && !problem);
    len = sizeof(s);
    CHECK("CM_DRP_CLASS is Keyboard",
          !CM_Get_DevNode_Registry_PropertyW(kbd, CM_DRP_CLASS, &type, s, &len, 0) && type == REG_SZ && !wcscmp(s, L"Keyboard"));
    len = sizeof(s);
    CHECK("CM_DRP_HARDWAREID lists HID\\VID_0627&PID_0001",
          !CM_Get_DevNode_Registry_PropertyW(kbd, CM_DRP_HARDWAREID, &type, s, &len, 0) && type == REG_MULTI_SZ &&
          in_list(s, L"HID\\VID_0627&PID_0001", FALSE));
    len = sizeof(s);
    CHECK("CM_DRP_SERVICE is the keyboard class driver kbdhid",
          !CM_Get_DevNode_Registry_PropertyW(kbd, CM_DRP_SERVICE, &type, s, &len, 0) && !wcscmp(s, L"kbdhid"));
    len = 2;
    CHECK("a property too big for the buffer: CR_BUFFER_SMALL and its size",
          CM_Get_DevNode_Registry_PropertyW(kbd, CM_DRP_CLASS, &type, s, &len, 0) == 0x1A && len == 18);
    len = sizeof(s);
    CHECK("a property the device does not have: CR_NO_SUCH_VALUE",
          CM_Get_DevNode_Registry_PropertyW(kbd, 0x11 /* CM_DRP_UI_NUMBER */, &type, s, &len, 0) == CR_NO_SUCH_VALUE);
    static const DEVPROPKEY parent_key = { { 0x4340a6c5, 0x93fa, 0x4706, { 0x97, 0x2c, 0x7b, 0x64, 0x80, 0x08, 0xa5, 0xa7 } }, 8 };
    static const DEVPROPKEY desc_key = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 2 };
    WCHAR pid[200];
    CHECK("CM_Get_Parent: the keyboard's USB device",
          !CM_Get_Parent(&parent, kbd, 0) && !CM_Get_Device_IDW(parent, pid, 200, 0) && !wcsncmp(pid, L"USB\\VID_0627&PID_0001\\", 22));
    len = sizeof(s);
    CHECK("DEVPKEY_Device_Parent names it",
          !CM_Get_DevNode_PropertyW(kbd, &parent_key, &type, (PBYTE)s, &len, 0) && type == 0x12 && !wcscmp(s, pid));
    len = sizeof(s);
    CHECK("DEVPKEY_Device_DeviceDesc", !CM_Get_DevNode_PropertyW(kbd, &desc_key, &type, (PBYTE)s, &len, 0) &&
                                       !wcscmp(s, L"HID Keyboard Device"));
    DWORD hub, hc;
    CHECK("its parent's parent is the root hub, then the controller",
          !CM_Get_Parent(&hub, parent, 0) && !CM_Get_Device_IDW(hub, s, 512, 0) && !wcsncmp(s, L"USB\\ROOT_HUB30\\", 15) &&
          !CM_Get_Parent(&hc, hub, 0) && !CM_Get_Device_IDW(hc, s, 512, 0) && !wcsncmp(s, L"PCI\\VEN_1B36&DEV_000D", 21));

    /* device ID lists */
    id_list(NULL, 0);
    CHECK("the whole list has the root, the controller and the keyboard",
          in_list(g_list, L"HTREE\\ROOT\\0", FALSE) && in_list(g_list, L"PCI\\VEN_1B36&DEV_000D", TRUE) && in_list(g_list, kbd_id, FALSE));
    CHECK("it has devices no longer present (the stick of the tests before)", in_list(g_list, L"USBSTOR\\", TRUE));
    id_list(NULL, 0x100);
    CHECK("CM_GETIDLIST_FILTER_PRESENT leaves them out", in_list(g_list, kbd_id, FALSE) && !in_list(g_list, L"USBSTOR\\", TRUE));
    id_list(L"USB", 0x101);
    CHECK("CM_GETIDLIST_FILTER_ENUMERATOR USB: only USB devices", g_list[0] && all_start(g_list, L"USB\\") &&
                                                               in_list(g_list, pid, FALSE));
    id_list(L"kbdhid", 0x102);
    CHECK("CM_GETIDLIST_FILTER_SERVICE kbdhid: the keyboard, not its USB device", in_list(g_list, kbd_id, FALSE) &&
                                                                                 !in_list(g_list, pid, FALSE));
    id_list(L"HidUsb", 0x102);
    CHECK("CM_GETIDLIST_FILTER_SERVICE HidUsb: its USB device", in_list(g_list, pid, FALSE) && !in_list(g_list, kbd_id, FALSE));
    id_list(L"{4d36e96b-e325-11ce-bfc1-08002be10318}", 0x300);
    CHECK("CM_GETIDLIST_FILTER_CLASS Keyboard: the keyboard", in_list(g_list, kbd_id, FALSE) && all_start(g_list, L"HID\\"));
    id_list(pid, 0x20);
    CHECK("CM_GETIDLIST_FILTER_BUSRELATIONS: the USB device's child is the keyboard", in_list(g_list, kbd_id, FALSE));

    /* a phantom */
    id_list(L"USBSTOR", 0x1);
    WCHAR phantom[200];
    wcscpy(phantom, g_list);
    CHECK("a phantom is not found without CM_LOCATE_DEVNODE_PHANTOM",
          phantom[0] && CM_Locate_DevNodeW(&dn, phantom, 0) == CR_NO_SUCH_DEVNODE);
    CHECK("it is found with it, but has no status", !CM_Locate_DevNodeW(&dn, phantom, 1) &&
                                                    CM_Get_DevNode_Status(&status, &problem, dn, 0) == CR_NO_SUCH_DEVNODE);
    len = sizeof(s);
    CHECK("its properties are still there",
          !CM_Get_DevNode_Registry_PropertyW(dn, CM_DRP_CLASS, &type, s, &len, 0) && !wcscmp(s, L"DiskDrive"));

    /* interfaces, keys */
    ULONG n = 0;
    CM_Get_Device_Interface_List_SizeW(&n, (LPGUID)&g_kbd_iface, NULL, 0);
    static WCHAR ifs[4096];
    CHECK("CM_Get_Device_Interface_List: the keyboard's GUID_DEVINTERFACE_KEYBOARD",
          n < 4096 && !CM_Get_Device_Interface_ListW((LPGUID)&g_kbd_iface, NULL, ifs, 4096, 0) &&
          in_list(ifs, L"\\\\?\\HID#VID_0627&PID_0001#", TRUE));
    for (WCHAR *p = ifs; *p; p += wcslen(p) + 1) printf("keyboard interface: %ls\n", p);
    HKEY k;
    CHECK("CM_Open_DevNode_Key: Enum\\<instance>\\Device Parameters",
          !CM_Open_DevNode_Key(kbd, KEY_READ | KEY_WRITE, 0, 0, &k, 0) && !RegCloseKey(k));
}

static void setupapi(DWORD kbd, const WCHAR *kbd_id)
{
    SP_DEVINFO_DATA d = { sizeof(d) };
    WCHAR s[512];
    DWORD type, need;
    HANDLE set = SetupDiGetClassDevsW(&g_kbd_class, NULL, NULL, DIGCF_PRESENT);
    BOOL found = FALSE;
    for (DWORD i = 0; set != INVALID_HANDLE_VALUE && SetupDiEnumDeviceInfo(set, i, &d); i++) {
        if (d.DevInst != kbd) continue;
        found = SetupDiGetDeviceInstanceIdW(set, &d, s, 512, NULL) && !wcscmp(s, kbd_id) &&
                !memcmp(&d.ClassGuid, &g_kbd_class, sizeof(GUID)) &&
                SetupDiGetDeviceRegistryPropertyW(set, &d, SPDRP_DEVICEDESC, &type, (PBYTE)s, sizeof(s), NULL) &&
                !wcscmp(s, L"HID Keyboard Device");
        HKEY k = SetupDiOpenDevRegKey(set, &d, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        CHECK("SetupDiOpenDevRegKey(DIREG_DEV)", k != INVALID_HANDLE_VALUE && !RegCloseKey(k));
    }
    CHECK("SetupDiGetClassDevs(Keyboard class): the keyboard, its ID, class and description", found);
    if (set != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(set);

    set = SetupDiGetClassDevsW(NULL, L"USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    int usb = 0, other = 0;
    for (DWORD i = 0; set != INVALID_HANDLE_VALUE && SetupDiEnumDeviceInfo(set, i, &d); i++) {
        if (SetupDiGetDeviceInstanceIdW(set, &d, s, 512, NULL) && !wcsncmp(s, L"USB\\", 4)) usb++;
        else other++;
    }
    CHECK("SetupDiGetClassDevs(enumerator USB, all classes): only USB devices, the root hub and the keyboard's",
          usb >= 2 && !other);
    if (set != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(set);

    set = SetupDiGetClassDevsW(&g_kbd_iface, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA ifd = { sizeof(ifd) };
    DetailW det;
    det.cbSize = 8;                                      /* (sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) on x64) */
    WCHAR path[256] = L"";
    found = FALSE;
    for (DWORD i = 0; set != INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, NULL, &g_kbd_iface, i, &ifd); i++) {
        SP_DEVINFO_DATA dd = { sizeof(dd) };
        need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, NULL, 0, &need, NULL);
        if (need <= sizeof(det) && SetupDiGetDeviceInterfaceDetailW(set, &ifd, &det, sizeof(det), NULL, &dd) && dd.DevInst == kbd) {
            wcscpy(path, det.DevicePath);
            found = TRUE;
        }
    }
    CHECK("SetupDiGetClassDevs(GUID_DEVINTERFACE_KEYBOARD): the keyboard's interface", found);
    CHECK("its path is the one cfgmgr32 lists", !_wcsnicmp(path, L"\\\\?\\HID#VID_0627&PID_0001#", 26) &&
                                                wcsstr(path, L"#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}"));
    if (set != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(set);

    set = SetupDiCreateDeviceInfoList(NULL, NULL);
    SP_DEVINFO_DATA od = { sizeof(od) };
    CHECK("SetupDiOpenDeviceInfo by instance ID", SetupDiOpenDeviceInfoW(set, kbd_id, NULL, 0, &od) && od.DevInst == kbd);
    CHECK("SetupDiOpenDeviceInfo of no such device: ERROR_NO_SUCH_DEVINST",
          !SetupDiOpenDeviceInfoW(set, L"USB\\VID_046D&PID_C52B\\5&1", NULL, 0, &od) && GetLastError() == 0xE000020B);
    SP_DEVICE_INTERFACE_DATA oi = { sizeof(oi) };
    CHECK("SetupDiOpenDeviceInterface by path", SetupDiOpenDeviceInterfaceW(set, path, 0, &oi) &&
                                                !memcmp(&oi.InterfaceClassGuid, &g_kbd_iface, sizeof(GUID)));
    SetupDiDestroyDeviceInfoList(set);
}

int main(void)
{
    WCHAR id[200];
    DWORD kbd = 0;
    printf("[PNP] step 1: plug in a USB keyboard\n");
    fflush(stdout);
    for (int i = 0; i < 600 && !(kbd = keyboard(id)); i++) Sleep(100);
    CHECK("the keyboard came into the tree", kbd != 0);
    if (kbd) {
        printf("keyboard: %ls (device %lu)\n", id, (unsigned long)kbd);
        cfgmgr(kbd, id);
        setupapi(kbd, id);
    }
    printf("devenumtest: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
