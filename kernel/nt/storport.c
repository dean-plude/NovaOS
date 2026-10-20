/*
 * storport.c — storport.sys: the port driver storage miniports run on
 *
 * Windows storage controllers' drivers are Storport miniports: they know
 * their hardware (a virtio-blk queue, an AHCI port, NVMe queues) and
 * nothing else.  Storport, the port driver, does the rest: it owns the
 * device objects, answers Plug and Play, hands the miniport the adapter's
 * resources, connects its interrupt, and sends it SCSI requests (SRBs).
 * Microsoft's storport.sys never ships with NovaOS; this is NovaOS's own,
 * on its I/O manager, PnP manager and DMA code (io.c, pnp.c, intr.c,
 * dma.c), with the miniport's interface as Windows documents it:
 *
 *   StorPortInitialize   from the miniport's DriverEntry: its
 *                        HW_INITIALIZATION_DATA; the driver object's
 *                        AddDevice and dispatch routines become Storport's
 *   AddDevice            an FDO on the PCI function's PDO (pnp.c)
 *   START_DEVICE         passed down to the PDO first; then
 *                        PORT_CONFIGURATION_INFORMATION from the resources
 *                        (access ranges, the interrupt), HwFindAdapter,
 *                        the interrupt connected (line-based: this host does
 *                        not take MSI yet, so the PnP manager keeps the line on), HwInitialize under the interrupt
 *                        lock, the passive-level initialize routine
 *   requests             STORAGE_REQUEST_BLOCKs (SRB_EX, as miniports
 *                        that ask for them with SrbTypeFlags get them):
 *                        HwBuildIo, then HwStartIo under the StartIo lock;
 *                        the miniport completes each with
 *                        StorPortNotification(RequestComplete), from its
 *                        StartIo, its interrupt or its DPC
 *
 * What Windows' disk class driver (disk.sys) and partition manager do on
 * top is NovaOS's own disk layer here: once the adapter runs, a thread
 * asks LUN 0 for its INQUIRY data and capacity and registers the disk with
 * the block layer (fs/block.h, "stor0", ...), and the drives code mounts
 * its volumes (fs/drives.c), as for a SATA or NVMe disk.  Each block-layer
 * transfer becomes one READ(10)/WRITE(10) (or the 16-byte forms past 2 TiB)
 * through a bounce buffer the adapter can reach, one request at a time.
 *
 * Not yet: removing a running adapter (the disk's volumes are in use, so
 * query-remove is refused), message-signalled interrupts, more than one
 * LUN per adapter, and the crash-dump path (Storport's dump mode).
 */
#include "ntapi.h"
#include "storport.h"
#include "../fs/block.h"
#include "../fs/drives.h"
#include "../ke/irql.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

#define TAG          0x74726F53u        /* 'Stor' */
#define MAX_RANGES   8
#define BOUNCE_SIZE  (64 * 1024)        /* the largest transfer sent at once */
#define REQ_TIMEOUT  (-30LL * 10000000) /* 30 s, relative */
#define REQ_POLL_AT  (-2LL * 10000000)  /* the first look for a missed interrupt, 2 s */
#define BUSY_RETRIES 50


typedef unsigned char (NT_API *HW_INITIALIZE)(void *ext);
typedef unsigned char (NT_API *HW_STARTIO)(void *ext, void *srb);
typedef unsigned char (NT_API *HW_INTERRUPT)(void *ext);
typedef unsigned int  (NT_API *HW_FIND_ADAPTER)(void *ext, void *hw_ctx, void *bus_info, char *args,
                                                STOR_PORT_CONFIGURATION_INFORMATION *ci, unsigned char *again);
typedef unsigned char (NT_API *HW_BUILDIO)(void *ext, void *srb);
typedef unsigned char (NT_API *HW_PASSIVE_INITIALIZE)(void *ext);
typedef void          (NT_API *HW_DPC_ROUTINE)(STOR_DPC *dpc, void *ext, void *a1, void *a2);

typedef struct StorDriver {
    struct StorDriver          *next;
    NT_DRIVER_OBJECT           *obj;
    char                        service[64];
    STOR_HW_INITIALIZATION_DATA hw;
    void                       *hw_ctx;
    int                         adapters;
} StorDriver;

typedef struct StorAdapter {
    struct StorAdapter *next;
    StorDriver         *drv;
    NT_DEVICE_OBJECT   *fdo, *pdo, *lower;
    void               *ext;            /* the miniport's device extension */
    STOR_PORT_CONFIGURATION_INFORMATION ci;
    STOR_ACCESS_RANGE   ranges[MAX_RANGES];
    void               *uncached;       /* StorPortGetUncachedExtension's */
    UINT32              uncached_len;
    void               *intr;           /* the connected interrupt object */
    NT_KSPIN_LOCK       startio_lock;
    HW_PASSIVE_INITIALIZE passive;
    bool                started;
    /* the one request in flight */
    UINT8              *srb_mem;        /* the SRB, its address and command (contiguous) */
    void               *srb_ext;        /* its SrbExtension (contiguous) */
    STOR_SCATTER_GATHER_LIST *sg;
    UINT8              *bounce;         /* data, below what the adapter reaches */
    UINT64              bounce_pa;
    UINT32              chunk;          /* the largest transfer, bytes */
    NT_KEVENT           req_lock;       /* (a synchronization event: one request at a time) */
    NT_KEVENT           done_ev;
    NT_KDPC             done_dpc;
    volatile UINT32     done;
    /* the disk on LUN 0 */
    BlockDev            dev;
    UINT32              block;          /* bytes per block */
    int                 index;
} StorAdapter;

