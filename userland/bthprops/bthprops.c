/*
 * bthprops.cpl — Bluetooth's classic API (Windows keeps it in the
 * Bluetooth Control Panel item, which forwards to BluetoothApis.dll)
 *
 * A radio is the kernel's Bluetooth host (kernel/nt/bthport.c) behind a
 * device interface, GUID_BTHPORT_DEVICE_INTERFACE, which
 * BluetoothFindFirstRadio lists through cfgmgr32 and opens, as Windows'
 * does.  Its control codes are in userland/include/novabth.h: Windows'
 * documented ones (IOCTL_BTH_GET_LOCAL_INFO, IOCTL_BTH_GET_DEVICE_INFO)
 * and NovaOS's own for what Windows does through private ones (an
 * inquiry, pairing, a service switched on or off, a device forgotten).
 *
 *   - radios: BluetoothFindFirstRadio/NextRadio/RadioClose,
 *     BluetoothGetRadioInfo, BluetoothIsConnectable/IsDiscoverable,
 *     BluetoothEnableDiscovery/EnableIncomingConnections;
 *   - devices: BluetoothFindFirstDevice/NextDevice/DeviceClose (with
 *     fIssueInquiry: a new inquiry of cTimeoutMultiplier x 1.28 s first),
 *     BluetoothGetDeviceInfo, BluetoothRemoveDevice;
 *   - pairing: BluetoothAuthenticateDevice(Ex), with no dialog: the host
 *     answers Secure Simple Pairing as "Just Works" and a legacy device's
 *     PIN request with the passkey given (or 0000);
 *   - services: BluetoothEnumerateInstalledServices (the ones switched
 *     on), BluetoothSetServiceState (on: the service gets its device
 *     node and driver; HID's is hidbth).
 *
 * With no radio, the searches end at once with ERROR_NO_MORE_ITEMS, as on
 * a PC whose Bluetooth is absent or switched off, and programs such as
 * Chromium's (Qt WebEngine) report Bluetooth as unavailable.
 *
 * The Service Discovery Protocol parsers (BluetoothSdp*) need no radio:
 * they read SDP records, the big-endian data elements of the Bluetooth
 * Core Specification (Vol 3, Part B, 3), and work in full.
 */
#include <windows.h>
#include "../include/novabth.h"

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

#ifndef ERROR_NOT_AUTHENTICATED
#define ERROR_NOT_AUTHENTICATED      1244
#endif
#ifndef ERROR_SERVICE_DOES_NOT_EXIST
#define ERROR_SERVICE_DOES_NOT_EXIST 1060
#endif

__declspec(dllimport) DWORD WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags);
__declspec(dllimport) DWORD WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags);

#define BTAPI __declspec(dllexport)
#define ERROR_REVISION_MISMATCH_ 1306

typedef struct { DWORD dwSize; } FIND_RADIO_PARAMS_;
typedef struct { DWORD dwSize; } SIZED_;               /* (every Bluetooth structure starts with dwSize) */

typedef struct {                                       /* BLUETOOTH_RADIO_INFO */
    DWORD dwSize;
    ULONGLONG address;
    WCHAR szName[248];
    ULONG ulClassofDevice;
    USHORT lmpSubversion, manufacturer;
} RADIO_INFO_;

typedef struct {                                       /* BLUETOOTH_DEVICE_INFO */
    DWORD dwSize;
    ULONGLONG Address;
    ULONG ulClassofDevice;
    BOOL fConnected, fRemembered, fAuthenticated;
    SYSTEMTIME stLastSeen, stLastUsed;
    WCHAR szName[248];
} DEVICE_INFO_;

typedef struct {                                       /* BLUETOOTH_DEVICE_SEARCH_PARAMS */
    DWORD dwSize;
    BOOL fReturnAuthenticated, fReturnRemembered, fReturnUnknown, fReturnConnected, fIssueInquiry;
    UCHAR cTimeoutMultiplier;
    HANDLE hRadio;
} SEARCH_PARAMS_;

