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
 * A USB device no NovaOS driver took gets the same, its PDO from the USB
 * bus interface (usbd.c), with no resources: its driver reaches the
 * device through URBs.  A started device's stack is then asked for its
 * children (IRP_MN_QUERY_DEVICE_RELATIONS, BusRelations), as Windows asks
 * every bus: each PDO a bus driver reports (FTDI's ftdibus.sys reports a
 * COM port) becomes a node under it, named by what the PDO answers to
 * IRP_MN_QUERY_ID, and is started like any device once a driver is
 * installed on it.  IoInvalidateDeviceRelations asks again; a child no
 * longer reported is removed.
 *
 * NtPnpStop takes a started device away again the way Windows disables
 * one: IRP_MN_QUERY_REMOVE_DEVICE (a driver may veto it), then
 * IRP_MN_REMOVE_DEVICE, its children first; the device's interfaces are
 * turned off, and a driver left with no device is unloaded.  A device
 * that is unplugged gets IRP_MN_SURPRISE_REMOVAL and then
 * IRP_MN_REMOVE_DEVICE (NtPnpSurpriseRemoval).
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
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
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

enum { BUS_PCI, BUS_USB, BUS_CHILD };

typedef struct NtPdo {
    NT_DEVICE_OBJECT    *pdo;           /* NULL once the device is gone for good */
    UINT32               node;
    int                  bus;           /* BUS_*: whose PDO it is */
    PciDevice            pci;           /* BUS_PCI */
    struct NtPdo        *parent;        /* BUS_CHILD: the device whose bus driver reported it */
    bool                 reported;      /* BUS_CHILD: in the parent's last BusRelations */
    UINT32               address;       /* BUS_CHILD: DEVICE_CAPABILITIES.Address */
    char                 instance[128];
    char                 service[64];   /* the driver running it ("" when none) */
    NT_CM_RESOURCE_LIST *raw, *xlat;
    UINT32               vector;        /* translated interrupt (0: none) */
    UINT8                irql;
    bool                 started;
    NtIface             *ifaces;
} NtPdo;

static NT_DRIVER_OBJECT g_pci;          /* the PCI bus driver: owns the PCI PDOs */
static NtPdo           *g_pdo[MAX_PDOS];
static int              g_npdo;
static KSpinLock        g_lock = KSPINLOCK_INIT;

static NtPdo *pdo_of(NT_DEVICE_OBJECT *d)
{
    for (int i = 0; d && i < g_npdo; i++) if (g_pdo[i]->pdo == d) return g_pdo[i];
    return NULL;
}

bool NtPnpInterruptOf(NT_DEVICE_OBJECT *pdo, UINT32 *vector, UINT8 *irql)
{
    NtPdo *p = pdo_of(pdo);
    if (!p || !p->vector) return false;
    *vector = p->vector;
    *irql = p->irql;
    return true;
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
        for (int k = 0; k < 2; k++) {
            NT_CM_PARTIAL_RESOURCE_DESCRIPTOR *r = desc_add(l[k]);
            r->Type = NT_CmResourceTypeInterrupt;
            r->ShareDisposition = level ? NT_CmResourceShareShared : NT_CmResourceShareDeviceExclusive;
            r->Flags = level ? NT_CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE : NT_CM_RESOURCE_INTERRUPT_LATCHED;
            r->u.Interrupt.Level = (unsigned short)(k ? p->irql : gsi);
            r->u.Interrupt.Vector = k ? vector : gsi;
            r->u.Interrupt.Affinity = k ? 1 : ~0ull;          /* (routed to the boot CPU) */
        }
    }
    return true;
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
static NT_API unsigned int bus_get(void *ctx, unsigned int type, void *buf, unsigned int off, unsigned int len)
{
    return type == NT_PCI_CONFIG_DATA ? config(ctx, false, buf, off, len) : 0;
}
static NT_API unsigned int bus_set(void *ctx, unsigned int type, void *buf, unsigned int off, unsigned int len)
{
    return type == NT_PCI_CONFIG_DATA ? config(ctx, true, buf, off, len) : 0;
}

