/*
 * pnp.c — the Plug and Play manager for Windows drivers, and the PCI bus
 * driver under them
 *
 * A Windows function driver never finds its device itself: the PnP
 * manager hands it one.  When a PCI function that no NovaOS driver runs
 * (problem 28 in the device tree, io/pnp.c) has a driver installed on it
 * (its HKLM\SYSTEM\CurrentControlSet\Enum\<instance> key names a Service,
 * as setupapi's device install writes it), NtPnpStart does what Windows
 * does:
 *
 *   - the bus driver's physical device object (PDO) for the function,
 *     named \Device\NTPNP_PCInnnn, which answers the bus's PnP requests
 *     (IRP_MN_QUERY_ID, QUERY_CAPABILITIES, QUERY_INTERFACE for
 *     BUS_INTERFACE_STANDARD, READ_CONFIG / WRITE_CONFIG, ...);
 *   - the service's driver loaded (ldr.c) and its AddDevice called with
 *     the PDO: it creates its functional device object and attaches it;
 *   - IRP_MN_QUERY_CAPABILITIES, then IRP_MN_START_DEVICE down the stack
 *     with the function's resources from its configuration space: each
 *     memory and I/O BAR, and its interrupt line (a GSI from ACPI's _PRT,
 *     routed to a vector of its own, intr.c), raw and translated.
 *
 * NtPnpStop takes a started device away again the way Windows disables
 * one: IRP_MN_QUERY_REMOVE_DEVICE (a driver may veto it), then
 * IRP_MN_REMOVE_DEVICE; the device's interfaces are turned off, and a
 * driver left with no device is unloaded.
 *
 * A USB device no NovaOS driver took gets a PDO from the USB bus
 * interface (usbd.c), with no resources (its driver reaches the device
 * through URBs), and starts the same way; so does each function of a
 * composite device (its USB\VID_v&PID_p&MI_nn node), as usbccgp.sys
 * gives each its own PDO; unplugged, it gets
 * IRP_MN_SURPRISE_REMOVAL and IRP_MN_REMOVE_DEVICE, its children first
 * (NtPnpSurpriseRemoval).
 *
 * A root-enumerated device (ROOT\<class>\NNNN), a virtual one no bus
 * finds, exists because a program registered it, as devcon or devgen do on
 * Windows (setupapi's SetupDiCreateDeviceInfo, then DIF_REGISTERDEVICE:
 * NtPnpRootAdd); it is kept under Enum\ROOT and comes back at every boot.
 * The PnP manager's own PDO stands for it (no resources) and its driver
 * starts as a PCI function's does.  Removing it (DIF_REMOVE, pnputil
 * /remove-device: NtPnpRootRemove) takes its driver away and leaves no node.
 *
 * Filter drivers: before the function driver's AddDevice, each lower filter
 * the device's Enum key (LowerFilters) and then its setup class's key
 * (Control\Class\{GUID}) name gets its AddDevice with the PDO, and after
 * it the upper filters (UpperFilters), so each attaches above the last, as
 * Windows builds a device stack.  A KMDF HID driver is such a filter, under
 * mshidkmdf (mshidkmdf.c).
 *
 * Child devices: a bus driver (vioser.sys's ports, KMDF child lists) calls
 * IoInvalidateDeviceRelations(BusRelations) on its PDO; the PnP thread
 * then asks the stack for its IRP_MN_QUERY_DEVICE_RELATIONS BusRelations
 * (as it also does right after a start), gives each new PDO in the answer
 * a device node under the parent's (its IDs from IRP_MN_QUERY_ID, its
 * text and capabilities) and starts it: with the function driver its
 * Enum key names, or, with none and the PDO RawDeviceOK, raw (the PDO is
 * its own function driver, IRP_MN_START_DEVICE with no resources).  A
 * PDO no longer in the answer is removed (IRP_MN_SURPRISE_REMOVAL if it
 * was started, then IRP_MN_REMOVE_DEVICE) and its node left as a
 * phantom; taking the parent away removes its children first.
 *
 * Device interfaces: IoRegisterDeviceInterface names one
 * \??\<instance with '#' for '\'>#{class GUID}, and IoSetDeviceInterfaceState
 * makes that name a link to the PDO, so a program opens the device by the
 * path SetupDiGetDeviceInterfaceDetail or CM_Get_Device_Interface_List
 * gives it (they learn a node's interfaces through NtPnpInterfaces).
 *
 * Requests here run at PASSIVE_LEVEL in a thread that may wait: the boot
 * thread, or a program's system call (pnputil, newdev).
 */
#include "ntapi.h"
#include "../io/pnp.h"
#include "../hal/pci.h"
#include "../hal/aml.h"
#include "../drivers/usb.h"
#include "../hal/ioapic.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../ke/scheduler.h"
#include "../um/um.h"

#define ENUM_KEY "Machine\\SYSTEM\\CurrentControlSet\\Enum"
#define MAX_PDOS 128

/* CM_PROB_* (problem codes Device Manager shows) */
#define PROB_NO_DRIVER       28
#define PROB_FAILED_START    10
#define PROB_FAILED_ADD      31
#define PROB_DRIVER_FAILED_LOAD 39
#define PROB_UNSIGNED_DRIVER 52
#define PROB_DISABLED        22

#define ST_INVALID_IMAGE_HASH ((NTSTATUS)0xC0000428)
#define ST_INVALID_PARAMETER_2 ((NT_STATUS)0xC00000F0)
#define ST_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184)

typedef struct NtIface {
    struct NtIface *next;
    NT_GUID         guid;
    char            link[200];          /* the link's name in \?? (no reference string) */
    bool            on;
} NtIface;

typedef struct NtPdo {
    NT_DEVICE_OBJECT    *pdo;
    UINT32               node;
    PciDevice            pci;
    char                 instance[128];
    char                 service[64];   /* the driver running it ("" when none) */
    char                 devname[48];   /* the PDO's name in \Device */
    NT_CM_RESOURCE_LIST *raw, *xlat;
    UINT32               vector;        /* translated line-based interrupt (0: none) */
    UINT8                irql;
    UINT32               gsi;           /* (its raw GSI) */
    bool                 level;
    unsigned             nbase;         /* descriptors before the interrupt's */
    UINT8                msi_kind;      /* 5: MSI, 0x11: MSI-X (the capability granted) */
    UINT8                msi_cap;       /* its capability's offset in configuration space */
    UINT8                msi_first;     /* the block of message vectors reserved for it */
    UINT8                msi_n;         /* (0: none; messages not granted) */
    bool                 msi_on;
    bool                 line_only;     /* its driver takes no messages */
    volatile UINT32     *msix_tab;      /* MSI-X's table, mapped */
    bool                 started;
    NtIface             *ifaces;
    struct NtPdo        *parent;        /* a bus driver's child: its parent (NULL: a PCI function) */
    bool                 seen;          /* (in the parent's last BusRelations) */
    bool                 rawpdo;        /* (started with no function driver) */
    unsigned int         caps;          /* DEVICE_CAPABILITIES.Flags (children) */
    UINT32               address;       /* DEVICE_CAPABILITIES.Address (children) */
    bool                 usb;           /* a USB device's PDO (usbd.c's) */
    bool                 root;          /* a root-enumerated device's PDO (ours, no resources) */
    char                 filters[128];  /* the filter drivers on its stack (REG_MULTI_SZ) */
} NtPdo;

/* DEVICE_CAPABILITIES.Flags */
#define CAP_UNIQUE_ID        0x40
#define CAP_RAW_DEVICE_OK    0x100

/* DEVICE_RELATIONS */
typedef struct { unsigned int Count; NT_DEVICE_OBJECT *Objects[1]; } NtRelations;

static NT_DRIVER_OBJECT g_pci;          /* the PCI bus driver: owns the PCI PDOs */
static NtPdo           *g_pdo[MAX_PDOS];
static int              g_npdo;
static KSpinLock        g_lock = KSPINLOCK_INIT;

static NT_KMUTEX       g_mutex;          /* (starting, stopping and enumerating: one at a time) */
static NT_KEVENT       g_enum_event;     /* (a bus asked for enumeration) */
static NT_DEVICE_OBJECT *g_enum_pending[8];
static bool            g_ready;

static NtPdo *pdo_of(NT_DEVICE_OBJECT *d)
{
    if (!d) return NULL;
    if (d->DriverObject == &g_pci) return (NtPdo *)d->DeviceExtension;
    for (int i = 0; i < g_npdo; i++) if (g_pdo[i]->pdo == d) return g_pdo[i];
    return NULL;
}

/* @d's record without touching @d (which may be gone) */
static NtPdo *pdo_find(NT_DEVICE_OBJECT *d)
{
    for (int i = 0; i < g_npdo; i++) if (g_pdo[i]->pdo == d) return g_pdo[i];
    return NULL;
}

static void pnp_init(void);
static void pnp_lock(void) { nt_KeWaitForSingleObject(&g_mutex, 0, 0, 0, NULL); }
static void pnp_unlock(void) { nt_KeReleaseMutex(&g_mutex, 0); }

bool NtPnpInterruptOf(NT_DEVICE_OBJECT *pdo, UINT32 *vector, UINT8 *irql)
{
    NtPdo *p = pdo_of(pdo);
    if (!p || !p->vector) return false;
    *vector = p->vector;
    *irql = p->irql;
    return true;
}

const char *NtPnpInstanceOf(NT_DEVICE_OBJECT *pdo)
{
    NtPdo *p = pdo_of(pdo);
    return p ? p->instance : NULL;
}

/* ---- strings for the driver: wide, in pool it frees with ExFreePool ---- */

static NT_WCHAR *pool_wide(const char *s, size_t n)
{
    NT_WCHAR *w = nt_ExAllocatePoolWithTag(0, 2 * (n + 1), 0x20706E50 /* 'Pnp ' */);
    if (!w) return NULL;
    for (size_t i = 0; i < n; i++) w[i] = (UINT8)s[i];
    w[n] = 0;
    return w;
}

/* A REG_MULTI_SZ list's length with its final NUL */
static size_t multi_len(const char *m)
{
    size_t n = 0;
    while (m[n]) n += strlen(m + n) + 1;
    return n + 1;
}

/* ---- the function's resources ---- */

static NT_CM_PARTIAL_RESOURCE_DESCRIPTOR *desc_add(NT_CM_RESOURCE_LIST *l)
{
    return &l->List[0].PartialResourceList.PartialDescriptors[l->List[0].PartialResourceList.Count++];
}

static void set_interrupt(NtPdo *p, bool msg);