_Static_assert(sizeof(RADIO_INFO_) == 520 && sizeof(DEVICE_INFO_) == 560, "BLUETOOTH_RADIO_INFO, BLUETOOTH_DEVICE_INFO");
_Static_assert(sizeof(SEARCH_PARAMS_) == (sizeof(void *) == 8 ? 40 : 32), "BLUETOOTH_DEVICE_SEARCH_PARAMS");

static GUID g_radio_guid = NOVABTH_RADIO_GUID_INIT;
/* {0000xxxx-0000-1000-8000-00805F9B34FB}: Bluetooth's base UUID */
static const GUID g_base = { 0x00000000, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };

static BOOL bad_size(const void *p, DWORD want)
{
    if (!p) { SetLastError(ERROR_INVALID_PARAMETER); return TRUE; }
    if (((const SIZED_ *)p)->dwSize != want) { SetLastError(ERROR_REVISION_MISMATCH_); return TRUE; }
    return FALSE;
}

static BOOL ioctl(HANDLE h, DWORD code, void *in, DWORD in_len, void *out, DWORD out_len, DWORD *got)
{
    DWORD n = 0;
    BOOL ok = DeviceIoControl(h, code, in, in_len, out, out_len, &n, NULL);
    if (got) *got = n;
    return ok;
}

static void wide(WCHAR *out, int cap, const char *utf8)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap)) out[0] = 0;
    out[cap - 1] = 0;
}

/* ---- radios ---- */

typedef struct { WCHAR *list, *next; } RadioFind;

static HANDLE open_next(RadioFind *f)
{
    while (f->next && *f->next) {
        WCHAR *path = f->next;
        f->next += lstrlenW(path) + 1;
        HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) return h;
    }
    return NULL;
}

BTAPI HANDLE WINAPI BluetoothFindFirstRadio(const FIND_RADIO_PARAMS_ *params, HANDLE *radio)
{
    if (radio) *radio = 0;
    if (!radio) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (bad_size(params, sizeof(FIND_RADIO_PARAMS_))) return 0;
    ULONG len = 0;
    if (CM_Get_Device_Interface_List_SizeW(&len, &g_radio_guid, NULL, 0) || len < 2) { SetLastError(ERROR_NO_MORE_ITEMS); return 0; }
    RadioFind *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f) + len * sizeof(WCHAR));
    if (!f) { SetLastError(ERROR_OUTOFMEMORY); return 0; }
    f->list = f->next = (WCHAR *)(f + 1);
    if (CM_Get_Device_Interface_ListW(&g_radio_guid, NULL, f->list, len, 0) || !(*radio = open_next(f))) {
        HeapFree(GetProcessHeap(), 0, f);
        SetLastError(ERROR_NO_MORE_ITEMS);
        return 0;
    }
    return f;
}

BTAPI BOOL WINAPI BluetoothFindNextRadio(HANDLE find, HANDLE *radio)
{
    if (radio) *radio = 0;
    if (!find || !radio) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (!(*radio = open_next(find))) { SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }
    return TRUE;
}

BTAPI BOOL WINAPI BluetoothFindRadioClose(HANDLE find)
{
    if (!find) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    HeapFree(GetProcessHeap(), 0, find);
    return TRUE;
}

/* @radio, or the first radio when it is NULL (*@own: close it after) */
static HANDLE radio_or_first(HANDLE radio, BOOL *own)
{
    *own = FALSE;
    if (radio) return radio;
    FIND_RADIO_PARAMS_ p = { sizeof(p) };
    HANDLE r = 0, f = BluetoothFindFirstRadio(&p, &r);
    if (!f) return NULL;
    BluetoothFindRadioClose(f);
    *own = TRUE;
    return r;
}

static BOOL local_info(HANDLE radio, NovaBthLocalRadioInfo *li)
{
    return ioctl(radio, IOCTL_BTH_GET_LOCAL_INFO, NULL, 0, li, sizeof(*li), NULL);
}