static StorDriver   *g_drivers;
static StorAdapter  *g_adapters;
static int           g_count;           /* disks named so far: stor0, stor1, ... */

static StorDriver *driver_of(NT_DRIVER_OBJECT *obj)
{
    for (StorDriver *d = g_drivers; d; d = d->next) if (d->obj == obj) return d;
    return NULL;
}

/* The adapter whose device extension is @ext (what every StorPort* call names) */
static StorAdapter *adapter_of(void *ext)
{
    for (StorAdapter *a = g_adapters; a; a = a->next) if (a->ext == ext) return a;
    return NULL;
}

static void *contig(UINT64 size)
{
    void *p = nt_MmAllocateContiguousMemorySpecifyCache(size, 0, -1, 0, 1 /* MmCached */);
    if (p) memset(p, 0, size);
    return p;
}

/* -----------------------------------------------------------------------
 * Requests: one SRB_EX at a time, waited for
 * ----------------------------------------------------------------------- */
#define SRB_ADDR_OFF 128
#define SRB_CDB_OFF  144
#define SRB_TOTAL    (SRB_CDB_OFF + sizeof(SRBEX_DATA_SCSI_CDB16))

static NT_API void done_dpc(NT_KDPC *k, void *ctx, void *a1, void *a2)
{
    (void)k; (void)a1; (void)a2;
    StorAdapter *a = ctx;
    nt_KeSetEvent(&a->done_ev, 0, 0);
}

/* The miniport's interrupt routine run by hand, under its interrupt lock
 * (what a delivered interrupt would have done) */
static NT_API unsigned char poll_sync(void *ctx)
{
    StorAdapter *a = ctx;
    return ((HW_INTERRUPT)a->drv->hw.HwInterrupt)(a->ext);
}

/* Send @cdb to LUN 0 and wait for its completion: the SRB status (0 when
 * the adapter never answered), *@got the bytes transferred.  @len bytes go
 * through the bounce buffer, in (@out false) or out. */
static UINT8 srb_run(StorAdapter *a, const UINT8 *cdb, UINT8 cdb_len, UINT32 len, bool out, UINT32 *got)
{
    STOR_HW_INITIALIZATION_DATA *hw = &a->drv->hw;
    UINT8 status = 0;
    for (int attempt = 0; attempt < BUSY_RETRIES; attempt++) {
        STORAGE_REQUEST_BLOCK *srb = (STORAGE_REQUEST_BLOCK *)a->srb_mem;
        memset(a->srb_mem, 0, SRB_TOTAL);
        srb->Length = sizeof(STORAGE_REQUEST_BLOCK);
        srb->Function = SRB_FUNCTION_STORAGE_REQUEST_BLOCK;
        srb->Signature = SRB_SIGNATURE;
        srb->Version = STORAGE_REQUEST_BLOCK_VERSION_1;
        srb->SrbLength = SRB_TOTAL;
        srb->SrbFunction = SRB_FUNCTION_EXECUTE_SCSI;
        srb->SrbFlags = (len ? (out ? SRB_FLAGS_DATA_OUT : SRB_FLAGS_DATA_IN) : SRB_FLAGS_NO_DATA_TRANSFER) |
                        SRB_FLAGS_DISABLE_SYNCH_TRANSFER;
        srb->TimeOutValue = 30;
        srb->AddressOffset = SRB_ADDR_OFF;
        srb->NumSrbExData = 1;
        srb->SrbExDataOffset[0] = SRB_CDB_OFF;
        srb->DataTransferLength = len;
        srb->DataBuffer = len ? a->bounce : NULL;
        srb->MiniportContext = a->srb_ext;
        STOR_ADDR_BTL8 *addr = (STOR_ADDR_BTL8 *)(a->srb_mem + SRB_ADDR_OFF);
        addr->Type = STOR_ADDRESS_TYPE_BTL8;
        addr->AddressLength = 4;
        SRBEX_DATA_SCSI_CDB16 *c = (SRBEX_DATA_SCSI_CDB16 *)(a->srb_mem + SRB_CDB_OFF);
        c->Type = SrbExDataTypeScsiCdb16;
        c->Length = sizeof(*c) - 8;
        c->CdbLength = cdb_len;
        memcpy(c->Cdb, cdb, cdb_len);
        a->sg->NumberOfElements = len ? 1 : 0;
        a->sg->List[0].PhysicalAddress = (long long)a->bounce_pa;
        a->sg->List[0].Length = len;

        a->done = 0;
        nt_KeClearEvent(&a->done_ev);
        UINT8 old = nt_KfRaiseIrql(DISPATCH_LEVEL);
        bool go = !hw->HwBuildIo || ((HW_BUILDIO)hw->HwBuildIo)(a->ext, srb);
        if (go) {
            nt_KeAcquireSpinLockAtDpcLevel(&a->startio_lock);
            ((HW_STARTIO)hw->HwStartIo)(a->ext, srb);
            nt_KeReleaseSpinLockFromDpcLevel(&a->startio_lock);
        }
        nt_KeLowerIrql(old);
        INT64 timeout = REQ_POLL_AT;
        if (nt_KeWaitForSingleObject(&a->done_ev, 0, 0, 0, &timeout) == NT_STATUS_TIMEOUT && !a->done) {
            /* Two seconds is a hundred times what a virtual disk needs.  An interrupt that never
             * reached the miniport (the first one after a restart, now and then, on KVM) is
             * recovered as Storport recovers a timed-out request: the adapter is asked whether it
             * has something to report; one that does completes the request. */
            bool claimed = a->intr && nt_KeSynchronizeExecution(a->intr, poll_sync, a);
            kprintf("[STOR] %s: SCSI command 0x%02x had no interrupt after 2 s (adapter %s)\n", a->drv->service,
                    cdb[0], claimed ? "had one pending" : "had none pending");
            timeout = REQ_TIMEOUT - REQ_POLL_AT;
            if (nt_KeWaitForSingleObject(&a->done_ev, 0, 0, 0, &timeout) == NT_STATUS_TIMEOUT && !a->done) {
                kprintf("[STOR] %s: SCSI command 0x%02x was never completed\n", a->drv->service, cdb[0]);
                return 0;
            }
        }
        status = SRB_STATUS(srb->SrbStatus);
        if (got) *got = srb->DataTransferLength;
        if (status != SRB_STATUS_BUSY) break;
        INT64 pause = -10000;                                /* (1 ms: the queue was full) */
        nt_KeDelayExecutionThread(0, 0, &pause);
    }
    return status;
}

