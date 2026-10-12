/*
 * cfgmgr32.dll — the Configuration Manager: the device tree and each
 * device's registry keys.
 *
 * The device tree is the kernel's Plug and Play tree (novapnp.h): a device
 * node's DEVINST is its node ID (the root, HTREE\ROOT\0, is 1), its device
 * instance ID, hardware and compatible IDs, setup class, service (the
 * NovaOS driver running it), description and location are the node's, and
 * a device unplugged stays in the tree as not present (Windows' phantom:
 * CM_Locate_DevNode finds it with CM_LOCATE_DEVNODE_PHANTOM, its
 * properties are still read, CM_Get_DevNode_Status says it is not there).
 *
 * The game controllers (novapad.h) are the HID devices: each is the
 * device DEVINST 0x10000 + its slot, a child of its USB device's node, with
 * the HID instance ID and interface path hid.dll opens; the tree's own
 * node for a USB game controller's HID function is left out in its
 * favour.  GUID_DEVINTERFACE_HID lists only them (a HID keyboard or mouse
 * is not opened through hid.dll on NovaOS); every other interface class
 * lists the tree's nodes that expose it (nova_pnp_interfaces).
 */
#include <windows.h>
#include <novapad.h>
#include <novapnp.h>
#include <string.h>

#define CFGMGR32 __declspec(dllexport)
typedef DWORD CONFIGRET;
typedef DWORD DEVINST, *PDEVINST;
#define CR_SUCCESS          0x00
#define CR_INVALID_POINTER  0x03
#define CR_INVALID_FLAG     0x04
#define CR_INVALID_DEVNODE  0x05
#define CR_NO_SUCH_DEVNODE  0x0D
#define CR_INVALID_DATA     0x1F
#define CR_BUFFER_SMALL     0x1A
#define CR_NO_SUCH_VALUE    0x25
#define CR_NO_SUCH_REGISTRY_KEY 0x2E
#define CR_INVALID_PROPERTY 0x35

#define CM_LOCATE_DEVNODE_PHANTOM      0x01
#define CM_GETIDLIST_FILTER_ENUMERATOR 0x001
#define CM_GETIDLIST_FILTER_SERVICE    0x002
#define CM_GETIDLIST_FILTER_BUSRELATIONS 0x020
#define CM_GETIDLIST_FILTER_PRESENT    0x100
#define CM_GETIDLIST_FILTER_CLASS      0x200
#define CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES 0x1
#define CM_REGISTRY_SOFTWARE           0x1
#define RegDisposition_OpenExisting    0x1

#define DN_ROOT_ENUMERATED 0x00000001
#define DN_DRIVER_LOADED   0x00000002
#define DN_ENUM_LOADED     0x00000004
#define DN_STARTED         0x00000008
#define DN_HAS_PROBLEM     0x00000400
#define DN_DISABLEABLE     0x00002000
#define DN_REMOVABLE       0x00004000
#define DN_NT_ENUMERATOR   0x00800000
#define DN_NT_DRIVER       0x01000000

#define PAD_BASE 0x10000u
#define ENUM_KEY "SYSTEM\\CurrentControlSet\\Enum\\"
#define CLASS_KEY "SYSTEM\\CurrentControlSet\\Control\\Class\\"

/* ---- the nodes ---- */

static BOOL ieq(const char *a, const char *b) { return !lstrcmpiA(a, b); }

static void hex4(char *out, WORD v)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int k = 0; k < 4; k++) out[k] = hex[(v >> (12 - 4 * k)) & 15];
}

/* "USB\VID_vvvv&PID_pppp" of a game controller */
static void pad_usb_id(const NovaPadInfo *p, char *out)
{
    memcpy(out, "USB\\VID_", 8);
    hex4(out + 8, p->vid);
    memcpy(out + 12, "&PID_", 5);
    hex4(out + 17, p->pid);
    out[21] = 0;
}

/* The tree node of the USB device @usb_id (present), or 0 */
static DWORD usb_node(const char *usb_id)
{
    DWORD count = nova_pnp_count();
    NovaPnpNode n;
    for (DWORD id = 2; id <= count; id++)
        if (nova_pnp_node(id, &n) && (n.flags & NOVA_PNP_PRESENT) && nova_pnp_prefix(n.instance, usb_id) &&
            (n.instance[21] == '\\' || n.instance[21] == '&'))
            return id;
    return 0;
}

