/*
 * setupapi.dll — device installation and enumeration (SetupDi*).
 *
 * A device information set holds devices of the Configuration Manager's
 * device tree (cfgmgr32: the kernel's Plug and Play tree, and the game
 * controllers as the HID devices): SetupDiGetClassDevs picks them by setup
 * class (or all classes), by device interface class, by enumerator ("USB",
 * or one device's instance ID) and by being present; SetupDiOpenDeviceInfo
 * and SetupDiOpenDeviceInterface add one by name.  A device's properties,
 * registry keys and interface paths are cfgmgr32's (CM_DRP_* is SPDRP_* +
 * 1).  Under GUID_DEVINTERFACE_HID (HidD_GetHidGuid) the game controllers
 * are listed, with the paths CreateFile opens for hid.dll.
 */
#include <windows.h>
#include <string.h>

#define SETUPAPI __declspec(dllexport)
#define ERROR_NO_MORE_ITEMS_ 259
#define ERROR_INVALID_DATA_  13
#define ERROR_INVALID_USER_BUFFER 1784
#define ERROR_NO_SUCH_DEVINST_          0xE000020B
#define ERROR_NO_SUCH_DEVICE_INTERFACE_ 0xE0000225
#define DIGCF_PRESENT          0x02
#define DIGCF_ALLCLASSES       0x04
#define DIGCF_DEVICEINTERFACE  0x10
#define SPINT_ACTIVE           0x01
#define DIREG_DRV              0x02
#define SET_MAGIC              0x53455432u      /* "SET2" */
#define MAX_DEVS               640

typedef DWORD CONFIGRET;
#define CR_SUCCESS         0x00
#define CR_BUFFER_SMALL    0x1A
#define CM_LOCATE_DEVNODE_PHANTOM 0x01