static bool scsi_ok(UINT8 status) { return status == SRB_STATUS_SUCCESS || status == SRB_STATUS_DATA_OVERRUN; }

static void put_be32(UINT8 *p, UINT32 v) { p[0] = (UINT8)(v >> 24); p[1] = (UINT8)(v >> 16); p[2] = (UINT8)(v >> 8); p[3] = (UINT8)v; }
static UINT32 be32(const UINT8 *p) { return (UINT32)p[0] << 24 | (UINT32)p[1] << 16 | (UINT32)p[2] << 8 | p[3]; }

/* READ/WRITE of @count blocks at @lba through the bounce buffer */
static bool rw(StorAdapter *a, UINT64 lba, UINT32 count, bool write)
{
    UINT8 cdb[16];
    memset(cdb, 0, sizeof(cdb));
    UINT8 n = 10;
    if (lba + count > 0xFFFFFFFFull) {
        n = 16;
        cdb[0] = write ? SCSIOP_WRITE16 : SCSIOP_READ16;
        put_be32(cdb + 2, (UINT32)(lba >> 32));
        put_be32(cdb + 6, (UINT32)lba);
        put_be32(cdb + 10, count);
    } else {
        cdb[0] = write ? SCSIOP_WRITE : SCSIOP_READ;
        put_be32(cdb + 2, (UINT32)lba);
        cdb[7] = (UINT8)(count >> 8);
        cdb[8] = (UINT8)count;
    }
    UINT8 st = srb_run(a, cdb, n, count * a->block, write, NULL);
    if (!scsi_ok(st))
        kprintf("[STOR] %s: %s of %u blocks at %llu failed (SRB status 0x%02x)\n", a->drv->service,
                write ? "write" : "read", count, (unsigned long long)lba, st);
    return scsi_ok(st);
}

/* -----------------------------------------------------------------------
 * The disk, for NovaOS's block layer
 * ----------------------------------------------------------------------- */
static bool disk_xfer(BlockDev *d, UINT64 lba, UINT32 count, void *buf, bool write)
{
    StorAdapter *a = d->ctx;
    if (d->gone) return false;
    nt_KeWaitForSingleObject(&a->req_lock, 0, 0, 0, NULL);
    bool ok = true;
    UINT8 *p = buf;
    UINT32 per = a->chunk / BLOCK_SECTOR;
    while (ok && count) {
        UINT32 n = count < per ? count : per;
        if (write) memcpy(a->bounce, p, (size_t)n * BLOCK_SECTOR);
        ok = rw(a, lba, n, write);
        if (ok && !write) memcpy(p, a->bounce, (size_t)n * BLOCK_SECTOR);
        lba += n;
        count -= n;
        p += (size_t)n * BLOCK_SECTOR;
    }
    nt_KeSetEvent(&a->req_lock, 0, 0);
    return ok;
}

static bool disk_read(BlockDev *d, UINT64 lba, UINT32 count, void *buf) { return disk_xfer(d, lba, count, buf, false); }
static bool disk_write(BlockDev *d, UINT64 lba, UINT32 count, const void *buf)
{
    return disk_xfer(d, lba, count, (void *)buf, true);
}

static bool disk_flush(BlockDev *d)
{
    StorAdapter *a = d->ctx;
    if (d->gone) return false;
    UINT8 cdb[10] = { SCSIOP_SYNCHRONIZE_CACHE };
    nt_KeWaitForSingleObject(&a->req_lock, 0, 0, 0, NULL);
    bool ok = scsi_ok(srb_run(a, cdb, 10, 0, false, NULL));
    nt_KeSetEvent(&a->req_lock, 0, 0);
    return ok;
}