/* A game controller as a device node */
static BOOL pad_node(int slot, NovaPnpNode *n)
{
    NovaPadInfo p;
    if (slot < 0 || slot >= NOVA_PAD_SLOTS || !nova_pad_info(slot, &p)) return FALSE;
    memset(n, 0, sizeof(*n));
    n->id = PAD_BASE + (DWORD)slot;
    n->flags = NOVA_PNP_PRESENT | NOVA_PNP_STARTED | NOVA_PNP_REMOVABLE;
    WCHAR path[96];
    nova_pad_path(&p, path, FALSE);                   /* \\?\HID#VID_...#8&...&0000#{guid} */
    int k = 0;
    for (const WCHAR *c = path + 4; *c && *c != L'{' && k < 127; c++) n->instance[k++] = (char)(*c == L'#' ? '\\' : *c);
    if (k && n->instance[k - 1] == '\\') k--;
    n->instance[k] = 0;
    int h = 0;
    while (n->instance[h] && n->instance[h] != '\\') h++;
    h++;
    while (n->instance[h] && n->instance[h] != '\\') h++;
    memcpy(n->hwids, n->instance, (size_t)h);          /* HID\VID_..&PID_..[&IG_00] */
    memcpy(n->compat, "HID_DEVICE_SYSTEM_GAME\0HID_DEVICE\0", 35);
    lstrcpyA(n->cls, "HIDClass");
    lstrcpyA(n->service, "novapad");
    lstrcpyA(n->desc, "HID-compliant game controller");
    lstrcpyA(n->mfg, "(Standard system devices)");
    char usb[24];
    pad_usb_id(&p, usb);
    n->parent = usb_node(usb);
    if (!n->parent) n->parent = NOVA_PNP_ROOT;
    return TRUE;
}

/* A tree node a game controller stands in for: a USB game controller's
 * HID function */
static BOOL stood_in_for(const NovaPnpNode *n)
{
    if (!nova_pnp_prefix(n->instance, "HID\\VID_") || !nova_pnp_prefix(n->compat, "HID_DEVICE_SYSTEM_GAME")) return FALSE;
    for (int slot = 0; slot < NOVA_PAD_SLOTS; slot++) {
        NovaPadInfo p;
        char usb[24];
        if (!nova_pad_info(slot, &p)) continue;
        pad_usb_id(&p, usb);
        if (nova_pnp_prefix(n->instance + 4, usb + 4)) return TRUE;
    }
    return FALSE;
}

/* Device node @dn (a tree node or a game controller), or FALSE */
static BOOL get(DEVINST dn, NovaPnpNode *n)
{
    if (dn >= PAD_BASE) return pad_node((int)(dn - PAD_BASE), n);
    return dn && nova_pnp_node(dn, n) && !stood_in_for(n);
}

static BOOL present(const NovaPnpNode *n) { return (n->flags & NOVA_PNP_PRESENT) != 0; }

/* The device node after @dn (0: the first) in DEVINST order, or 0 */
static DEVINST next(DEVINST dn, NovaPnpNode *n)
{
    DWORD count = nova_pnp_count();
    for (DEVINST id = dn < PAD_BASE ? dn + 1 : PAD_BASE + NOVA_PAD_SLOTS; id <= count; id++)
        if (get(id, n)) return id;
    for (DEVINST id = dn < PAD_BASE ? PAD_BASE : dn + 1; id < PAD_BASE + NOVA_PAD_SLOTS; id++)
        if (get(id, n)) return id;
    return 0;
}

/* The node with instance ID @id (any case) */
static DEVINST find(const char *id, NovaPnpNode *n)
{
    for (DEVINST dn = next(0, n); dn; dn = next(dn, n))
        if (ieq(n->instance, id)) return dn;
    return 0;
}

static void to_ansi(const WCHAR *w, char *a, int cap)
{
    int k = 0;
    if (w) for (; w[k] && k < cap - 1; k++) a[k] = (char)w[k];
    a[k] = 0;
}

/* ---- locating nodes and walking the tree ---- */