BTAPI DWORD WINAPI BluetoothGetRadioInfo(HANDLE radio, RADIO_INFO_ *info)
{
    if (!radio || !info) return ERROR_INVALID_PARAMETER;
    if (info->dwSize != sizeof(*info)) return ERROR_REVISION_MISMATCH_;
    NovaBthLocalRadioInfo li;
    if (!local_info(radio, &li)) return GetLastError() ? GetLastError() : ERROR_INVALID_HANDLE;
    info->address = li.localInfo.address;
    wide(info->szName, 248, li.localInfo.name);
    info->ulClassofDevice = li.localInfo.classOfDevice;
    info->lmpSubversion = li.radioInfo.lmpSubversion;
    info->manufacturer = li.radioInfo.mfg;
    return ERROR_SUCCESS;
}

static BOOL radio_flag(HANDLE radio, unsigned int flag)
{
    BOOL own;
    HANDLE r = radio_or_first(radio, &own);
    NovaBthLocalRadioInfo li;
    BOOL on = r && local_info(r, &li) && (li.flags & flag);
    if (own) CloseHandle(r);
    return on;
}

static BOOL set_scan(HANDLE radio, unsigned int flag, BOOL on)
{
    BOOL own;
    HANDLE r = radio_or_first(radio, &own);
    NovaBthLocalRadioInfo li;
    BOOL ok = FALSE;
    if (r && local_info(r, &li)) {
        UINT32 scan = (li.flags & NOVABTH_RADIO_DISCOVERABLE ? NOVABTH_SCAN_INQUIRY : 0) |
                      (li.flags & NOVABTH_RADIO_CONNECTABLE ? NOVABTH_SCAN_PAGE : 0);
        scan = on ? scan | flag : scan & ~flag;
        if (flag == NOVABTH_SCAN_INQUIRY && on) scan |= NOVABTH_SCAN_PAGE;      /* (discoverable needs connectable) */
        if (flag == NOVABTH_SCAN_PAGE && !on) scan = 0;
        ok = ioctl(r, IOCTL_NOVABTH_SET_SCAN, &scan, sizeof(scan), NULL, 0, NULL);
    }
    if (own) CloseHandle(r);
    return ok;
}

BTAPI BOOL WINAPI BluetoothIsConnectable(HANDLE radio) { return radio_flag(radio, NOVABTH_RADIO_CONNECTABLE); }
BTAPI BOOL WINAPI BluetoothIsDiscoverable(HANDLE radio) { return radio_flag(radio, NOVABTH_RADIO_DISCOVERABLE); }
BTAPI BOOL WINAPI BluetoothEnableDiscovery(HANDLE radio, BOOL on) { return set_scan(radio, NOVABTH_SCAN_INQUIRY, on); }
BTAPI BOOL WINAPI BluetoothEnableIncomingConnections(HANDLE radio, BOOL on) { return set_scan(radio, NOVABTH_SCAN_PAGE, on); }

/* ---- devices ---- */

typedef struct {
    HANDLE radio;
    BOOL own;
    SEARCH_PARAMS_ p;
    DWORD next, count;
    NovaBthDeviceInfoList *list;
} DeviceFind;

static NovaBthDeviceInfoList *device_list(HANDLE radio)
{
    DWORD cap = 8 + 32 * sizeof(NovaBthDeviceInfo);
    NovaBthDeviceInfoList *l = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap);
    if (l && !ioctl(radio, IOCTL_BTH_GET_DEVICE_INFO, NULL, 0, l, cap, NULL) && GetLastError() != ERROR_MORE_DATA) {
        HeapFree(GetProcessHeap(), 0, l);
        return NULL;
    }
    if (l && l->numOfDevices > 32) l->numOfDevices = 32;
    return l;
}

static void to_info(const NovaBthDeviceInfo *d, DEVICE_INFO_ *o)
{
    DWORD size = o->dwSize;
    memset(o, 0, sizeof(*o));
    o->dwSize = size;
    o->Address = d->address;
    o->ulClassofDevice = d->classOfDevice;
    o->fConnected = (d->flags & NOVABTH_BDIF_CONNECTED) != 0;
    o->fAuthenticated = o->fRemembered = (d->flags & NOVABTH_BDIF_PAIRED) != 0;
    wide(o->szName, 248, d->name);
    GetSystemTime(&o->stLastSeen);
    o->stLastUsed = o->stLastSeen;
}