/* Copy @n bytes of INQUIRY text, trailing spaces trimmed */
static void inquiry_text(char *out, const UINT8 *s, int n)
{
    int len = (int)strlen(out);
    if (len) out[len++] = ' ';
    for (int i = 0; i < n; i++) out[len++] = s[i] >= 0x20 && s[i] < 0x7F ? (char)s[i] : ' ';
    while (len && out[len - 1] == ' ') len--;
    out[len] = 0;
}

/* The disk on LUN 0: what it is and how big, then the block layer and the
 * drives have it (its own thread: the reads wait for its interrupts) */
static void disk_thread(void *arg)
{
    StorAdapter *a = arg;
    UINT8 cdb[16];
    UINT32 got = 0;

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSIOP_INQUIRY;
    cdb[4] = 96;
    memset(a->bounce, 0, 96);
    UINT8 st = srb_run(a, cdb, 6, 96, false, &got);
    if (scsi_ok(st) && (a->bounce[0] & 0x1F) == 0) {             /* DIRECT_ACCESS_DEVICE */
        inquiry_text(a->dev.model, a->bounce + 8, 8);
        inquiry_text(a->dev.model, a->bounce + 16, 16);
        inquiry_text(a->dev.model, a->bounce + 32, 4);
    } else if (scsi_ok(st)) {
        kprintf("[STOR] %s: LUN 0 is not a disk (peripheral type %u)\n", a->drv->service, a->bounce[0] & 0x1F);
        goto out;
    } else {
        kprintf("[STOR] %s: LUN 0 does not answer INQUIRY (SRB status 0x%02x)\n", a->drv->service, st);
        goto out;
    }

    UINT64 blocks = 0;
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSIOP_READ_CAPACITY;
    st = srb_run(a, cdb, 10, 8, false, NULL);
    if (scsi_ok(st)) {
        blocks = (UINT64)be32(a->bounce) + 1;
        a->block = be32(a->bounce + 4);
    }
    if (scsi_ok(st) && blocks == 0x100000000ull) {               /* (past 2 TiB: the 16-byte form) */
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSIOP_SERVICE_ACTION_IN16;
        cdb[1] = SERVICE_ACTION_READ_CAPACITY16;
        cdb[13] = 32;
        st = srb_run(a, cdb, 16, 32, false, NULL);
        if (scsi_ok(st)) {
            blocks = ((UINT64)be32(a->bounce) << 32 | be32(a->bounce + 4)) + 1;
            a->block = be32(a->bounce + 8);
        }
    }
    if (!scsi_ok(st) || !blocks) {
        kprintf("[STOR] %s: LUN 0's capacity cannot be read (SRB status 0x%02x)\n", a->drv->service, st);
        goto out;
    }
    if (a->block != BLOCK_SECTOR) {
        kprintf("[STOR] %s: \"%s\" uses %u-byte blocks, not supported\n", a->drv->service, a->dev.model, a->block);
        goto out;
    }
    a->dev.sectors = blocks;
    ksnprintf(a->dev.name, sizeof(a->dev.name), "stor%d", a->index);
    a->dev.read = disk_read;
    a->dev.write = disk_write;
    a->dev.flush = disk_flush;
    a->dev.ctx = a;
    kprintf("[STOR] %s: disk \"%s\" on LUN 0:0:0, %llu MiB, through Storport\n", a->drv->service, a->dev.model,
            (unsigned long long)(blocks >> 11));
    BlockRegister(&a->dev);
    DrivesAttach(&a->dev);
out:
    sched_exit_current();
}

/* -----------------------------------------------------------------------
 * Starting an adapter
 * ----------------------------------------------------------------------- */
static NT_API unsigned char isr(void *obj, void *ctx)
{
    (void)obj;
    StorAdapter *a = ctx;
    return ((HW_INTERRUPT)a->drv->hw.HwInterrupt)(a->ext);
}

static NT_API unsigned char init_sync(void *ctx)
{
    StorAdapter *a = ctx;
    return ((HW_INITIALIZE)a->drv->hw.HwInitialize)(a->ext);
}

