/*
 * novapnp.h — the Plug and Play device tree as NovaOS's kernel keeps it
 *
 * NtNovaGuiCtl(0, NOVA_CTL_PNP, op | id << 8, ptr) reads the device nodes
 * of kernel/io/pnp.h (whose PnpNode and PnpEvent these mirror) and waits
 * for devices to arrive and leave: cfgmgr32, setupapi, user32
 * (WM_DEVICECHANGE), the PlugPlay service and the self-test pnptest use
 * it.  A node's ID is its DEVINST; the root is 1.
 */
#pragma once
#include <windows.h>
#include <winternl.h>
#include <string.h>

#define NOVA_CTL_PNP 56

#define NOVA_PNP_ROOT       1
#define NOVA_PNP_PRESENT    0x01
#define NOVA_PNP_STARTED    0x02
#define NOVA_PNP_REMOVABLE  0x04
#define NOVA_PNP_WINDOWS    0x08    /* the driver running it is a Windows driver */

typedef struct {
    DWORD id, parent, flags, problem, seq, reserved;
    char  instance[128];
    char  hwids[160];               /* REG_MULTI_SZ */
    char  compat[160];              /* REG_MULTI_SZ */
    char  cls[32];                  /* setup class name */
    char  service[32];              /* the NovaOS driver, or "" */
    char  desc[96];
    char  mfg[48];
    char  location[64];
    char  friendly[96];             /* the device's own name, or "" */
} NovaPnpNode;

#define NOVA_PNP_ARRIVAL 1
#define NOVA_PNP_REMOVAL 2
typedef struct { DWORD seq, kind, id, reserved; } NovaPnpEvent;

typedef struct {
    DWORD after, wait_ms, max;      /* in */
    DWORD newest;                   /* out */
    NovaPnpEvent e[32];
} NovaPnpRead;

/* The highest node ID */
static inline DWORD nova_pnp_count(void)
{
    return (DWORD)NtNovaGuiCtl(0, NOVA_CTL_PNP, 0, NULL);
}

static inline BOOL nova_pnp_node(DWORD id, NovaPnpNode *n)
{
    return NtNovaGuiCtl(0, NOVA_CTL_PNP, 1 | (ULONG_PTR)id << 8, n) != 0;
}

/* The events after r->after (r->max of them at most), waiting up to
 * r->wait_ms (1 s at most) for one; returns how many, r->newest the
 * newest's seq */
static inline int nova_pnp_events(NovaPnpRead *r)
{
    return (int)NtNovaGuiCtl(0, NOVA_CTL_PNP, 2, r);
}

/* ---- Windows' names for the nodes (cfgmgr32, setupapi, user32, plugplay) ---- */

/* @s starts with @pre, ignoring ASCII case */
static inline BOOL nova_pnp_prefix(const char *s, const char *pre)
{
    for (; *pre; s++, pre++) {
        char a = *s, b = *pre;
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b) return FALSE;
    }
    return TRUE;
}

static inline BOOL nova_pnp_has(const char *s, const char *part)
{
    for (; *s; s++) if (nova_pnp_prefix(s, part)) return TRUE;
    return FALSE;
}

/* The setup class GUID of a class name (the GUID of "System" for one
 * not known, and of none for "") */