static BOOL wanted(const SEARCH_PARAMS_ *p, const NovaBthDeviceInfo *d)
{
    BOOL paired = (d->flags & NOVABTH_BDIF_PAIRED) != 0, conn = (d->flags & NOVABTH_BDIF_CONNECTED) != 0;
    return (p->fReturnAuthenticated && paired) || (p->fReturnRemembered && paired) || (p->fReturnUnknown && !paired) ||
           (p->fReturnConnected && conn);
}

static BOOL next_device(DeviceFind *f, DEVICE_INFO_ *info)
{
    while (f->next < f->count) {
        const NovaBthDeviceInfo *d = &f->list->deviceList[f->next++];
        if (wanted(&f->p, d)) { to_info(d, info); return TRUE; }
    }
    return FALSE;
}

BTAPI HANDLE WINAPI BluetoothFindFirstDevice(const SEARCH_PARAMS_ *params, DEVICE_INFO_ *info)
{
    if (!params || !info) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (bad_size(params, sizeof(SEARCH_PARAMS_)) || bad_size(info, sizeof(DEVICE_INFO_))) return 0;
    if (params->fIssueInquiry && (params->cTimeoutMultiplier < 1 || params->cTimeoutMultiplier > 48)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    DeviceFind *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f) { SetLastError(ERROR_OUTOFMEMORY); return 0; }
    f->p = *params;
    f->radio = radio_or_first(params->hRadio, &f->own);
    if (f->radio && params->fIssueInquiry) {
        NovaBthInquiry q = { params->cTimeoutMultiplier };
        UINT32 n = 0;
        ioctl(f->radio, IOCTL_NOVABTH_INQUIRY, &q, sizeof(q), &n, sizeof(n), NULL);
    }
    f->list = f->radio ? device_list(f->radio) : NULL;
    f->count = f->list ? f->list->numOfDevices : 0;
    if (!next_device(f, info)) {
        if (f->own) CloseHandle(f->radio);
        if (f->list) HeapFree(GetProcessHeap(), 0, f->list);
        HeapFree(GetProcessHeap(), 0, f);
        SetLastError(ERROR_NO_MORE_ITEMS);
        return 0;
    }
    return f;
}

BTAPI BOOL WINAPI BluetoothFindNextDevice(HANDLE find, DEVICE_INFO_ *info)
{
    if (!find) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (bad_size(info, sizeof(DEVICE_INFO_))) return FALSE;
    if (!next_device(find, info)) { SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }
    return TRUE;
}

BTAPI BOOL WINAPI BluetoothFindDeviceClose(HANDLE find)
{
    DeviceFind *f = find;
    if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (f->own) CloseHandle(f->radio);
    if (f->list) HeapFree(GetProcessHeap(), 0, f->list);
    HeapFree(GetProcessHeap(), 0, f);
    return TRUE;
}

BTAPI DWORD WINAPI BluetoothGetDeviceInfo(HANDLE radio, DEVICE_INFO_ *info)
{
    if (!radio || !info) return ERROR_INVALID_PARAMETER;
    if (info->dwSize != sizeof(*info)) return ERROR_REVISION_MISMATCH_;
    NovaBthDeviceInfoList *l = device_list(radio);
    DWORD e = ERROR_NOT_FOUND;
    for (DWORD i = 0; l && i < l->numOfDevices && e; i++)
        if (l->deviceList[i].address == info->Address) { to_info(&l->deviceList[i], info); e = ERROR_SUCCESS; }
    if (l) HeapFree(GetProcessHeap(), 0, l);
    return e;
}

/* A control code about the device at @addr, on @radio or each radio */
static DWORD device_call(HANDLE radio, DWORD code, void *in, DWORD in_len, void *out, DWORD out_len)
{
    BOOL own;
    HANDLE r = radio_or_first(radio, &own);
    if (!r) return ERROR_NOT_FOUND;
    DWORD e = ioctl(r, code, in, in_len, out, out_len, NULL) ? ERROR_SUCCESS : GetLastError();
    if (own) CloseHandle(r);
    return e == ERROR_FILE_NOT_FOUND ? ERROR_NOT_FOUND : e;
}