/* The raw and translated lists of @p's BARs and interrupt */
static bool build_resources(NtPdo *p)
{
    size_t size = sizeof(NT_CM_RESOURCE_LIST) + 8 * sizeof(NT_CM_PARTIAL_RESOURCE_DESCRIPTOR);
    p->raw = kzalloc(size);
    p->xlat = kzalloc(size);
    if (!p->raw || !p->xlat) return false;
    NT_CM_RESOURCE_LIST *l[2] = { p->raw, p->xlat };
    for (int k = 0; k < 2; k++) {
        l[k]->Count = 1;
        l[k]->List[0].InterfaceType = NT_PCIBus;
        l[k]->List[0].BusNumber = p->pci.bus;
        l[k]->List[0].PartialResourceList.Version = 1;
        l[k]->List[0].PartialResourceList.Revision = 1;
    }
    const PciDevice *d = &p->pci;
    UINT16 cmd = PciRead16(d->bus, d->dev, d->func, 0x04);
    PciWrite16(d->bus, d->dev, d->func, 0x04, (UINT16)(cmd & ~3u));   /* (decoding off while sizing) */
    for (int bar = 0; bar < 6; bar++) {
        UINT8 off = (UINT8)(0x10 + 4 * bar);
        UINT32 lo = PciRead32(d->bus, d->dev, d->func, off);
        bool io = lo & 1, is64 = !io && ((lo >> 1) & 3) == 2;
        PciWrite32(d->bus, d->dev, d->func, off, 0xFFFFFFFFu);
        UINT64 mask = PciRead32(d->bus, d->dev, d->func, off) & (io ? ~3ull : ~0xFull);
        PciWrite32(d->bus, d->dev, d->func, off, lo);
        UINT64 base = lo & (io ? ~3u : ~0xFu);
        if (is64) {
            UINT8 off2 = (UINT8)(off + 4);
            UINT32 hi = PciRead32(d->bus, d->dev, d->func, off2);
            PciWrite32(d->bus, d->dev, d->func, off2, 0xFFFFFFFFu);
            mask |= (UINT64)PciRead32(d->bus, d->dev, d->func, off2) << 32;
            PciWrite32(d->bus, d->dev, d->func, off2, hi);
            base |= (UINT64)hi << 32;
        } else {
            mask |= io ? 0xFFFFFFFFFFFF0000ull : 0xFFFFFFFF00000000ull;
        }
        UINT64 len = mask ? ~mask + 1 : 0;
        if (len && (mask & 0xFFFFFFFFull) && base) {
            for (int k = 0; k < 2; k++) {
                NT_CM_PARTIAL_RESOURCE_DESCRIPTOR *r = desc_add(l[k]);
                r->Type = io ? NT_CmResourceTypePort : NT_CmResourceTypeMemory;
                r->ShareDisposition = NT_CmResourceShareDeviceExclusive;
                r->Flags = io ? NT_CM_RESOURCE_PORT_IO : (UINT16)(NT_CM_RESOURCE_MEMORY_READ_WRITE | (lo & 8 ? 4 : 0));
                r->u.Memory.Start = (long long)base;
                r->u.Memory.Length = (unsigned int)len;
            }
        }
        if (is64) bar++;
    }
    PciWrite16(d->bus, d->dev, d->func, 0x04, cmd);

    p->nbase = l[0]->List[0].PartialResourceList.Count;
    UINT32 r3c = PciRead32(d->bus, d->dev, d->func, 0x3C);
    UINT8 pin = (UINT8)(r3c >> 8);
    UINT32 gsi = 0;
    bool level = true, low = true, have = false;
    if (pin >= 1 && pin <= 4) {
        have = d->bus == 0 && AmlPciIrq(d->dev, (UINT8)(pin - 1), &gsi, &level, &low);
        if (!have && (UINT8)r3c && (UINT8)r3c != 0xFF) { gsi = (UINT8)r3c; have = true; level = low = true; }
    }
    UINT8 vector = have ? NtIntrVector(gsi, level, low) : 0;
    if (vector) {
        p->vector = vector;
        p->irql = (UINT8)(vector >> 4);
        p->gsi = gsi;
        p->level = level;
    }
    set_interrupt(p, false);
    return true;
}

/* The function's interrupt descriptor in both lists (after its BARs): the
 * line-based one, or, with @msg, the message one for the block reserved */
static void set_interrupt(NtPdo *p, bool msg)
{
    NT_CM_RESOURCE_LIST *l[2] = { p->raw, p->xlat };
    for (int k = 0; k < 2; k++) l[k]->List[0].PartialResourceList.Count = p->nbase;
    if (!msg && !p->vector) return;
    for (int k = 0; k < 2; k++) {
        NT_CM_PARTIAL_RESOURCE_DESCRIPTOR *r = desc_add(l[k]);
        r->Type = NT_CmResourceTypeInterrupt;
        if (msg) {
            r->ShareDisposition = NT_CmResourceShareDeviceExclusive;
            r->Flags = NT_CM_RESOURCE_INTERRUPT_MESSAGE;
            if (!k) {
                r->u.MessageInterrupt.Raw.MessageCount = p->msi_n;
                r->u.MessageInterrupt.Raw.Affinity = ~0ull;
            } else {
                r->u.MessageInterrupt.Translated.Level = (unsigned short)(p->msi_first >> 4);
                r->u.MessageInterrupt.Translated.Vector = p->msi_first;
                r->u.MessageInterrupt.Translated.Affinity = 1;      /* (the boot CPU) */
            }
        } else {
            r->ShareDisposition = p->level ? NT_CmResourceShareShared : NT_CmResourceShareDeviceExclusive;
            r->Flags = p->level ? NT_CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE : NT_CM_RESOURCE_INTERRUPT_LATCHED;
            r->u.Interrupt.Level = (unsigned short)(k ? p->irql : p->gsi);
            r->u.Interrupt.Vector = k ? p->vector : p->gsi;
            r->u.Interrupt.Affinity = k ? 1 : ~0ull;          /* (routed to the boot CPU) */
        }
    }
}

/* ---- the PCI bus driver's PDOs ---- */

static const NT_GUID g_bus_std = { 0x496B8280, 0x6F25, 0x11D0, { 0xBE, 0xAF, 0x08, 0x00, 0x2B, 0xE2, 0x09, 0x2F } };
static const NT_GUID g_bus_pci = { 0xC8EBDFB0, 0xB510, 0x11D0, { 0x80, 0xE5, 0x00, 0xA0, 0xC9, 0x25, 0x42, 0xE3 } };

/* Configuration space, as BUS_INTERFACE_STANDARD and IRP_MN_READ_CONFIG
 * reach it: bytes that exist (256) */
static UINT32 config(NtPdo *p, bool write, UINT8 *buf, UINT32 off, UINT32 len)
{
    if (off >= 256) return 0;
    if (len > 256 - off) len = 256 - off;
    const PciDevice *d = &p->pci;
    for (UINT32 i = 0; i < len; i++) {
        UINT8 a = (UINT8)(off + i), sh = (UINT8)((a & 3) * 8);
        UINT32 v = PciRead32(d->bus, d->dev, d->func, (UINT8)(a & ~3));
        if (write) PciWrite32(d->bus, d->dev, d->func, (UINT8)(a & ~3), (v & ~(0xFFu << sh)) | ((UINT32)buf[i] << sh));
        else buf[i] = (UINT8)(v >> sh);
    }
    return len;
}

UINT32 NtPnpConfig(NT_DEVICE_OBJECT *pdo, bool write, void *buf, UINT32 off, UINT32 len)
{
    NtPdo *p = pdo_of(pdo);
    return p ? config(p, write, buf, off, len) : 0;
}

static NT_API void bus_ref(void *ctx) { (void)ctx; }
static NT_API UINT8 bus_translate(void *ctx, long long addr, unsigned int len, unsigned int *space, long long *out)
{
    (void)ctx; (void)len;
    *out = addr;                                       /* (CPU and bus addresses are the same on a PC) */
    if (space) *space = *space & 1;
    return 1;
}
static NT_API NT_DMA_ADAPTER *bus_dma(void *ctx, NT_DEVICE_DESCRIPTION *d, unsigned int *regs)
{
    return nt_IoGetDmaAdapter(((NtPdo *)ctx)->pdo, d, regs);
}
/* (drivers pass PCI_WHICHSPACE_CONFIG, 0, as the type, as virtio-win's
 * do, or the bus data type PCIConfiguration, 4: both are config space) */
static NT_API unsigned int bus_get(void *ctx, unsigned int type, void *buf, unsigned int off, unsigned int len)
{
    return type == NT_PCI_WHICHSPACE_CONFIG || type == NT_PCI_CONFIG_DATA ? config(ctx, false, buf, off, len) : 0;
}
static NT_API unsigned int bus_set(void *ctx, unsigned int type, void *buf, unsigned int off, unsigned int len)
{
    return type == NT_PCI_WHICHSPACE_CONFIG || type == NT_PCI_CONFIG_DATA ? config(ctx, true, buf, off, len) : 0;
}

/* A root-enumerated device's PDO: no bus behind it, so no resources,
 * configuration space or bus interface */
static NT_STATUS root_pnp(NtPdo *p, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    NT_STATUS st = irp->IoStatus.Status;
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE:
    case NT_IRP_MN_REMOVE_DEVICE:
    case NT_IRP_MN_SURPRISE_REMOVAL:
    case NT_IRP_MN_QUERY_REMOVE_DEVICE:
    case NT_IRP_MN_CANCEL_REMOVE_DEVICE:
    case NT_IRP_MN_QUERY_STOP_DEVICE:
    case NT_IRP_MN_CANCEL_STOP_DEVICE:
    case NT_IRP_MN_STOP_DEVICE:
    case NT_IRP_MN_QUERY_PNP_DEVICE_STATE:
        return NT_STATUS_SUCCESS;
    case NT_IRP_MN_QUERY_CAPABILITIES: {
        NT_DEVICE_CAPABILITIES *c = sl->Parameters.Others.Argument1;
        if (!c || c->Size < sizeof(*c)) return NT_STATUS_INVALID_PARAMETER;
        c->Address = c->UINumber = 0xFFFFFFFFu;
        c->DeviceState[1] = 1;                          /* PowerSystemWorking: D0 */
        for (int i = 2; i < 7; i++) c->DeviceState[i] = 4;
        c->SystemWake = c->DeviceWake = 0;
        return NT_STATUS_SUCCESS;
    }
    case NT_IRP_MN_QUERY_ID:
    case NT_IRP_MN_QUERY_DEVICE_TEXT: {
        PnpNode n;
        int type = (int)(UINT64)sl->Parameters.Others.Argument1;
        if (!PnpGet(p->node, &n)) break;
        const char *s = NULL;
        size_t len = 0;
        if (sl->MinorFunction == NT_IRP_MN_QUERY_DEVICE_TEXT) { if (type == 0) s = n.desc; else if (type == 1) s = n.location; }
        else if (type == 0) s = n.instance, len = (size_t)(strrchr(n.instance, '\\') - n.instance);   /* ROOT\<class> */
        else if (type == 1) s = n.hwids, len = multi_len(n.hwids);
        else if (type == 2) s = n.compat, len = multi_len(n.compat);
        else if (type == 3) s = strrchr(n.instance, '\\') + 1;
        if (!s) break;
        if (!len) len = strlen(s);
        NT_WCHAR *w = pool_wide(s, len);
        if (!w) return NT_STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = (NT_ULONG_PTR)w;
        return NT_STATUS_SUCCESS;
    }
    case NT_IRP_MN_QUERY_DEVICE_RELATIONS:
        if ((int)(UINT64)sl->Parameters.Others.Argument1 == 4) {       /* TargetDeviceRelation */
            struct { unsigned int Count; NT_DEVICE_OBJECT *Objects[1]; } *r = nt_ExAllocatePoolWithTag(0, sizeof(*r), 0x20706E50);
            if (!r) return NT_STATUS_INSUFFICIENT_RESOURCES;
            r->Count = 1;
            r->Objects[0] = p->pdo;
            irp->IoStatus.Information = (NT_ULONG_PTR)r;
            return NT_STATUS_SUCCESS;
        }
        break;
    }
    return st;
}