__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW(PULONG len, LPCWSTR filter, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_ListW(LPCWSTR filter, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW(DWORD dn, ULONG prop, PULONG type, PVOID buf, PULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_PropertyW(DWORD dn, const void *key, PULONG type, PBYTE buf, PULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Open_DevNode_Key(DWORD dn, DWORD sam, ULONG profile, DWORD disp, HKEY *key, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags);
/* (setupapi's own exports of these, below, pass them to cfgmgr32) */
SETUPAPI DWORD WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags);
SETUPAPI DWORD WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DWORD dn, ULONG flags);
SETUPAPI DWORD WINAPI CM_Get_Device_IDW(DWORD dn, WCHAR *buf, ULONG len, ULONG flags);

typedef struct { DWORD cbSize; GUID InterfaceClassGuid; DWORD Flags; ULONG_PTR Reserved; } IfaceData;
typedef struct { DWORD cbSize; GUID ClassGuid; DWORD DevInst; ULONG_PTR Reserved; } DevData;
typedef struct { DWORD cbSize; WCHAR DevicePath[1]; } IfaceDetailW;
typedef struct { DWORD cbSize; CHAR DevicePath[1]; } IfaceDetailA;

/* A device information set: the devices picked when it was made, and
 * those opened into it since */
typedef struct {
    DWORD magic;
    int n;
    DWORD dev[MAX_DEVS];
} Set;

static Set *set_of(HANDLE h)
{
    Set *s = h;
    if (!s || h == INVALID_HANDLE_VALUE || s->magic != SET_MAGIC) { SetLastError(ERROR_INVALID_HANDLE); return NULL; }
    return s;
}

static BOOL instance_of(DWORD dn, WCHAR *id)
{
    return CM_Get_Device_IDW(dn, id, 200, 0) == CR_SUCCESS;
}

/* The setup class GUID of device @dn (GUID_NULL: none) */
static GUID class_of(DWORD dn)
{
    static const BYTE key[] = { 0x4e, 0x25, 0x5c, 0xa4, 0x1c, 0xdf, 0xfd, 0x4e, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0,
                                10, 0, 0, 0 };                    /* DEVPKEY_Device_ClassGuid */
    GUID g;
    ULONG type, len = sizeof(g);
    if (CM_Get_DevNode_PropertyW(dn, key, &type, (PBYTE)&g, &len, 0)) memset(&g, 0, sizeof(g));
    return g;
}

/* Device @dn's interface of class @cls: its path into @path (200), or FALSE */
static BOOL interface_of(DWORD dn, const GUID *cls, WCHAR *path)
{
    WCHAR id[200];
    ULONG len = 0;
    if (!instance_of(dn, id) || CM_Get_Device_Interface_List_SizeW(&len, (LPGUID)cls, id, 0) || len < 2) return FALSE;
    WCHAR *list = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    BOOL ok = list && !CM_Get_Device_Interface_ListW((LPGUID)cls, id, list, len, 0) && list[0];
    if (ok) lstrcpynW(path, list, 200);
    HeapFree(GetProcessHeap(), 0, list);
    return ok;
}

static BOOL present(DWORD dn)
{
    ULONG status, problem;
    return CM_Get_DevNode_Status(&status, &problem, dn, 0) == CR_SUCCESS;
}

static void add(Set *s, DWORD dn)
{
    for (int i = 0; i < s->n; i++) if (s->dev[i] == dn) return;
    if (s->n < MAX_DEVS) s->dev[s->n++] = dn;
}

/* Whether instance @id is under @enumerator: "USB" (the part before its
 * first '\'), or a whole instance ID */
static BOOL under(const WCHAR *id, const WCHAR *enumerator)
{
    int k = lstrlenW(enumerator);
    for (int i = 0; i < k; i++) if (enumerator[i] == L'\\') return !lstrcmpiW(id, enumerator);
    for (int i = 0; i < k; i++) {
        WCHAR a = id[i], b = enumerator[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return FALSE;
    }
    return id[k] == L'\\';
}

static HANDLE new_set(const GUID *cls, LPCWSTR enumerator, DWORD flags, BOOL pick)
{
    Set *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Set));
    if (!s) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE; }
    s->magic = SET_MAGIC;
    if (!pick) return s;
    if (!cls && !(flags & DIGCF_ALLCLASSES)) { s->magic = 0; HeapFree(GetProcessHeap(), 0, s); SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE; }
    ULONG len = 0;
    CM_Get_Device_ID_List_SizeW(&len, NULL, 0);
    WCHAR *ids = HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR));
    if (ids && CM_Get_Device_ID_ListW(NULL, ids, len + 1, 0) == CR_SUCCESS) {
        for (WCHAR *id = ids; *id; id += lstrlenW(id) + 1) {
            DWORD dn;
            WCHAR path[200];
            if (CM_Locate_DevNodeW(&dn, id, CM_LOCATE_DEVNODE_PHANTOM)) continue;
            if ((flags & DIGCF_PRESENT) && !present(dn)) continue;
            if (enumerator && enumerator[0] && !under(id, enumerator)) continue;
            if (flags & DIGCF_DEVICEINTERFACE) {
                if (cls && !(flags & DIGCF_ALLCLASSES) && !interface_of(dn, cls, path)) continue;
            } else if (!(flags & DIGCF_ALLCLASSES)) {
                GUID g = class_of(dn);
                if (memcmp(&g, cls, sizeof(GUID))) continue;
            }
            add(s, dn);
        }
    }
    HeapFree(GetProcessHeap(), 0, ids);
    return s;
}

SETUPAPI HANDLE WINAPI SetupDiGetClassDevsW(const GUID *cls, LPCWSTR enumerator, HWND parent, DWORD flags)
{ (void)parent; return new_set(cls, enumerator, flags, TRUE); }
SETUPAPI HANDLE WINAPI SetupDiGetClassDevsA(const GUID *cls, LPCSTR enumerator, HWND parent, DWORD flags)
{
    (void)parent;
    WCHAR w[200];
    if (enumerator) MultiByteToWideChar(CP_ACP, 0, enumerator, -1, w, 200);
    return new_set(cls, enumerator ? w : NULL, flags, TRUE);
}
SETUPAPI HANDLE WINAPI SetupDiGetClassDevsExW(const GUID *cls, LPCWSTR enumerator, HWND parent, DWORD flags, HANDLE set, LPCWSTR machine, PVOID r)
{ (void)parent; (void)set; (void)machine; (void)r; return new_set(cls, enumerator, flags, TRUE); }
SETUPAPI HANDLE WINAPI SetupDiCreateDeviceInfoList(const GUID *cls, HWND parent) { (void)cls; (void)parent; return new_set(NULL, NULL, 0, FALSE); }
SETUPAPI BOOL WINAPI SetupDiDestroyDeviceInfoList(HANDLE set)
{
    Set *s = set_of(set);
    if (!s) return FALSE;
    s->magic = 0;
    HeapFree(GetProcessHeap(), 0, s);
    return TRUE;
}

static BOOL no_more(void) { SetLastError(ERROR_NO_MORE_ITEMS_); return FALSE; }