static inline BOOL nova_pnp_class_guid(const char *cls, GUID *g)
{
    static const struct { const char *name; GUID g; } t[] = {
        { "System",       { 0x4d36e97d, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } } },
        { "USB",          { 0x36fc9e60, 0xc465, 0x11cf, { 0x80, 0x56, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } } },
        { "HIDClass",     { 0x745a17a0, 0x74d3, 0x11d0, { 0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda } } },
        { "Ports",        { 0x4d36e978, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } } },
        { "Keyboard",     { 0x4d36e96b, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } } },
        { "Mouse",        { 0x4d36e96f, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } } },
        { "DiskDrive",    { 0x4d36e967, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } } },
        { "MEDIA",        { 0x4d36e96c, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } } },
        { "Net",          { 0x4d36e972, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } } },
        { "Display",      { 0x4d36e968, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } } },
        { "HDC",          { 0x4d36e96a, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } } },
        { "SCSIAdapter",  { 0x4d36e97b, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } } },
        { "XnaComposite", { 0xd61ca365, 0x5af4, 0x4486, { 0x99, 0x8b, 0x9d, 0xb4, 0x73, 0x4c, 0x6c, 0xa3 } } },
    };
    if (!cls || !cls[0]) return FALSE;
    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        if (!lstrcmpiA(cls, t[i].name)) { *g = t[i].g; return TRUE; }
    *g = t[0].g;
    return TRUE;
}

/* Device interface classes */
#define NOVA_IF_USB_DEVICE { 0xa5dcbf10, 0x6530, 0x11d2, { 0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed } }
#define NOVA_IF_USB_HUB    { 0xf18a0e88, 0xc30c, 0x11d0, { 0x88, 0x15, 0x00, 0xa0, 0xc9, 0x06, 0xbe, 0xd8 } }
#define NOVA_IF_HID        { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } }
#define NOVA_IF_KEYBOARD   { 0x884b96c3, 0x56ef, 0x11d1, { 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd } }
#define NOVA_IF_MOUSE      { 0x378de44c, 0x56ef, 0x11d1, { 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd } }
#define NOVA_IF_DISK       { 0x53f56307, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } }
#define NOVA_IF_AUDIO      { 0x6994ad04, 0x93ef, 0x11d0, { 0xa3, 0xcc, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } }
#define NOVA_IF_NET        { 0xcac88484, 0x7515, 0x4c03, { 0x82, 0xe6, 0x71, 0xa8, 0x7a, 0xba, 0xc3, 0x61 } }
#define NOVA_IF_DISPLAY    { 0x5b45201d, 0xf2f2, 0x4f3b, { 0x85, 0xbb, 0x30, 0xff, 0x1f, 0x95, 0x35, 0x99 } }

/* The device interface classes a node exposes (at most 4 into @g): its
 * count.  A USB device (not a function of a composite one) is a
 * GUID_DEVINTERFACE_USB_DEVICE, a hub also a USB_HUB, a HID node a HID and
 * a KEYBOARD or MOUSE, a disk a DISK, an audio function KSCATEGORY_AUDIO,
 * a network adapter NET, a display adapter DISPLAY_ADAPTER. */
static inline int nova_pnp_interfaces(const NovaPnpNode *n, GUID *g)
{
    static const GUID usb = NOVA_IF_USB_DEVICE, hub = NOVA_IF_USB_HUB, hid = NOVA_IF_HID, kbd = NOVA_IF_KEYBOARD,
                      mouse = NOVA_IF_MOUSE, disk = NOVA_IF_DISK, audio = NOVA_IF_AUDIO, net = NOVA_IF_NET,
                      disp = NOVA_IF_DISPLAY;
    int k = 0;
    if (nova_pnp_prefix(n->instance, "USB\\VID_") && !nova_pnp_has(n->instance, "&MI_")) g[k++] = usb;
    if (nova_pnp_prefix(n->instance, "USB\\") && !lstrcmpiA(n->service, "usbhub")) g[k++] = hub;
    if (nova_pnp_prefix(n->instance, "HID\\")) {
        g[k++] = hid;
        if (!lstrcmpiA(n->cls, "Keyboard")) g[k++] = kbd;
        if (!lstrcmpiA(n->cls, "Mouse")) g[k++] = mouse;
    }
    if (!lstrcmpiA(n->cls, "DiskDrive")) g[k++] = disk;
    if (!lstrcmpiA(n->cls, "MEDIA")) g[k++] = audio;
    if (!lstrcmpiA(n->cls, "Net")) g[k++] = net;
    if (!lstrcmpiA(n->cls, "Display")) g[k++] = disp;
    /* and those a Windows driver on it turned on (IoSetDeviceInterfaceState):
     * a PCI device's, or a child device's its bus driver reported */
    if (!nova_pnp_prefix(n->instance, "USB\\") && !nova_pnp_prefix(n->instance, "HID\\")) {
        GUID w[8];
        int m = (int)NtNovaGuiCtl(0, NOVA_CTL_PNP, 3 | (ULONG_PTR)n->id << 8, w);
        for (int i = 0; i < m && k < 4; i++) g[k++] = w[i];
    }
    return k;
}

