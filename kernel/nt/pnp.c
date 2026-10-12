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
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../ke/scheduler.h"
#include "../um/um.h"

#define ENUM_KEY "Machine\\SYSTEM\\CurrentControlSet\\Enum"
#define MAX_PDOS 64

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
    UINT32               vector;        /* translated interrupt (0: none) */
    UINT8                irql;
    bool                 started;
    NtIface             *ifaces;
    struct NtPdo        *parent;        /* a bus driver's child: its parent (NULL: a PCI function) */
    bool                 seen;          /* (in the parent's last BusRelations) */
    bool                 rawpdo;        /* (started with no function driver) */
    unsigned int         caps;          /* DEVICE_CAPABILITIES.Flags (children) */
} NtPdo;

/* DEVICE_CAPABILITIES.Flags */
#define CAP_UNIQUE_ID        0x40
#define CAP_RAW_DEVICE_OK    0x100

/* DEVICE_RELATIONS */
typedef struct { unsigned int Count; NT_DEVICE_OBJECT *Objects[1]; } NtRelations;

static NT_DRIVER_OBJECT g_pci;          /* the PCI bus driver: owns the PDOs */
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

/* Load @service (unless "": a raw PDO, its own function driver), call its
 * AddDevice with @p's PDO and start the stack with @p's resources: 0, or
 * the problem code */
static UINT32 start_stack(NtPdo *p, const char *service)
{
    NtDriver *drv = NULL;
    NTSTATUS st;
    if (service[0]) {
        drv = NtDrvFind(service);
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
        st = drv->ext.AddDevice(drv->obj, p->pdo);
        NT_DEVICE_OBJECT *top = nt_IoGetAttachedDevice(p->pdo);
        if (!NT_SUCCESS(st) || top == p->pdo) {
            kprintf("[PNP] %s: %s's AddDevice failed (0x%08x)\n", p->instance, service, (UINT32)st);
            unload_if_idle(drv);
            return PROB_FAILED_ADD;
        }
        for (NT_DEVICE_OBJECT *d = p->pdo->AttachedDevice; d; d = d->AttachedDevice)
            if (d->Flags & NT_DO_DEVICE_INITIALIZING) {
                kprintf("[PNP] %s: %s's AddDevice left DO_DEVICE_INITIALIZING set\n", p->instance, service);
                d->Flags &= ~NT_DO_DEVICE_INITIALIZING;
            }
    }

    NT_DEVICE_CAPABILITIES caps;
    memset(&caps, 0, sizeof(caps));
    caps.Size = sizeof(caps);
    caps.Version = 1;
    caps.Address = caps.UINumber = 0xFFFFFFFFu;
    send_pnp(p->pdo, NT_IRP_MN_QUERY_CAPABILITIES, &caps, NULL, NULL);

    kprintf("[PNP] %s: starting %s%s\n", p->instance, service[0] ? service : "it raw",
            p->xlat ? " with its resources" : "");
    if (p->xlat) log_resources(p);
    st = send_pnp(p->pdo, NT_IRP_MN_START_DEVICE, p->raw, p->xlat, NULL);
    if (!NT_SUCCESS(st)) {
        kprintf("[PNP] %s: IRP_MN_START_DEVICE failed (0x%08x)\n", p->instance, (UINT32)st);
        send_pnp(p->pdo, NT_IRP_MN_REMOVE_DEVICE, NULL, NULL, NULL);
        if (drv && p->pdo->AttachedDevice) nt_IoDetachDevice(p->pdo);
        unload_if_idle(drv);
        return PROB_FAILED_START;
    }
    p->started = true;
    return 0;
}

static void enumerate(NtPdo *bus);

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
    if (!PnpGet(node, n) || !(n->flags & PNP_PRESENT) || strncmp(n->instance, "PCI\\", 4) || !pci_of(node, &pci)) {
        prob = PROB_NO_DRIVER;
        goto out;
    }
    p = pdo_by_node(node);
    if (p && p->started) goto out;                           /* (already running) */
    if ((n->flags & PNP_STARTED) && !p) { prob = PROB_FAILED_START; goto out; }   /* (a NovaOS driver has it) */
    ksnprintf(key, sizeof(key), ENUM_KEY "\\%s", n->instance);
    if (!um_registry_get_sz(key, "Service", service, sizeof(service)) || !service[0]) { prob = PROB_NO_DRIVER; goto out; }

    if (!p) p = pdo_new(node, &pci, n->instance);
    if (!p) { prob = PROB_FAILED_START; goto out; }
    prob = start_stack(p, service);
    if (prob) goto out;
    strncpy(p->service, service, sizeof(p->service) - 1);
    PciClaim(&p->pci, p->service);
    PnpSetWindowsDriver(node, p->service);              /* (after PciClaim's PnpSetDriver) */
    kprintf("[PNP] %s: started (driver %s)\n", n->instance, service);
    enumerate(p);                                        /* (its children, if it is a bus) */
out:
    if (prob && prob != PROB_NO_DRIVER && !((n->flags & PNP_STARTED) && !pdo_by_node(node)))   /* (not a NovaOS driver's) */
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
    p->started = false;
    PciClaim(&p->pci, NULL);
    PnpSetProblem(node, PROB_DISABLED);
    kprintf("[PNP] %s: removed (driver %s)\n", p->instance, p->service);
    p->service[0] = 0;
    unload_if_idle(drv);
    pnp_unlock();
    return STATUS_SUCCESS;
}

void NtPnpBoot(void)
{
    pnp_init();
    UINT32 count = PnpCount();
    PnpNode *n = kmalloc(sizeof(PnpNode));
    if (!n) return;
    char key[200], service[64];
    for (UINT32 id = 1; id <= count; id++) {
        if (!PnpGet(id, n) || !(n->flags & PNP_PRESENT) || (n->flags & PNP_STARTED) || strncmp(n->instance, "PCI\\", 4))
            continue;
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
    pdo->Flags |= NT_DO_BUS_ENUMERATED_DEVICE;
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
    /* An instance ID unique only on its bus gets the parent's node in
     * front, as Windows puts a prefix of the parent's there */
    if (caps.Flags & CAP_UNIQUE_ID) ksnprintf(c->instance, sizeof(c->instance), "%s\\%s", dev, inst);
    else ksnprintf(c->instance, sizeof(c->instance), "%s\\%u&%s", dev, (unsigned)bus->node, inst);
    strcpy(n->instance, c->instance);
    if (!n->hwids[0]) strncpy(n->hwids, dev, sizeof(n->hwids) - 2);
    if (!n->desc[0]) strncpy(n->desc, dev, sizeof(n->desc) - 1);
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
    PnpSetWindowsDriver(c->node, c->service);
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
    kfree(c);
    unload_if_idle(drv);
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
    case 13: if (!p->parent) { num = NT_PCIBus; is_num = true; } break;     /* LegacyBusType */
    case 14: if (!p->parent) { num = p->pci.bus; is_num = true; } break;    /* BusNumber */
    case 15: strncpy(tmp, n->instance, sizeof(tmp) - 1);        /* EnumeratorName */
             tmp[sizeof(tmp) - 1] = 0;
             if (strchr(tmp, '\\')) *strchr(tmp, '\\') = 0;
             s = tmp; break;
    case 16: if (!p->parent) { num = (UINT32)p->pci.dev << 16 | p->pci.func; is_num = true; } break;   /* Address */
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