static void dev_data(Set *s, int i, DevData *d)
{
    d->ClassGuid = class_of(s->dev[i]);
    d->DevInst = s->dev[i];
    d->Reserved = (ULONG_PTR)i + 1;
}

SETUPAPI BOOL WINAPI SetupDiEnumDeviceInfo(HANDLE set, DWORD i, PVOID info)
{
    Set *s = set_of(set);
    if (!s) return FALSE;
    DevData *d = info;
    if (!d || d->cbSize != sizeof(DevData)) { SetLastError(ERROR_INVALID_USER_BUFFER); return FALSE; }
    if (i >= (DWORD)s->n) return no_more();
    dev_data(s, (int)i, d);
    return TRUE;
}

/* The set's device @info names (its index), or -1 */
static int dev_index(Set *s, const void *info, BOOL iface)
{
    ULONG_PTR r = info ? iface ? ((const IfaceData *)info)->Reserved : ((const DevData *)info)->Reserved : 0;
    if (!r || r > (ULONG_PTR)s->n) { SetLastError(ERROR_INVALID_PARAMETER); return -1; }
    return (int)r - 1;
}

/* Takes the device out of the set (not off the system); the devices after
 * it move up one, as SetupDiEnumDeviceInfo then lists them */
SETUPAPI BOOL WINAPI SetupDiDeleteDeviceInfo(HANDLE set, PVOID info)
{
    Set *s = set_of(set);
    if (!s) return FALSE;
    int i = dev_index(s, info, FALSE);
    if (i < 0) return FALSE;
    memmove(&s->dev[i], &s->dev[i + 1], (size_t)(s->n - i - 1) * sizeof(s->dev[0]));
    s->n--;
    return TRUE;
}

/* (each device has one interface of a class at most) */
SETUPAPI BOOL WINAPI SetupDiEnumDeviceInterfaces(HANDLE set, PVOID dev, const GUID *cls, DWORD i, PVOID info)
{
    Set *s = set_of(set);
    if (!s) return FALSE;
    IfaceData *d = info;
    if (!d || d->cbSize != sizeof(IfaceData)) { SetLastError(ERROR_INVALID_USER_BUFFER); return FALSE; }
    if (!cls) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    int first = 0, last = s->n;
    if (dev) { if ((first = dev_index(s, dev, FALSE)) < 0) return FALSE; last = first + 1; }
    WCHAR path[200];
    for (int k = first; k < last; k++) {
        if (!interface_of(s->dev[k], cls, path)) continue;
        if (i--) continue;
        d->InterfaceClassGuid = *cls;
        d->Flags = SPINT_ACTIVE;
        d->Reserved = (ULONG_PTR)k + 1;
        return TRUE;
    }
    return no_more();
}

static BOOL iface_detail(HANDLE set, PVOID iface, PVOID detail, DWORD n, PDWORD need, PVOID dev, BOOL wide)
{
    Set *s = set_of(set);
    if (!s) return FALSE;
    int i = dev_index(s, iface, TRUE);
    if (i < 0) return FALSE;
    WCHAR path[200];
    if (!interface_of(s->dev[i], &((IfaceData *)iface)->InterfaceClassGuid, path)) { SetLastError(ERROR_NO_SUCH_DEVICE_INTERFACE_); return FALSE; }
    DWORD len = (DWORD)lstrlenW(path) + 1;
    DWORD head = (DWORD)__builtin_offsetof(IfaceDetailW, DevicePath);
    DWORD size = head + len * (wide ? sizeof(WCHAR) : 1);
    if (need) *need = size;
    if (dev) {
        if (((DevData *)dev)->cbSize != sizeof(DevData)) { SetLastError(ERROR_INVALID_USER_BUFFER); return FALSE; }
        dev_data(s, i, dev);
    }
    if (!detail) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    DWORD want = wide ? sizeof(IfaceDetailW) : sizeof(IfaceDetailA);
    if (((IfaceDetailW *)detail)->cbSize != want) { SetLastError(ERROR_INVALID_USER_BUFFER); return FALSE; }
    if (n < size) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    for (DWORD k = 0; k < len; k++) {
        if (wide) ((IfaceDetailW *)detail)->DevicePath[k] = path[k];
        else ((IfaceDetailA *)detail)->DevicePath[k] = (CHAR)path[k];
    }
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HANDLE set, PVOID iface, PVOID detail, DWORD n, PDWORD need, PVOID dev)
{ return iface_detail(set, iface, detail, n, need, dev, TRUE); }
SETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceDetailA(HANDLE set, PVOID iface, PVOID detail, DWORD n, PDWORD need, PVOID dev)
{ return iface_detail(set, iface, detail, n, need, dev, FALSE); }