/* PORT_CONFIGURATION_INFORMATION for @a from its translated resources */
static void config_info(StorAdapter *a, NT_CM_RESOURCE_LIST *raw, NT_CM_RESOURCE_LIST *xlat)
{
    STOR_PORT_CONFIGURATION_INFORMATION *ci = &a->ci;
    STOR_HW_INITIALIZATION_DATA *hw = &a->drv->hw;
    memset(ci, 0, sizeof(*ci));
    ci->Length = sizeof(*ci);
    ci->AdapterInterfaceType = hw->AdapterInterfaceType;
    ci->MaximumTransferLength = SP_UNINITIALIZED_VALUE;
    ci->NumberOfPhysicalBreaks = SP_UNINITIALIZED_VALUE;
    ci->DmaChannel = ci->DmaPort = SP_UNINITIALIZED_VALUE;
    ci->DmaChannel2 = ci->DmaPort2 = SP_UNINITIALIZED_VALUE;
    ci->NumberOfAccessRanges = hw->NumberOfAccessRanges;
    ci->AccessRanges = (STOR_ACCESS_RANGE (*)[])a->ranges;
    ci->NumberOfBuses = 1;
    for (int i = 0; i < 8; i++) ci->InitiatorBusId[i] = (char)-1;
    ci->MapBuffers = hw->MapBuffers;
    ci->NeedPhysicalAddresses = hw->NeedPhysicalAddresses;
    ci->TaggedQueuing = hw->TaggedQueuing;
    ci->AutoRequestSense = hw->AutoRequestSense;
    ci->MultipleRequestPerLu = hw->MultipleRequestPerLu;
    ci->MaximumNumberOfTargets = 1;
    ci->MaximumNumberOfLogicalUnits = 1;
    ci->SrbType = (hw->SrbTypeFlags & SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK) ? 1 : 0;
    ci->DeviceExtensionSize = hw->DeviceExtensionSize;
    ci->SpecificLuExtensionSize = hw->SpecificLuExtensionSize;
    ci->SrbExtensionSize = hw->SrbExtensionSize;
    ci->DmaAddressWidth = 64;
    ci->FeatureSupport = hw->FeatureSupport;

    UINT32 addr = 0, result = 0;
    if (NT_SUCCESS_(nt_IoGetDeviceProperty(a->pdo, 16 /* DevicePropertyAddress */, 4, &addr, &result)))
        ci->SlotNumber = (addr >> 16 & 0x1F) | (addr & 7) << 5;   /* PCI_SLOT_NUMBER: device, function */
    if (raw) ci->SystemIoBusNumber = raw->List[0].BusNumber;

    int n = 0;
    NT_CM_PARTIAL_RESOURCE_LIST *pl = xlat ? &xlat->List[0].PartialResourceList : NULL;
    NT_CM_PARTIAL_RESOURCE_LIST *pr = raw ? &raw->List[0].PartialResourceList : NULL;
    for (UINT32 i = 0; pl && i < pl->Count; i++) {
        NT_CM_PARTIAL_RESOURCE_DESCRIPTOR *r = &pl->PartialDescriptors[i];
        if ((r->Type == NT_CmResourceTypeMemory || r->Type == NT_CmResourceTypePort) && n < MAX_RANGES &&
            (UINT32)n < hw->NumberOfAccessRanges) {
            a->ranges[n].RangeStart = r->u.Generic.Start;
            a->ranges[n].RangeLength = r->u.Generic.Length;
            a->ranges[n].RangeInMemory = r->Type == NT_CmResourceTypeMemory;
            n++;
        } else if (r->Type == NT_CmResourceTypeInterrupt && pr && i < pr->Count) {
            ci->BusInterruptLevel = pr->PartialDescriptors[i].u.Interrupt.Level;
            ci->BusInterruptVector = pr->PartialDescriptors[i].u.Interrupt.Vector;
            ci->InterruptMode = (r->Flags & NT_CM_RESOURCE_INTERRUPT_LATCHED) ? 1 : 0;
        }
    }
}