BTAPI DWORD WINAPI BluetoothRemoveDevice(const ULONGLONG *addr)
{
    if (!addr) return ERROR_INVALID_PARAMETER;
    ULONGLONG a = *addr;
    return device_call(NULL, IOCTL_NOVABTH_REMOVE, &a, sizeof(a), NULL, 0);
}

/* ---- pairing and services ---- */

BTAPI DWORD WINAPI BluetoothAuthenticateDeviceEx(HWND parent, HANDLE radio, DEVICE_INFO_ *info, void *oob, int requirements)
{
    (void)parent; (void)oob; (void)requirements;
    if (!info || info->dwSize != sizeof(*info)) return ERROR_INVALID_PARAMETER;
    ULONGLONG a = info->Address;
    DWORD e = device_call(radio, IOCTL_NOVABTH_PAIR, &a, sizeof(a), NULL, 0);
    if (!e) info->fAuthenticated = info->fRemembered = TRUE;
    return e == ERROR_ACCESS_DENIED ? ERROR_NOT_AUTHENTICATED : e;
}

BTAPI DWORD WINAPI BluetoothAuthenticateDevice(HWND parent, HANDLE radio, DEVICE_INFO_ *info, const WCHAR *passkey, ULONG len)
{
    (void)passkey; (void)len;                          /* (the host answers a PIN request with 0000) */
    return BluetoothAuthenticateDeviceEx(parent, radio, info, NULL, 0);
}

static BOOL uuid16_of(const GUID *g, USHORT *out)
{
    GUID b = *g;
    *out = (USHORT)b.Data1;
    b.Data1 &= 0xFFFF0000u;
    return !memcmp(&b, &g_base, sizeof(b)) && !(g->Data1 >> 16);
}

BTAPI DWORD WINAPI BluetoothEnumerateInstalledServices(HANDLE radio, const DEVICE_INFO_ *info, DWORD *count, GUID *out)
{
    if (!info || !count || info->dwSize != sizeof(*info)) return ERROR_INVALID_PARAMETER;
    ULONGLONG a = info->Address;
    NovaBthServices s;
    union { ULONGLONG a; NovaBthServices s; } io;
    memset(&io, 0, sizeof(io));
    io.a = a;
    DWORD e = device_call(radio, IOCTL_NOVABTH_SERVICES, &io, sizeof(io.a), &io, sizeof(io.s));
    if (e) return e;
    s = io.s;
    DWORD n = 0, room = out ? *count : 0;
    for (DWORD i = 0; i < s.count && i < NOVABTH_MAX_SERVICES; i++) {
        if (!s.enabled[i]) continue;
        if (n < room) { out[n] = g_base; out[n].Data1 = s.uuid[i]; }
        n++;
    }
    *count = n;
    return out && n > room ? ERROR_MORE_DATA : ERROR_SUCCESS;
}

BTAPI DWORD WINAPI BluetoothSetServiceState(HANDLE radio, const DEVICE_INFO_ *info, const GUID *service, DWORD flags)
{
    USHORT u;
    if (!info || !service || info->dwSize != sizeof(*info)) return ERROR_INVALID_PARAMETER;
    if (!uuid16_of(service, &u)) return ERROR_SERVICE_DOES_NOT_EXIST;
    NovaBthSetService q = { info->Address, u, (unsigned short)(flags & 1) };
    DWORD e = device_call(radio, IOCTL_NOVABTH_SET_SERVICE, &q, sizeof(q), NULL, 0);
    return e == ERROR_NOT_FOUND ? ERROR_SERVICE_DOES_NOT_EXIST : e;
}

/* -----------------------------------------------------------------------
 * SDP records
 * ----------------------------------------------------------------------- */
enum { T_NIL, T_UINT, T_INT, T_UUID, T_STRING, T_BOOLEAN, T_SEQUENCE, T_ALTERNATIVE, T_URL };

