/*
 * usbpnp.c — USB devices in the Plug and Play device tree (io/pnp.h)
 *
 * The shape is Windows': each controller has a root hub node under its PCI
 * function; a device is a node under its hub ("USB\VID_v&PID_p\..."), and
 * a composite device (several functions) has a child per function
 * ("USB\VID_v&PID_p&MI_nn\...").  A HID keyboard or mouse function has a
 * "HID\..." child of class Keyboard or Mouse under it, a mass-storage one
 * a "USBSTOR\Disk..." child of class DiskDrive.  The service is the NovaOS
 * driver that took the function (usb.c's probe order); a function no
 * driver took has no service and problem 28, as on Windows without a
 * driver.  Everything here runs on the usb thread.
 */

#include "usb_hc.h"
#include "../io/pnp.h"
#include "../nt/nt.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../ke/printf.h"

static void put(char *dst, size_t cap, const char *src)
{
    size_t n = strnlen(src, cap - 1);
    memcpy(dst, src, n);
    dst[n] = 0;
}

/* REG_MULTI_SZ from up to four strings (empty ones skipped) */
static void ids(char *out, size_t cap, const char *a, const char *b, const char *c, const char *d)
{
    const char *v[4] = { a, b, c, d };
    size_t at = 0;
    for (int i = 0; i < 4; i++) {
        if (!v[i] || !v[i][0]) continue;
        size_t l = strlen(v[i]);
        if (at + l + 2 > cap) break;
        memcpy(out + at, v[i], l + 1);
        at += l + 1;
    }
    out[at] = 0;
}

void UsbPnpController(UsbHc *hc, const PciDevice *pci)
{
    PnpNode *n = kzalloc(sizeof(PnpNode));
    if (!n) return;
    const char *hub = pci->prog_if == 0x30 ? "ROOT_HUB30" : pci->prog_if == 0x20 ? "ROOT_HUB20" : "ROOT_HUB";
    char a[64], b[64], c[32];
    ksnprintf(a, sizeof(a), "USB\\%s&VID%04X&PID%04X", hub, pci->vendor, pci->device);
    ksnprintf(b, sizeof(b), "USB\\%s", hub);
    ids(n->hwids, sizeof(n->hwids), a, b, NULL, NULL);
    ksnprintf(n->instance, sizeof(n->instance), "USB\\%s\\4&%X&0&0", hub, hc->num);
    ksnprintf(c, sizeof(c), "USB bus %d", hc->num);
    put(n->location, sizeof(n->location), c);
    put(n->cls, sizeof(n->cls), "USB");
    put(n->desc, sizeof(n->desc), pci->prog_if == 0x30 ? "USB Root Hub (USB 3.0)" : "USB Root Hub");
    put(n->mfg, sizeof(n->mfg), "(Standard USB HUBs)");
    put(n->service, sizeof(n->service), "usb");
    n->flags = PNP_STARTED;
    hc->pci = *pci;
    hc->pnp = PnpAdd(PnpPciNode(pci->bus, pci->dev, pci->func), n);
    kfree(n);
}

/* The class, description and service of a function a driver took */
static void function_kind(const UsbPnpFn *f, PnpNode *n)
{
    const char *cls = "USB", *desc = "USB Device";
    switch (f->kind) {
    case USBPNP_HUB:   desc = "Generic USB Hub"; break;
    case USBPNP_HID:   cls = "HIDClass"; desc = "USB Input Device"; break;
    case USBPNP_PAD:   cls = "HIDClass"; desc = "USB Input Device"; break;
    case USBPNP_XPAD:  cls = "XnaComposite"; desc = "Xbox Controller"; break;
    case USBPNP_MSC:   desc = "USB Mass Storage Device"; break;
    case USBPNP_AUDIO: cls = "MEDIA"; desc = "USB Audio Device"; break;
    default:
        cls = "";
        desc = f->cls == 3 ? "USB Input Device" : f->cls == 8 ? "USB Mass Storage Device" :
               f->cls == 1 ? "USB Audio Device" : f->cls == 0x0E ? "USB Video Device" : f->cls == 0xE0 ? "Bluetooth Radio" : "USB Device";
        break;
    }
    put(n->cls, sizeof(n->cls), cls);
    put(n->desc, sizeof(n->desc), desc);
    if (f->driver) {
        put(n->service, sizeof(n->service), f->driver);
        n->flags |= PNP_STARTED;
    }
}