static NT_STATUS adapter_start(StorAdapter *a, NT_CM_RESOURCE_LIST *raw, NT_CM_RESOURCE_LIST *xlat)
{
    StorDriver *d = a->drv;
    STOR_HW_INITIALIZATION_DATA *hw = &d->hw;
    if (a->started) return NT_STATUS_SUCCESS;
    a->ext = kzalloc(hw->DeviceExtensionSize ? hw->DeviceExtensionSize : 16);
    a->srb_mem = contig(4096);
    a->srb_ext = hw->SrbExtensionSize ? contig(hw->SrbExtensionSize) : NULL;
    a->sg = kzalloc(sizeof(STOR_SCATTER_GATHER_LIST) + sizeof(STOR_SCATTER_GATHER_ELEMENT));
    a->bounce = contig(BOUNCE_SIZE);
    if (!a->ext || !a->srb_mem || (hw->SrbExtensionSize && !a->srb_ext) || !a->sg || !a->bounce)
        return NT_STATUS_INSUFFICIENT_RESOURCES;
    a->bounce_pa = nt_MmGetPhysicalAddress(a->bounce);

    config_info(a, raw, xlat);
    unsigned char again = 0;
    kprintf("[STOR] %s: HwFindAdapter\n", d->service);
    UINT32 found = ((HW_FIND_ADAPTER)hw->HwFindAdapter)(a->ext, d->hw_ctx, NULL, NULL, &a->ci, &again);
    if (found != SP_RETURN_FOUND) {
        kprintf("[STOR] %s: HwFindAdapter did not find its adapter (%u)\n", d->service, found);
        return NT_STATUS_UNSUCCESSFUL;
    }
    a->chunk = BOUNCE_SIZE;
    if (a->ci.MaximumTransferLength != SP_UNINITIALIZED_VALUE && a->ci.MaximumTransferLength < a->chunk)
        a->chunk = a->ci.MaximumTransferLength & ~(UINT32)(BLOCK_SECTOR - 1);
    if (a->chunk < BLOCK_SECTOR) a->chunk = BLOCK_SECTOR;

    if (hw->HwInterrupt || a->ci.HwMSInterruptRoutine) {
        NT_IO_CONNECT_INTERRUPT_PARAMETERS p;
        memset(&p, 0, sizeof(p));
        p.Version = NT_CONNECT_LINE_BASED;                   /* (no MSI for Windows drivers yet) */
        p.LineBased.PhysicalDeviceObject = a->pdo;
        p.LineBased.InterruptObject = &a->intr;
        p.LineBased.ServiceRoutine = isr;
        p.LineBased.ServiceContext = a;
        NT_STATUS st = hw->HwInterrupt ? nt_IoConnectInterruptEx(&p) : NT_STATUS_INVALID_PARAMETER;
        if (!NT_SUCCESS_(st)) {
            kprintf("[STOR] %s: its interrupt could not be connected (0x%08x)\n", d->service, (UINT32)st);
            return NT_STATUS_UNSUCCESSFUL;
        }
    }
    kprintf("[STOR] %s: HwInitialize\n", d->service);
    UINT8 ok = a->intr ? nt_KeSynchronizeExecution(a->intr, init_sync, a) : init_sync(a);
    if (!ok) {
        kprintf("[STOR] %s: HwInitialize failed\n", d->service);
        return NT_STATUS_UNSUCCESSFUL;
    }
    if (a->passive && !a->passive(a->ext)) {
        kprintf("[STOR] %s: its passive-level initialization failed\n", d->service);
        return NT_STATUS_UNSUCCESSFUL;
    }
    a->started = true;
    a->index = g_count++;
    d->adapters++;
    kprintf("[STOR] %s: adapter running (%u access ranges, interrupt %s, %u-byte transfers)\n", d->service,
            a->ci.NumberOfAccessRanges, a->intr ? "line-based" : "none", a->chunk);
    if (!sched_create_thread("storport", disk_thread, a, 8))
        kprintf("[STOR] %s: no thread to bring up its disk\n", d->service);
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * The I/O manager's side: AddDevice and the IRPs
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS sync_done(NT_DEVICE_OBJECT *dev, NT_IRP *irp, void *ctx)
{
    (void)dev; (void)irp;
    nt_KeSetEvent(ctx, 0, 0);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;
}

/* Pass @irp down to the PDO and wait for it: its status (the IRP is ours again) */
static NT_STATUS forward_and_wait(StorAdapter *a, NT_IRP *irp)
{
    NT_KEVENT ev;
    nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    NT_IO_STACK_LOCATION *cur = NtIrpCurrentStack(irp), *next = NtIrpNextStack(irp);
    memcpy(next, cur, __builtin_offsetof(NT_IO_STACK_LOCATION, CompletionRoutine));
    next->CompletionRoutine = sync_done;
    next->Context = &ev;
    next->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    if (nt_IofCallDriver(a->lower, irp) == NT_STATUS_PENDING) nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    return irp->IoStatus.Status;
}

static NT_STATUS forward(StorAdapter *a, NT_IRP *irp)
{
    irp->CurrentLocation++;                                  /* IoSkipCurrentIrpStackLocation */
    irp->Tail.Overlay.CurrentStackLocation++;
    return nt_IofCallDriver(a->lower, irp);
}

static NT_STATUS complete(NT_IRP *irp, NT_STATUS st)
{
    irp->IoStatus.Status = st;
    nt_IofCompleteRequest(irp, 0);
    return st;
}

static NT_STATUS adapter_pnp(StorAdapter *a, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE: {
        NT_STATUS st = forward_and_wait(a, irp);
        if (NT_SUCCESS_(st))
            st = adapter_start(a, sl->Parameters.StartDevice.AllocatedResources,
                               sl->Parameters.StartDevice.AllocatedResourcesTranslated);
        return complete(irp, st);
    }
    case NT_IRP_MN_QUERY_REMOVE_DEVICE:
    case NT_IRP_MN_QUERY_STOP_DEVICE:
        if (a->started) {                                    /* (its volumes are in use: not yet) */
            kprintf("[STOR] %s: a running storage adapter cannot be removed yet\n", a->drv->service);
            return complete(irp, NT_STATUS_UNSUCCESSFUL);
        }
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        return forward(a, irp);
    case NT_IRP_MN_REMOVE_DEVICE: {                          /* (only one that never started) */
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        NT_STATUS st = forward(a, irp);
        if (!a->started) {
            for (StorAdapter **pp = &g_adapters; *pp; pp = &(*pp)->next)
                if (*pp == a) { *pp = a->next; break; }
            if (a->intr) nt_IoDisconnectInterrupt(a->intr);
            nt_IoDetachDevice(a->lower);
            nt_IoDeleteDevice(a->fdo);
        }
        return st;
    }
    default:
        return forward(a, irp);
    }
}

static NT_API NT_STATUS dispatch(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    StorAdapter *a = dev->DeviceExtension ? *(StorAdapter **)dev->DeviceExtension : NULL;
    if (!a) return complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST);
    switch (sl->MajorFunction) {
    case NT_IRP_MJ_PNP:            return adapter_pnp(a, irp, sl);
    case NT_IRP_MJ_POWER:
    case NT_IRP_MJ_SYSTEM_CONTROL: return forward(a, irp);
    case NT_IRP_MJ_CREATE:
    case NT_IRP_MJ_CLOSE:
    case NT_IRP_MJ_CLEANUP:        return complete(irp, NT_STATUS_SUCCESS);
    default:                       return complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST);
    }
}