typedef struct {
    int type, specificType;                             /* SDP_TYPE, SDP_SPECIFICTYPE */
    union {
        struct { ULONGLONG lo; LONGLONG hi; } int128;
        LONGLONG int64; LONG int32; SHORT int16; CHAR int8;
        struct { ULONGLONG lo, hi; } uint128;
        ULONGLONG uint64; ULONG uint32; USHORT uint16; UCHAR uint8;
        UCHAR booleanVal;
        GUID uuid128; ULONG uuid32; USHORT uuid16;
        struct { LPBYTE value; ULONG length; } string, url, sequence, alternative;
    } data;
} SDP_ELEMENT_DATA_;

static ULONGLONG be(const BYTE *p, int n) { ULONGLONG v = 0; for (int i = 0; i < n; i++) v = v << 8 | p[i]; return v; }

/* The element at @p (@n bytes left): its type, the size index, where its
 * data starts and how long the data and the whole element are; 0 if it
 * does not fit or is malformed */
static int element(const BYTE *p, ULONG n, int *type, int *szi, const BYTE **data, ULONG *dlen, ULONG *total)
{
    if (!p || n < 1) return 0;
    int t = p[0] >> 3, si = p[0] & 7;
    ULONG hdr = 1, len;
    if (t > T_URL) return 0;
    if (si < 5) {
        static const ULONG fixed[5] = { 1, 2, 4, 8, 16 };
        len = t == T_NIL ? 0 : fixed[si];
        if (t == T_NIL && si) return 0;
        if (t == T_STRING || t == T_SEQUENCE || t == T_ALTERNATIVE || t == T_URL) return 0;
        if (t == T_BOOLEAN && si) return 0;
        if (t == T_UUID && si != 1 && si != 2 && si != 4) return 0;
    } else {
        int k = si == 5 ? 1 : si == 6 ? 2 : 4;
        if (n < 1 + (ULONG)k) return 0;
        if (t != T_STRING && t != T_SEQUENCE && t != T_ALTERNATIVE && t != T_URL) return 0;
        len = (ULONG)be(p + 1, k);
        hdr += (ULONG)k;
    }
    if (len > n - hdr) return 0;
    *type = t; *szi = si; *data = p + hdr; *dlen = len; *total = hdr + len;
    return 1;
}

static DWORD fill(const BYTE *p, ULONG n, SDP_ELEMENT_DATA_ *out)
{
    int t, si;
    const BYTE *d;
    ULONG len, total;
    if (!out) return ERROR_INVALID_PARAMETER;
    if (!element(p, n, &t, &si, &d, &len, &total)) return ERROR_INVALID_PARAMETER;
    memset(out, 0, sizeof(*out));
    out->type = t;
    out->specificType = t == T_UINT || t == T_INT || t == T_UUID ? (si << 8) | (t << 4) : 0;
    switch (t) {
    case T_UINT:
        if (si == 0) out->data.uint8 = d[0];
        else if (si == 1) out->data.uint16 = (USHORT)be(d, 2);
        else if (si == 2) out->data.uint32 = (ULONG)be(d, 4);
        else if (si == 3) out->data.uint64 = be(d, 8);
        else { out->data.uint128.hi = be(d, 8); out->data.uint128.lo = be(d + 8, 8); }
        break;
    case T_INT:
        if (si == 0) out->data.int8 = (CHAR)d[0];
        else if (si == 1) out->data.int16 = (SHORT)be(d, 2);
        else if (si == 2) out->data.int32 = (LONG)be(d, 4);
        else if (si == 3) out->data.int64 = (LONGLONG)be(d, 8);
        else { out->data.int128.hi = (LONGLONG)be(d, 8); out->data.int128.lo = be(d + 8, 8); }
        break;
    case T_UUID:
        if (si == 1) out->data.uuid16 = (USHORT)be(d, 2);
        else if (si == 2) out->data.uuid32 = (ULONG)be(d, 4);
        else {
            GUID *g = &out->data.uuid128;
            g->Data1 = (ULONG)be(d, 4); g->Data2 = (USHORT)be(d + 4, 2); g->Data3 = (USHORT)be(d + 6, 2);
            for (int i = 0; i < 8; i++) g->Data4[i] = d[8 + i];
        }
        break;
    case T_BOOLEAN: out->data.booleanVal = d[0]; break;
    case T_STRING: out->data.string.value = (LPBYTE)d; out->data.string.length = len; break;
    case T_URL: out->data.url.value = (LPBYTE)d; out->data.url.length = len; break;
    case T_SEQUENCE: case T_ALTERNATIVE:                /* (the element itself, header included) */
        out->data.sequence.value = (LPBYTE)p; out->data.sequence.length = total; break;
    }
    return ERROR_SUCCESS;
}

