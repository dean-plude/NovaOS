/*
 * devmgmt.exe — Device Manager: the devices of this PC by type (or by
 * connection), each one's status, driver and resources, read through the
 * public device APIs as Windows' Device Manager reads them
 *
 *   - setupapi: SetupDiGetClassDevs (all classes, present or not) lists
 *     the devices, SetupDiGetClassDescription names their classes
 *     ("Display adapters"), SetupDiGetDeviceRegistryProperty gives each
 *     one's name, hardware and compatible IDs, manufacturer, location and
 *     service, SetupDiGetDeviceProperty its DEVPKEY_Device_Driver*
 *     properties;
 *   - cfgmgr32: CM_Get_DevNode_Status gives the status and problem code,
 *     CM_Get_Child/Sibling walk the tree for "Devices by connection", and
 *     CM_Get_First_Log_Conf/CM_Get_Next_Res_Des/CM_Get_Res_Des_Data read
 *     the allocated resources (memory and I/O ranges, the IRQ).
 *
 * The tree is on the left; the right pane shows what Windows' Properties
 * dialog shows for the selected device (General, Driver, Details,
 * Resources).  Action > Scan for hardware changes (F5) and a device
 * arriving or leaving (WM_DEVICECHANGE) list them again.
 *
 * "devmgmt --check" (the devices suite's "devmgr" boot) also checks the
 * two APIs against each other: every present device setupapi lists is
 * reached exactly once by walking cfgmgr32's tree from the root, the
 * class GUIDs agree, and a device with a problem says so in its status.
 * It logs the tree ("devmgmt: <class>: <name> [<instance>] <status>") and
 * each PCI device's resources with OutputDebugString, shows the tree with
 * every class open and the display adapter selected ("devmgmt: showing",
 * the test's screenshot), and closes with "devmgmt: N passed, M failed".
 */
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef DWORD CONFIGRET;
typedef ULONG_PTR LOG_CONF, RES_DES;
typedef struct { DWORD cbSize; GUID ClassGuid; DWORD DevInst; ULONG_PTR Reserved; } SP_DEVINFO_DATA;
typedef struct { GUID fmtid; ULONG pid; } DEVPROPKEY;

__declspec(dllimport) HANDLE WINAPI SetupDiGetClassDevsW(const GUID *cls, LPCWSTR enumerator, HWND parent, DWORD flags);
__declspec(dllimport) BOOL WINAPI SetupDiEnumDeviceInfo(HANDLE set, DWORD i, SP_DEVINFO_DATA *info);
__declspec(dllimport) BOOL WINAPI SetupDiDestroyDeviceInfoList(HANDLE set);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceRegistryPropertyW(HANDLE set, SP_DEVINFO_DATA *dev, DWORD prop, PDWORD type,
                                                                    PBYTE buf, DWORD n, PDWORD need);
__declspec(dllimport) BOOL WINAPI SetupDiGetDevicePropertyW(HANDLE set, SP_DEVINFO_DATA *dev, const DEVPROPKEY *key, PULONG type,
                                                            PBYTE buf, DWORD n, PDWORD need, DWORD flags);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceInstanceIdW(HANDLE set, SP_DEVINFO_DATA *dev, LPWSTR id, DWORD n, PDWORD need);