static NT_API NT_STATUS add_device(NT_DRIVER_OBJECT *obj, NT_DEVICE_OBJECT *pdo)
{
    NtPnpLineOnly(pdo);                                   /* (this host connects the line: no MSI-X yet) */
    StorDriver *d = driver_of(obj);
    if (!d) return NT_STATUS_INVALID_DEVICE_REQUEST;
    StorAdapter *a = kzalloc(sizeof(*a));
    if (!a) return NT_STATUS_INSUFFICIENT_RESOURCES;
    NT_DEVICE_OBJECT *fdo;
    NT_STATUS st = nt_IoCreateDevice(obj, sizeof(StorAdapter *), NULL, 0x04 /* FILE_DEVICE_CONTROLLER */, 0, 0, &fdo);
    if (!NT_SUCCESS_(st)) { kfree(a); return st; }
    *(StorAdapter **)fdo->DeviceExtension = a;
    a->drv = d;
    a->fdo = fdo;
    a->pdo = pdo;
    nt_KeInitializeEvent(&a->req_lock, NT_SynchronizationEvent, 1);
    nt_KeInitializeEvent(&a->done_ev, NT_NotificationEvent, 0);
    nt_KeInitializeDpc(&a->done_dpc, done_dpc, a);
    a->lower = nt_IoAttachDeviceToDeviceStack(fdo, pdo);
    if (!a->lower) { nt_IoDeleteDevice(fdo); kfree(a); return NT_STATUS_UNSUCCESSFUL; }
    a->next = g_adapters;
    g_adapters = a;
    fdo->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * The exports
 * ----------------------------------------------------------------------- */
NT_API UINT32 nt_StorPortInitialize(NT_DRIVER_OBJECT *obj, NT_UNICODE_STRING *reg, STOR_HW_INITIALIZATION_DATA *hw,
                                    void *hw_ctx)
{
    (void)reg;
    if (!obj || !hw || hw->HwInitializationDataSize < __builtin_offsetof(STOR_HW_INITIALIZATION_DATA, HwBuildIo) ||
        !hw->HwFindAdapter || !hw->HwInitialize || !hw->HwStartIo)
        return (UINT32)NT_STATUS_INVALID_PARAMETER;
    if (hw->AdapterInterfaceType != NT_PCIBus) return (UINT32)NT_STATUS_NOT_SUPPORTED;
    StorDriver *d = driver_of(obj);
    if (!d) {
        d = kzalloc(sizeof(*d));
        if (!d) return (UINT32)NT_STATUS_INSUFFICIENT_RESOURCES;
        d->obj = obj;
        d->next = g_drivers;
        g_drivers = d;
    }
    memset(&d->hw, 0, sizeof(d->hw));
    memcpy(&d->hw, hw, hw->HwInitializationDataSize < sizeof(d->hw) ? hw->HwInitializationDataSize : sizeof(d->hw));
    d->hw_ctx = hw_ctx;
    NtDriver *nd = NtIoDriverOf(obj);
    strncpy(d->service, nd ? nd->service : "storport", sizeof(d->service) - 1);
    obj->DriverExtension->AddDevice = add_device;
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) obj->MajorFunction[i] = dispatch;
    kprintf("[STOR] %s: Storport miniport registered (%s requests, %u-byte device extension)\n", d->service,
            (hw->SrbTypeFlags & SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK) ? "SRB_EX" : "SCSI_REQUEST_BLOCK",
            hw->DeviceExtensionSize);
    if (!(hw->SrbTypeFlags & SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK))
        kprintf("[STOR] %s: only miniports that take STORAGE_REQUEST_BLOCKs are supported\n", d->service);
    return NT_STATUS_SUCCESS;
}

/* Requests a miniport makes of Storport.  It is variadic in storport.h; the
 * arguments after the device extension are read as the request needs them
 * (the Microsoft x64 convention passes them in the same places). */
NT_API UINT32 nt_StorPortNotification(UINT32 type, void *ext, void *a1, void *a2, void *a3, void *a4)
{
    StorAdapter *a = adapter_of(ext);
    switch (type) {
    case StorRequestComplete:
        if (a && a1 == a->srb_mem) {
            a->done = 1;
            nt_KeInsertQueueDpc(&a->done_dpc, NULL, NULL);       /* (from its interrupt, too) */
        }
        return 0;
    case StorEnablePassiveInitialization:
        if (a) a->passive = (HW_PASSIVE_INITIALIZE)a1;
        if (a2) *(UINT8 *)a2 = a != NULL;
        return 0;
    case StorInitializeDpc: {
        STOR_DPC *dpc = a1;
        nt_KeInitializeDpc(&dpc->Dpc, (NT_KDEFERRED_ROUTINE)a2, ext);   /* (HW_DPC_ROUTINE has KDPC's shape) */
        dpc->Lock = 0;
        return 0;
    }
    case StorIssueDpc: {
        UINT8 queued = nt_KeInsertQueueDpc(&((STOR_DPC *)a1)->Dpc, a2, a3);
        if (a4) *(UINT8 *)a4 = queued;
        return 0;
    }
    case StorAcquireSpinLock: {
        STOR_LOCK_HANDLE *h = a3;
        h->Lock = (int)(UINT64)a1;
        if ((UINT64)a1 == StorInterruptLock && a && a->intr) {
            h->Taken = a->intr;
            h->OldIrql = nt_KeAcquireInterruptSpinLock(a->intr);
        } else {
            NT_KSPIN_LOCK *l = (UINT64)a1 == StorDpcLock && a2 ? &((STOR_DPC *)a2)->Lock
                             : a ? &a->startio_lock : NULL;
            h->Taken = l;
            h->OldIrql = l ? nt_KeAcquireSpinLockRaiseToDpc(l) : nt_KfRaiseIrql(DISPATCH_LEVEL);
        }
        return 0;
    }
    case StorReleaseSpinLock: {
        STOR_LOCK_HANDLE *h = a1;
        if (h->Lock == StorInterruptLock && a && h->Taken == a->intr && a->intr)
            nt_KeReleaseInterruptSpinLock(a->intr, h->OldIrql);
        else if (h->Taken) nt_KeReleaseSpinLock(h->Taken, h->OldIrql);
        else nt_KeLowerIrql(h->OldIrql);
        return 0;
    }
    case StorStateChangeDetected:                            /* (no rescans: one LUN, found once) */
    case StorNextRequest:
    case StorNextLuRequest:
    case StorResetDetected:
    case StorBusChangeDetected:
    case StorTraceNotification:
        return 0;
    default:
        kprintf("[STOR] StorPortNotification %u: not supported\n", type);
        return STOR_STATUS_NOT_IMPLEMENTED;
    }
}