/* The node under a HID or mass-storage function: what the function is */
static void add_grandchild(UsbDev *d, const UsbPnpFn *f, UINT32 parent, const char *base, const char *suffix,
                           const char *product)
{
    PnpNode *n = kzalloc(sizeof(PnpNode));
    if (!n) return;
    char a[96], b[96];
    if (f->kind == USBPNP_MSC) {
        char prod[48];
        int k = 0;
        for (const char *s = product; *s && k < (int)sizeof(prod) - 1; s++)
            prod[k++] = *s == ' ' ? '_' : *s;
        prod[k] = 0;
        ksnprintf(a, sizeof(a), "USBSTOR\\Disk&Ven_&Prod_%s&Rev_%04X", prod, d->rev);
        ksnprintf(b, sizeof(b), "USBSTOR\\Disk&Ven_&Prod_%s", prod);
        ids(n->hwids, sizeof(n->hwids), a, b, "USBSTOR\\Disk", "GenDisk");
        ids(n->compat, sizeof(n->compat), "USBSTOR\\Disk", "USBSTOR\\RAW", "GenDisk", NULL);
        ksnprintf(n->instance, sizeof(n->instance), "%s\\%s&0", a, suffix);
        put(n->cls, sizeof(n->cls), "DiskDrive");
        ksnprintf(n->desc, sizeof(n->desc), "%s USB Device", product[0] ? product : "Disk");
        put(n->mfg, sizeof(n->mfg), "(Standard disk drives)");
    } else {
        ksnprintf(a, sizeof(a), "HID\\%s", base + 4);           /* (base: "USB\VID_...", maybe "&MI_nn") */
        ids(n->hwids, sizeof(n->hwids), a, NULL, NULL, NULL);
        bool kbd = f->kind == USBPNP_HID && f->proto == 1, mouse = f->kind == USBPNP_HID && f->proto == 2;
        ids(n->compat, sizeof(n->compat), kbd ? "HID_DEVICE_SYSTEM_KEYBOARD" : mouse ? "HID_DEVICE_SYSTEM_MOUSE" :
            f->kind == USBPNP_PAD ? "HID_DEVICE_SYSTEM_GAME" : "HID_DEVICE_UP:000D_U:0002", "HID_DEVICE", NULL, NULL);
        ksnprintf(n->instance, sizeof(n->instance), "%s\\%s&0000", a, suffix);
        put(n->cls, sizeof(n->cls), kbd ? "Keyboard" : mouse ? "Mouse" : "HIDClass");
        put(n->desc, sizeof(n->desc), kbd ? "HID Keyboard Device" : mouse ? "HID-compliant mouse" :
            f->kind == USBPNP_PAD ? "HID-compliant game controller" : "HID-compliant device");
        put(n->mfg, sizeof(n->mfg), kbd ? "(Standard keyboards)" : mouse ? "Microsoft" : "(Standard system devices)");
    }
    put(n->location, sizeof(n->location), d->name);
    put(n->friendly, sizeof(n->friendly), product);
    put(n->service, sizeof(n->service), f->driver);
    n->flags = PNP_STARTED | PNP_REMOVABLE;
    PnpAdd(parent, n);
    kfree(n);
}

/* A function's node (@mi >= 0: a composite device's interface @mi) */
static UINT32 add_function(UsbDev *d, const UsbPnpFn *f, int mi, UINT32 parent, const char *suffix, const char *product)
{
    PnpNode *n = kzalloc(sizeof(PnpNode));
    if (!n) return 0;
    char base[48], a[96], b[64], c1[64], c2[48], c3[32], inst_suffix[40];
    if (mi >= 0) ksnprintf(base, sizeof(base), "USB\\VID_%04X&PID_%04X&MI_%02X", d->vid, d->pid, mi);
    else         ksnprintf(base, sizeof(base), "USB\\VID_%04X&PID_%04X", d->vid, d->pid);
    if (mi >= 0) {
        ksnprintf(a, sizeof(a), "USB\\VID_%04X&PID_%04X&REV_%04X&MI_%02X", d->vid, d->pid, d->rev, mi);
        ksnprintf(inst_suffix, sizeof(inst_suffix), "%s&%04X", suffix, mi);
    } else {
        ksnprintf(a, sizeof(a), "USB\\VID_%04X&PID_%04X&REV_%04X", d->vid, d->pid, d->rev);
        put(inst_suffix, sizeof(inst_suffix), suffix);
    }
    ids(n->hwids, sizeof(n->hwids), a, base, NULL, NULL);
    ksnprintf(c1, sizeof(c1), "USB\\Class_%02X&SubClass_%02X&Prot_%02X", f->cls, f->sub, f->proto);
    ksnprintf(c2, sizeof(c2), "USB\\Class_%02X&SubClass_%02X", f->cls, f->sub);
    ksnprintf(c3, sizeof(c3), "USB\\Class_%02X", f->cls);
    ids(n->compat, sizeof(n->compat), c1, c2, c3, NULL);
    ksnprintf(n->instance, sizeof(n->instance), "%s\\%s", base, inst_suffix);
    ksnprintf(b, sizeof(b), "Port_#%04u.Hub_#%04u", d->parent ? d->parent_port : d->root_port,
              d->parent ? (unsigned)d->parent->depth + 2 : (unsigned)d->hc->num);
    put(n->location, sizeof(n->location), b);
    put(n->friendly, sizeof(n->friendly), product);
    put(n->mfg, sizeof(n->mfg), f->kind == USBPNP_HUB ? "(Standard USB HUBs)" : "(Standard USB Host Controller)");
    n->flags = PNP_REMOVABLE;
    function_kind(f, n);
    UINT32 id = PnpAdd(parent, n);
    kfree(n);
    if (id && f->driver && (f->kind == USBPNP_MSC || f->kind == USBPNP_PAD ||
                            (f->kind == USBPNP_HID && (f->proto == 1 || f->proto == 2))))
        add_grandchild(d, f, id, base, inst_suffix, product);
    return id;
}