static NT_STATUS pdo_pnp(NtPdo *p, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    if (p->root) return root_pnp(p, irp, sl);
    const PciDevice *d = &p->pci;
    NT_STATUS st = irp->IoStatus.Status;
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE: {
        UINT16 cmd = PciRead16(d->bus, d->dev, d->func, 0x04);
        cmd |= 7;                                       /* I/O and memory decoding, bus mastering */
        if (p->vector) cmd &= (UINT16)~(1u << 10);      /* its interrupt line on */
        PciWrite16(d->bus, d->dev, d->func, 0x04, cmd);
        NtPnpMsiEnable(p->pdo, true);                   /* (its messages, if it was given them) */
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_REMOVE_DEVICE:
    case NT_IRP_MN_SURPRISE_REMOVAL: {
        NtPnpMsiEnable(p->pdo, false);
        UINT16 cmd = PciRead16(d->bus, d->dev, d->func, 0x04);
        PciWrite16(d->bus, d->dev, d->func, 0x04, (UINT16)((cmd & ~7u) | (1u << 10)));
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_REMOVE_DEVICE:
    case NT_IRP_MN_CANCEL_REMOVE_DEVICE:
    case NT_IRP_MN_QUERY_STOP_DEVICE:
    case NT_IRP_MN_CANCEL_STOP_DEVICE:
    case NT_IRP_MN_STOP_DEVICE:
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_QUERY_CAPABILITIES: {
        NT_DEVICE_CAPABILITIES *c = sl->Parameters.Others.Argument1;
        if (!c || c->Size < sizeof(*c)) { st = NT_STATUS_INVALID_PARAMETER; break; }
        c->Address = (unsigned int)d->dev << 16 | d->func;
        c->UINumber = 0xFFFFFFFFu;
        c->DeviceState[1] = 1;                          /* PowerSystemWorking: D0 */
        for (int i = 2; i < 7; i++) c->DeviceState[i] = 4;   /* sleeping, off: D3 */
        c->SystemWake = c->DeviceWake = 0;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_ID: {
        PnpNode n;
        if (!PnpGet(p->node, &n)) break;
        int type = (int)(UINT64)sl->Parameters.Others.Argument1;
        const char *s = NULL;
        size_t len = 0;
        if (type == 0) { s = n.hwids; len = strlen(n.hwids); }                  /* BusQueryDeviceID */
        else if (type == 1) { s = n.hwids; len = multi_len(n.hwids); }         /* BusQueryHardwareIDs */
        else if (type == 2) { s = n.compat; len = multi_len(n.compat); }       /* BusQueryCompatibleIDs */
        else if (type == 3) {                                                   /* BusQueryInstanceID */
            s = n.instance;
            for (const char *c = n.instance; *c; c++) if (*c == '\\') s = c + 1;
            len = strlen(s);
        }
        if (!s) break;
        NT_WCHAR *w = pool_wide(s, len);
        if (!w) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
        irp->IoStatus.Information = (NT_ULONG_PTR)w;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_DEVICE_TEXT: {
        PnpNode n;
        int type = (int)(UINT64)sl->Parameters.Others.Argument1;
        if (!PnpGet(p->node, &n) || type > 1) break;
        const char *s = type ? n.location : n.desc;
        NT_WCHAR *w = pool_wide(s, strlen(s));
        if (!w) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
        irp->IoStatus.Information = (NT_ULONG_PTR)w;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_INTERFACE: {
        const NT_GUID *g = sl->Parameters.QueryInterface.InterfaceType;
        NT_BUS_INTERFACE_STANDARD *b = (NT_BUS_INTERFACE_STANDARD *)sl->Parameters.QueryInterface.Interface;
        if (!g || memcmp(g, &g_bus_std, sizeof(*g)) || !b || sl->Parameters.QueryInterface.Size < sizeof(*b)) break;
        b->Header.Size = sizeof(*b);
        b->Header.Version = 1;
        b->Header.Context = p;
        b->Header.InterfaceReference = bus_ref;
        b->Header.InterfaceDereference = bus_ref;
        b->TranslateBusAddress = bus_translate;
        b->GetDmaAdapter = bus_dma;
        b->SetBusData = bus_set;
        b->GetBusData = bus_get;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_READ_CONFIG:
    case NT_IRP_MN_WRITE_CONFIG:
        if (sl->Parameters.ReadWriteConfig.WhichSpace != NT_PCI_WHICHSPACE_CONFIG) { st = NT_STATUS_NOT_SUPPORTED; break; }
        irp->IoStatus.Information = config(p, sl->MinorFunction == NT_IRP_MN_WRITE_CONFIG, sl->Parameters.ReadWriteConfig.Buffer,
                                           sl->Parameters.ReadWriteConfig.Offset, sl->Parameters.ReadWriteConfig.Length);
        st = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_QUERY_BUS_INFORMATION: {
        struct { NT_GUID BusTypeGuid; int LegacyBusType; unsigned int BusNumber; } *b =
            nt_ExAllocatePoolWithTag(0, sizeof(*b), 0x20706E50);
        if (!b) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
        b->BusTypeGuid = g_bus_pci;
        b->LegacyBusType = NT_PCIBus;
        b->BusNumber = d->bus;
        irp->IoStatus.Information = (NT_ULONG_PTR)b;
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_QUERY_DEVICE_RELATIONS:
        if ((int)(UINT64)sl->Parameters.Others.Argument1 == 4) {       /* TargetDeviceRelation: the PDO itself */
            struct { unsigned int Count; NT_DEVICE_OBJECT *Objects[1]; } *r = nt_ExAllocatePoolWithTag(0, sizeof(*r), 0x20706E50);
            if (!r) { st = NT_STATUS_INSUFFICIENT_RESOURCES; break; }
            r->Count = 1;
            r->Objects[0] = p->pdo;
            irp->IoStatus.Information = (NT_ULONG_PTR)r;
            st = NT_STATUS_SUCCESS;
        }
        break;
    case NT_IRP_MN_QUERY_PNP_DEVICE_STATE:
        st = NT_STATUS_SUCCESS;
        break;
    }
    return st;
}

/* The PDOs' dispatch routine (every major function) */
static NT_API NT_STATUS pdo_dispatch(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    NtPdo *p = pdo_of(dev);
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    NT_STATUS st;
    switch (sl->MajorFunction) {
    case NT_IRP_MJ_PNP:            st = pdo_pnp(p, irp, sl); break;
    case NT_IRP_MJ_POWER:          st = NT_STATUS_SUCCESS; break;   /* (D0 always: no power management yet) */
    case NT_IRP_MJ_SYSTEM_CONTROL: st = irp->IoStatus.Status; break;
    default:                       st = NT_STATUS_INVALID_DEVICE_REQUEST; break;
    }
    irp->IoStatus.Status = st;
    nt_IofCompleteRequest(irp, 0);
    return st;
}

static NtPdo *pdo_new(UINT32 node, const PciDevice *pci, const char *instance)
{
    if (g_npdo >= MAX_PDOS) return NULL;
    if (!g_pci.Type) {
        g_pci.Type = 4;
        g_pci.Size = (short)sizeof(g_pci);
        for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) g_pci.MajorFunction[i] = pdo_dispatch;
    }
    char name[48];
    ksnprintf(name, sizeof(name), "\\Device\\NTPNP_PCI%04u", (unsigned)g_npdo + 1);
    NT_WCHAR w[48];
    size_t n = strlen(name);
    for (size_t i = 0; i <= n; i++) w[i] = (UINT8)name[i];
    NT_UNICODE_STRING u = { (unsigned short)(2 * n), (unsigned short)(2 * n + 2), w };
    NT_DEVICE_OBJECT *d;
    if (!NT_SUCCESS(nt_IoCreateDevice(&g_pci, sizeof(NtPdo), &u, NT_FILE_DEVICE_UNKNOWN, 0, 0, &d))) return NULL;
    d->Flags = (d->Flags & ~NT_DO_DEVICE_INITIALIZING) | NT_DO_BUS_ENUMERATED_DEVICE | NT_DO_POWER_PAGABLE;
    NtPdo *p = d->DeviceExtension;
    p->pdo = d;
    p->node = node;
    p->pci = *pci;
    strncpy(p->instance, instance, sizeof(p->instance) - 1);
    strcpy(p->devname, name);
    if (!build_resources(p)) return NULL;
    g_pdo[g_npdo++] = p;
    return p;
}

static NtPdo *pdo_by_node(UINT32 node)
{
    for (int i = 0; i < g_npdo; i++) if (g_pdo[i]->node == node) return g_pdo[i];
    return NULL;
}

/* A record for USB device @node's PDO (usbd.c's) */
static NtPdo *usb_record(UINT32 node, const char *instance)
{
    NT_DEVICE_OBJECT *d = NtUsbPdo(node);
    if (!d || g_npdo >= MAX_PDOS) return NULL;
    NtPdo *p = kzalloc(sizeof(*p));
    if (!p) return NULL;
    p->pdo = d;
    p->node = node;
    p->usb = true;
    strncpy(p->instance, instance, sizeof(p->instance) - 1);
    NtIoDeviceName(d, p->devname, sizeof(p->devname));
    g_pdo[g_npdo++] = p;
    return p;
}

/* A PDO for root-enumerated device @node: the PnP manager's own, as for a
 * PCI function, with no resources */
static NtPdo *root_new(UINT32 node, const char *instance)
{
    static PciDevice none;
    if (g_npdo >= MAX_PDOS) return NULL;
    if (!g_pci.Type) {
        g_pci.Type = 4;
        g_pci.Size = (short)sizeof(g_pci);
        for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) g_pci.MajorFunction[i] = pdo_dispatch;
    }
    static unsigned seq;
    char name[48];
    ksnprintf(name, sizeof(name), "\\Device\\NTPNP_ROOT%04u", ++seq);
    NT_WCHAR w[48];
    size_t n = strlen(name);
    for (size_t i = 0; i <= n; i++) w[i] = (UINT8)name[i];
    NT_UNICODE_STRING u = { (unsigned short)(2 * n), (unsigned short)(2 * n + 2), w };
    NT_DEVICE_OBJECT *d;
    if (!NT_SUCCESS(nt_IoCreateDevice(&g_pci, sizeof(NtPdo), &u, NT_FILE_DEVICE_UNKNOWN, 0, 0, &d))) return NULL;
    d->Flags = (d->Flags & ~NT_DO_DEVICE_INITIALIZING) | NT_DO_BUS_ENUMERATED_DEVICE | NT_DO_POWER_PAGABLE;
    NtPdo *p = d->DeviceExtension;
    memset(p, 0, sizeof(*p));
    p->pdo = d;
    p->node = node;
    p->pci = none;
    p->root = true;
    strncpy(p->instance, instance, sizeof(p->instance) - 1);
    strcpy(p->devname, name);
    g_pdo[g_npdo++] = p;
    return p;
}

/* ---- PnP requests to a device's stack ---- */

/* Send IRP_MJ_PNP @minor (@a1, @a2 its parameters) to the top of @pdo's
 * stack and wait for it: its status, *@info its Information */
static NT_STATUS send_pnp(NT_DEVICE_OBJECT *pdo, UINT8 minor, void *a1, void *a2, NT_ULONG_PTR *info)
{
    NT_DEVICE_OBJECT *top = nt_IoGetAttachedDevice(pdo);
    int n = top->StackSize > 0 ? top->StackSize : 1;
    NT_IRP *irp = kzalloc(sizeof(NT_IRP) + (size_t)n * sizeof(NT_IO_STACK_LOCATION));
    if (!irp) return NT_STATUS_INSUFFICIENT_RESOURCES;
    NT_KEVENT done;
    NT_IO_STATUS_BLOCK iosb = { { 0 }, 0 };
    nt_KeInitializeEvent(&done, NT_NotificationEvent, 0);
    irp->Type = 6;
    irp->Size = (unsigned short)(sizeof(NT_IRP) + (size_t)n * sizeof(NT_IO_STACK_LOCATION));
    irp->StackCount = (char)n;
    irp->CurrentLocation = (char)(n + 1);
    irp->Tail.Overlay.CurrentStackLocation = (NT_IO_STACK_LOCATION *)(irp + 1) + n;
    irp->RequestorMode = 0;                                  /* KernelMode */
    irp->UserIosb = &iosb;
    irp->UserEvent = &done;
    irp->IoStatus.Status = NT_STATUS_NOT_SUPPORTED;         /* (what a PnP IRP starts as) */
    irp->ThreadListEntry.Flink = irp->ThreadListEntry.Blink = &irp->ThreadListEntry;
    NT_IO_STACK_LOCATION *sl = NtIrpNextStack(irp);
    sl->MajorFunction = NT_IRP_MJ_PNP;
    sl->MinorFunction = minor;
    sl->Parameters.Others.Argument1 = a1;
    sl->Parameters.Others.Argument2 = a2;
    NT_STATUS st = nt_IofCallDriver(top, irp);
    if (st != NT_STATUS_PENDING && !nt_KeReadStateEvent(&done)) {
        kprintf("[PNP] IRP_MJ_PNP 0x%02x: dispatch returned 0x%08x without completing its IRP\n", minor, (UINT32)st);
        return st;                                           /* (the driver may still hold it) */
    }
    nt_KeWaitForSingleObject(&done, 0, 0, 0, NULL);
    if (info) *info = iosb.Information;
    kfree(irp);
    return iosb.Status;
}

/* ---- starting and stopping a device ---- */

static void ascii_guid(const NT_GUID *g, char *out)
{
    ksnprintf(out, 40, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g->Data1, g->Data2, g->Data3, g->Data4[0],
              g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

/* ---- message-signalled interrupts ---- */

/* The offset of PCI capability @id in @d's configuration space, 0 if none */
static UINT8 pci_cap(const PciDevice *d, UINT8 id)
{
    if (!(PciRead16(d->bus, d->dev, d->func, 0x06) & 0x10)) return 0;       /* (no capability list) */
    UINT8 off = (UINT8)(PciRead32(d->bus, d->dev, d->func, 0x34) & 0xFC);
    for (int i = 0; off && i < 48; i++) {
        UINT32 hdr = PciRead32(d->bus, d->dev, d->func, off);
        if ((UINT8)hdr == id) return off;
        off = (UINT8)((hdr >> 8) & 0xFC);
    }
    return 0;
}

#define MSI_KEY "\\Device Parameters\\Interrupt Management\\MessageSignaledInterruptProperties"

/* As Windows does: a device gets message-signalled interrupts when it has an MSI or MSI-X
 * capability and its driver's INF opted in (HKR, "Interrupt Management\MessageSignaledInterruptProperties",
 * MSISupported, 0x00010001, 1; MessageNumberLimit, if there, caps the count).  MSI-X is
 * preferred; the count is a power of two (at most 16) of the vectors NtIntrMsiAlloc has.
 * Sets the descriptor in p's lists either way. */
static void msi_prepare(NtPdo *p)
{
    const PciDevice *d = &p->pci;
    char key[240];
    UINT32 on = 0, limit = 16;
    ksnprintf(key, sizeof(key), ENUM_KEY "\\%s" MSI_KEY, p->instance);
    if (p->line_only || !um_registry_get_dword(key, "MSISupported", &on) || on != 1) { set_interrupt(p, false); return; }
    if (um_registry_get_dword(key, "MessageNumberLimit", &limit) && limit > 16) limit = 16;
    UINT8 x = pci_cap(d, 0x11), m = x ? 0 : pci_cap(d, 0x05);
    UINT32 n = 0;
    if (x) n = (PciRead16(d->bus, d->dev, d->func, (UINT8)(x + 2)) & 0x7FF) + 1u;
    else if (m) n = 1u << ((PciRead16(d->bus, d->dev, d->func, (UINT8)(m + 2)) >> 1) & 7);
    if (n > limit) n = limit;
    while (n & (n - 1)) n &= n - 1;                      /* (down to a power of two) */
    UINT8 first = 0;
    for (; n && !(first = NtIntrMsiAlloc(n)); n >>= 1) { }
    if (!first) { set_interrupt(p, false); return; }
    p->msi_kind = x ? 0x11 : 5;
    p->msi_cap = x ? x : m;
    p->msi_first = first;
    p->msi_n = (UINT8)n;
    set_interrupt(p, true);
    kprintf("[PNP] %s: %s, %u message(s) from vector 0x%x at IRQL %u\n", p->instance, x ? "MSI-X" : "MSI", n, first,
            first >> 4);
}

/* Give the vectors back (the device stopped) and fall back to the line */
static void msi_release(NtPdo *p)
{
    if (!p->msi_n) return;
    if (p->msi_on) NtPnpMsiEnable(p->pdo, false);
    NtIntrMsiFree(p->msi_first, p->msi_n);
    p->msi_n = 0;
    set_interrupt(p, false);
}

void NtPnpLineOnly(NT_DEVICE_OBJECT *pdo)
{
    NtPdo *p = pdo_of(pdo);
    if (p) p->line_only = true;
}

bool NtPnpMessagesOf(NT_DEVICE_OBJECT *pdo, UINT8 *first, UINT32 *count, UINT8 *irql)
{
    NtPdo *p = pdo_of(pdo);
    if (!p || !p->msi_n) return false;
    *first = p->msi_first;
    *count = p->msi_n;
    *irql = (UINT8)(p->msi_first >> 4);
    return true;
}

/* Program the function's MSI or MSI-X capability with the block's messages and turn it on
 * (when the device starts, as Windows' PCI driver does when it assigns the resources; a
 * KMDF driver connects each message as a fully specified vector and never asks), or turn it off */
void NtPnpMsiEnable(NT_DEVICE_OBJECT *pdo, bool on)
{
    NtPdo *p = pdo_of(pdo);
    if (!p || !p->msi_n || on == p->msi_on) return;
    const PciDevice *d = &p->pci;
    UINT8 cap = p->msi_cap, ctl_off = (UINT8)(cap + 2);
    UINT16 ctl = PciRead16(d->bus, d->dev, d->func, ctl_off), cmd = PciRead16(d->bus, d->dev, d->func, 0x04);
    UINT64 addr;
    UINT32 data;
    if (!on) {
        PciWrite16(d->bus, d->dev, d->func, ctl_off, (UINT16)(ctl & ~(p->msi_kind == 0x11 ? 0x8000u : 1u)));
        if (p->vector) PciWrite16(d->bus, d->dev, d->func, 0x04, (UINT16)(cmd & ~(1u << 10)));   /* (its line on again) */
        p->msi_on = false;
        return;
    }
    IoApicMsiMessage(p->msi_first, &addr, &data);
    if (p->msi_kind == 0x11) {
        UINT32 tab = PciRead32(d->bus, d->dev, d->func, (UINT8)(cap + 4));
        if (!p->msix_tab) {
            UINT64 bar = PciBarAddress(d, tab & 7);
            p->msix_tab = bar ? (volatile UINT32 *)PciMapPhysical(bar + (tab & ~7u), 16u * 16) : NULL;
        }
        if (!p->msix_tab) return;
        for (unsigned i = 0; i < p->msi_n; i++) {
            volatile UINT32 *e = p->msix_tab + 4 * i;
            e[3] = 1;                                           /* (masked while it changes) */
            e[0] = (UINT32)addr;
            e[1] = (UINT32)(addr >> 32);
            e[2] = data + i;
            e[3] = 0;
        }
        ctl = (UINT16)((ctl | 0x8000u) & ~0x4000u);              /* enabled, function not masked */
    } else {
        bool is64 = ctl & 0x80;
        PciWrite32(d->bus, d->dev, d->func, (UINT8)(cap + 4), (UINT32)addr);
        if (is64) PciWrite32(d->bus, d->dev, d->func, (UINT8)(cap + 8), (UINT32)(addr >> 32));
        PciWrite16(d->bus, d->dev, d->func, (UINT8)(cap + (is64 ? 12 : 8)), (UINT16)data);
        if (ctl & 0x100) PciWrite32(d->bus, d->dev, d->func, (UINT8)(cap + (is64 ? 16 : 12)), 0);   /* (no vector masked) */
        unsigned log2 = 0;
        while ((1u << log2) < p->msi_n) log2++;
        ctl = (UINT16)((ctl & ~0x70u) | (log2 << 4) | 1u);
    }
    PciWrite16(d->bus, d->dev, d->func, 0x04, (UINT16)(cmd | (1u << 10)));      /* (no INTx while messages are on) */
    PciWrite16(d->bus, d->dev, d->func, ctl_off, ctl);
    p->msi_on = true;
}

static void log_resources(NtPdo *p)
{
    NT_CM_PARTIAL_RESOURCE_LIST *l = &p->xlat->List[0].PartialResourceList;
    for (unsigned i = 0; i < l->Count; i++) {
        NT_CM_PARTIAL_RESOURCE_DESCRIPTOR *r = &l->PartialDescriptors[i];
        if (r->Type == NT_CmResourceTypeMemory)
            kprintf("[PNP]   memory 0x%llx, %u bytes\n", (unsigned long long)r->u.Memory.Start, r->u.Memory.Length);
        else if (r->Type == NT_CmResourceTypePort)
            kprintf("[PNP]   I/O ports 0x%llx, %u\n", (unsigned long long)r->u.Port.Start, r->u.Port.Length);
        else if (r->Type == NT_CmResourceTypeInterrupt && (r->Flags & NT_CM_RESOURCE_INTERRUPT_MESSAGE))
            kprintf("[PNP]   interrupt: %u message(s) from vector 0x%x at IRQL %u\n",
                    p->raw->List[0].PartialResourceList.PartialDescriptors[i].u.MessageInterrupt.Raw.MessageCount,
                    r->u.MessageInterrupt.Translated.Vector, r->u.MessageInterrupt.Translated.Level);
        else if (r->Type == NT_CmResourceTypeInterrupt)
            kprintf("[PNP]   interrupt: GSI %u, vector 0x%x at IRQL %u\n",
                    p->raw->List[0].PartialResourceList.PartialDescriptors[i].u.Interrupt.Vector, r->u.Interrupt.Vector,
                    r->u.Interrupt.Level);
    }
}

/* The PCI function that is node @node */
static bool pci_of(UINT32 node, PciDevice *out)
{
    for (int i = 0; PciAt(i, out, NULL); i++)
        if (PnpPciNode(out->bus, out->dev, out->func) == node) return true;
    return false;
}

static void unload_if_idle(NtDriver *drv)
{
    if (!drv || drv->obj->DeviceObject) return;
    char service[64];
    strncpy(service, drv->service, sizeof(service) - 1);
    service[sizeof(service) - 1] = 0;
    if (drv->obj->DriverUnload) NtDrvUnload(service);
}

/* REG_MULTI_SZ value @name of key @key as ASCII into @out (ending in an
 * empty string), appended after the @at bytes already there: the new length */
static int multi_get(const char *key, const char *name, char *out, int at, int cap)
{
    NT_WCHAR w[128];
    UINT32 type = 0;
    int n = um_registry_get_value(key, name, &type, w, sizeof(w) - 4);
    if (n <= 0 || (type != 7 /* REG_MULTI_SZ */ && type != 1)) { out[at] = 0; return at; }
    w[n / 2] = w[n / 2 + 1] = 0;
    for (int i = 0; w[i] && at < cap - 2; i++) {             /* (each string, then its NUL) */
        for (; w[i] && at < cap - 2; i++) out[at++] = w[i] < 0x80 ? (char)w[i] : '?';
        out[at++] = 0;
    }
    out[at] = 0;
    return at;
}

/* The filter drivers of device @instance, lower (@upper false) or upper:
 * the device's own (its Enum key's LowerFilters/UpperFilters), then its
 * setup class's (Control\Class\{GUID}), as REG_MULTI_SZ in @out */
static void filters_of(const char *instance, bool upper, char *out, int cap)
{
    char key[200], guid[48];
    const char *name = upper ? "UpperFilters" : "LowerFilters";
    ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", instance);
    int at = multi_get(key, name, out, 0, cap);
    if (um_registry_get_sz(key, "ClassGUID", guid, sizeof(guid)) && guid[0]) {
        ksnprintf(key, sizeof(key), "Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\%s", guid);
        multi_get(key, name, out, at, cap);
    }
}

/* Load @service and call its AddDevice with @p's PDO (@function: the
 * function driver, which must attach a device of its own): 0, or the
 * problem code */
static UINT32 add_one(NtPdo *p, const char *service, bool function)
{
    NTSTATUS st;
    NtDriver *drv = NtDrvFind(service);
    if (!drv) {
        st = NtDrvLoad(service);
        drv = NtDrvFind(service);
        if (!drv) {
            kprintf("[PNP] %s: driver %s did not load (0x%08x)\n", p->instance, service, (UINT32)st);
            return st == ST_INVALID_IMAGE_HASH ? PROB_UNSIGNED_DRIVER : PROB_DRIVER_FAILED_LOAD;
        }
    }
    if (!drv->ext.AddDevice) {
        kprintf("[PNP] %s: driver %s has no AddDevice routine\n", p->instance, service);
        unload_if_idle(drv);
        return PROB_FAILED_ADD;
    }
    NT_DEVICE_OBJECT *before = nt_IoGetAttachedDevice(p->pdo);
    st = drv->ext.AddDevice(drv->obj, p->pdo);
    NT_DEVICE_OBJECT *top = nt_IoGetAttachedDevice(p->pdo);
    if (!NT_SUCCESS(st) || (function && top == before)) {
        kprintf("[PNP] %s: %s's AddDevice failed (0x%08x)\n", p->instance, service, (UINT32)st);
        unload_if_idle(drv);
        return PROB_FAILED_ADD;
    }
    if (!function) kprintf("[PNP] %s: filter driver %s added\n", p->instance, service);
    for (NT_DEVICE_OBJECT *d = p->pdo->AttachedDevice; d; d = d->AttachedDevice)
        if (d->Flags & NT_DO_DEVICE_INITIALIZING) {
            kprintf("[PNP] %s: %s's AddDevice left DO_DEVICE_INITIALIZING set\n", p->instance, service);
            d->Flags &= ~NT_DO_DEVICE_INITIALIZING;
        }
    return 0;
}

/* The filter drivers on @p's stack: each one unloaded once it has no
 * device left */
static void filters_gone(NtPdo *p)
{
    for (const char *f = p->filters; *f; f += strlen(f) + 1) unload_if_idle(NtDrvFind(f));
    p->filters[0] = 0;
}

/* Build @p's stack: lower filters, @service's (unless "": a raw PDO, its
 * own function driver), upper filters, each AddDevice with the PDO; then
 * start it with @p's resources: 0, or the problem code */
static UINT32 start_stack(NtPdo *p, const char *service)
{
    NtDriver *drv = NULL;
    NTSTATUS st;
    UINT32 prob = 0;
    char lower[128], upper[128];
    p->filters[0] = 0;
    filters_of(p->instance, false, lower, sizeof(lower));
    filters_of(p->instance, true, upper, sizeof(upper));
    int nf = 0;
    for (int pass = 0; pass < 3 && !prob; pass++) {
        if (pass == 1) {
            if (service[0]) prob = add_one(p, service, true);
            if (!prob && service[0]) drv = NtDrvFind(service);
            continue;
        }
        for (const char *f = pass ? upper : lower; *f && !prob; f += strlen(f) + 1) {
            prob = add_one(p, f, false);
            if (!prob && nf + (int)strlen(f) + 2 < (int)sizeof(p->filters)) {
                strcpy(p->filters + nf, f);
                nf += (int)strlen(f) + 1;
                p->filters[nf] = 0;
            }
        }
    }
    if (prob) {                                         /* (the drivers added so far let their devices go) */
        if (p->pdo->AttachedDevice) {
            send_pnp(p->pdo, NT_IRP_MN_REMOVE_DEVICE, NULL, NULL, NULL);
            if (p->pdo->AttachedDevice) nt_IoDetachDevice(p->pdo);
        }
        unload_if_idle(drv);
        filters_gone(p);
        return prob;
    }

    NT_DEVICE_CAPABILITIES caps;
    memset(&caps, 0, sizeof(caps));
    caps.Size = sizeof(caps);
    caps.Version = 1;
    caps.Address = caps.UINumber = 0xFFFFFFFFu;
    send_pnp(p->pdo, NT_IRP_MN_QUERY_CAPABILITIES, &caps, NULL, NULL);

    if (p->xlat && !p->parent) msi_prepare(p);
    kprintf("[PNP] %s: starting %s%s\n", p->instance, service[0] ? service : "it raw",
            p->xlat ? " with its resources" : "");
    if (p->xlat) log_resources(p);
    st = send_pnp(p->pdo, NT_IRP_MN_START_DEVICE, p->raw, p->xlat, NULL);
    if (!NT_SUCCESS(st)) {
        kprintf("[PNP] %s: IRP_MN_START_DEVICE failed (0x%08x)\n", p->instance, (UINT32)st);
        send_pnp(p->pdo, NT_IRP_MN_REMOVE_DEVICE, NULL, NULL, NULL);
        msi_release(p);
        if (p->pdo->AttachedDevice) nt_IoDetachDevice(p->pdo);
        unload_if_idle(drv);
        filters_gone(p);
        return PROB_FAILED_START;
    }
    p->started = true;
    return 0;
}

static void enumerate(NtPdo *bus);
static void child_start(NtPdo *c);

UINT32 NtPnpStart(UINT32 node)
{
    PnpNode *n = kmalloc(sizeof(PnpNode));
    if (!n) return PROB_FAILED_START;
    pnp_init();
    pnp_lock();
    UINT32 prob = 0;
    PciDevice pci;
    char key[200], service[64] = "";
    NtPdo *p = NULL;
    if (!PnpGet(node, n) || !(n->flags & PNP_PRESENT)) { prob = PROB_NO_DRIVER; goto out; }
    p = pdo_by_node(node);
    bool is_pci = !strncmp(n->instance, "PCI\\", 4), is_usb = !strncmp(n->instance, "USB\\", 4);
    bool is_root = !strncmp(n->instance, "ROOT\\", 5) && n->parent == PNP_ROOT;
    if (!p && !(is_pci && pci_of(node, &pci)) && !(is_usb && UsbFnByPnp(node, NULL)) && !is_root) {
        prob = PROB_NO_DRIVER;                               /* (no bus here offers it to Windows drivers) */
        goto out;
    }
    if (p && p->started) goto out;                           /* (already running) */
    if (p && p->parent) {                                    /* (a child its driver was installed on since) */
        child_start(p);
        if (!p->started) prob = PROB_FAILED_START;
        kfree(n);
        pnp_unlock();
        return prob;
    }
    bool inbox = (n->flags & PNP_STARTED) && !p;             /* (a NovaOS driver has it) */
    if (inbox && !(is_pci && (n->flags & PNP_INBOX))) { prob = PROB_FAILED_START; goto out; }
    ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", n->instance);
    if (!um_registry_get_sz(key, "Service", service, sizeof(service)) || !service[0]) { prob = PROB_NO_DRIVER; goto out; }

    char cls[32] = "", friendly[96] = "";                    /* (as its install wrote them: Ports, "... (COM3)") */
    um_registry_get_sz(key, "Class", cls, sizeof(cls));
    um_registry_get_sz(key, "FriendlyName", friendly, sizeof(friendly));
    PnpSetNames(node, cls, friendly);

    /* NovaOS's own driver steps aside for the installed one, as Windows
     * replaces an inbox driver with a vendor's (once that one has loaded) */
    if (inbox) {
        if (!NtDrvFind(service)) NtDrvLoad(service);
        if (!NtDrvFind(service)) {
            kprintf("[PNP] %s: driver %s did not load\n", n->instance, service);
            prob = PROB_DRIVER_FAILED_LOAD;
            goto out;
        }
        if (!PciSetAside(&pci)) {
            kprintf("[PNP] %s: its NovaOS driver did not step aside for %s\n", n->instance, service);
            prob = PROB_FAILED_START;
            goto out;
        }
        n->flags &= ~(UINT32)PNP_STARTED;
    }
    /* (made after that: a service that did not load leaves the inbox driver's device as it was) */
    if (!p) p = is_pci ? pdo_new(node, &pci, n->instance) : is_root ? root_new(node, n->instance) : usb_record(node, n->instance);
    if (!p) { prob = PROB_FAILED_START; goto out; }
    prob = start_stack(p, service);
    if (prob) goto out;
    strncpy(p->service, service, sizeof(p->service) - 1);
    if (!p->usb && !p->root) PciClaim(&p->pci, p->service);
    PnpSetWindowsDriver(node, p->service);              /* (after PciClaim's PnpSetDriver) */
    kprintf("[PNP] %s: started (driver %s)\n", n->instance, service);
    enumerate(p);                                        /* (its children, if it is a bus) */
out:
    if (prob && prob != PROB_NO_DRIVER && !(n->flags & PNP_STARTED))   /* (not while a NovaOS driver runs it) */
        PnpSetProblem(node, prob);
    pnp_unlock();
    kfree(n);
    return prob;
}

static void ifaces_off(NtPdo *p);
static void child_remove(NtPdo *c, bool missing);

static NtPdo *child_of(NtPdo *bus)
{
    for (int i = 0; i < g_npdo; i++) if (g_pdo[i]->parent == bus) return g_pdo[i];
    return NULL;
}

/* IRP_MN_QUERY_REMOVE_DEVICE to @p and everything under it, children
 * first; on a veto, IRP_MN_CANCEL_REMOVE_DEVICE to those that agreed */
static NT_STATUS query_remove(NtPdo *p)
{
    NtPdo *asked[MAX_PDOS];
    int n = 0;
    NT_STATUS st = NT_STATUS_SUCCESS;
    for (int i = 0; i < g_npdo && NT_SUCCESS(st); i++) {
        NtPdo *c = g_pdo[i];
        if (c->parent != p || !c->started) continue;
        st = send_pnp(c->pdo, NT_IRP_MN_QUERY_REMOVE_DEVICE, NULL, NULL, NULL);
        if (NT_SUCCESS(st)) asked[n++] = c;
        else kprintf("[PNP] %s: removal vetoed by its driver (0x%08x)\n", c->instance, (UINT32)st);
    }
    if (NT_SUCCESS(st)) {
        st = send_pnp(p->pdo, NT_IRP_MN_QUERY_REMOVE_DEVICE, NULL, NULL, NULL);
        if (!NT_SUCCESS(st)) {
            kprintf("[PNP] %s: removal vetoed by its driver (0x%08x)\n", p->instance, (UINT32)st);
            send_pnp(p->pdo, NT_IRP_MN_CANCEL_REMOVE_DEVICE, NULL, NULL, NULL);
        }
    }
    if (!NT_SUCCESS(st))
        for (int i = 0; i < n; i++) send_pnp(asked[i]->pdo, NT_IRP_MN_CANCEL_REMOVE_DEVICE, NULL, NULL, NULL);
    return st;
}

NTSTATUS NtPnpStop(UINT32 node)
{
    pnp_init();
    pnp_lock();
    NtPdo *p = pdo_by_node(node);
    if (!p || !p->started || p->parent) { pnp_unlock(); return ST_INVALID_DEVICE_STATE; }
    NT_STATUS st = query_remove(p);
    if (!NT_SUCCESS(st)) { pnp_unlock(); return st; }
    for (NtPdo *c; (c = child_of(p));) child_remove(c, false);
    NtDriver *drv = NtDrvFind(p->service);
    send_pnp(p->pdo, NT_IRP_MN_REMOVE_DEVICE, NULL, NULL, NULL);
    if (p->pdo->AttachedDevice) {
        kprintf("[PNP] %s: %s left its device attached after IRP_MN_REMOVE_DEVICE\n", p->instance, p->service);
        nt_IoDetachDevice(p->pdo);
    }
    ifaces_off(p);
    msi_release(p);
    p->started = false;
    if (!p->usb && !p->root) PciClaim(&p->pci, NULL);
    PnpSetProblem(node, PROB_DISABLED);
    kprintf("[PNP] %s: removed (driver %s)\n", p->instance, p->service);
    p->service[0] = 0;
    unload_if_idle(drv);
    filters_gone(p);
    pnp_unlock();
    return STATUS_SUCCESS;
}

/* ---- USB arrivals and removals (on a work item, after NtPnpBoot) ---- */

NT_API NT_IO_WORKITEM *nt_IoAllocateWorkItem(NT_DEVICE_OBJECT *dev);
NT_API void nt_IoFreeWorkItem(NT_IO_WORKITEM *w);
NT_API void nt_IoQueueWorkItem(NT_IO_WORKITEM *w, NT_IO_WORKITEM_ROUTINE routine, int queue_type, void *ctx);

enum { WORK_USB_ARRIVED, WORK_SURPRISE };
typedef struct { NT_IO_WORKITEM *w; int what; UINT32 node; } PnpWork;

/* USB device @p is gone: its children, then its own stack get
 * IRP_MN_SURPRISE_REMOVAL and IRP_MN_REMOVE_DEVICE; its record goes (it
 * comes back with a new PDO) */
static void usb_remove(NtPdo *p)
{
    kprintf("[PNP] %s: the device is gone (surprise removal)\n", p->instance);
    for (NtPdo *c; (c = child_of(p));) child_remove(c, true);
    NtDriver *drv = p->started ? NtDrvFind(p->service) : NULL;
    if (p->started) {
        send_pnp(p->pdo, NT_IRP_MN_SURPRISE_REMOVAL, NULL, NULL, NULL);
        ifaces_off(p);
        send_pnp(p->pdo, NT_IRP_MN_REMOVE_DEVICE, NULL, NULL, NULL);
        if (p->pdo->AttachedDevice) nt_IoDetachDevice(p->pdo);
        kprintf("[PNP] %s: removed (driver %s)\n", p->instance, p->service);
    }
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < g_npdo; i++)
        if (g_pdo[i] == p) {
            memmove(&g_pdo[i], &g_pdo[i + 1], (size_t)(g_npdo - i - 1) * sizeof(g_pdo[0]));
            g_npdo--;
            break;
        }
    spin_unlock_irqrestore(&g_lock, s);
    for (NtIface *f = p->ifaces, *next; f; f = next) { next = f->next; kfree(f); }
    unload_if_idle(drv);
    filters_gone(p);
    PnpRemove(p->node);          /* (the usb thread left the node for now: see remove_dev) */
    kfree(p);
}

/* A USB HID device no NovaOS driver took and nothing was installed on
 * (a vendor-defined one): NovaOS's own HID minidriver (hidusb.c) as its
 * driver, written in its Enum key @key as Windows' input.inf installs
 * hidusb.sys on USB\Class_03 */
static bool inbox_hid(const PnpNode *n, const char *key)
{
    bool hid = false;
    for (const char *c = n->compat; *c && !hid; c += strlen(c) + 1) hid = !strcmp(c, "USB\\Class_03");
    if (!hid) return false;
    um_registry_set_sz(key, "Service", "HidUsb");
    um_registry_set_sz(key, "Class", "HIDClass");
    um_registry_set_sz(key, "ClassGUID", "{745a17a0-74d3-11d0-b6fe-00a0c90f57da}");
    return true;
}

/* A HID collection nothing was installed on: the class driver Windows'
 * keyboard.inf or msmouse.inf would install, NovaOS's own (kbdhid.c):
 * kbdhid on a keyboard, and on the consumer and system control
 * collections (media and power keys, which Windows reads in win32k),
 * mouhid on a mouse or another pointer, hiddigi on a touch screen, a pen,
 * a precision touchpad's finger collection and a digitizer's
 * configuration collection (Windows' input.inf leaves those raw for
 * win32k to read; they stay in the HIDClass class, as there).  Its
 * Service, Class and ClassGUID go in its Enum key @key, its class in @n. */
static bool inbox_input(PnpNode *n, const char *key)
{
    const char *service = NULL, *cls = "HIDClass", *guid = "{745a17a0-74d3-11d0-b6fe-00a0c90f57da}";
    for (const char *c = n->hwids; *c && !service; c += strlen(c) + 1) {   /* (hidclass lists them as hardware IDs) */
        if (!strcmp(c, "HID_DEVICE_SYSTEM_KEYBOARD")) {
            service = "kbdhid", cls = "Keyboard", guid = "{4d36e96b-e325-11ce-bfc1-08002be10318}";
        } else if (!strcmp(c, "HID_DEVICE_SYSTEM_MOUSE") || !strcmp(c, "HID_DEVICE_UP:0001_U:0001")) {
            service = "mouhid", cls = "Mouse", guid = "{4d36e96f-e325-11ce-bfc1-08002be10318}";
        } else if (!strcmp(c, "HID_DEVICE_UP:000C_U:0001") || !strcmp(c, "HID_DEVICE_UP:0001_U:0080")) {
            service = "kbdhid";
        } else if (!strcmp(c, "HID_DEVICE_UP:000D_U:0002") || !strcmp(c, "HID_DEVICE_UP:000D_U:0004") ||
                   !strcmp(c, "HID_DEVICE_UP:000D_U:0005") || !strcmp(c, "HID_DEVICE_UP:000D_U:000E")) {
            service = "hiddigi";
        }
    }
    if (!service) return false;
    um_registry_set_sz(key, "Service", service);
    um_registry_set_sz(key, "Class", cls);
    um_registry_set_sz(key, "ClassGUID", guid);
    strcpy(n->cls, cls);
    return true;
}

/* The class drivers above: NovaOS's own drivers, which Device Manager and
 * pnputil show as such (an installed package does not replace them) */
static bool inbox_class_driver(const char *service)
{
    return !strcmp(service, "kbdhid") || !strcmp(service, "mouhid") || !strcmp(service, "hiddigi");
}

/* A service NovaOS no longer runs these devices with: usbhid.c ran USB
 * keyboards and mice before kbdhid/mouhid did, and an Enum key written
 * then still names it */
static bool stale_service(const char *service)
{
    return !strcmp(service, "usbhid");
}

/* A USB camera (video interfaces) no NovaOS driver took and nothing was
 * installed on: NovaOS's own UVC driver (usbvideo.c), as Windows'
 * usbvideo.inf installs usbvideo.sys on USB\Class_0E */
static bool inbox_video(const PnpNode *n, const char *key)
{
    bool video = false;
    for (const char *c = n->compat; *c && !video; c += strlen(c) + 1) video = !strcmp(c, "USB\\Class_0E");
    if (!video) return false;
    um_registry_set_sz(key, "Service", "usbvideo");
    um_registry_set_sz(key, "Class", "Camera");
    um_registry_set_sz(key, "ClassGUID", "{ca3e7ab9-b4c3-4ae6-8251-579ef933890f}");
    return true;
}

static NT_API void work_routine(NT_DEVICE_OBJECT *dev, void *ctx)
{
    (void)dev;
    PnpWork *k = ctx;
    if (k->what == WORK_USB_ARRIVED) {
        char key[200], service[64];
        PnpNode *n = kmalloc(sizeof(PnpNode));
        if (n && PnpGet(k->node, n) && (n->flags & PNP_PRESENT) && !(n->flags & PNP_STARTED)) {
            ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", n->instance);
            UINT32 disabled = 0;
            if (um_registry_get_dword(key, "ConfigFlags", &disabled) && (disabled & 1)) PnpSetProblem(k->node, PROB_DISABLED);
            else if ((um_registry_get_sz(key, "Service", service, sizeof(service)) && service[0] && !stale_service(service)) ||
                     inbox_hid(n, key) || inbox_video(n, key))
                NtPnpStart(k->node);
        }
        kfree(n);
    } else {
        bool found = false;
        pnp_lock();
        for (int i = 0; i < g_npdo; i++)
            if (g_pdo[i]->usb && g_pdo[i]->node == k->node) { usb_remove(g_pdo[i]); found = true; break; }
        pnp_unlock();
        if (!found) PnpRemove(k->node);
    }
    nt_IoFreeWorkItem(k->w);
    kfree(k);
}

static bool defer(int what, UINT32 node)
{
    PnpWork *k = kzalloc(sizeof(*k));
    if (!k) return false;
    k->w = nt_IoAllocateWorkItem(NULL);
    if (!k->w) { kfree(k); return false; }
    k->what = what;
    k->node = node;
    nt_IoQueueWorkItem(k->w, work_routine, 1 /* DelayedWorkQueue */, k);
    return true;
}

/* Set once NtPnpBoot ran: the registry is loaded and the work-item thread
 * is up.  A USB device enumerated before that is started by NtPnpBoot. */
static volatile bool g_booted;

void NtPnpUsbArrived(UINT32 node) { if (g_booted) defer(WORK_USB_ARRIVED, node); }
bool NtPnpSurpriseRemoval(UINT32 node) { return g_booted && defer(WORK_SURPRISE, node); }

/* ---- root-enumerated devices ---- */

UINT32 NtPnpRootAdd(const char *instance, const char *cls, const char *hwids, const char *compat, const char *desc)
{
    if (strncmp(instance, "ROOT\\", 5) || !strchr(instance + 5, '\\') || !hwids[0]) return 0;
    PnpNode *n = kzalloc(sizeof(PnpNode));
    if (!n) return 0;
    strncpy(n->instance, instance, sizeof(n->instance) - 1);
    memcpy(n->hwids, hwids, (size_t)(multi_len(hwids) < sizeof(n->hwids) - 1 ? multi_len(hwids) : sizeof(n->hwids) - 2));
    if (compat) memcpy(n->compat, compat, (size_t)(multi_len(compat) < sizeof(n->compat) - 1 ? multi_len(compat) : sizeof(n->compat) - 2));
    strncpy(n->desc, desc && desc[0] ? desc : instance, sizeof(n->desc) - 1);
    if (cls) strncpy(n->cls, cls, sizeof(n->cls) - 1);
    n->flags = PNP_PRESENT;
    UINT32 id = PnpAdd(PNP_ROOT, n);
    kfree(n);
    return id;
}

NTSTATUS NtPnpRootRemove(UINT32 node)
{
    PnpNode n;
    if (!PnpGet(node, &n) || strncmp(n.instance, "ROOT\\", 5) || n.parent != PNP_ROOT) return ST_INVALID_DEVICE_STATE;
    pnp_init();
    pnp_lock();
    NtPdo *p = pdo_by_node(node);
    if (p && p->started) {
        NT_STATUS st = query_remove(p);
        if (!NT_SUCCESS(st)) { pnp_unlock(); return st; }
        for (NtPdo *c; (c = child_of(p));) child_remove(c, false);
        NtDriver *drv = NtDrvFind(p->service);
        send_pnp(p->pdo, NT_IRP_MN_REMOVE_DEVICE, NULL, NULL, NULL);
        if (p->pdo->AttachedDevice) nt_IoDetachDevice(p->pdo);
        ifaces_off(p);
        p->started = false;
        kprintf("[PNP] %s: removed (driver %s)\n", p->instance, p->service);
        unload_if_idle(drv);
        filters_gone(p);
    }
    if (p) {
        IrqState s = spin_lock_irqsave(&g_lock);
        for (int i = 0; i < g_npdo; i++)
            if (g_pdo[i] == p) {
                memmove(&g_pdo[i], &g_pdo[i + 1], (size_t)(g_npdo - i - 1) * sizeof(g_pdo[0]));
                g_npdo--;
                break;
            }
        spin_unlock_irqrestore(&g_lock, s);
        for (NtIface *f = p->ifaces, *next; f; f = next) { next = f->next; kfree(f); }
        nt_IoDeleteDevice(p->pdo);                           /* (p is its extension: last) */
    }
    PnpDelete(node);
    kprintf("[PNP] %s: device removed\n", n.instance);
    pnp_unlock();
    return STATUS_SUCCESS;
}

/* The root-enumerated devices registered before (Enum\ROOT\<class>\<n>
 * with a HardwareID) come back */
static void root_boot(void)
{
    char key[200], cls[64], inst[64], instance[128], desc[96], setup[32];
    char *hw = kmalloc(160), *compat = kmalloc(160);
    NT_WCHAR w[64];
    for (UINT32 i = 0; hw && compat; i++) {
        int len = um_registry_enum_key(ENUM_KEY "\\ROOT", i, w, 63, NULL);
        if (len < 0) break;
        for (int k = 0; k <= len && k < 63; k++) cls[k] = k < len ? (char)w[k] : 0;
        for (UINT32 j = 0;; j++) {
            ksnprintf(key, sizeof(key), ENUM_KEY "\\ROOT\\%s", cls);
            len = um_registry_enum_key(key, j, w, 63, NULL);
            if (len < 0) break;
            for (int k = 0; k <= len && k < 63; k++) inst[k] = k < len ? (char)w[k] : 0;
            ksnprintf(instance, sizeof(instance), "ROOT\\%s\\%s", cls, inst);
            ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", instance);
            multi_get(key, "HardwareID", hw, 0, 160);
            multi_get(key, "CompatibleIDs", compat, 0, 160);
            if (!um_registry_get_sz(key, "DeviceDesc", desc, sizeof(desc))) desc[0] = 0;
            if (!um_registry_get_sz(key, "Class", setup, sizeof(setup))) setup[0] = 0;
            if (hw[0]) NtPnpRootAdd(instance, setup, hw, compat, desc);
        }
    }
    kfree(hw);
    kfree(compat);
}

void NtPnpBoot(void)
{
    pnp_init();
    root_boot();
    UINT32 count = PnpCount();
    PnpNode *n = kmalloc(sizeof(PnpNode));
    if (!n) return;
    char key[200], service[64];
    g_booted = true;
    for (UINT32 id = 1; id <= count; id++) {
        if (!PnpGet(id, n) || !(n->flags & PNP_PRESENT) || ((n->flags & PNP_STARTED) && !(n->flags & PNP_INBOX))) continue;
        if (!strncmp(n->instance, "USB\\", 4)) {             /* (on the work-item thread, with later arrivals) */
            if (UsbFnByPnp(id, NULL)) defer(WORK_USB_ARRIVED, id);
            continue;
        }
        if (strncmp(n->instance, "PCI\\", 4) && (strncmp(n->instance, "ROOT\\", 5) || n->parent != PNP_ROOT)) continue;
        ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", n->instance);
        UINT32 disabled = 0;
        if (um_registry_get_dword(key, "ConfigFlags", &disabled) && (disabled & 1)) {   /* CONFIGFLAG_DISABLED */
            PnpSetProblem(id, PROB_DISABLED);
            continue;
        }
        /* (an inbox driver's own name, as plugplay recorded it, is not an installed service) */
        if (um_registry_get_sz(key, "Service", service, sizeof(service)) && service[0] &&
            !((n->flags & PNP_STARTED) && !strcmp(service, n->service)))
            NtPnpStart(id);
    }
    kfree(n);
}

/* ---- a bus driver's children ---- */

/* A wide string (a REG_MULTI_SZ list when @multi) as ASCII in @out */
static void from_wide(const NT_WCHAR *w, char *out, size_t cap, bool multi)
{
    size_t k = 0;
    for (size_t i = 0; w && k + 2 < cap; i++) {
        out[k++] = w[i] && w[i] < 0x80 ? (char)w[i] : w[i] ? '?' : 0;
        if (!w[i] && (!multi || !w[i + 1])) { k--; break; }
    }
    out[k] = 0;
    out[k + 1] = 0;
}

/* IRP_MN_QUERY_ID or QUERY_DEVICE_TEXT of type @type: the driver's
 * string (pool, freed with ExFreePool), or NULL */
static NT_WCHAR *query_string(NT_DEVICE_OBJECT *pdo, UINT8 minor, int type)
{
    NT_ULONG_PTR info = 0;
    NT_STATUS st = send_pnp(pdo, minor, (void *)(UINT64)type, (void *)(UINT64)0x409 /* (locale: en-US) */, &info);
    return NT_SUCCESS(st) ? (NT_WCHAR *)info : NULL;
}

static void query_into(NT_DEVICE_OBJECT *pdo, UINT8 minor, int type, char *out, size_t cap, bool multi)
{
    NT_WCHAR *w = query_string(pdo, minor, type);
    from_wide(w, out, cap, multi);
    if (w) nt_ExFreePool(w);
}

/* A record and a device node for @pdo, a child @bus reported */
static NtPdo *child_new(NtPdo *bus, NT_DEVICE_OBJECT *pdo)
{
    if (g_npdo >= MAX_PDOS) return NULL;
    NtPdo *c = kzalloc(sizeof(*c));
    PnpNode *n = kzalloc(sizeof(*n));
    char dev[128], inst[64];
    if (!c || !n) goto fail;
    c->pdo = pdo;
    c->parent = bus;
    pdo->Flags = (pdo->Flags & ~NT_DO_DEVICE_INITIALIZING) | NT_DO_BUS_ENUMERATED_DEVICE;
    NtIoNameDevice(pdo);                                     /* (a bus driver may leave it unnamed) */
    if (!NtIoDeviceName(pdo, c->devname, sizeof(c->devname)))
        kprintf("[PNP] %s: a child device object with no name\n", bus->instance);
    query_into(pdo, NT_IRP_MN_QUERY_ID, 0, dev, sizeof(dev) - 1, false);        /* BusQueryDeviceID */
    query_into(pdo, NT_IRP_MN_QUERY_ID, 1, n->hwids, sizeof(n->hwids), true);  /* BusQueryHardwareIDs */
    query_into(pdo, NT_IRP_MN_QUERY_ID, 2, n->compat, sizeof(n->compat), true);
    query_into(pdo, NT_IRP_MN_QUERY_ID, 3, inst, sizeof(inst) - 1, false);     /* BusQueryInstanceID */
    query_into(pdo, NT_IRP_MN_QUERY_DEVICE_TEXT, 0, n->desc, sizeof(n->desc) - 1, false);
    query_into(pdo, NT_IRP_MN_QUERY_DEVICE_TEXT, 1, n->location, sizeof(n->location) - 1, false);
    if (!dev[0]) {
        kprintf("[PNP] %s: a child with no device ID\n", bus->instance);
        goto fail;
    }
    NT_DEVICE_CAPABILITIES caps;
    memset(&caps, 0, sizeof(caps));
    caps.Size = sizeof(caps);
    caps.Version = 1;
    caps.Address = caps.UINumber = 0xFFFFFFFFu;
    send_pnp(pdo, NT_IRP_MN_QUERY_CAPABILITIES, &caps, NULL, NULL);
    c->caps = caps.Flags;
    c->address = caps.Address;
    /* An instance ID unique only on its bus gets the parent's node in
     * front, as Windows puts a prefix of the parent's there */
    if (caps.Flags & CAP_UNIQUE_ID) ksnprintf(c->instance, sizeof(c->instance), "%s\\%s", dev, inst);
    else ksnprintf(c->instance, sizeof(c->instance), "%s\\%u&%s", dev, (unsigned)bus->node, inst);
    strcpy(n->instance, c->instance);
    if (!n->hwids[0]) strncpy(n->hwids, dev, sizeof(n->hwids) - 2);
    if (!n->desc[0]) strncpy(n->desc, dev, sizeof(n->desc) - 1);
    if (!strncmp(dev, "HID\\", 4)) {                       /* (a HID collection, as input.inf files it) */
        char key[200], service[64] = "";
        strcpy(n->cls, "HIDClass");
        ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", c->instance);
        if (!um_registry_get_sz(key, "Service", service, sizeof(service)) || !service[0] || stale_service(service))
            inbox_input(n, key);                             /* (a keyboard or a mouse: kbdhid, mouhid) */
    }
    n->flags = PNP_PRESENT;
    c->node = PnpAdd(bus->node, n);
    if (!c->node) goto fail;
    g_pdo[g_npdo++] = c;
    kprintf("[PNP] %s: child %s (%s)\n", bus->instance, c->instance, c->devname);
    kfree(n);
    return c;
fail:
    kfree(c);
    kfree(n);
    return NULL;
}

/* Start child @c: with the function driver its Enum key names, else raw
 * when its bus allows that */
static void child_start(NtPdo *c)
{
    char key[200], service[64] = "";
    ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", c->instance);
    UINT32 flags = 0;
    if (um_registry_get_dword(key, "ConfigFlags", &flags) && (flags & 1)) { PnpSetProblem(c->node, PROB_DISABLED); return; }
    um_registry_get_sz(key, "Service", service, sizeof(service));
    if (stale_service(service)) service[0] = 0;
    char cls[32] = "", friendly[96] = "";
    um_registry_get_sz(key, "Class", cls, sizeof(cls));
    um_registry_get_sz(key, "FriendlyName", friendly, sizeof(friendly));
    PnpSetNames(c->node, cls, friendly);
    if (!service[0] && !(c->caps & CAP_RAW_DEVICE_OK)) { PnpSetProblem(c->node, PROB_NO_DRIVER); return; }
    /* Resource assignment, as Windows asks it before the start: what the
     * child needs and what its bus gave it.  A child of a software bus
     * asks for nothing; KMDF also takes the first question as the sign
     * that PnP knows the PDO, and only then registers its interfaces */
    NT_ULONG_PTR info = 0;
    if (NT_SUCCESS(send_pnp(c->pdo, NT_IRP_MN_QUERY_RESOURCE_REQUIREMENTS, NULL, NULL, &info)) && info)
        nt_ExFreePool((void *)info);
    info = 0;
    if (NT_SUCCESS(send_pnp(c->pdo, NT_IRP_MN_QUERY_RESOURCES, NULL, NULL, &info)) && info)
        nt_ExFreePool((void *)info);
    UINT32 prob = start_stack(c, service);
    if (prob) { PnpSetProblem(c->node, prob); return; }
    c->rawpdo = !service[0];
    strncpy(c->service, c->rawpdo ? c->parent->service : service, sizeof(c->service) - 1);
    if (c->rawpdo) PnpSetRawDriver(c->node, c->service);
    else if (inbox_class_driver(c->service)) PnpSetDriver(c->node, c->service);
    else PnpSetWindowsDriver(c->node, c->service);
    kprintf("[PNP] %s: started (%s %s)\n", c->instance, c->rawpdo ? "raw, bus driver" : "driver", c->service);
    enumerate(c);
}

/* Take child @c (and its own children) away: IRP_MN_SURPRISE_REMOVAL
 * first when its bus no longer reports it (@missing) and it ran, then
 * IRP_MN_REMOVE_DEVICE; its node stays as a phantom */
static void child_remove(NtPdo *c, bool missing)
{
    for (NtPdo *g; (g = child_of(c));) child_remove(g, missing);
    NtDriver *drv = c->started && !c->rawpdo ? NtDrvFind(c->service) : NULL;
    if (missing && c->started) send_pnp(c->pdo, NT_IRP_MN_SURPRISE_REMOVAL, NULL, NULL, NULL);
    ifaces_off(c);
    send_pnp(c->pdo, NT_IRP_MN_REMOVE_DEVICE, NULL, NULL, NULL);
    if (drv && c->pdo->AttachedDevice) nt_IoDetachDevice(c->pdo);
    PnpRemove(c->node);
    kprintf("[PNP] %s: removed\n", c->instance);
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < g_npdo; i++)
        if (g_pdo[i] == c) {
            memmove(&g_pdo[i], &g_pdo[i + 1], (size_t)(g_npdo - i - 1) * sizeof(g_pdo[0]));
            g_npdo--;
            break;
        }
    spin_unlock_irqrestore(&g_lock, s);
    for (NtIface *f = c->ifaces, *next; f; f = next) { next = f->next; kfree(f); }
    unload_if_idle(drv);
    filters_gone(c);
    kfree(c);
}

/* IRP_MN_QUERY_DEVICE_RELATIONS BusRelations to @bus's stack: new
 * children get nodes and start, missing ones are removed (pnp_lock held) */
static void enumerate(NtPdo *bus)
{
    if (!bus || !bus->started) return;
    NT_ULONG_PTR info = 0;
    NT_STATUS st = send_pnp(bus->pdo, NT_IRP_MN_QUERY_DEVICE_RELATIONS, (void *)0 /* BusRelations */, NULL, &info);
    if (st == NT_STATUS_NOT_SUPPORTED) return;               /* (not a bus: its children stay as they are) */
    NtRelations *r = NT_SUCCESS(st) ? (NtRelations *)info : NULL;
    if (!NT_SUCCESS(st)) kprintf("[PNP] %s: BusRelations failed (0x%08x)\n", bus->instance, (UINT32)st);
    for (int i = 0; i < g_npdo; i++) if (g_pdo[i]->parent == bus) g_pdo[i]->seen = false;
    NtPdo *fresh[16];
    int nfresh = 0;
    for (unsigned k = 0; r && k < r->Count; k++) {
        NtPdo *c = pdo_find(r->Objects[k]);
        if (c) { c->seen = true; continue; }
        c = child_new(bus, r->Objects[k]);
        if (c) {
            c->seen = true;
            if (nfresh < 16) fresh[nfresh++] = c;
        }
    }
    if (r) nt_ExFreePool(r);
    for (;;) {
        NtPdo *gone = NULL;
        for (int i = 0; i < g_npdo && !gone; i++) if (g_pdo[i]->parent == bus && !g_pdo[i]->seen) gone = g_pdo[i];
        if (!gone) break;
        child_remove(gone, true);
    }
    for (int i = 0; i < nfresh; i++) child_start(fresh[i]);
}

/* The PnP thread: enumerates the buses IoInvalidateDeviceRelations names */
static void enum_thread(void *arg)
{
    (void)arg;
    for (;;) {
        nt_KeWaitForSingleObject(&g_enum_event, 0, 0, 0, NULL);
        for (;;) {
            NT_DEVICE_OBJECT *d = NULL;
            IrqState s = spin_lock_irqsave(&g_lock);
            for (int i = 0; i < 8 && !d; i++) if (g_enum_pending[i]) { d = g_enum_pending[i]; g_enum_pending[i] = NULL; }
            spin_unlock_irqrestore(&g_lock, s);
            if (!d) break;
            pnp_lock();
            enumerate(pdo_find(d));
            pnp_unlock();
        }
    }
}

static void pnp_init(void)
{
    if (g_ready) return;
    nt_KeInitializeMutex(&g_mutex, 0);
    nt_KeInitializeEvent(&g_enum_event, NT_SynchronizationEvent, 0);
    g_ready = true;
    if (!sched_create_thread_ex("nt-pnp", enum_thread, NULL, 9, 64 * 1024))
        kprintf("[PNP] no thread for enumerating buses\n");
}

/* ---- device interfaces ---- */

int NtPnpInterfaces(UINT32 node, NT_GUID *out, int max)
{
    NtPdo *p = pdo_by_node(node);
    int k = 0;
    IrqState s = spin_lock_irqsave(&g_lock);
    for (NtIface *i = p ? p->ifaces : NULL; i && k < max; i = i->next)
        if (i->on) out[k++] = i->guid;
    spin_unlock_irqrestore(&g_lock, s);
    return k;
}

static NtIface *iface_by_link(const char *link, NtPdo **owner)
{
    for (int i = 0; i < g_npdo; i++)
        for (NtIface *f = g_pdo[i]->ifaces; f; f = f->next)
            if (!strcmp(f->link, link)) { *owner = g_pdo[i]; return f; }
    return NULL;
}

/* A UNICODE_STRING's ASCII, without the \??\ in front and a reference
 * string after the link's name */
static bool link_from(const NT_UNICODE_STRING *u, char *out, int cap)
{
    if (!u || !u->Buffer || u->Length / 2 >= (unsigned)cap) return false;
    int n = u->Length / 2, at = 0;
    for (int i = 0; i < n; i++) out[i] = u->Buffer[i] < 0x80 ? (char)u->Buffer[i] : '?';
    out[n] = 0;
    if (!strncmp(out, "\\??\\", 4) || !strncmp(out, "\\\\?\\", 4)) at = 4;
    memmove(out, out + at, (size_t)(n - at + 1));
    char *slash = strchr(out, '\\');
    if (slash) *slash = 0;
    return out[0] != 0;
}

static NT_STATUS link_set(NtPdo *p, NtIface *f, bool on)
{
    char a[220], b[48];
    ksnprintf(a, sizeof(a), "\\??\\%s", f->link);
    strcpy(b, p->devname);
    NT_WCHAR wa[220], wb[48];
    size_t na = strlen(a), nb = strlen(b);
    for (size_t i = 0; i <= na; i++) wa[i] = (UINT8)a[i];
    for (size_t i = 0; i <= nb; i++) wb[i] = (UINT8)b[i];
    NT_UNICODE_STRING ua = { (unsigned short)(2 * na), (unsigned short)(2 * na + 2), wa };
    NT_UNICODE_STRING ub = { (unsigned short)(2 * nb), (unsigned short)(2 * nb + 2), wb };
    return on ? nt_IoCreateSymbolicLink(&ua, &ub) : nt_IoDeleteSymbolicLink(&ua);
}

static void ifaces_off(NtPdo *p)
{
    for (NtIface *f = p->ifaces; f; f = f->next)
        if (f->on) {
            link_set(p, f, false);
            f->on = false;
        }
}

NT_API NT_STATUS nt_IoRegisterDeviceInterface(NT_DEVICE_OBJECT *pdo, const NT_GUID *guid, NT_UNICODE_STRING *ref,
                                              NT_UNICODE_STRING *out)
{
    NtPdo *p = pdo_of(pdo);
    if (!p || !guid || !out) return NT_STATUS_INVALID_DEVICE_REQUEST;
    char g[40], link[200];
    ascii_guid(guid, g);
    int k = 0;
    for (const char *c = p->instance; *c && k < 150; c++) link[k++] = *c == '\\' ? '#' : *c;
    link[k] = 0;
    ksnprintf(link + k, sizeof(link) - (size_t)k, "#%s", g);
    NtPdo *owner;
    if (!iface_by_link(link, &owner)) {
        NtIface *f = kzalloc(sizeof(*f));
        if (!f) return NT_STATUS_INSUFFICIENT_RESOURCES;
        f->guid = *guid;
        strcpy(f->link, link);
        IrqState s = spin_lock_irqsave(&g_lock);
        f->next = p->ifaces;
        p->ifaces = f;
        spin_unlock_irqrestore(&g_lock, s);
    }
    char full[260];
    int rl = ref && ref->Buffer ? ref->Length / 2 : 0;
    ksnprintf(full, sizeof(full), "\\??\\%s%s", link, rl ? "\\" : "");
    size_t n = strlen(full);
    NT_WCHAR *w = nt_ExAllocatePoolWithTag(1, 2 * (n + (size_t)rl + 1), 0x20706E50);
    if (!w) return NT_STATUS_INSUFFICIENT_RESOURCES;
    for (size_t i = 0; i < n; i++) w[i] = (UINT8)full[i];
    for (int i = 0; i < rl; i++) w[n + (size_t)i] = ref->Buffer[i];
    w[n + (size_t)rl] = 0;
    out->Buffer = w;
    out->Length = (unsigned short)(2 * (n + (size_t)rl));
    out->MaximumLength = (unsigned short)(out->Length + 2);
    return NT_STATUS_SUCCESS;
}

NT_API NT_STATUS nt_IoSetDeviceInterfaceState(NT_UNICODE_STRING *name, UINT8 enable)
{
    char link[220];
    NtPdo *p;
    NtIface *f = link_from(name, link, sizeof(link)) ? iface_by_link(link, &p) : NULL;
    if (!f) return NT_STATUS_OBJECT_NAME_NOT_FOUND;
    if (!!enable == f->on) return NT_STATUS_SUCCESS;
    NT_STATUS st = link_set(p, f, enable);
    if (NT_SUCCESS(st)) {
        f->on = enable;
        kprintf("[PNP] %s: interface %s %s\n", p->instance, strrchr(link, '#') + 1, enable ? "on" : "off");
    }
    return st;
}

/* ---- device properties ---- */

NT_API NT_STATUS nt_IoGetDeviceProperty(NT_DEVICE_OBJECT *pdo, int prop, UINT32 len, void *buf, UINT32 *result)
{
    NtPdo *p = pdo_of(pdo);
    PnpNode *n = p ? kmalloc(sizeof(PnpNode)) : NULL;
    if (!n || !PnpGet(p->node, n)) { kfree(n); return NT_STATUS_INVALID_DEVICE_REQUEST; }
    char key[200], tmp[160];
    ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", n->instance);
    const char *s = NULL;
    size_t slen = 0;
    UINT32 num = 0;
    bool is_num = false;
    switch (prop) {
    case 0: s = n->desc; break;                                  /* DevicePropertyDeviceDescription */
    case 1: s = n->hwids; slen = multi_len(n->hwids); break;     /* HardwareID */
    case 2: s = n->compat; slen = multi_len(n->compat); break;   /* CompatibleIDs */
    case 5: s = n->cls; break;                                   /* ClassName */
    case 6: if (um_registry_get_sz(key, "ClassGUID", tmp, sizeof(tmp))) s = tmp; break;
    case 7: if (um_registry_get_sz(key, "Driver", tmp, sizeof(tmp))) s = tmp; break;
    case 8: s = n->mfg; break;                                   /* Manufacturer */
    case 9: if (um_registry_get_sz(key, "FriendlyName", tmp, sizeof(tmp))) s = tmp; break;
    case 10: s = n->location; break;                             /* LocationInformation */
    case 11: s = p->devname; break;                              /* PhysicalDeviceObjectName */
    case 13: if (!p->parent && !p->usb) { num = NT_PCIBus; is_num = true; } break;     /* LegacyBusType */
    case 14:                                                     /* BusNumber */
        if (p->usb) is_num = NtUsbPdoProperty(pdo, 14, &num);
        else if (!p->parent) { num = p->pci.bus; is_num = true; }
        break;
    case 15: strncpy(tmp, n->instance, sizeof(tmp) - 1);        /* EnumeratorName */
             tmp[sizeof(tmp) - 1] = 0;
             if (strchr(tmp, '\\')) *strchr(tmp, '\\') = 0;
             s = tmp; break;
    case 16:                                                     /* Address */
        if (p->usb) is_num = NtUsbPdoProperty(pdo, 16, &num);
        else if (!p->parent) { num = (UINT32)p->pci.dev << 16 | p->pci.func; is_num = true; }
        else if (p->address != 0xFFFFFFFFu) { num = p->address; is_num = true; }
        break;
    }
    NT_STATUS st;
    if (is_num) {
        *result = 4;
        st = len < 4 ? NT_STATUS_BUFFER_TOO_SMALL : NT_STATUS_SUCCESS;
        if (!st) memcpy(buf, &num, 4);
    } else if (!s) {
        *result = 0;
        st = prop <= 16 ? NT_STATUS_OBJECT_NAME_NOT_FOUND : ST_INVALID_PARAMETER_2;
    } else {
        if (!slen) slen = strlen(s) + 1;
        *result = (UINT32)(2 * slen);
        st = len < *result ? NT_STATUS_BUFFER_TOO_SMALL : NT_STATUS_SUCCESS;
        if (!st) for (size_t i = 0; i < slen; i++) ((NT_WCHAR *)buf)[i] = (UINT8)s[i];
    }
    kfree(n);
    return st;
}

/* ---- the device's registry keys ---- */

typedef struct {
    UINT32 Length, pad;
    void *RootDirectory;
    NT_UNICODE_STRING *ObjectName;
    UINT32 Attributes;
    void *SecurityDescriptor, *SecurityQualityOfService;
} PnpObjectAttributes;

NT_API NTSTATUS nt_ZwCreateKey(void *handle, UINT32 access, PnpObjectAttributes *oa, UINT32 title,
                               NT_UNICODE_STRING *cls, UINT32 options, UINT32 *disposition);

/* PLUGPLAY_REGKEY_DEVICE (1): the instance key's Device Parameters, where
 * an INF's HW AddReg puts a driver's settings; PLUGPLAY_REGKEY_DRIVER
 * (2): its software key under Control\Class */
NT_API NTSTATUS nt_IoOpenDeviceRegistryKey(NT_DEVICE_OBJECT *dev, UINT32 type, UINT32 access, void **handle)
{
    NtPdo *p = pdo_of(dev);
    if (!p || !handle) return NT_STATUS_INVALID_DEVICE_REQUEST;
    char path[260], drv[80];
    if (type & 1) {
        ksnprintf(path, sizeof(path), "\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum\\%s\\Device Parameters",
                  p->instance);
    } else if (type & 2) {
        char key[200];
        ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", p->instance);
        if (!um_registry_get_sz(key, "Driver", drv, sizeof(drv)) || !drv[0]) return NT_STATUS_OBJECT_NAME_NOT_FOUND;
        ksnprintf(path, sizeof(path), "\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\%s", drv);
    } else {
        return ST_INVALID_PARAMETER_2;
    }
    NT_WCHAR w[260];
    size_t n = strlen(path);
    for (size_t i = 0; i <= n; i++) w[i] = (UINT8)path[i];
    NT_UNICODE_STRING u = { (unsigned short)(2 * n), (unsigned short)(2 * n + 2), w };
    PnpObjectAttributes oa = { sizeof(oa), 0, NULL, &u, 0x240 /* OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE */, NULL, NULL };
    return nt_ZwCreateKey(handle, access, &oa, 0, NULL, 0, NULL);
}

/* The symbolic links of the interfaces of class @guid that are on (of
 * @pdo's device only, when given): a MULTI_SZ in pool the caller frees */
NT_API NTSTATUS nt_IoGetDeviceInterfaces(const NT_GUID *guid, NT_DEVICE_OBJECT *pdo, UINT32 flags, NT_WCHAR **out)
{
    (void)flags;                                             /* (DEVICE_INTERFACE_INCLUDE_NONACTIVE: only active ones kept) */
    if (!guid || !out) return NT_STATUS_INVALID_PARAMETER;
    size_t total = 1;
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < g_npdo; i++)
        for (NtIface *f = g_pdo[i]->ifaces; f; f = f->next)
            if (f->on && !memcmp(&f->guid, guid, sizeof(*guid)) && (!pdo || g_pdo[i]->pdo == pdo))
                total += strlen(f->link) + 5;
    spin_unlock_irqrestore(&g_lock, s);
    NT_WCHAR *w = nt_ExAllocatePoolWithTag(1, 2 * total + 64, 0x20706E50);
    if (!w) return NT_STATUS_INSUFFICIENT_RESOURCES;
    size_t k = 0;
    s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < g_npdo; i++)
        for (NtIface *f = g_pdo[i]->ifaces; f; f = f->next)
            if (f->on && !memcmp(&f->guid, guid, sizeof(*guid)) && (!pdo || g_pdo[i]->pdo == pdo) &&
                k + strlen(f->link) + 5 < total) {
                const char *pre = "\\??\\";
                for (const char *c = pre; *c; c++) w[k++] = (UINT8)*c;
                for (const char *c = f->link; *c; c++) w[k++] = (UINT8)*c;
                w[k++] = 0;
            }
    spin_unlock_irqrestore(&g_lock, s);
    w[k] = 0;
    *out = w;
    return NT_STATUS_SUCCESS;
}

/* ---- what WDM drivers' PnP code calls besides ---- */

/* A bus's children changed (BusRelations, type 0): the PnP thread asks
 * it which they are.  Any IRQL up to DISPATCH_LEVEL. */
NT_API void nt_IoInvalidateDeviceRelations(NT_DEVICE_OBJECT *pdo, int type)
{
    if (type != 0 || !pdo || !g_ready) return;
    bool queued = false;
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < 8; i++) if (g_enum_pending[i] == pdo) queued = true;
    for (int i = 0; i < 8 && !queued; i++) if (!g_enum_pending[i]) { g_enum_pending[i] = pdo; queued = true; }
    spin_unlock_irqrestore(&g_lock, s);
    if (!queued) kprintf("[PNP] IoInvalidateDeviceRelations: too many buses waiting\n");
    nt_KeSetEvent(&g_enum_event, 0, 0);
}

NT_API void nt_PoStartNextPowerIrp(NT_IRP *irp) { (void)irp; }

NT_API NT_STATUS nt_PoCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp) { return nt_IofCallDriver(dev, irp); }