__declspec(dllimport) BOOL WINAPI SetupDiGetClassDescriptionW(const GUID *g, PWSTR out, DWORD n, PDWORD need);
__declspec(dllimport) CONFIGRET WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Child(PDWORD c, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Sibling(PDWORD s, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_First_Log_Conf(LOG_CONF *lc, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Next_Res_Des(RES_DES *rd, ULONG_PTR from, ULONG want, PULONG type, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Res_Des_Data_Size(PULONG size, RES_DES rd, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Res_Des_Data(RES_DES rd, PVOID buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Free_Res_Des_Handle(RES_DES rd);
__declspec(dllimport) CONFIGRET WINAPI CM_Free_Log_Conf_Handle(LOG_CONF lc);

#define DIGCF_PRESENT       0x02
#define DIGCF_ALLCLASSES    0x04
#define SPDRP_DEVICEDESC    0x00
#define SPDRP_HARDWAREID    0x01
#define SPDRP_COMPATIBLEIDS 0x02
#define SPDRP_SERVICE       0x04
#define SPDRP_CLASSGUID     0x08
#define SPDRP_MFG           0x0B
#define SPDRP_FRIENDLYNAME  0x0C
#define SPDRP_LOCATION_INFORMATION 0x0D
#define CR_SUCCESS          0
#define DN_STARTED          0x00000008
#define DN_HAS_PROBLEM      0x00000400
#define ALLOC_LOG_CONF      2
#define ResType_All         0
#define ResType_Mem         1
#define ResType_IO          2
#define ResType_IRQ         4
#define DEVPROP_TYPE_FILETIME 0x10

#pragma pack(push, 1)
typedef struct { DWORD Count, Type; ULONGLONG Base, End; DWORD Flags, Reserved; } MEM_DES;
typedef struct { DWORD Count, Type; ULONGLONG Base, End; DWORD Flags; } IO_DES;
typedef struct { DWORD Count, Type, Flags; ULONG Num; ULONG_PTR Affinity; } IRQ_DES;
#pragma pack(pop)

#define DRV_FMTID { 0xa8b865dd, 0x2e3d, 0x4094, { 0xad, 0x97, 0xe5, 0x93, 0xa7, 0x0c, 0x75, 0xd6 } }
static const DEVPROPKEY k_drv_date = { DRV_FMTID, 2 }, k_drv_version = { DRV_FMTID, 3 }, k_drv_inf = { DRV_FMTID, 5 },
                        k_drv_provider = { DRV_FMTID, 9 };

#define ID_TREE        100
#define ID_LIST        101
#define IDM_EXIT       200
#define IDM_SCAN       201
#define IDM_BY_TYPE    202
#define IDM_BY_CONN    203
#define IDM_HIDDEN     204
#define TIMER_RESCAN   1
#define TIMER_CHECK    2
#define MAX_DEVS       640
#define MAX_CLASSES    64

typedef struct {
    DWORD   dn;
    int     cls;                    /* index in g_cls */
    BOOL    present;
    WCHAR   name[128];
    WCHAR   id[200];
    HTREEITEM item;
} Dev;

typedef struct {
    GUID      g;
    WCHAR     desc[96];
    HTREEITEM item;
} Cls;

static HWND g_wnd, g_tree, g_list;
static HMENU g_view;
static Dev g_dev[MAX_DEVS];
static int g_ndev;
static Cls g_cls[MAX_CLASSES];
static int g_ncls;
static BOOL g_by_conn, g_hidden, g_check;
static int g_pass, g_fail;

static void say(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

#define CHECK(what, cond) do { if (cond) g_pass++; else { g_fail++; say("FAIL: devmgmt: %s\n", what); } } while (0)

/* ---- what a device is ---- */

static const WCHAR *problem_text(ULONG p)
{
    switch (p) {
    case 1:  return L"This device is not configured correctly. (Code 1)";
    case 3:  return L"The driver for this device might be corrupted, or your system may be running low on memory or other resources. (Code 3)";
    case 10: return L"This device cannot start. (Code 10)";
    case 12: return L"This device cannot find enough free resources that it can use. (Code 12)";
    case 18: return L"Reinstall the drivers for this device. (Code 18)";
    case 21: return L"NovaOS is removing this device. (Code 21)";
    case 22: return L"This device is disabled. (Code 22)";
    case 24: return L"This device is not present, is not working properly, or does not have all its drivers installed. (Code 24)";
    case 28: return L"The drivers for this device are not installed. (Code 28)";
    case 31: return L"This device is not working properly because NovaOS cannot load the drivers required for this device. (Code 31)";
    case 37: return L"NovaOS cannot initialize the device driver for this hardware. (Code 37)";
    case 39: return L"NovaOS cannot load the device driver for this hardware. The driver may be corrupted or missing. (Code 39)";
    case 43: return L"NovaOS has stopped this device because it has reported problems. (Code 43)";
    case 45: return L"Currently, this hardware device is not connected to the computer. (Code 45)";
    case 52: return L"NovaOS cannot verify the digital signature for the drivers required for this device. (Code 52)";
    }
    return NULL;
}

/* The device's status as Device Manager words it */
static void status_text(DWORD dn, WCHAR *out, int cap, ULONG *problem_out)
{
    ULONG st = 0, pr = 0;
    CONFIGRET cr = CM_Get_DevNode_Status(&st, &pr, dn, 0);
    if (problem_out) *problem_out = cr ? 45 : pr;
    if (cr) { lstrcpynW(out, problem_text(45), cap); return; }
    if (st & DN_HAS_PROBLEM) {
        const WCHAR *t = problem_text(pr);
        if (t) lstrcpynW(out, t, cap);
        else _snwprintf(out, (size_t)cap, L"This device has a problem. (Code %lu)", pr);
        return;
    }
    lstrcpynW(out, (st & DN_STARTED) ? L"This device is working properly." : L"This device is not started.", cap);
}

static BOOL reg_prop(HANDLE set, SP_DEVINFO_DATA *d, DWORD prop, WCHAR *out, DWORD cap)
{
    DWORD type, need;
    out[0] = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, d, prop, &type, (PBYTE)out, (cap - 1) * sizeof(WCHAR), &need)) return FALSE;
    out[cap - 1] = 0;
    return out[0] != 0;
}

static int class_of(const GUID *g)
{
    for (int i = 0; i < g_ncls; i++) if (!memcmp(&g_cls[i].g, g, sizeof(GUID))) return i;
    if (g_ncls >= MAX_CLASSES) return g_ncls - 1;
    Cls *c = &g_cls[g_ncls];
    c->g = *g;
    static const GUID none;
    if (!memcmp(g, &none, sizeof(GUID)) || !SetupDiGetClassDescriptionW(g, c->desc, 96, NULL))
        lstrcpyW(c->desc, L"Other devices");
    return g_ncls++;
}

/* List the devices again (all, or the present ones) */
static void enumerate(void)
{
    g_ndev = g_ncls = 0;
    HANDLE set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | (g_hidden ? 0 : DIGCF_PRESENT));
    if (set == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA d = { sizeof(d) };
    for (DWORD i = 0; g_ndev < MAX_DEVS && SetupDiEnumDeviceInfo(set, i, &d); i++) {
        Dev *v = &g_dev[g_ndev];
        memset(v, 0, sizeof(*v));
        v->dn = d.DevInst;
        if (!SetupDiGetDeviceInstanceIdW(set, &d, v->id, 200, NULL)) continue;
        if (!wcsncmp(v->id, L"HTREE\\ROOT", 10)) continue;          /* (the root is the computer) */
        if (!reg_prop(set, &d, SPDRP_FRIENDLYNAME, v->name, 128) && !reg_prop(set, &d, SPDRP_DEVICEDESC, v->name, 128))
            lstrcpynW(v->name, v->id, 128);
        ULONG st, pr;
        v->present = CM_Get_DevNode_Status(&st, &pr, d.DevInst, 0) == CR_SUCCESS;
        v->cls = class_of(&d.ClassGuid);
        g_ndev++;
    }
    SetupDiDestroyDeviceInfoList(set);
}

/* ---- the tree ---- */

static HTREEITEM add_item(HTREEITEM parent, const WCHAR *text, LPARAM lp)
{
    TVINSERTSTRUCTW is;
    memset(&is, 0, sizeof(is));
    is.hParent = parent;
    is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT | TVIF_PARAM;
    is.item.pszText = (WCHAR *)text;
    is.item.lParam = lp;
    return TreeView_InsertItem(g_tree, &is);
}

static int by_class(const void *a, const void *b)
{
    const Dev *x = a, *y = b;
    int c = lstrcmpiW(g_cls[x->cls].desc, g_cls[y->cls].desc);
    return c ? c : lstrcmpiW(x->name, y->name);
}

static Dev *dev_of_node(DWORD dn)
{
    for (int i = 0; i < g_ndev; i++) if (g_dev[i].dn == dn) return &g_dev[i];
    return NULL;
}

static void label(const Dev *v, WCHAR *out, int cap)
{
    if (v->present) lstrcpynW(out, v->name, cap);
    else _snwprintf(out, (size_t)cap, L"%ls (not connected)", v->name);
}

/* Devices by connection: each node under its parent, from the root */
static void add_children(HTREEITEM parent, DWORD dn, int depth)
{
    DWORD c;
    if (depth > 32 || CM_Get_Child(&c, dn, 0) != CR_SUCCESS) return;
    do {
        Dev *v = dev_of_node(c);
        if (v) {
            WCHAR t[160];
            label(v, t, 160);
            v->item = add_item(parent, t, (LPARAM)(v - g_dev) + 1);
            add_children(v->item, c, depth + 1);
        }
    } while (CM_Get_Sibling(&c, c, 0) == CR_SUCCESS);
}

static void fill_tree(void)
{
    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(g_tree);
    WCHAR pc[64];
    DWORD n = 64;
    if (!GetComputerNameW(pc, &n)) lstrcpyW(pc, L"This PC");
    HTREEITEM root = add_item(TVI_ROOT, pc, 0);
    if (g_by_conn) {
        DWORD r;
        if (CM_Locate_DevNodeW(&r, NULL, 0) == CR_SUCCESS) add_children(root, r, 0);
    } else {
        qsort(g_dev, (size_t)g_ndev, sizeof(Dev), by_class);
        int last = -1;
        for (int i = 0; i < g_ndev; i++) {
            Dev *v = &g_dev[i];
            if (v->cls != last) {
                last = v->cls;
                g_cls[last].item = add_item(root, g_cls[last].desc, -(LPARAM)last - 1);
            }
            WCHAR t[160];
            label(v, t, 160);
            v->item = add_item(g_cls[last].item, t, i + 1);
        }
    }
    TreeView_Expand(g_tree, root, TVE_EXPAND);
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_tree, NULL, TRUE);
}

/* ---- the details pane ---- */

static int g_row;

static void row(const WCHAR *name, const WCHAR *value)
{
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = g_row;
    it.pszText = (WCHAR *)name;
    int at = (int)SendMessageW(g_list, LVM_INSERTITEMW, 0, (LPARAM)&it);
    if (at < 0) return;
    it.iSubItem = 1;
    it.pszText = (WCHAR *)(value ? value : L"");
    SendMessageW(g_list, LVM_SETITEMTEXTW, (WPARAM)at, (LPARAM)&it);
    g_row++;
}

/* Each string of a REG_MULTI_SZ list on its own row */
static void rows_multi(const WCHAR *name, const WCHAR *m)
{
    if (!m[0]) { row(name, L"(none)"); return; }
    for (BOOL first = TRUE; *m; m += lstrlenW(m) + 1, first = FALSE) row(first ? name : L"", m);
}

/* The device's allocated resources, as rows (and logged in --check) */
static int resources(DWORD dn, BOOL show, const WCHAR *who)
{
    LOG_CONF lc;
    int n = 0;
    if (CM_Get_First_Log_Conf(&lc, dn, ALLOC_LOG_CONF) != CR_SUCCESS) {
        return 0;
    }
    RES_DES rd, prev = 0;
    ULONG type;
    ULONG_PTR from = lc;
    while (CM_Get_Next_Res_Des(&rd, from, ResType_All, &type, 0) == CR_SUCCESS) {
        ULONG size = 0;
        BYTE data[64];
        WCHAR t[96] = L"";
        const WCHAR *what = L"";
        if (!CM_Get_Res_Des_Data_Size(&size, rd, 0) && size <= sizeof(data) && !CM_Get_Res_Des_Data(rd, data, size, 0)) {
            if (type == ResType_Mem) {
                MEM_DES *m = (MEM_DES *)data;
                what = L"  Memory Range";
                _snwprintf(t, 96, L"%016llX - %016llX", m->Base, m->End);
            } else if (type == ResType_IO) {
                IO_DES *o = (IO_DES *)data;
                what = L"  I/O Range";
                _snwprintf(t, 96, L"%04llX - %04llX", o->Base, o->End);
            } else if (type == ResType_IRQ) {
                IRQ_DES *q = (IRQ_DES *)data;
                what = L"  IRQ";
                _snwprintf(t, 96, L"0x%08lX (%lu)", q->Num, q->Num);
            }
            if (show) row(what, t);
            if (g_check && who) say("devmgmt: resources of %ls: %ls %ls\n", who, what + 2, t);
            n++;
        }
        if (prev) CM_Free_Res_Des_Handle(prev);
        prev = rd;
        from = rd;
    }
    if (prev) CM_Free_Res_Des_Handle(prev);
    CM_Free_Log_Conf_Handle(lc);
    return n;
}

static void show_device(const Dev *v)
{
    HANDLE set = SetupDiGetClassDevsW(NULL, v->id, NULL, DIGCF_ALLCLASSES);
    SP_DEVINFO_DATA d = { sizeof(d) };
    if (set == INVALID_HANDLE_VALUE) return;
    if (!SetupDiEnumDeviceInfo(set, 0, &d)) { SetupDiDestroyDeviceInfoList(set); return; }
    WCHAR v1[512], st[200];
    ULONG problem;
    row(L"General", NULL);
    row(L"  Device", v->name);
    row(L"  Device type", g_cls[v->cls].desc);
    row(L"  Manufacturer", reg_prop(set, &d, SPDRP_MFG, v1, 512) ? v1 : L"(unknown)");
    row(L"  Location", reg_prop(set, &d, SPDRP_LOCATION_INFORMATION, v1, 512) ? v1 : L"(unknown)");
    status_text(v->dn, st, 200, &problem);
    row(L"  Device status", st);
    row(L"Driver", NULL);
    ULONG type;
    DWORD need;
    BOOL has = SetupDiGetDevicePropertyW(set, &d, &k_drv_provider, &type, (PBYTE)v1, sizeof(v1), &need, 0);
    row(L"  Driver Provider", has ? v1 : L"(no driver installed)");
    FILETIME ft;
    SYSTEMTIME tm;
    if (SetupDiGetDevicePropertyW(set, &d, &k_drv_date, &type, (PBYTE)&ft, sizeof(ft), &need, 0) && type == DEVPROP_TYPE_FILETIME &&
        FileTimeToSystemTime(&ft, &tm)) {
        _snwprintf(v1, 512, L"%u/%u/%u", tm.wMonth, tm.wDay, tm.wYear);
        row(L"  Driver Date", v1);
    }
    if (SetupDiGetDevicePropertyW(set, &d, &k_drv_version, &type, (PBYTE)v1, sizeof(v1), &need, 0)) row(L"  Driver Version", v1);
    if (reg_prop(set, &d, SPDRP_SERVICE, v1, 512)) row(L"  Service", v1);
    if (SetupDiGetDevicePropertyW(set, &d, &k_drv_inf, &type, (PBYTE)v1, sizeof(v1), &need, 0)) row(L"  INF name", v1);
    row(L"Details", NULL);
    row(L"  Device instance path", v->id);
    v1[0] = v1[1] = 0;
    reg_prop(set, &d, SPDRP_HARDWAREID, v1, 510);
    rows_multi(L"  Hardware Ids", v1);
    v1[0] = v1[1] = 0;
    reg_prop(set, &d, SPDRP_COMPATIBLEIDS, v1, 510);
    rows_multi(L"  Compatible Ids", v1);
    row(L"Resources", NULL);
    if (!resources(v->dn, TRUE, NULL)) {
        SendMessageW(g_list, LVM_DELETEITEM, (WPARAM)(g_row - 1), 0);
        g_row--;
        row(L"  (none)", L"This device is not using any resources.");
    }
    SetupDiDestroyDeviceInfoList(set);
}

static void show_selection(void)
{
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LVM_DELETEALLITEMS, 0, 0);
    g_row = 0;
    TVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = TVIF_PARAM;
    it.hItem = TreeView_GetSelection(g_tree);
    if (it.hItem && TreeView_GetItem(g_tree, &it)) {
        if (it.lParam > 0 && it.lParam <= g_ndev) show_device(&g_dev[it.lParam - 1]);
        else if (it.lParam < 0 && -it.lParam - 1 < g_ncls) {
            const Cls *c = &g_cls[-it.lParam - 1];
            WCHAR g[64];
            StringFromGUID2(&c->g, g, 64);
            int k = 0;
            for (int i = 0; i < g_ndev; i++) k += g_dev[i].cls == -it.lParam - 1;
            WCHAR n[16];
            _snwprintf(n, 16, L"%d", k);
            row(L"Device type", c->desc);
            row(L"Class GUID", g);
            row(L"Devices", n);
        } else {
            WCHAR n[16];
            _snwprintf(n, 16, L"%d", g_ndev);
            row(L"Devices", n);
            row(L"View", g_by_conn ? L"Devices by connection" : L"Devices by type");
        }
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, NULL, TRUE);
}

static void refresh(void)
{
    WCHAR keep[200] = L"";
    TVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = TVIF_PARAM;
    it.hItem = TreeView_GetSelection(g_tree);
    if (it.hItem && TreeView_GetItem(g_tree, &it) && it.lParam > 0 && it.lParam <= g_ndev) lstrcpyW(keep, g_dev[it.lParam - 1].id);
    enumerate();
    fill_tree();
    for (int i = 0; keep[0] && i < g_ndev; i++)
        if (!lstrcmpiW(g_dev[i].id, keep)) { TreeView_SelectItem(g_tree, g_dev[i].item); TreeView_EnsureVisible(g_tree, g_dev[i].item); }
    show_selection();
    say("devmgmt: showing %d devices in %d classes\n", g_ndev, g_ncls);
}

/* ---- --check: setupapi against cfgmgr32 ---- */

static int g_seen[MAX_DEVS];

static void walk(DWORD dn, int depth, int *nodes)
{
    DWORD c;
    if (depth > 32 || CM_Get_Child(&c, dn, 0) != CR_SUCCESS) return;
    do {
        (*nodes)++;
        Dev *v = dev_of_node(c);
        if (v) g_seen[v - g_dev]++;
        walk(c, depth + 1, nodes);
    } while (CM_Get_Sibling(&c, c, 0) == CR_SUCCESS);
}

static void check(void)
{
    char a[200];
    int decoding = 0;
    CHECK("SetupDiGetClassDevs lists the devices", g_ndev > 0);
    memset(g_seen, 0, sizeof(g_seen));
    DWORD root;
    int nodes = 0;
    CHECK("CM_Locate_DevNode finds the root", CM_Locate_DevNodeW(&root, NULL, 0) == CR_SUCCESS);
    walk(root, 0, &nodes);
    CHECK("the tree walk reaches as many devices as SetupDiGetClassDevs lists", nodes == g_ndev);
    HANDLE set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    SP_DEVINFO_DATA d = { sizeof(d) };
    for (int i = 0; i < g_ndev; i++) {
        Dev *v = &g_dev[i];
        WideCharToMultiByte(CP_ACP, 0, v->id, -1, a, sizeof(a), NULL, NULL);
        if (g_seen[i] != 1) { g_fail++; say("FAIL: devmgmt: %s reached %d times walking the tree\n", a, g_seen[i]); }
        else g_pass++;
        /* the class GUID: SP_DEVINFO_DATA's, SPDRP_CLASSGUID's string and the class node's agree */
        BOOL found = FALSE;
        for (DWORD k = 0; set != INVALID_HANDLE_VALUE && SetupDiEnumDeviceInfo(set, k, &d); k++) {
            if (d.DevInst != v->dn) continue;
            found = TRUE;
            WCHAR gs[64] = L"", want[64] = L"";
            static const GUID none;
            if (memcmp(&d.ClassGuid, &none, sizeof(GUID))) StringFromGUID2(&d.ClassGuid, want, 64);
            reg_prop(set, &d, SPDRP_CLASSGUID, gs, 64);
            if (lstrcmpiW(gs, want) || memcmp(&d.ClassGuid, &g_cls[v->cls].g, sizeof(GUID))) {
                g_fail++;
                say("FAIL: devmgmt: %s: class GUID %ls, SPDRP_CLASSGUID %ls\n", a, want, gs);
            } else g_pass++;
        }
        CHECK("every device is in a fresh SetupDiGetClassDevs set", found);
        WCHAR st[200];
        ULONG problem;
        status_text(v->dn, st, 200, &problem);
        CHECK("a device with a problem code says why", !problem || problem_text(problem) || st[0]);
        say("devmgmt: %ls: %ls [%s] %ls\n", g_cls[v->cls].desc, v->name, a, st);
        if (!wcsncmp(v->id, L"PCI\\", 4) && resources(v->dn, FALSE, v->id) > 0) decoding++;
    }
    CHECK("PCI devices have resources (CM_Get_First_Log_Conf, CM_Get_Next_Res_Des)", decoding > 0);
    if (set != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(set);
}

/* ---- the window ---- */

static void layout(void)
{
    RECT r;
    GetClientRect(g_wnd, &r);
    int w = r.right * 2 / 5;
    MoveWindow(g_tree, 0, 0, w, r.bottom, TRUE);
    MoveWindow(g_list, w + 4, 0, r.right - w - 4, r.bottom, TRUE);
    SendMessageW(g_list, LVM_SETCOLUMNWIDTH, 0, 170);
    SendMessageW(g_list, LVM_SETCOLUMNWIDTH, 1, r.right - w - 4 - 170 - 24);
}

static void set_view(BOOL conn)
{
    g_by_conn = conn;
    CheckMenuItem(g_view, IDM_BY_TYPE, conn ? MF_UNCHECKED : MF_CHECKED);
    CheckMenuItem(g_view, IDM_BY_CONN, conn ? MF_CHECKED : MF_UNCHECKED);
    refresh();
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (g_tree) layout();
        return 0;
    case WM_NOTIFY: {
        NMHDR *n = (NMHDR *)lp;
        if (n->idFrom == ID_TREE && n->code == TVN_SELCHANGEDW) show_selection();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_EXIT: DestroyWindow(h); break;
        case IDM_SCAN: refresh(); break;
        case IDM_BY_TYPE: set_view(FALSE); break;
        case IDM_BY_CONN: set_view(TRUE); break;
        case IDM_HIDDEN:
            g_hidden = !g_hidden;
            CheckMenuItem(g_view, IDM_HIDDEN, g_hidden ? MF_CHECKED : MF_UNCHECKED);
            refresh();
            break;
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_F5) refresh();
        return 0;
    case WM_DEVICECHANGE:                       /* (several come at once: list them again once they settle) */
        SetTimer(h, TIMER_RESCAN, 500, NULL);
        return TRUE;
    case WM_TIMER:
        KillTimer(h, wp);
        if (wp == TIMER_RESCAN) refresh();
        else if (wp == TIMER_CHECK) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HMENU make_menu(void)
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), action = CreatePopupMenu();
    g_view = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit");
    AppendMenuW(action, MF_STRING, IDM_SCAN, L"&Scan for hardware changes\tF5");
    AppendMenuW(g_view, MF_STRING | MF_CHECKED, IDM_BY_TYPE, L"Devices by &type");
    AppendMenuW(g_view, MF_STRING, IDM_BY_CONN, L"Devices by &connection");
    AppendMenuW(g_view, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_view, MF_STRING, IDM_HIDDEN, L"Show &hidden devices");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)action, L"&Action");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)g_view, L"&View");
    return bar;
}