static BOOL registry_property(HANDLE set, PVOID dev, DWORD prop, PDWORD type, PBYTE buf, DWORD n, PDWORD need, BOOL wide)
{
    Set *s = set_of(set);
    if (!s) return FALSE;
    int i = dev_index(s, dev, FALSE);
    if (i < 0) return FALSE;
    WCHAR v[256];
    ULONG t = 0, len = sizeof(v);
    CONFIGRET cr = CM_Get_DevNode_Registry_PropertyW(s->dev[i], prop + 1, &t, v, &len, 0);
    if (cr) { SetLastError(ERROR_INVALID_DATA_); return FALSE; }
    DWORD size = t == REG_DWORD || wide ? len : len / sizeof(WCHAR);
    if (type) *type = t;
    if (need) *need = size;
    if (!buf || n < size) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    if (t == REG_DWORD || wide) memcpy(buf, v, size);
    else for (DWORD k = 0; k < size; k++) buf[k] = (BYTE)v[k];
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupDiGetDeviceRegistryPropertyW(HANDLE set, PVOID dev, DWORD prop, PDWORD type, PBYTE buf, DWORD n, PDWORD need)
{ return registry_property(set, dev, prop, type, buf, n, need, TRUE); }
SETUPAPI BOOL WINAPI SetupDiGetDeviceRegistryPropertyA(HANDLE set, PVOID dev, DWORD prop, PDWORD type, PBYTE buf, DWORD n, PDWORD need)
{ return registry_property(set, dev, prop, type, buf, n, need, FALSE); }

/* Unified device properties (DEVPKEY_*): cfgmgr32's */
SETUPAPI BOOL WINAPI SetupDiGetDevicePropertyW(HANDLE set, PVOID dev, const void *key, PULONG type, PBYTE buf, DWORD n, PDWORD need, DWORD flags)
{
    (void)flags;
    Set *s = set_of(set);
    if (!s) return FALSE;
    int i = dev_index(s, dev, FALSE);
    if (i < 0) return FALSE;
    if (!key || !type) { SetLastError(ERROR_INVALID_USER_BUFFER); return FALSE; }
    ULONG len = buf ? n : 0;
    CONFIGRET cr = CM_Get_DevNode_PropertyW(s->dev[i], key, type, buf, &len, 0);
    if (need) *need = len;
    if (cr == CR_BUFFER_SMALL) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    if (cr) { *type = 0; SetLastError(ERROR_NOT_FOUND); return FALSE; }
    return TRUE;
}

static BOOL instance_id_of(HANDLE set, PVOID dev, void *id, DWORD n, PDWORD need, BOOL wide)
{
    Set *s = set_of(set);
    if (!s) return FALSE;
    int i = dev_index(s, dev, FALSE);
    if (i < 0) return FALSE;
    WCHAR v[200];
    if (!instance_of(s->dev[i], v)) { SetLastError(ERROR_NO_SUCH_DEVINST_); return FALSE; }
    DWORD len = (DWORD)lstrlenW(v) + 1;
    if (need) *need = len;
    if (!id || n < len) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    for (DWORD k = 0; k < len; k++) {
        if (wide) ((WCHAR *)id)[k] = v[k];
        else ((CHAR *)id)[k] = (CHAR)v[k];
    }
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupDiGetDeviceInstanceIdW(HANDLE set, PVOID dev, LPWSTR id, DWORD n, PDWORD need)
{ return instance_id_of(set, dev, id, n, need, TRUE); }
SETUPAPI BOOL WINAPI SetupDiGetDeviceInstanceIdA(HANDLE set, PVOID dev, LPSTR id, DWORD n, PDWORD need)
{ return instance_id_of(set, dev, id, n, need, FALSE); }

/* A device's hardware key (DIREG_DEV: Enum\<instance>\Device Parameters)
 * or software key (DIREG_DRV), as cfgmgr32 opens them */
SETUPAPI HKEY WINAPI SetupDiOpenDevRegKey(HANDLE set, PVOID dev, DWORD scope, DWORD profile, DWORD type, REGSAM sam)
{
    (void)scope; (void)profile;
    Set *s = set_of(set);
    if (!s) return INVALID_HANDLE_VALUE;
    int i = dev_index(s, dev, FALSE);
    if (i < 0) return INVALID_HANDLE_VALUE;
    HKEY k;
    if (CM_Open_DevNode_Key(s->dev[i], sam, 0, 0, &k, type == DIREG_DRV ? 1 : 0)) {
        SetLastError(ERROR_FILE_NOT_FOUND);              /* (ERROR_KEY_DOES_NOT_EXIST on Windows) */
        return INVALID_HANDLE_VALUE;
    }
    return k;
}

/* ---- opening a device or device interface by name ---- */

static BOOL open_device(HANDLE set, const WCHAR *id, PVOID dev)
{
    Set *s = set_of(set);
    if (!s) return FALSE;
    if (!id) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    DWORD dn;
    if (CM_Locate_DevNodeW(&dn, id, CM_LOCATE_DEVNODE_PHANTOM)) { SetLastError(ERROR_NO_SUCH_DEVINST_); return FALSE; }
    add(s, dn);
    if (dev) {
        if (((DevData *)dev)->cbSize != sizeof(DevData)) { SetLastError(ERROR_INVALID_USER_BUFFER); return FALSE; }
        for (int i = 0; i < s->n; i++) if (s->dev[i] == dn) dev_data(s, i, dev);
    }
    return TRUE;
}
SETUPAPI BOOL WINAPI SetupDiOpenDeviceInfoW(HANDLE set, LPCWSTR id, HWND parent, DWORD flags, PVOID dev)
{ (void)parent; (void)flags; return open_device(set, id, dev); }
SETUPAPI BOOL WINAPI SetupDiOpenDeviceInfoA(HANDLE set, LPCSTR id, HWND parent, DWORD flags, PVOID dev)
{
    (void)parent; (void)flags;
    WCHAR w[200];
    if (id) MultiByteToWideChar(CP_ACP, 0, id, -1, w, 200);
    return open_device(set, id ? w : NULL, dev);
}

/* "\\?\<instance with '#' for '\'>#{guid}": the device and the class */
static BOOL open_interface(HANDLE set, const WCHAR *path, PVOID iface)
{
    Set *s = set_of(set);
    if (!s) return FALSE;
    if (!path) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    int len = lstrlenW(path), hash = -1;
    for (int i = 0; i < len; i++) if (path[i] == L'#') hash = i;
    WCHAR id[200], gs[40];
    GUID g;
    if (len < 4 || hash < 4 || hash - 4 >= 200 || len - hash - 1 != 38) { SetLastError(ERROR_NO_SUCH_DEVICE_INTERFACE_); return FALSE; }
    for (int i = 4; i < hash; i++) id[i - 4] = path[i] == L'#' ? L'\\' : path[i];
    id[hash - 4] = 0;
    lstrcpynW(gs, path + hash + 1, 40);
    typedef DWORD (WINAPI *clsid_fn)(LPCWSTR, GUID *);
    clsid_fn parse = (clsid_fn)GetProcAddress(LoadLibraryA("ole32.dll"), "CLSIDFromString");
    DWORD dn;
    WCHAR found[200];
    if (!parse || parse(gs, &g) || CM_Locate_DevNodeW(&dn, id, 0) || !interface_of(dn, &g, found) || lstrcmpiW(found, path)) {
        SetLastError(ERROR_NO_SUCH_DEVICE_INTERFACE_);
        return FALSE;
    }
    add(s, dn);
    IfaceData *d = iface;
    if (d) {
        if (d->cbSize != sizeof(IfaceData)) { SetLastError(ERROR_INVALID_USER_BUFFER); return FALSE; }
        for (int i = 0; i < s->n; i++) if (s->dev[i] == dn) d->Reserved = (ULONG_PTR)i + 1;
        d->InterfaceClassGuid = g;
        d->Flags = SPINT_ACTIVE;
    }
    return TRUE;
}
SETUPAPI BOOL WINAPI SetupDiOpenDeviceInterfaceW(HANDLE set, LPCWSTR path, DWORD flags, PVOID iface)
{ (void)flags; return open_interface(set, path, iface); }
SETUPAPI BOOL WINAPI SetupDiOpenDeviceInterfaceA(HANDLE set, LPCSTR path, DWORD flags, PVOID iface)
{
    (void)flags;
    WCHAR w[260];
    if (path) MultiByteToWideChar(CP_ACP, 0, path, -1, w, 260);
    return open_interface(set, path ? w : NULL, iface);
}

static BOOL no_device(void) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }

/* Device setup classes exist without devices: the class GUIDs Windows
 * defines for the names programs ask about, and the descriptions Device
 * Manager shows for them (SetupDiGetClassDescription) */
#define SC(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) { a, b, c, { d0, d1, d2, d3, d4, d5, d6, d7 } }
#define STD(x) SC(x, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18)
static const struct { const char *name; GUID guid; const char *desc; } g_classes[] = {
    { "AudioEndpoint", SC(0xc166523c, 0xfe0c, 0x4a94, 0xa5, 0x86, 0xf1, 0xa8, 0x0c, 0xfb, 0xbf, 0x3e), "Audio inputs and outputs" },
    { "Battery",      SC(0x72631e54, 0x78a4, 0x11d0, 0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a), "Batteries" },
    { "Bluetooth",    SC(0xe0cbf06c, 0xcd8b, 0x4647, 0xbb, 0x8a, 0x26, 0x3b, 0x43, 0xf0, 0xf9, 0x74), "Bluetooth" },
    { "Camera",       SC(0xca3e7ab9, 0xb4c3, 0x4ae6, 0x82, 0x51, 0x57, 0x9e, 0xf9, 0x33, 0x89, 0x0f), "Cameras" },
    { "Computer",     STD(0x4d36e966), "Computer" },
    { "DiskDrive",    STD(0x4d36e967), "Disk drives" },
    { "Display",      STD(0x4d36e968), "Display adapters" },
    { "HDC",          STD(0x4d36e96a), "IDE ATA/ATAPI controllers" },
    { "HIDClass",     SC(0x745a17a0, 0x74d3, 0x11d0, 0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda), "Human Interface Devices" },
    { "Image",        SC(0x6bdd1fc6, 0x810f, 0x11d0, 0xbe, 0xc7, 0x08, 0x00, 0x2b, 0xe2, 0x09, 0x2f), "Imaging devices" },
    { "Keyboard",     STD(0x4d36e96b), "Keyboards" },
    { "Media",        STD(0x4d36e96c), "Sound, video and game controllers" },
    { "Monitor",      STD(0x4d36e96e), "Monitors" },
    { "Mouse",        STD(0x4d36e96f), "Mice and other pointing devices" },
    { "Net",          STD(0x4d36e972), "Network adapters" },
    { "Ports",        STD(0x4d36e978), "Ports (COM & LPT)" },
    { "Processor",    SC(0x50127dc3, 0x0f36, 0x415e, 0xa6, 0xcc, 0x4c, 0xb3, 0xbe, 0x91, 0x0b, 0x65), "Processors" },
    { "SCSIAdapter",  STD(0x4d36e97b), "Storage controllers" },
    { "SoftwareDevice", SC(0x62f9c741, 0xb25a, 0x46ce, 0xb5, 0x4c, 0x9b, 0xcc, 0xce, 0x08, 0xb6, 0xf2), "Software devices" },
    { "System",       STD(0x4d36e97d), "System devices" },
    { "USB",          SC(0x36fc9e60, 0xc465, 0x11cf, 0x80, 0x56, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00), "Universal Serial Bus controllers" },
    { "USBDevice",    SC(0x88bae032, 0x5a81, 0x49f0, 0xbc, 0x3d, 0xa4, 0xff, 0x13, 0x82, 0x16, 0xd6), "Universal Serial Bus devices" },
    { "Volume",       SC(0x71a27cdd, 0x812a, 0x11d0, 0xbe, 0xc7, 0x08, 0x00, 0x2b, 0xe2, 0x09, 0x2f), "Storage volumes" },
    { "XnaComposite", SC(0x05f5cfe2, 0x4733, 0x4950, 0xa6, 0xbb, 0x07, 0xaa, 0xd0, 0x1a, 0x3a, 0x84), "Xbox 360 Peripherals" },
};
#define N_CLASSES (sizeof(g_classes) / sizeof(g_classes[0]))

static int class_index(const GUID *g)
{
    /* (the GUID NovaOS's device tree gives Xbox 360 controllers: novapnp.h) */
    static const GUID xna = SC(0xd61ca365, 0x5af4, 0x4486, 0x99, 0x8b, 0x9d, 0xb4, 0x73, 0x4c, 0x6c, 0xa3);
    if (!g) return -1;
    if (!memcmp(g, &xna, sizeof(GUID))) g = &g_classes[N_CLASSES - 1].guid;
    for (unsigned i = 0; i < N_CLASSES; i++) if (!memcmp(&g_classes[i].guid, g, sizeof(GUID))) return (int)i;
    return -1;
}

/* "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" (lower case, as the Class key's names) */
static BOOL StringFromGUID2_(const GUID *g, WCHAR *out)
{
    static const char hex[] = "0123456789abcdef";
    static const signed char order[] = { 3, 2, 1, 0, -1, 5, 4, -1, 7, 6, -1, 8, 9, -1, 10, 11, 12, 13, 14, 15 };
    const BYTE *b = (const BYTE *)g;
    int j = 0;
    out[j++] = L'{';
    for (unsigned i = 0; i < sizeof(order); i++) {
        if (order[i] < 0) { out[j++] = L'-'; continue; }
        out[j++] = (WCHAR)hex[b[order[i]] >> 4];
        out[j++] = (WCHAR)hex[b[order[i]] & 15];
    }
    out[j++] = L'}';
    out[j] = 0;
    return TRUE;
}

/* The class's own key, Control\Class\{guid}: its default value is the
 * description an installed class gives itself, "Class" its name */
static BOOL class_value(const GUID *g, const WCHAR *name, WCHAR *out, DWORD cap)
{
    WCHAR k[100], gs[40];
    if (!g || !StringFromGUID2_(g, gs)) return FALSE;
    lstrcpyW(k, L"SYSTEM\\CurrentControlSet\\Control\\Class\\");
    lstrcatW(k, gs);
    HKEY h;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &h)) return FALSE;
    DWORD t, l = (cap - 1) * sizeof(WCHAR);
    LONG r = RegQueryValueExW(h, name, NULL, &t, (BYTE *)out, &l);
    RegCloseKey(h);
    if (r || t != REG_SZ || l < sizeof(WCHAR)) return FALSE;
    out[l / sizeof(WCHAR)] = 0;
    return out[0] != 0;
}