BTAPI DWORD WINAPI BluetoothSdpGetElementData(LPBYTE stream, ULONG n, SDP_ELEMENT_DATA_ *out)
{
    return fill(stream, n, out);
}

/* The elements of a sequence or alternative, one per call: *@pos is 0 to
 * start and then where the next one is */
BTAPI DWORD WINAPI BluetoothSdpGetContainerElementData(LPBYTE stream, ULONG n, HANDLE *pos, SDP_ELEMENT_DATA_ *out)
{
    int t, si;
    const BYTE *d;
    ULONG len, total;
    if (!pos || !out || !element(stream, n, &t, &si, &d, &len, &total) || (t != T_SEQUENCE && t != T_ALTERNATIVE))
        return ERROR_INVALID_PARAMETER;
    const BYTE *at = *pos ? (const BYTE *)*pos : d, *end = d + len;
    if (at < d || at > end) return ERROR_INVALID_PARAMETER;
    if (at == end) return ERROR_NO_MORE_ITEMS;
    int et, esi;
    const BYTE *ed;
    ULONG elen, etotal;
    if (!element(at, (ULONG)(end - at), &et, &esi, &ed, &elen, &etotal)) return ERROR_INVALID_PARAMETER;
    DWORD e = fill(at, etotal, out);
    if (!e) *pos = (HANDLE)(at + etotal);
    return e;
}

/* A record is a sequence of attribute ID (16-bit unsigned) and value pairs */
typedef BOOL (CALLBACK *SDP_ENUM_CB_)(ULONG id, LPBYTE value, ULONG n, LPVOID param);

static DWORD walk(LPBYTE rec, ULONG n, int want, SDP_ENUM_CB_ cb, LPVOID param, LPBYTE *found, ULONG *flen)
{
    int t, si;
    const BYTE *d;
    ULONG len, total;
    if (!element(rec, n, &t, &si, &d, &len, &total) || t != T_SEQUENCE) return ERROR_INVALID_PARAMETER;
    for (const BYTE *p = d, *end = d + len; p < end; ) {
        int it, isi, vt, vsi;
        const BYTE *id, *vd;
        ULONG il, itot, vl, vtot;
        if (!element(p, (ULONG)(end - p), &it, &isi, &id, &il, &itot) || it != T_UINT || isi != 1) return ERROR_INVALID_PARAMETER;
        p += itot;
        if (!element(p, (ULONG)(end - p), &vt, &vsi, &vd, &vl, &vtot)) return ERROR_INVALID_PARAMETER;
        USHORT a = (USHORT)be(id, 2);
        if (want >= 0 && a == want) { *found = (LPBYTE)p; *flen = vtot; return ERROR_SUCCESS; }
        if (cb && !cb(a, (LPBYTE)p, vtot, param)) return ERROR_SUCCESS;
        p += vtot;
    }
    return want >= 0 ? ERROR_FILE_NOT_FOUND : ERROR_SUCCESS;
}

BTAPI DWORD WINAPI BluetoothSdpGetAttributeValue(LPBYTE rec, ULONG n, USHORT id, SDP_ELEMENT_DATA_ *out)
{
    LPBYTE v = 0;
    ULONG vl = 0;
    if (!out) return ERROR_INVALID_PARAMETER;
    DWORD e = walk(rec, n, id, 0, 0, &v, &vl);
    return e ? e : fill(v, vl, out);
}

BTAPI BOOL WINAPI BluetoothSdpEnumAttributes(LPBYTE rec, ULONG n, SDP_ENUM_CB_ cb, LPVOID param)
{
    if (!cb) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    DWORD e = walk(rec, n, -1, cb, param, 0, 0);
    if (e) { SetLastError(e); return FALSE; }
    return TRUE;
}