static CONFIGRET locate(PDEVINST dn, const char *id, ULONG flags)
{
    if (!dn) return CR_INVALID_POINTER;
    *dn = 0;
    if (flags & ~0x7u) return CR_INVALID_FLAG;
    NovaPnpNode n;
    DEVINST r = id && id[0] ? find(id, &n) : (nova_pnp_node(NOVA_PNP_ROOT, &n) ? NOVA_PNP_ROOT : 0);
    if (!r || (!present(&n) && !(flags & CM_LOCATE_DEVNODE_PHANTOM))) return CR_NO_SUCH_DEVNODE;
    *dn = r;
    return CR_SUCCESS;
}
CFGMGR32 CONFIGRET WINAPI CM_Locate_DevNodeA(PDEVINST dn, LPCSTR id, ULONG flags) { return locate(dn, id, flags); }
CFGMGR32 CONFIGRET WINAPI CM_Locate_DevNodeW(PDEVINST dn, LPCWSTR id, ULONG flags)
{
    char a[200];
    to_ansi(id, a, sizeof(a));
    return locate(dn, a, flags);
}
CFGMGR32 CONFIGRET WINAPI CM_Locate_DevNode_ExW(PDEVINST dn, LPCWSTR id, ULONG flags, HANDLE machine)
{ (void)machine; return CM_Locate_DevNodeW(dn, id, flags); }