/* @g's description (@name: its name) into @out */
static BOOL class_text(const GUID *g, BOOL name, void *out, DWORD n, PDWORD need, BOOL wide)
{
    WCHAR v[128];
    int i = class_index(g);
    if (!class_value(g, name ? L"Class" : NULL, v, 128)) {
        if (i < 0) { SetLastError(0xE0000209 /* ERROR_INVALID_CLASS */); return FALSE; }
        MultiByteToWideChar(CP_ACP, 0, name ? g_classes[i].name : g_classes[i].desc, -1, v, 128);
    }
    DWORD len = (DWORD)lstrlenW(v) + 1;
    if (need) *need = len;
    if (!out || n < len) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    if (wide) lstrcpyW(out, v);
    else WideCharToMultiByte(CP_ACP, 0, v, -1, out, (int)n, NULL, NULL);
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupDiGetClassDescriptionW(const GUID *g, PWSTR out, DWORD n, PDWORD need)
{ return class_text(g, FALSE, out, n, need, TRUE); }
SETUPAPI BOOL WINAPI SetupDiGetClassDescriptionA(const GUID *g, LPSTR out, DWORD n, PDWORD need)
{ return class_text(g, FALSE, out, n, need, FALSE); }
SETUPAPI BOOL WINAPI SetupDiGetClassDescriptionExW(const GUID *g, PWSTR out, DWORD n, PDWORD need, PCWSTR machine, PVOID r)
{ (void)machine; (void)r; return class_text(g, FALSE, out, n, need, TRUE); }
SETUPAPI BOOL WINAPI SetupDiClassNameFromGuidW(const GUID *g, PWSTR out, DWORD n, PDWORD need)
{ return class_text(g, TRUE, out, n, need, TRUE); }
SETUPAPI BOOL WINAPI SetupDiClassNameFromGuidA(const GUID *g, LPSTR out, DWORD n, PDWORD need)
{ return class_text(g, TRUE, out, n, need, FALSE); }
SETUPAPI BOOL WINAPI SetupDiClassNameFromGuidExW(const GUID *g, PWSTR out, DWORD n, PDWORD need, PCWSTR machine, PVOID r)
{ (void)machine; (void)r; return class_text(g, TRUE, out, n, need, TRUE); }

SETUPAPI BOOL WINAPI SetupDiClassGuidsFromNameA(LPCSTR name, GUID *list, DWORD n, PDWORD need)
{
    if (!name || !need) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    DWORD k = 0;
    for (DWORD i = 0; i < N_CLASSES; i++)
        if (!lstrcmpiA(name, g_classes[i].name)) { if (list && k < n) list[k] = g_classes[i].guid; k++; }
    *need = k;
    if (k > n) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    return TRUE;
}

SETUPAPI HKEY WINAPI SetupDiOpenDeviceInterfaceRegKey(HANDLE set, PVOID iface, DWORD reserved, REGSAM sam)
{
    (void)set; (void)iface; (void)reserved; (void)sam;
    SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_HANDLE_VALUE;
}
SETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceAlias(HANDLE set, PVOID iface, const GUID *cls, PVOID alias)
{ (void)set; (void)iface; (void)cls; (void)alias; return no_device(); }

/* setupapi.dll carries Configuration Manager calls too (cfgmgr32.dll's) */
typedef DWORD (WINAPI *cm_locatew_fn)(PDWORD, LPCWSTR, ULONG);
static FARPROC cfgmgr(const char *fn);
SETUPAPI DWORD WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags)
{
    cm_locatew_fn f = (cm_locatew_fn)cfgmgr("CM_Locate_DevNodeW");
    return f ? f(dn, id, flags) : 0x0D /* CR_NO_SUCH_DEVNODE */;
}