/* Start the Windows driver installed on node @id (its Enum key's Service,
 * as a device install writes it): 0, the problem code it has now (28: no
 * driver installed, 10: it failed to start...), or (DWORD)-1 without
 * SeLoadDriverPrivilege */
static inline DWORD nova_pnp_start(DWORD id)
{
    return (DWORD)NtNovaGuiCtl(0, NOVA_CTL_PNP, 4 | (ULONG_PTR)id << 8, NULL);
}

/* Take node @id's Windows driver away (IRP_MN_QUERY_REMOVE_DEVICE, then
 * IRP_MN_REMOVE_DEVICE): the NTSTATUS, a driver's veto among them */
static inline NTSTATUS nova_pnp_stop(DWORD id)
{
    return (NTSTATUS)NtNovaGuiCtl(0, NOVA_CTL_PNP, 5 | (ULONG_PTR)id << 8, NULL);
}

/* A device interface's symbolic link: "\\?\" + the instance ID with each
 * '\' a '#', then "#{guid}" (lower case, as Windows writes it) */
static inline void nova_pnp_iface_path(const NovaPnpNode *n, const GUID *g, WCHAR *out, int cap)
{
    static const char hex[] = "0123456789abcdef";
    int k = 0;
    const char *pre = "\\\\?\\";
    for (const char *s = pre; *s && k < cap - 1; s++) out[k++] = (WCHAR)*s;
    for (const char *s = n->instance; *s && k < cap - 1; s++) out[k++] = (WCHAR)(*s == '\\' ? '#' : *s);
    const unsigned char *b = (const unsigned char *)g;
    char gs[40];
    int j = 0;
    gs[j++] = '#'; gs[j++] = '{';
    for (int i = 3; i >= 0; i--) { gs[j++] = hex[b[i] >> 4]; gs[j++] = hex[b[i] & 15]; }
    gs[j++] = '-';
    for (int i = 5; i >= 4; i--) { gs[j++] = hex[b[i] >> 4]; gs[j++] = hex[b[i] & 15]; }
    gs[j++] = '-';
    for (int i = 7; i >= 6; i--) { gs[j++] = hex[b[i] >> 4]; gs[j++] = hex[b[i] & 15]; }
    gs[j++] = '-';
    for (int i = 8; i < 10; i++) { gs[j++] = hex[b[i] >> 4]; gs[j++] = hex[b[i] & 15]; }
    gs[j++] = '-';
    for (int i = 10; i < 16; i++) { gs[j++] = hex[b[i] >> 4]; gs[j++] = hex[b[i] & 15]; }
    gs[j++] = '}'; gs[j] = 0;
    for (const char *s = gs; *s && k < cap - 1; s++) out[k++] = (WCHAR)*s;
    out[k] = 0;
}

/* @g as Windows writes a class GUID: "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"
 * in lower case (39 characters with the NUL) */
static inline void nova_pnp_guid_str(const GUID *g, char *out)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *b = (const unsigned char *)g;
    static const signed char order[] = { 3, 2, 1, 0, -1, 5, 4, -1, 7, 6, -1, 8, 9, -1, 10, 11, 12, 13, 14, 15 };
    int j = 0;
    out[j++] = '{';
    for (unsigned i = 0; i < sizeof(order); i++) {
        if (order[i] < 0) { out[j++] = '-'; continue; }
        out[j++] = hex[b[order[i]] >> 4];
        out[j++] = hex[b[order[i]] & 15];
    }
    out[j++] = '}';
    out[j] = 0;
}