void UsbPnpDevice(UsbDev *d, UINT8 dev_class, const UsbPnpFn *fn, int nfn)
{
    UINT32 parent = d->parent ? d->parent->pnp : d->hc->pnp;
    if (!parent || !nfn) return;
    char product[64], suffix[32], id[48];
    if (!UsbDevProductName(d, product, sizeof(product))) product[0] = 0;
    ksnprintf(suffix, sizeof(suffix), "5&%X&%X&%u", d->hc->num, d->route << 8 | d->root_port,
              d->parent ? d->parent_port : d->root_port);
    /* The device's instance ID ends in its serial number when it has a
     * usable one, as on Windows (so it keeps its ID on any port), else in
     * where it is plugged in; a composite device's functions are told
     * apart by where it is */
    if (!UsbDevSerialId(d, id, sizeof(id))) memcpy(id, suffix, sizeof(suffix));

    /* Functions: the interfaces, a run of audio interfaces counting as
     * one (its control interface and streams), and a camera's video
     * control interface with the streaming ones after it as one (its
     * interface association); a vendor or hub device is one function
     * whatever its interfaces */
    UsbPnpFn f[USBPNP_MAX_FN];
    int n = 0;
    for (int i = 0; i < nfn; i++) {
        UINT32 bit = fn[i].number < 32 ? 1u << fn[i].number : 0;
        if (n && fn[i].cls == 1 && f[n - 1].cls == 1) {
            UINT32 had = f[n - 1].ifaces;
            if (!f[n - 1].driver) f[n - 1] = fn[i];
            f[n - 1].ifaces = had | bit;
            continue;
        }
        if (n && fn[i].cls == 0x0E && fn[i].sub == 2 && f[n - 1].cls == 0x0E) { f[n - 1].ifaces |= bit; continue; }
        if (n < USBPNP_MAX_FN) { f[n] = fn[i]; f[n++].ifaces = bit; }
    }
    bool composite = n > 1 && dev_class != 0xFF && dev_class != 0x09;
    if (!composite) {
        UsbPnpFn one = f[0];
        for (int i = 1; i < n && !one.driver; i++) if (f[i].driver) one = f[i];
        d->pnp = add_function(d, &one, -1, parent, id, product);
        if (d->pnp && !one.driver) NtPnpUsbArrived(d->pnp);    /* (a Windows driver may be installed on it) */
        return;
    }
    PnpNode *c = kzalloc(sizeof(PnpNode));
    if (!c) return;
    char a[64], b[48];
    ksnprintf(a, sizeof(a), "USB\\VID_%04X&PID_%04X&REV_%04X", d->vid, d->pid, d->rev);
    ksnprintf(b, sizeof(b), "USB\\VID_%04X&PID_%04X", d->vid, d->pid);
    ids(c->hwids, sizeof(c->hwids), a, b, NULL, NULL);
    ids(c->compat, sizeof(c->compat), "USB\\DevClass_00&SubClass_00&Prot_00", "USB\\DevClass_00&SubClass_00",
        "USB\\DevClass_00", "USB\\COMPOSITE");
    ksnprintf(c->instance, sizeof(c->instance), "%s\\%s", b, id);
    ksnprintf(c->location, sizeof(c->location), "Port_#%04u.Hub_#%04u", d->parent ? d->parent_port : d->root_port,
              d->parent ? (unsigned)d->parent->depth + 2 : (unsigned)d->hc->num);
    put(c->friendly, sizeof(c->friendly), product);
    put(c->cls, sizeof(c->cls), "USB");
    put(c->desc, sizeof(c->desc), "USB Composite Device");
    put(c->mfg, sizeof(c->mfg), "(Standard USB Host Controller)");
    put(c->service, sizeof(c->service), "usb");
    c->flags = PNP_STARTED | PNP_REMOVABLE;
    d->pnp = PnpAdd(parent, c);
    kfree(c);
    /* Each function its own node (USB\VID_v&PID_p&MI_nn), as usbccgp.sys
     * gives each its own PDO: one no NovaOS driver took may have a
     * Windows driver (nt/usbd.c, nt/pnp.c), with only its own interfaces */
    d->nfn_pnp = 0;
    for (int i = 0; i < n && d->pnp; i++) {
        UINT32 id = add_function(d, &f[i], f[i].number, d->pnp, suffix, product);
        if (!id || d->nfn_pnp >= USB_MAX_FNS) continue;
        d->fn_pnp[d->nfn_pnp] = id;
        d->fn_ifaces[d->nfn_pnp++] = f[i].ifaces;
        if (!f[i].driver) NtPnpUsbArrived(id);
    }
}