SETUPAPI BOOL WINAPI SetupDiClassGuidsFromNameW(LPCWSTR name, GUID *list, DWORD n, PDWORD need)
{
    char a[64];
    if (!name || !WideCharToMultiByte(CP_ACP, 0, name, -1, a, sizeof(a), 0, 0)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return SetupDiClassGuidsFromNameA(a, list, n, need);
}

/* The CM_* calls setupapi exports for cfgmgr32 (forwarders on Windows) */
typedef DWORD (WINAPI *cm_status_fn)(PULONG, PULONG, DWORD, ULONG);
typedef DWORD (WINAPI *cm_locate_fn)(PDWORD, LPCSTR, ULONG);
static FARPROC cfgmgr(const char *fn)
{
    HMODULE m = LoadLibraryA("cfgmgr32.dll");
    return m ? GetProcAddress(m, fn) : 0;
}
SETUPAPI DWORD WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DWORD dn, ULONG flags)
{
    cm_status_fn f = (cm_status_fn)cfgmgr("CM_Get_DevNode_Status");
    return f ? f(status, problem, dn, flags) : 0x0D /* CR_NO_SUCH_DEVNODE */;
}
SETUPAPI DWORD WINAPI CM_Locate_DevNodeA(PDWORD dn, LPCSTR id, ULONG flags)
{
    cm_locate_fn f = (cm_locate_fn)cfgmgr("CM_Locate_DevNodeA");
    return f ? f(dn, id, flags) : 0x0D /* CR_NO_SUCH_DEVNODE */;
}
typedef DWORD (WINAPI *cm_parent_fn)(PDWORD, DWORD, ULONG);
typedef DWORD (WINAPI *cm_id_fn)(DWORD, void *, ULONG, ULONG);
SETUPAPI DWORD WINAPI CM_Get_Parent(PDWORD parent, DWORD dn, ULONG flags)
{
    cm_parent_fn f = (cm_parent_fn)cfgmgr("CM_Get_Parent");
    return f ? f(parent, dn, flags) : 0x0D /* CR_NO_SUCH_DEVNODE */;
}
SETUPAPI DWORD WINAPI CM_Get_Device_IDA(DWORD dn, char *buf, ULONG len, ULONG flags)
{
    cm_id_fn f = (cm_id_fn)cfgmgr("CM_Get_Device_IDA");
    return f ? f(dn, buf, len, flags) : 0x0D /* CR_NO_SUCH_DEVNODE */;
}
SETUPAPI DWORD WINAPI CM_Get_Device_IDW(DWORD dn, WCHAR *buf, ULONG len, ULONG flags)
{
    cm_id_fn f = (cm_id_fn)cfgmgr("CM_Get_Device_IDW");
    return f ? f(dn, buf, len, flags) : 0x0D /* CR_NO_SUCH_DEVNODE */;
}
typedef DWORD (WINAPI *cm_id_size_fn)(PULONG, DWORD, ULONG);
SETUPAPI DWORD WINAPI CM_Get_Device_ID_Size(PULONG len, DWORD dn, ULONG flags)
{
    cm_id_size_fn f = (cm_id_size_fn)cfgmgr("CM_Get_Device_ID_Size");
    return f ? f(len, dn, flags) : 0x0D /* CR_NO_SUCH_DEVNODE */;
}