/* The first present node after @from whose parent is @parent */
static CONFIGRET child_after(PDEVINST out, DEVINST parent, DEVINST from)
{
    if (!out) return CR_INVALID_POINTER;
    *out = 0;
    NovaPnpNode n;
    for (DEVINST dn = next(from, &n); dn; dn = next(dn, &n))
        if (n.parent == parent && present(&n)) { *out = dn; return CR_SUCCESS; }
    return CR_NO_SUCH_DEVNODE;
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Child(PDEVINST child, DEVINST dn, ULONG flags)
{
    (void)flags;
    NovaPnpNode n;
    if (!get(dn, &n)) { if (child) *child = 0; return CR_INVALID_DEVNODE; }
    return child_after(child, dn, 0);
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Sibling(PDEVINST sib, DEVINST dn, ULONG flags)
{
    (void)flags;
    NovaPnpNode n;
    if (!get(dn, &n)) { if (sib) *sib = 0; return CR_INVALID_DEVNODE; }
    if (dn == NOVA_PNP_ROOT) { if (sib) *sib = 0; return CR_NO_SUCH_DEVNODE; }
    return child_after(sib, n.parent, dn);
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Parent(PDEVINST parent, DEVINST dn, ULONG flags)
{
    (void)flags;
    if (!parent) return CR_INVALID_POINTER;
    *parent = 0;
    NovaPnpNode n;
    if (!get(dn, &n)) return CR_INVALID_DEVNODE;
    if (!n.parent) return CR_NO_SUCH_DEVNODE;
    *parent = n.parent;
    return CR_SUCCESS;
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Child_Ex(PDEVINST c, DEVINST dn, ULONG f, HANDLE m) { (void)m; return CM_Get_Child(c, dn, f); }
CFGMGR32 CONFIGRET WINAPI CM_Get_Sibling_Ex(PDEVINST s, DEVINST dn, ULONG f, HANDLE m) { (void)m; return CM_Get_Sibling(s, dn, f); }
CFGMGR32 CONFIGRET WINAPI CM_Get_Parent_Ex(PDEVINST p, DEVINST dn, ULONG f, HANDLE m) { (void)m; return CM_Get_Parent(p, dn, f); }
CFGMGR32 CONFIGRET WINAPI CM_Get_Depth(PULONG depth, DEVINST dn, ULONG flags)
{
    (void)flags;
    if (!depth) return CR_INVALID_POINTER;
    NovaPnpNode n;
    if (!get(dn, &n)) return CR_INVALID_DEVNODE;
    ULONG d = 0;
    while (n.parent && d < 64 && get(n.parent, &n)) d++;
    *depth = d;
    return CR_SUCCESS;
}

CFGMGR32 CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DEVINST dn, ULONG flags)
{
    (void)flags;
    if (!status || !problem) return CR_INVALID_POINTER;
    *status = *problem = 0;
    NovaPnpNode n;
    if (!get(dn, &n)) return CR_INVALID_DEVNODE;
    if (!present(&n)) return CR_NO_SUCH_DEVNODE;          /* (CR_NO_SUCH_DEVINST: a phantom) */
    ULONG s = DN_NT_ENUMERATOR | DN_NT_DRIVER;
    if (dn == NOVA_PNP_ROOT || n.parent == NOVA_PNP_ROOT) s |= DN_ROOT_ENUMERATED;
    if (n.flags & NOVA_PNP_STARTED) s |= DN_DRIVER_LOADED | DN_STARTED | DN_ENUM_LOADED;
    if (n.flags & NOVA_PNP_REMOVABLE) s |= DN_REMOVABLE | DN_DISABLEABLE;
    if (n.problem) { s |= DN_HAS_PROBLEM; *problem = n.problem; }
    *status = s;
    return CR_SUCCESS;
}
CFGMGR32 CONFIGRET WINAPI CM_Get_DevNode_Status_Ex(PULONG s, PULONG p, DEVINST dn, ULONG f, HANDLE m)
{ (void)m; return CM_Get_DevNode_Status(s, p, dn, f); }

/* ---- device instance IDs ---- */

static CONFIGRET put_str(const char *s, void *buf, ULONG len, BOOL wide)
{
    if (!buf) return CR_INVALID_POINTER;
    ULONG n = (ULONG)lstrlenA(s);
    for (ULONG k = 0; k < len && k <= n; k++) {
        if (wide) ((WCHAR *)buf)[k] = (BYTE)s[k];
        else ((char *)buf)[k] = s[k];
    }
    return n < len ? CR_SUCCESS : CR_BUFFER_SMALL;     /* (n == len: all of it, no NUL) */
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Device_ID_Size(PULONG len, DEVINST dn, ULONG flags)
{
    (void)flags;
    if (!len) return CR_INVALID_POINTER;
    NovaPnpNode n;
    if (!get(dn, &n)) { *len = 0; return CR_INVALID_DEVNODE; }
    *len = (ULONG)lstrlenA(n.instance);
    return CR_SUCCESS;
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Device_IDW(DEVINST dn, WCHAR *buf, ULONG len, ULONG flags)
{
    (void)flags;
    NovaPnpNode n;
    if (!get(dn, &n)) { if (buf && len) buf[0] = 0; return CR_INVALID_DEVNODE; }
    return put_str(n.instance, buf, len, TRUE);
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Device_IDA(DEVINST dn, char *buf, ULONG len, ULONG flags)
{
    (void)flags;
    NovaPnpNode n;
    if (!get(dn, &n)) { if (buf && len) buf[0] = 0; return CR_INVALID_DEVNODE; }
    return put_str(n.instance, buf, len, FALSE);
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Device_ID_ExW(DEVINST dn, WCHAR *buf, ULONG len, ULONG flags, HANDLE m)
{ (void)m; return CM_Get_Device_IDW(dn, buf, len, flags); }

/* Whether node @n is in a device ID list with @filter and @flags */
static BOOL listed(const NovaPnpNode *n, const char *filter, ULONG flags)
{
    if ((flags & CM_GETIDLIST_FILTER_PRESENT) && !present(n)) return FALSE;
    if (!filter || !filter[0]) return TRUE;
    if (flags & CM_GETIDLIST_FILTER_ENUMERATOR) {
        int k = lstrlenA(filter);
        BOOL whole = FALSE;
        for (int i = 0; i < k; i++) if (filter[i] == '\\') whole = TRUE;
        return whole ? ieq(n->instance, filter) : nova_pnp_prefix(n->instance, filter) && n->instance[k] == '\\';
    }
    if (flags & CM_GETIDLIST_FILTER_SERVICE) return ieq(n->service, filter);
    if (flags & CM_GETIDLIST_FILTER_CLASS) {
        GUID g;
        char gs[40];
        return nova_pnp_class_guid(n->cls, &g) && (nova_pnp_guid_str(&g, gs), ieq(gs, filter));
    }
    if (flags & CM_GETIDLIST_FILTER_BUSRELATIONS) {
        NovaPnpNode p;
        return present(n) && n->parent && get(n->parent, &p) && ieq(p.instance, filter);
    }
    return TRUE;
}

/* The device ID list (each ID with its NUL, then a NUL) into @out (@cap
 * characters); the characters it takes */
static ULONG id_list(const char *filter, ULONG flags, void *out, ULONG cap, BOOL wide)
{
    ULONG k = 0;
    NovaPnpNode n;
    for (DEVINST dn = next(0, &n); dn; dn = next(dn, &n)) {
        if (!listed(&n, filter, flags)) continue;
        for (const char *s = n.instance;; s++, k++) {
            if (out && k < cap) { if (wide) ((WCHAR *)out)[k] = (BYTE)*s; else ((char *)out)[k] = *s; }
            if (!*s) { k++; break; }
        }
    }
    if (out && k < cap) { if (wide) ((WCHAR *)out)[k] = 0; else ((char *)out)[k] = 0; }
    return k + 1;
}

CFGMGR32 CONFIGRET WINAPI CM_Get_Device_ID_List_SizeA(PULONG len, LPCSTR filter, ULONG flags)
{
    if (!len) return CR_INVALID_POINTER;
    *len = id_list(filter, flags, NULL, 0, FALSE);
    return CR_SUCCESS;
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW(PULONG len, LPCWSTR filter, ULONG flags)
{
    char a[200];
    to_ansi(filter, a, sizeof(a));
    return CM_Get_Device_ID_List_SizeA(len, a, flags);
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Device_ID_ListA(LPCSTR filter, char *buf, ULONG len, ULONG flags)
{
    if (!buf) return CR_INVALID_POINTER;
    if (id_list(filter, flags, NULL, 0, FALSE) > len) return CR_BUFFER_SMALL;
    id_list(filter, flags, buf, len, FALSE);
    return CR_SUCCESS;
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Device_ID_ListW(LPCWSTR filter, WCHAR *buf, ULONG len, ULONG flags)
{
    if (!buf) return CR_INVALID_POINTER;
    char a[200];
    to_ansi(filter, a, sizeof(a));
    if (id_list(a, flags, NULL, 0, TRUE) > len) return CR_BUFFER_SMALL;
    id_list(a, flags, buf, len, TRUE);
    return CR_SUCCESS;
}

/* ---- properties ---- */

static CONFIGRET registry_property(DEVINST dn, ULONG prop, PULONG type, PVOID buf, PULONG len, BOOL wide)
{
    if (!len) return CR_INVALID_POINTER;
    NovaPnpNode n;
    if (!get(dn, &n)) { *len = 0; return CR_INVALID_DEVNODE; }
    if (prop < 1) return CR_INVALID_PROPERTY;
    char v[200];
    DWORD t;
    int size = nova_pnp_property(&n, prop - 1, v, sizeof(v), &t);   /* (CM_DRP_* is SPDRP_* + 1) */
    if (size < 0) { *len = 0; return CR_NO_SUCH_VALUE; }
    ULONG need = t == REG_DWORD ? 4 : (ULONG)size * (wide ? sizeof(WCHAR) : 1);
    if (type) *type = t;
    ULONG have = buf ? *len : 0;
    *len = need;
    if (have < need) return CR_BUFFER_SMALL;
    if (t == REG_DWORD || !wide) memcpy(buf, v, need);
    else for (int k = 0; k < size; k++) ((WCHAR *)buf)[k] = (BYTE)v[k];
    return CR_SUCCESS;
}
CFGMGR32 CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW(DEVINST dn, ULONG prop, PULONG type, PVOID buf, PULONG len, ULONG flags)
{ (void)flags; return registry_property(dn, prop, type, buf, len, TRUE); }
CFGMGR32 CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyA(DEVINST dn, ULONG prop, PULONG type, PVOID buf, PULONG len, ULONG flags)
{ (void)flags; return registry_property(dn, prop, type, buf, len, FALSE); }

/* Unified properties (DEVPKEY_*) */
typedef struct { GUID fmtid; ULONG pid; } DevPropKey;
#define DEVPROP_TYPE_UINT32      0x07
#define DEVPROP_TYPE_GUID        0x0D
#define DEVPROP_TYPE_BOOLEAN     0x11
#define DEVPROP_TYPE_STRING      0x12
#define DEVPROP_TYPE_STRING_LIST 0x2012
static const GUID k_device = { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } };
static const GUID k_inst = { 0x78c34fc8, 0x104a, 0x4aca, { 0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57 } };
static const GUID k_node = { 0x4340a6c5, 0x93fa, 0x4706, { 0x97, 0x2c, 0x7b, 0x64, 0x80, 0x08, 0xa5, 0xa7 } };
static const GUID k_present = { 0x540b947e, 0x8b40, 0x45bc, { 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2 } };

static CONFIGRET put_value(ULONG t, const void *v, ULONG size, PULONG type, PBYTE buf, PULONG len)
{
    *type = t;
    ULONG have = buf ? *len : 0;
    *len = size;
    if (have < size) return CR_BUFFER_SMALL;
    memcpy(buf, v, size);
    return CR_SUCCESS;
}

CFGMGR32 CONFIGRET WINAPI CM_Get_DevNode_PropertyW(DEVINST dn, const void *key, PULONG type, PBYTE buf, PULONG len, ULONG flags)
{
    (void)flags;
    const DevPropKey *k = key;
    if (!k || !type || !len) return CR_INVALID_POINTER;
    NovaPnpNode n;
    if (!get(dn, &n)) { *len = 0; return CR_INVALID_DEVNODE; }
    ULONG prop = ~0u;
    if (!memcmp(&k->fmtid, &k_device, sizeof(GUID))) {
        static const ULONG map[][2] = { { 2, 0x00 }, { 3, 0x01 }, { 4, 0x02 }, { 6, 0x04 }, { 9, 0x07 }, { 10, 0x08 }, { 11, 0x09 },
                                        { 13, 0x0B }, { 14, 0x0C }, { 15, 0x0D }, { 16, 0x0E }, { 24, 0x16 } };
        for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) if (map[i][0] == k->pid) prop = map[i][1];
    }
    WCHAR w[200];
    char v[200];
    DWORD t;
    if (prop == 0x08) {                                            /* (a GUID, not its string) */
        GUID g;
        if (!nova_pnp_class_guid(n.cls, &g)) { *len = 0; return CR_NO_SUCH_VALUE; }
        return put_value(DEVPROP_TYPE_GUID, &g, sizeof(g), type, buf, len);
    }
    if (prop != ~0u || (!memcmp(&k->fmtid, &k_inst, sizeof(GUID)) && k->pid == 256) ||
        (!memcmp(&k->fmtid, &k_node, sizeof(GUID)) && k->pid == 8)) {
        int size;
        if (prop != ~0u) size = nova_pnp_property(&n, prop, v, sizeof(v), &t);
        else {
            NovaPnpNode p;
            const char *s = k->pid == 256 ? n.instance : n.parent && get(n.parent, &p) ? p.instance : "";
            lstrcpynA(v, s, sizeof(v));
            size = v[0] ? lstrlenA(v) + 1 : -1;
            t = REG_SZ;
        }
        if (size < 0 || size > 200) { *len = 0; return CR_NO_SUCH_VALUE; }
        for (int i = 0; i < size; i++) w[i] = (BYTE)v[i];
        return put_value(t == REG_MULTI_SZ ? DEVPROP_TYPE_STRING_LIST : DEVPROP_TYPE_STRING, w, (ULONG)size * sizeof(WCHAR),
                         type, buf, len);
    }
    if (!memcmp(&k->fmtid, &k_node, sizeof(GUID)) && (k->pid == 2 || k->pid == 3)) {   /* DevNodeStatus, ProblemCode */
        ULONG s, p;
        if (CM_Get_DevNode_Status(&s, &p, dn, 0)) { *len = 0; return CR_NO_SUCH_VALUE; }
        ULONG val = k->pid == 2 ? s : p;
        return put_value(DEVPROP_TYPE_UINT32, &val, 4, type, buf, len);
    }
    if (!memcmp(&k->fmtid, &k_present, sizeof(GUID)) && k->pid == 5) {                  /* IsPresent */
        BYTE b = present(&n) ? 0xFF : 0;
        return put_value(DEVPROP_TYPE_BOOLEAN, &b, 1, type, buf, len);
    }
    *len = 0;
    return CR_NO_SUCH_VALUE;
}

/* The device's hardware key (Enum\<instance>\Device Parameters) or
 * software key (Control\Class\{class}\NNNN, the one SPDRP_DRIVER names) */
CFGMGR32 CONFIGRET WINAPI CM_Open_DevNode_Key(DEVINST dn, DWORD sam, ULONG profile, DWORD disp, HKEY *key, ULONG flags)
{
    (void)profile;
    if (!key) return CR_INVALID_POINTER;
    *key = NULL;
    NovaPnpNode n;
    if (!get(dn, &n)) return CR_INVALID_DEVNODE;
    char path[260], v[64];
    DWORD t;
    if (flags & CM_REGISTRY_SOFTWARE) {
        if (nova_pnp_property(&n, 0x09, v, sizeof(v), &t) < 0) return CR_NO_SUCH_REGISTRY_KEY;
        lstrcpyA(path, CLASS_KEY);
        lstrcatA(path, v);
    } else {
        lstrcpyA(path, ENUM_KEY);
        lstrcatA(path, n.instance);
        lstrcatA(path, "\\Device Parameters");
    }
    LONG e = disp == RegDisposition_OpenExisting ? RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, sam, key)
                                                 : RegCreateKeyExA(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, sam, NULL, key, NULL);
    if (e) { *key = NULL; return CR_NO_SUCH_REGISTRY_KEY; }
    return CR_SUCCESS;
}

/* ---- device interfaces ---- */

/* The interfaces of class @cls (of device @id, or all): each path with its
 * NUL, then a NUL, into @out (@cap characters); the characters it takes */
static ULONG iface_list(const GUID *cls, const char *id, ULONG flags, WCHAR *out, ULONG cap)
{
    static const GUID hid = NOVA_IF_HID;
    ULONG n = 0;
    WCHAR path[200];
    if (cls && !memcmp(cls, &hid, sizeof(GUID))) {
        for (int slot = 0; slot < NOVA_PAD_SLOTS; slot++) {
            NovaPadInfo i;
            NovaPnpNode node;
            if (!nova_pad_info(slot, &i)) continue;
            if (id && id[0] && !(pad_node(slot, &node) && ieq(node.instance, id))) continue;
            int len = nova_pad_path(&i, path, TRUE) + 1;
            for (int k = 0; k < len; k++, n++) if (out && n < cap) out[n] = path[k];
        }
    } else if (cls) {
        NovaPnpNode node;
        DWORD count = nova_pnp_count();
        for (DWORD dn = 1; dn <= count; dn++) {
            if (!nova_pnp_node(dn, &node)) continue;
            if (!present(&node) && !(flags & CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES)) continue;
            if (id && id[0] && !ieq(node.instance, id)) continue;
            GUID g[4];
            int k = nova_pnp_interfaces(&node, g);
            for (int j = 0; j < k; j++) {
                if (memcmp(&g[j], cls, sizeof(GUID))) continue;
                nova_pnp_iface_path(&node, cls, path, 200);
                int len = lstrlenW(path) + 1;
                for (int c = 0; c < len; c++, n++) if (out && n < cap) out[n] = path[c];
            }
        }
    }
    if (out && n < cap) out[n] = 0;
    return n + 1;
}

CFGMGR32 CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags)
{
    if (!len) return CR_INVALID_POINTER;
    char a[200];
    to_ansi(id, a, sizeof(a));
    *len = iface_list(cls, a, flags, NULL, 0);
    return CR_SUCCESS;
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags)
{
    if (!buf) return CR_INVALID_POINTER;
    char a[200];
    to_ansi(id, a, sizeof(a));
    if (iface_list(cls, a, flags, NULL, 0) > len) return CR_BUFFER_SMALL;
    iface_list(cls, a, flags, buf, len);
    return CR_SUCCESS;
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeA(PULONG len, LPGUID cls, LPCSTR id, ULONG flags)
{
    if (!len) return CR_INVALID_POINTER;
    *len = iface_list(cls, id, flags, NULL, 0);
    return CR_SUCCESS;
}
CFGMGR32 CONFIGRET WINAPI CM_Get_Device_Interface_ListA(LPGUID cls, LPCSTR id, char *buf, ULONG len, ULONG flags)
{
    if (!buf) return CR_INVALID_POINTER;
    ULONG n = iface_list(cls, id, flags, NULL, 0);
    if (n > len) return CR_BUFFER_SMALL;
    WCHAR *w = HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR));
    if (!w) return 0x02;                                     /* CR_OUT_OF_MEMORY */
    iface_list(cls, id, flags, w, n);
    for (ULONG k = 0; k < n; k++) buf[k] = (char)w[k];
    HeapFree(GetProcessHeap(), 0, w);
    return CR_SUCCESS;
}

/* ---- starting and disabling a device's Windows driver ----
 * (kernel/nt/pnp.c: the PnP manager starts the driver a device install
 * named in the device's Enum key, and takes it away with
 * IRP_MN_QUERY_REMOVE_DEVICE and IRP_MN_REMOVE_DEVICE.)  A device a NovaOS
 * driver runs cannot be disabled. */
#define CR_REMOVE_VETOED    0x17
#define CR_NOT_DISABLEABLE  0x21
#define CR_ACCESS_DENIED    0x33
#define CONFIGFLAG_DISABLED 0x1

/* Set or clear CONFIGFLAG_DISABLED in the device's Enum key */
static BOOL config_disabled(const NovaPnpNode *n, BOOL on)
{
    char k[300];
    lstrcpyA(k, "SYSTEM\\CurrentControlSet\\Enum\\");
    lstrcatA(k, n->instance);
    HKEY h;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, k, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &h, NULL)) return FALSE;
    DWORD v = 0, type, len = sizeof(v);
    if (RegQueryValueExA(h, "ConfigFlags", NULL, &type, (BYTE *)&v, &len) || type != REG_DWORD) v = 0;
    v = on ? v | CONFIGFLAG_DISABLED : v & ~(DWORD)CONFIGFLAG_DISABLED;
    BOOL ok = !RegSetValueExA(h, "ConfigFlags", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    RegCloseKey(h);
    return ok;
}

CFGMGR32 CONFIGRET WINAPI CM_Disable_DevNode(DEVINST dn, ULONG flags)
{
    (void)flags;
    NovaPnpNode n;
    if (dn >= PAD_BASE || !get(dn, &n)) return CR_NO_SUCH_DEVNODE;
    if (!nova_pnp_prefix(n.instance, "PCI\\")) return CR_NOT_DISABLEABLE;
    if (n.flags & NOVA_PNP_STARTED) {
        NTSTATUS st = nova_pnp_stop(dn);
        if (st == (NTSTATUS)0xC0000061) return CR_ACCESS_DENIED;           /* (STATUS_PRIVILEGE_NOT_HELD) */
        if (st == (NTSTATUS)0xC0000184) return CR_NOT_DISABLEABLE;         /* (a NovaOS driver's) */
        if (st < 0) return CR_REMOVE_VETOED;
    }
    return config_disabled(&n, TRUE) ? CR_SUCCESS : CR_ACCESS_DENIED;
}

CFGMGR32 CONFIGRET WINAPI CM_Enable_DevNode(DEVINST dn, ULONG flags)
{
    (void)flags;
    NovaPnpNode n;
    if (dn >= PAD_BASE || !get(dn, &n)) return CR_NO_SUCH_DEVNODE;
    if (!config_disabled(&n, FALSE)) return CR_ACCESS_DENIED;
    if (nova_pnp_start(dn) == (DWORD)-1) return CR_ACCESS_DENIED;
    return CR_SUCCESS;                          /* (a driver that fails leaves its problem code) */
}

/* CM_SETUP_DEVNODE_READY (0): start the device's driver again */
CFGMGR32 CONFIGRET WINAPI CM_Setup_DevNode(DEVINST dn, ULONG flags)
{
    (void)flags;
    NovaPnpNode n;
    if (dn >= PAD_BASE || !get(dn, &n)) return CR_NO_SUCH_DEVNODE;
    return nova_pnp_start(dn) == (DWORD)-1 ? CR_ACCESS_DENIED : CR_SUCCESS;
}

CFGMGR32 CONFIGRET WINAPI CM_Disable_DevNode_Ex(DEVINST dn, ULONG f, HANDLE m) { (void)m; return CM_Disable_DevNode(dn, f); }
CFGMGR32 CONFIGRET WINAPI CM_Enable_DevNode_Ex(DEVINST dn, ULONG f, HANDLE m) { (void)m; return CM_Enable_DevNode(dn, f); }

CFGMGR32 DWORD WINAPI CM_MapCrToWin32Err(CONFIGRET cr, DWORD dflt)
{
    switch (cr) {
    case CR_SUCCESS:         return ERROR_SUCCESS;
    case CR_INVALID_POINTER: return 1784;          /* ERROR_INVALID_USER_BUFFER */
    case CR_INVALID_FLAG:    return ERROR_INVALID_FLAGS;
    case CR_INVALID_DEVNODE:
    case CR_INVALID_DATA:
    case CR_INVALID_PROPERTY: return 13;           /* ERROR_INVALID_DATA */
    case CR_BUFFER_SMALL:    return ERROR_INSUFFICIENT_BUFFER;
    case CR_NO_SUCH_DEVNODE:
    case CR_NO_SUCH_VALUE:
    case CR_NO_SUCH_REGISTRY_KEY: return ERROR_NOT_FOUND;
    default:                 return dflt;
    }
}