int main(int argc, char **argv)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--check")) g_check = TRUE;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"NovaDeviceManager";
    RegisterClassExW(&wc);
    g_wnd = CreateWindowExW(0, wc.lpszClassName, L"Device Manager", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 980, 620,
                            NULL, make_menu(), inst, NULL);
    if (!g_wnd) { say("devmgmt: no window\n"); return 1; }
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    g_tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_HASBUTTONS |
                             TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 10, 10, g_wnd, (HMENU)ID_TREE, inst, NULL);
    g_list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOSORTHEADER |
                             LVS_SINGLESEL, 0, 0, 10, 10, g_wnd, (HMENU)ID_LIST, inst, NULL);
    SendMessageW(g_tree, WM_SETFONT, (WPARAM)font, 0);
    SendMessageW(g_list, WM_SETFONT, (WPARAM)font, 0);
    SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
    LVCOLUMNW col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 170;
    col.pszText = L"Property";
    SendMessageW(g_list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
    col.cx = 380;
    col.pszText = L"Value";
    SendMessageW(g_list, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
    layout();
    refresh();
    if (g_check) {
        check();
        /* every class open, the display adapter selected: the screenshot */
        for (int i = 0; i < g_ncls; i++) if (g_cls[i].item) TreeView_Expand(g_tree, g_cls[i].item, TVE_EXPAND);
        for (int i = 0; i < g_ndev; i++)
            if (!lstrcmpiW(g_cls[g_dev[i].cls].desc, L"Display adapters")) { TreeView_SelectItem(g_tree, g_dev[i].item); break; }
        say("devmgmt: %d passed, %d failed\n", g_pass, g_fail);
        SetTimer(g_wnd, TIMER_CHECK, 8000, NULL);
    }
    ShowWindow(g_wnd, SW_SHOWNORMAL);
    UpdateWindow(g_wnd);
    if (g_check) say("devmgmt: showing the tree\n");
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_F5) { refresh(); continue; }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return g_check && g_fail ? 1 : 0;
}