/* The length of a REG_MULTI_SZ list in bytes, its final NUL included
 * (2 for an empty one) */
static inline int nova_pnp_multi_len(const char *m)
{
    int n = 0;
    while (m[n]) while (m[n++]) ;
    return n ? n + 1 : 2;
}

/* A device registry property (SPDRP_*; cfgmgr32's CM_DRP_* is one more)
 * of node @n into @out (@cap bytes, ANSI): its size in bytes, NULs
 * included, and its type in *@type; -1 for one the node does not have.
 * A REG_DWORD is 4 bytes of @out. */
static inline int nova_pnp_property(const NovaPnpNode *n, DWORD prop, char *out, int cap, DWORD *type)
{
    const char *s = NULL;
    char tmp[64];
    DWORD dw = 0;
    *type = REG_SZ;
    switch (prop) {
    case 0x00: s = n->desc; break;                                    /* SPDRP_DEVICEDESC */
    case 0x01: case 0x02: {                                           /* SPDRP_HARDWAREID, _COMPATIBLEIDS */
        const char *m = prop == 1 ? n->hwids : n->compat;
        if (!m[0]) return -1;
        int len = nova_pnp_multi_len(m);
        *type = REG_MULTI_SZ;
        if (len <= cap) memcpy(out, m, (size_t)len);
        return len;
    }
    case 0x04: s = n->service; break;                                 /* SPDRP_SERVICE */
    case 0x07: s = n->cls; break;                                     /* SPDRP_CLASS */
    case 0x08: case 0x09: {                                           /* SPDRP_CLASSGUID, _DRIVER */
        GUID g;
        if (!nova_pnp_class_guid(n->cls, &g)) return -1;
        nova_pnp_guid_str(&g, tmp);
        if (prop == 0x09) {
            if (!n->service[0]) return -1;
            DWORD v = n->id;
            char *e = tmp + 38;
            *e++ = '\\';
            for (int d = 1000; d; d /= 10) *e++ = (char)('0' + v / (DWORD)d % 10);
            *e = 0;
        }
        s = tmp;
        break;
    }
    case 0x0A: *type = REG_DWORD; break;                              /* SPDRP_CONFIGFLAGS */
    case 0x0B: s = n->mfg; break;                                     /* SPDRP_MFG */
    case 0x0C: s = n->friendly; break;                                /* SPDRP_FRIENDLYNAME */
    case 0x0D: s = n->location; break;                                /* SPDRP_LOCATION_INFORMATION */
    case 0x0E: {                                                      /* SPDRP_PHYSICAL_DEVICE_OBJECT_NAME */
        static const char hex[] = "0123456789abcdef";
        memcpy(tmp, "\\Device\\", 8);
        for (int k = 0; k < 8; k++) tmp[8 + k] = hex[(n->id >> (28 - 4 * k)) & 15];
        tmp[16] = 0;
        s = tmp;
        break;
    }
    case 0x0F:                                                        /* SPDRP_CAPABILITIES */
        *type = REG_DWORD;
        dw = (n->flags & NOVA_PNP_REMOVABLE) ? 0x04 | 0x10 : 0;       /* CM_DEVCAP_REMOVABLE | _SURPRISEREMOVALOK */
        break;
    case 0x16: {                                                      /* SPDRP_ENUMERATOR_NAME */
        int k = 0;
        while (n->instance[k] && n->instance[k] != '\\' && k < 63) { tmp[k] = n->instance[k]; k++; }
        tmp[k] = 0;
        s = tmp;
        break;
    }
    default: return -1;
    }
    if (*type == REG_DWORD) {
        if (cap >= 4) memcpy(out, &dw, 4);
        return 4;
    }
    if (!s || !s[0]) return -1;
    int len = lstrlenA(s) + 1;
    if (len <= cap) memcpy(out, s, (size_t)len);
    return len;
}