NT_API UINT32 nt_StorPortExtendedFunction(UINT32 fn, void *ext, void *a1, void *a2, void *a3)
{
    switch (fn) {
    case ExtFunctionAllocatePool: {
        void *p = nt_ExAllocatePoolWithTag(0, (UINT64)a1, (UINT32)(UINT64)a2);
        if (p) memset(p, 0, (UINT64)a1);
        if (a3) *(void **)a3 = p;
        return p ? STOR_STATUS_SUCCESS : STOR_STATUS_INSUFFICIENT_RESOURCES;
    }
    case ExtFunctionFreePool:
        if (a1) nt_ExFreePool(a1);
        return STOR_STATUS_SUCCESS;
    case ExtFunctionGetMessageInterruptInformation:          /* (no MSI yet: line-based only) */
        return STOR_STATUS_INVALID_PARAMETER;
    default:
        (void)ext;
        return STOR_STATUS_NOT_IMPLEMENTED;
    }
}

NT_API UINT32 nt_StorPortGetBusData(void *ext, UINT32 type, UINT32 bus, UINT32 slot, void *buf, UINT32 len)
{
    (void)bus; (void)slot;
    StorAdapter *a = adapter_of(ext);
    if (!a || type != 4 /* PCIConfiguration */ || !len) return 0;
    return NtPnpConfig(a->pdo, false, buf, 0, len);
}

NT_API void *nt_StorPortGetDeviceBase(void *ext, int bus_type, UINT32 bus, INT64 addr, UINT32 len, UINT8 io)
{
    (void)ext; (void)bus_type; (void)bus;
    if (io) return (void *)(UINT64)addr;                      /* (x86 port I/O: the port number) */
    return nt_MmMapIoSpace(addr, len, 0 /* MmNonCached */);
}

/* The physical address of @va and how many bytes from there are
 * physically contiguous (to the end of its page, or further) */
NT_API INT64 nt_StorPortGetPhysicalAddress(void *ext, void *srb, void *va, UINT32 *len)
{
    (void)ext; (void)srb;
    UINT64 pa = nt_MmGetPhysicalAddress(va);
    if (len) {
        UINT64 run = 4096 - (pa & 4095), v = (UINT64)va + run;
        while (run < 0x100000 && nt_MmGetPhysicalAddress((void *)v) == pa + run) { run += 4096; v += 4096; }
        *len = (UINT32)run;
    }
    return (INT64)pa;
}

NT_API STOR_SCATTER_GATHER_LIST *nt_StorPortGetScatterGatherList(void *ext, void *srb)
{
    StorAdapter *a = adapter_of(ext);
    return a && srb == a->srb_mem ? a->sg : NULL;
}

/* Memory the adapter shares with the CPU (its rings): asked for once, from
 * HwFindAdapter; physically contiguous */
NT_API void *nt_StorPortGetUncachedExtension(void *ext, STOR_PORT_CONFIGURATION_INFORMATION *ci, UINT32 len)
{
    (void)ci;
    StorAdapter *a = adapter_of(ext);
    if (!a || !len) return NULL;
    if (a->uncached) return len <= a->uncached_len ? a->uncached : NULL;
    a->uncached = contig(len);
    a->uncached_len = a->uncached ? len : 0;
    return a->uncached;
}

NT_API UINT8 nt_StorPortSetDeviceQueueDepth(void *ext, UINT8 path, UINT8 target, UINT8 lun, UINT32 depth)
{
    (void)ext; (void)path; (void)target; (void)lun; (void)depth;
    return 1;
}

NT_API UINT8 nt_StorPortBusy(void *ext, UINT32 requests)
{
    (void)ext; (void)requests;                               /* (the request comes back SRB_STATUS_BUSY: retried) */
    return 1;
}

NT_API void nt_StorPortStallExecution(UINT32 us) { nt_KeStallExecutionProcessor(us); }