static NT_STATUS pdo_pnp(NtPdo *p, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    const PciDevice *d = &p->pci;
    NT_STATUS st = irp->IoStatus.Status;
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE: {
        UINT16 cmd = PciRead16(d->bus, d->dev, d->func, 0x04);
        cmd |= 7;                                       /* I/O and memory decoding, bus mastering */
        if (p->vector) cmd &= (UINT16)~(1u << 10);      /* its interrupt line on */
        PciWrite16(d->bus, d->dev, d->func, 0x04, cmd);
        st = NT_STATUS_SUCCESS;
        break;
    }
    case NT_IRP_MN_REMOVE_DEVICE:
    case NT_IRP_MN_SURPRISE_REMOVAL: {
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
    if (!build_resources(p)) return NULL;
    g_pdo[g_npdo++] = p;
    return p;
}

static NtPdo *pdo_by_node(UINT32 node)
{
    for (int i = 0; i < g_npdo; i++) if (g_pdo[i]->node == node && g_pdo[i]->pdo) return g_pdo[i];
    return NULL;
}

/* A record for a PDO another driver made (a USB device's, a bus driver's child) */
static NtPdo *pdo_record(NT_DEVICE_OBJECT *d, UINT32 node, int bus, const char *instance)
{
    if (g_npdo >= MAX_PDOS) return NULL;
    NtPdo *p = kzalloc(sizeof(*p));
    if (!p) return NULL;
    p->pdo = d;
    p->node = node;
    p->bus = bus;
    strncpy(p->instance, instance, sizeof(p->instance) - 1);
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

static void log_resources(NtPdo *p)
{
    NT_CM_PARTIAL_RESOURCE_LIST *l = &p->xlat->List[0].PartialResourceList;
    for (unsigned i = 0; i < l->Count; i++) {
        NT_CM_PARTIAL_RESOURCE_DESCRIPTOR *r = &l->PartialDescriptors[i];
        if (r->Type == NT_CmResourceTypeMemory)
            kprintf("[PNP]   memory 0x%llx, %u bytes\n", (unsigned long long)r->u.Memory.Start, r->u.Memory.Length);
        else if (r->Type == NT_CmResourceTypePort)
            kprintf("[PNP]   I/O ports 0x%llx, %u\n", (unsigned long long)r->u.Port.Start, r->u.Port.Length);
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

static void enumerate(NtPdo *b);

UINT32 NtPnpStart(UINT32 node)
{
    PnpNode *n = kmalloc(sizeof(PnpNode));
    if (!n) return PROB_FAILED_START;
    UINT32 prob = 0;
    PciDevice pci;
    char key[200], service[64] = "";
    NtPdo *p = NULL;
    if (!PnpGet(node, n) || !(n->flags & PNP_PRESENT)) { prob = PROB_NO_DRIVER; goto out; }
    p = pdo_by_node(node);
    bool is_pci = !strncmp(n->instance, "PCI\\", 4), is_usb = !strncmp(n->instance, "USB\\", 4);
    if (!p && !(is_pci && pci_of(node, &pci)) && !(is_usb && UsbDevByPnp(node))) {
        prob = PROB_NO_DRIVER;                               /* (no bus here offers it to Windows drivers) */
        goto out;
    }
    if (p && p->started) goto out;                           /* (already running) */
    if ((n->flags & PNP_STARTED) && !p) { prob = PROB_FAILED_START; goto out; }   /* (a NovaOS driver has it) */
    ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", n->instance);
    if (!um_registry_get_sz(key, "Service", service, sizeof(service)) || !service[0]) { prob = PROB_NO_DRIVER; goto out; }
    char cls[32] = "", friendly[96] = "";                    /* (as its install wrote them: Ports, "... (COM3)") */
    um_registry_get_sz(key, "Class", cls, sizeof(cls));
    um_registry_get_sz(key, "FriendlyName", friendly, sizeof(friendly));
    PnpSetNames(node, cls, friendly);

    if (!p && is_pci) p = pdo_new(node, &pci, n->instance);
    else if (!p) {
        NT_DEVICE_OBJECT *usb = NtUsbPdo(node);
        if (usb) p = pdo_record(usb, node, BUS_USB, n->instance);
    }
    if (!p) { prob = PROB_FAILED_START; goto out; }
    NtDriver *drv = NtDrvFind(service);
    if (!drv) {
        NTSTATUS st = NtDrvLoad(service);
        drv = NtDrvFind(service);
        if (!drv) {
            kprintf("[PNP] %s: driver %s did not load (0x%08x)\n", n->instance, service, (UINT32)st);
            prob = st == ST_INVALID_IMAGE_HASH ? PROB_UNSIGNED_DRIVER : PROB_DRIVER_FAILED_LOAD;
            goto out;
        }
    }
    if (!drv->ext.AddDevice) {
        kprintf("[PNP] %s: driver %s has no AddDevice routine\n", n->instance, service);
        prob = PROB_FAILED_ADD;
        unload_if_idle(drv);
        goto out;
    }
    NTSTATUS st = drv->ext.AddDevice(drv->obj, p->pdo);
    NT_DEVICE_OBJECT *top = nt_IoGetAttachedDevice(p->pdo);
    if (!NT_SUCCESS(st) || top == p->pdo) {
        kprintf("[PNP] %s: %s's AddDevice failed (0x%08x)\n", n->instance, service, (UINT32)st);
        prob = PROB_FAILED_ADD;
        unload_if_idle(drv);
        goto out;
    }
    for (NT_DEVICE_OBJECT *d = p->pdo->AttachedDevice; d; d = d->AttachedDevice)
        if (d->Flags & NT_DO_DEVICE_INITIALIZING) {
            kprintf("[PNP] %s: %s's AddDevice left DO_DEVICE_INITIALIZING set\n", n->instance, service);
            d->Flags &= ~NT_DO_DEVICE_INITIALIZING;
        }

    NT_DEVICE_CAPABILITIES caps;
    memset(&caps, 0, sizeof(caps));
    caps.Size = sizeof(caps);
    caps.Version = 1;
    caps.Address = caps.UINumber = 0xFFFFFFFFu;
    send_pnp(p->pdo, NT_IRP_MN_QUERY_CAPABILITIES, &caps, NULL, NULL);

    if (p->raw) {
        kprintf("[PNP] %s: starting %s with its resources\n", n->instance, service);
        log_resources(p);
    } else {
        kprintf("[PNP] %s: starting %s\n", n->instance, service);
    }
    st = send_pnp(p->pdo, NT_IRP_MN_START_DEVICE, p->raw, p->xlat, NULL);
    if (!NT_SUCCESS(st)) {
        kprintf("[PNP] %s: IRP_MN_START_DEVICE failed (0x%08x)\n", n->instance, (UINT32)st);
        send_pnp(p->pdo, NT_IRP_MN_REMOVE_DEVICE, NULL, NULL, NULL);
        if (p->pdo->AttachedDevice) nt_IoDetachDevice(p->pdo);
        unload_if_idle(drv);
        prob = PROB_FAILED_START;
        goto out;
    }
    strncpy(p->service, service, sizeof(p->service) - 1);
    p->started = true;
    PnpSetDriver(node, p->service);
    if (p->bus == BUS_PCI) PciClaim(&p->pci, p->service);
    kprintf("[PNP] %s: started (driver %s)\n", n->instance, service);
    kfree(n);
    n = NULL;
    enumerate(p);                                            /* (its children, if it is a bus) */
out:
    if (n && prob && prob != PROB_NO_DRIVER && !((n->flags & PNP_STARTED) && !pdo_by_node(node)))   /* (not a NovaOS driver's) */
        PnpSetProblem(node, prob);
    kfree(n);
    return prob;
}

static void ifaces_off(NtPdo *p);

/* Take @p's driver away: IRP_MN_REMOVE_DEVICE (after
 * IRP_MN_SURPRISE_REMOVAL when the hardware is gone), its children's
 * first; @problem its node's problem afterwards (0: it left the tree) */
static void remove_tree(NtPdo *p, bool surprise, UINT32 problem)
{
    for (int i = 0; i < g_npdo; i++)
        if (g_pdo[i]->parent == p && g_pdo[i]->pdo) {
            NtPdo *c = g_pdo[i];
            remove_tree(c, surprise, 0);
            PnpRemove(c->node);                              /* (it stays a phantom, as on Windows) */
            c->pdo = NULL;                                   /* (its bus driver deletes the PDO) */
        }
    if (!p->started) return;
    NtDriver *drv = NtDrvFind(p->service);
    if (surprise) send_pnp(p->pdo, NT_IRP_MN_SURPRISE_REMOVAL, NULL, NULL, NULL);
    send_pnp(p->pdo, NT_IRP_MN_REMOVE_DEVICE, NULL, NULL, NULL);
    if (p->pdo->AttachedDevice) {
        kprintf("[PNP] %s: %s left its device attached after IRP_MN_REMOVE_DEVICE\n", p->instance, p->service);
        nt_IoDetachDevice(p->pdo);
    }
    ifaces_off(p);
    p->started = false;
    if (p->bus == BUS_PCI) PciClaim(&p->pci, NULL);
    if (problem) PnpSetProblem(p->node, problem);
    kprintf("[PNP] %s: removed (driver %s)\n", p->instance, p->service);
    p->service[0] = 0;
    unload_if_idle(drv);
}

/* IRP_MN_QUERY_REMOVE_DEVICE to @p's started children and then to @p:
 * the first veto, after IRP_MN_CANCEL_REMOVE_DEVICE to those that agreed */
static NT_STATUS query_remove(NtPdo *p)
{
    NtPdo *asked[MAX_PDOS];
    int n = 0;
    NT_STATUS st = NT_STATUS_SUCCESS;
    for (int i = 0; i < g_npdo && NT_SUCCESS(st); i++)
        if (g_pdo[i]->parent == p && g_pdo[i]->pdo && g_pdo[i]->started) {
            st = query_remove(g_pdo[i]);
            if (NT_SUCCESS(st)) asked[n++] = g_pdo[i];
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
    NtPdo *p = pdo_by_node(node);
    if (!p || !p->started) return ST_INVALID_DEVICE_STATE;
    NT_STATUS st = query_remove(p);
    if (!NT_SUCCESS(st)) return st;
    remove_tree(p, false, PROB_DISABLED);
    return STATUS_SUCCESS;
}

/* ---- children: the PDOs a bus driver reports ---- */

/* An ID or text a PDO answers (IRP_MN_QUERY_ID @type, or
 * IRP_MN_QUERY_DEVICE_TEXT @type with @text) as ASCII; a list (hardware
 * and compatible IDs) keeps its NULs and ends with an empty string */
static bool query_string(NT_DEVICE_OBJECT *pdo, bool text, int type, char *out, int cap)
{
    NT_ULONG_PTR info = 0;
    out[0] = out[1] = 0;
    NT_STATUS st = send_pnp(pdo, text ? NT_IRP_MN_QUERY_DEVICE_TEXT : NT_IRP_MN_QUERY_ID,
                            (void *)(UINT64)type, text ? (void *)(UINT64)0x409 : NULL, &info);
    NT_WCHAR *w = (NT_WCHAR *)info;
    if (!NT_SUCCESS(st) || !w) return false;
    bool multi = !text && (type == 1 || type == 2);
    int k = 0;
    for (int i = 0; k < cap - 2; i++) {
        NT_WCHAR c = w[i];
        out[k++] = c && c < 0x80 ? (char)c : c ? '?' : 0;
        if (!c && (!multi || !w[i + 1] || k >= cap - 2)) break;
    }
    out[k] = out[k + 1 < cap ? k + 1 : k] = 0;
    nt_ExFreePool(w);
    return true;
}

/* A PDO @b's bus driver reported: its node and record, and its driver
 * started if one is installed */
static void child_found(NtPdo *b, NT_DEVICE_OBJECT *d)
{
    NtPdo *c = pdo_of(d);
    if (c) { c->reported = true; return; }
    d->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    NtIoNameDevice(d);
    PnpNode *n = kzalloc(sizeof(PnpNode));
    char *dev = kmalloc(256), *inst = kmalloc(128);
    if (!n || !dev || !inst) goto out;
    if (!query_string(d, false, 0, dev, 200) || !dev[0]) {
        kprintf("[PNP] %s: its bus driver reported a child with no device ID\n", b->instance);
        goto out;
    }
    if (!query_string(d, false, 3, inst, 64)) inst[0] = 0;
    NT_DEVICE_CAPABILITIES caps;
    memset(&caps, 0, sizeof(caps));
    caps.Size = sizeof(caps);
    caps.Version = 1;
    caps.Address = caps.UINumber = 0xFFFFFFFFu;
    send_pnp(d, NT_IRP_MN_QUERY_CAPABILITIES, &caps, NULL, NULL);
    /* An instance ID that is not unique (UniqueID clear) gets the
     * parent's prefix, so two buses' children do not collide */
    UINT32 h = 2166136261u;
    for (const char *s = b->instance; *s; s++) h = (h ^ (UINT8)*s) * 16777619u;
    if (caps.Flags & 0x40) ksnprintf(n->instance, sizeof(n->instance), "%s\\%s", dev, inst[0] ? inst : "0000");
    else ksnprintf(n->instance, sizeof(n->instance), "%s\\8&%x&0&%s", dev, h, inst[0] ? inst : "0000");
    if (!query_string(d, false, 1, n->hwids, sizeof(n->hwids))) { strcpy(n->hwids, dev); n->hwids[strlen(dev) + 1] = 0; }
    query_string(d, false, 2, n->compat, sizeof(n->compat));
    query_string(d, true, 0, n->desc, sizeof(n->desc));
    query_string(d, true, 1, n->location, sizeof(n->location));
    char key[200];
    ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", n->instance);
    um_registry_get_sz(key, "Class", n->cls, sizeof(n->cls));
    if (!um_registry_get_sz(key, "FriendlyName", n->friendly, sizeof(n->friendly))) n->friendly[0] = 0;
    UINT32 id = PnpAdd(b->node, n);
    if (!id) goto out;
    c = pdo_record(d, id, BUS_CHILD, n->instance);
    if (!c) goto out;
    c->parent = b;
    c->reported = true;
    c->address = caps.Address;
    kprintf("[PNP] %s: reported by %s's bus driver\n", n->instance, b->instance);
    char service[64];
    if (um_registry_get_sz(key, "Service", service, sizeof(service)) && service[0]) {
        UINT32 disabled = 0;
        if (um_registry_get_dword(key, "ConfigFlags", &disabled) && (disabled & 1)) PnpSetProblem(id, PROB_DISABLED);
        else NtPnpStart(id);
    }
out:
    kfree(inst);
    kfree(dev);
    kfree(n);
}

/* Ask started device @b's stack for its children (BusRelations): new
 * ones are added, those no longer reported are removed */
static void enumerate(NtPdo *b)
{
    if (!b->started || !b->pdo) return;
    NT_ULONG_PTR info = 0;
    NT_STATUS st = send_pnp(b->pdo, NT_IRP_MN_QUERY_DEVICE_RELATIONS, (void *)0 /* BusRelations */, NULL, &info);
    struct { unsigned int Count; NT_DEVICE_OBJECT *Objects[1]; } *r = NT_SUCCESS(st) ? (void *)info : NULL;
    for (int i = 0; i < g_npdo; i++) if (g_pdo[i]->parent == b) g_pdo[i]->reported = false;
    for (unsigned k = 0; r && k < r->Count; k++) child_found(b, r->Objects[k]);
    if (r) nt_ExFreePool(r);
    for (int i = 0; i < g_npdo; i++) {
        NtPdo *c = g_pdo[i];
        if (c->parent != b || c->reported || !c->pdo) continue;
        kprintf("[PNP] %s: no longer reported by its bus driver\n", c->instance);
        remove_tree(c, true, 0);
        PnpRemove(c->node);
        c->pdo = NULL;
    }
}

/* ---- deferred work: arrivals, removals, re-enumeration ---- */

NT_API NT_IO_WORKITEM *nt_IoAllocateWorkItem(NT_DEVICE_OBJECT *dev);
NT_API void nt_IoFreeWorkItem(NT_IO_WORKITEM *w);
NT_API void nt_IoQueueWorkItem(NT_IO_WORKITEM *w, NT_IO_WORKITEM_ROUTINE routine, int queue_type, void *ctx);

enum { WORK_USB_ARRIVED, WORK_SURPRISE, WORK_RELATIONS };
typedef struct { NT_IO_WORKITEM *w; int what; UINT32 node; NT_DEVICE_OBJECT *pdo; } PnpWork;

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
            else if (um_registry_get_sz(key, "Service", service, sizeof(service)) && service[0]) NtPnpStart(k->node);
        }
        kfree(n);
    } else if (k->what == WORK_SURPRISE) {
        NtPdo *p = pdo_by_node(k->node);
        if (p) {
            kprintf("[PNP] %s: the device is gone (surprise removal)\n", p->instance);
            remove_tree(p, true, 0);
            p->pdo = NULL;                                   /* (it comes back with a new PDO) */
        }
    } else {
        NtPdo *p = pdo_of(k->pdo);
        if (p) enumerate(p);
    }
    nt_IoFreeWorkItem(k->w);
    kfree(k);
}

static void defer(int what, UINT32 node, NT_DEVICE_OBJECT *pdo)
{
    PnpWork *k = kzalloc(sizeof(*k));
    if (!k) return;
    k->w = nt_IoAllocateWorkItem(NULL);
    if (!k->w) { kfree(k); return; }
    k->what = what;
    k->node = node;
    k->pdo = pdo;
    nt_IoQueueWorkItem(k->w, work_routine, 1 /* DelayedWorkQueue */, k);
}

/* Set once NtPnpBoot ran: the registry is loaded and the work-item thread
 * is up.  A USB device enumerated before that is started by NtPnpBoot. */
static volatile bool g_booted;

void NtPnpUsbArrived(UINT32 node) { if (g_booted) defer(WORK_USB_ARRIVED, node, NULL); }
void NtPnpSurpriseRemoval(UINT32 node) { if (g_booted) defer(WORK_SURPRISE, node, NULL); }

/* A bus driver's children changed: ask it again (BusRelations), later */
NT_API void nt_IoInvalidateDeviceRelations(NT_DEVICE_OBJECT *pdo, int type)
{
    if (type == 0 && pdo_of(pdo)) defer(WORK_RELATIONS, 0, pdo);
}

void NtPnpBoot(void)
{
    UINT32 count = PnpCount();
    PnpNode *n = kmalloc(sizeof(PnpNode));
    if (!n) return;
    char key[200], service[64];
    g_booted = true;
    for (UINT32 id = 1; id <= count; id++) {
        if (!PnpGet(id, n) || !(n->flags & PNP_PRESENT) || (n->flags & PNP_STARTED)) continue;
        if (!strncmp(n->instance, "USB\\", 4)) {             /* (on the work-item thread, with later arrivals) */
            if (UsbDevByPnp(id)) defer(WORK_USB_ARRIVED, id, NULL);
            continue;
        }
        if (strncmp(n->instance, "PCI\\", 4)) continue;
        ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", n->instance);
        UINT32 disabled = 0;
        if (um_registry_get_dword(key, "ConfigFlags", &disabled) && (disabled & 1)) {   /* CONFIGFLAG_DISABLED */
            PnpSetProblem(id, PROB_DISABLED);
            continue;
        }
        if (um_registry_get_sz(key, "Service", service, sizeof(service)) && service[0]) NtPnpStart(id);
    }
    kfree(n);
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
    if (!NtIoDeviceName(p->pdo, b, sizeof(b))) return NT_STATUS_INVALID_DEVICE_REQUEST;
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
    case 11: if (NtIoDeviceName(p->pdo, tmp, sizeof(tmp))) s = tmp; break;   /* PhysicalDeviceObjectName */
    case 13: if (p->bus == BUS_PCI) { num = NT_PCIBus; is_num = true; } break;   /* LegacyBusType */
    case 14:                                                     /* BusNumber */
        if (p->bus == BUS_PCI) { num = p->pci.bus; is_num = true; }
        else if (p->bus == BUS_USB) is_num = NtUsbPdoProperty(pdo, 14, &num);
        break;
    case 15: {                                                   /* EnumeratorName: the instance's first part */
        int k = 0;
        while (n->instance[k] && n->instance[k] != '\\' && k < (int)sizeof(tmp) - 1) { tmp[k] = n->instance[k]; k++; }
        tmp[k] = 0;
        s = tmp;
        break;
    }
    case 16:                                                     /* Address */
        if (p->bus == BUS_PCI) { num = (UINT32)p->pci.dev << 16 | p->pci.func; is_num = true; }
        else if (p->bus == BUS_USB) is_num = NtUsbPdoProperty(pdo, 16, &num);
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

NT_API void nt_PoStartNextPowerIrp(NT_IRP *irp) { (void)irp; }

NT_API NT_STATUS nt_PoCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp) { return nt_IofCallDriver(dev, irp); }

/* Remove locks: requests in flight on a device, which IRP_MN_REMOVE_DEVICE waits out */
NT_API void nt_IoInitializeRemoveLockEx(NT_IO_REMOVE_LOCK *l, UINT32 tag, UINT32 max_min, UINT32 high, UINT32 size)
{
    (void)tag; (void)max_min; (void)high; (void)size;
    l->Removed = 0;
    l->IoCount = 1;
    nt_KeInitializeEvent(&l->RemoveEvent, NT_NotificationEvent, 0);
}

NT_API NT_STATUS nt_IoAcquireRemoveLockEx(NT_IO_REMOVE_LOCK *l, void *tag, const char *file, UINT32 line, UINT32 size)
{
    (void)tag; (void)file; (void)line; (void)size;
    __atomic_add_fetch(&l->IoCount, 1, __ATOMIC_ACQ_REL);
    if (!__atomic_load_n(&l->Removed, __ATOMIC_ACQUIRE)) return NT_STATUS_SUCCESS;
    if (__atomic_sub_fetch(&l->IoCount, 1, __ATOMIC_ACQ_REL) == 0) nt_KeSetEvent(&l->RemoveEvent, 0, 0);
    return NT_STATUS_DELETE_PENDING;
}

NT_API void nt_IoReleaseRemoveLockEx(NT_IO_REMOVE_LOCK *l, void *tag, UINT32 size)
{
    (void)tag; (void)size;
    if (__atomic_sub_fetch(&l->IoCount, 1, __ATOMIC_ACQ_REL) == 0) nt_KeSetEvent(&l->RemoveEvent, 0, 0);
}

NT_API void nt_IoReleaseRemoveLockAndWaitEx(NT_IO_REMOVE_LOCK *l, void *tag, UINT32 size)
{
    (void)tag; (void)size;
    __atomic_store_n(&l->Removed, 1, __ATOMIC_RELEASE);
    /* the caller's own acquisition and the initial count */
    if (__atomic_sub_fetch(&l->IoCount, 2, __ATOMIC_ACQ_REL) > 0)
        nt_KeWaitForSingleObject(&l->RemoveEvent, 0, 0, 0, NULL);
}

