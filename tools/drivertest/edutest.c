/*
 * edutest.sys — a Plug and Play function driver for QEMU's edu device
 * (-device edu, PCI 1234:11e8), NovaOS's test of step 30.3
 *
 * A WDM driver as Windows has them: the PnP manager (kernel/nt/pnp.c)
 * matches its INF (edutest.inf) by hardware ID, calls AddDevice, which
 * makes the function device object, attaches it above the PCI bus's PDO
 * and registers a device interface; IRP_MN_START_DEVICE hands it the
 * device's resources (CM_RESOURCE_LIST, raw and translated), from which it
 * maps the memory BAR (MmMapIoSpace), connects its interrupt
 * (IoConnectInterruptEx, message-based with the line-based fallback, as
 * KMDF asks: with MSISupported set in the device's Interrupt Management
 * key the resources carry an MSI message and the connect returns the
 * message table, otherwise the line), gets a DMA adapter for the device's 28-bit DMA
 * (IoGetDmaAdapter) and turns the interface on.  The ISR runs at the
 * interrupt's IRQL, acknowledges the device and queues a DPC that wakes
 * the waiting request.
 *
 * Its control codes (edutest.h) have the device compute a factorial
 * (interrupt on completion) and move a buffer to the device and back by
 * DMA three ways: GetScatterGatherList, a common buffer, and
 * AllocateAdapterChannel with MapTransfer.  IRP_MN_QUERY_REMOVE_DEVICE is
 * refused while a handle is open; IRP_MN_REMOVE_DEVICE and
 * IRP_MN_SURPRISE_REMOVAL release the hardware, and REMOVE waits on the
 * remove lock, detaches and deletes the device.
 *
 * Built by tools/drivertest/mksys.py, as wdmtest.sys is; the edu self-test
 * installs it with "pnputil /add-driver edutest.inf /install" and drives it
 * with userland/programs/edutest.c.
 */
#include "../../kernel/nt/wdm.h"
#include "edutest.h"

#define IMPORT __declspec(dllimport) NT_API
IMPORT NT_STATUS IoCreateDevice(NT_DRIVER_OBJECT *drv, unsigned int ext_size, NT_UNICODE_STRING *name, unsigned int type,
                                unsigned int characteristics, unsigned char exclusive, NT_DEVICE_OBJECT **out);
IMPORT void      IoDeleteDevice(NT_DEVICE_OBJECT *dev);
IMPORT NT_DEVICE_OBJECT *IoAttachDeviceToDeviceStack(NT_DEVICE_OBJECT *src, NT_DEVICE_OBJECT *target);
IMPORT void      IoDetachDevice(NT_DEVICE_OBJECT *target);
IMPORT NT_STATUS IofCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp);
IMPORT void      IofCompleteRequest(NT_IRP *irp, char boost);
IMPORT NT_STATUS IoRegisterDeviceInterface(NT_DEVICE_OBJECT *pdo, const NT_GUID *guid, NT_UNICODE_STRING *ref,
                                           NT_UNICODE_STRING *link);
IMPORT NT_STATUS IoSetDeviceInterfaceState(NT_UNICODE_STRING *link, unsigned char enable);
IMPORT NT_STATUS IoGetDeviceProperty(NT_DEVICE_OBJECT *pdo, int prop, unsigned int len, void *buf, unsigned int *result);
IMPORT NT_STATUS IoConnectInterruptEx(NT_IO_CONNECT_INTERRUPT_PARAMETERS *p);
IMPORT void      IoDisconnectInterruptEx(NT_IO_DISCONNECT_INTERRUPT_PARAMETERS *p);
IMPORT NT_DMA_ADAPTER *IoGetDmaAdapter(NT_DEVICE_OBJECT *pdo, NT_DEVICE_DESCRIPTION *desc, unsigned int *regs);
IMPORT void     *MmMapIoSpace(long long pa, unsigned long long size, int cache);
IMPORT void      MmUnmapIoSpace(void *va, unsigned long long size);
IMPORT void      IoInitializeRemoveLockEx(NT_IO_REMOVE_LOCK *l, unsigned int tag, unsigned int max_min, unsigned int high,
                                          unsigned int size);
IMPORT NT_STATUS IoAcquireRemoveLockEx(NT_IO_REMOVE_LOCK *l, void *tag, const char *file, unsigned int line,
                                       unsigned int size);
IMPORT void      IoReleaseRemoveLockEx(NT_IO_REMOVE_LOCK *l, void *tag, unsigned int size);
IMPORT void      IoReleaseRemoveLockAndWaitEx(NT_IO_REMOVE_LOCK *l, void *tag, unsigned int size);
IMPORT void      PoStartNextPowerIrp(NT_IRP *irp);
IMPORT NT_STATUS PoCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp);
IMPORT void      RtlFreeUnicodeString(NT_UNICODE_STRING *u);
IMPORT unsigned int DbgPrint(const char *fmt, ...);
IMPORT void      KeInitializeDpc(NT_KDPC *dpc, NT_KDEFERRED_ROUTINE fn, void *ctx);
IMPORT unsigned char KeInsertQueueDpc(NT_KDPC *dpc, void *a1, void *a2);
IMPORT void      KeInitializeEvent(NT_KEVENT *ev, int type, unsigned char state);
IMPORT int       KeSetEvent(NT_KEVENT *ev, int increment, unsigned char wait);
IMPORT NT_STATUS KeWaitForSingleObject(void *obj, int reason, char mode, unsigned char alertable, long long *timeout);
IMPORT void     *ExAllocatePoolWithTag(int type, unsigned long long size, unsigned int tag);
IMPORT void      ExFreePoolWithTag(void *p, unsigned int tag);
IMPORT NT_MDL   *IoAllocateMdl(void *va, unsigned int len, unsigned char secondary, unsigned char quota, NT_IRP *irp);
IMPORT void      IoFreeMdl(NT_MDL *m);
IMPORT void      MmBuildMdlForNonPagedPool(NT_MDL *m);

#define NonPagedPoolNx        512
#define MmNonCached           0
#define TAG_EDU               0x74756445u        /* 'Edut' */
#define FILE_DEVICE_SECURE_OPEN 0x100
#define DevicePropertyLocationInformation 10

/* edu's registers (QEMU's docs/specs/edu.rst) */
#define EDU_IDENT     0x00
#define EDU_LIVENESS  0x04
#define EDU_FACT      0x08
#define EDU_STATUS    0x20                       /* bit 0 computing, bit 7 interrupt when done */
#define EDU_IRQ_STAT  0x24
#define EDU_IRQ_ACK   0x64
#define EDU_DMA_SRC   0x80
#define EDU_DMA_DST   0x88
#define EDU_DMA_COUNT 0x90
#define EDU_DMA_CMD   0x98                       /* bit 0 start, bit 1 device to RAM, bit 2 interrupt when done */
#define EDU_BUFFER    0x40000                    /* the device's own 4 KiB, as its DMA sees it */
#define EDU_FACT_IRQ  0x001
#define EDU_DMA_IRQ   0x100

typedef struct {
    NT_DEVICE_OBJECT *self, *pdo, *lower;
    NT_IO_REMOVE_LOCK lock;
    NT_UNICODE_STRING iface;                     /* the device interface's link (IoRegisterDeviceInterface) */
    volatile unsigned char *regs;                /* the memory BAR, mapped */
    unsigned int      bar_len;
    long long         bar_raw;
    void             *interrupt;                 /* (IoConnectInterruptEx's object) */
    unsigned int      connect_version;
    NT_DMA_ADAPTER   *dma;
    unsigned int      dma_regs;
    NT_KDPC           dpc;
    NT_KEVENT         done;                      /* set by the DPC */
    NT_KEVENT         ready;                     /* an adapter channel or scatter/gather list arrived */
    void             *map_base;
    NT_SCATTER_GATHER_LIST *sg;
    volatile unsigned int isr_status, isr_irql, dpc_irql, interrupts, dma_interrupts, message_interrupts, bad_message_id;
    EDUTEST_INFO      info;
    int               started, removed, opens;
} Ext;

/* (no C runtime in a driver: byte loops, which -fno-builtin keeps as loops) */
static void mcopy(void *d, const void *s, unsigned long long n)
{
    for (unsigned long long i = 0; i < n; i++) ((unsigned char *)d)[i] = ((const unsigned char *)s)[i];
}
static void mzero(void *d, unsigned long long n)
{
    for (unsigned long long i = 0; i < n; i++) ((unsigned char *)d)[i] = 0;
}
static int mdiffer(const void *a, const void *b, unsigned long long n)
{
    for (unsigned long long i = 0; i < n; i++)
        if (((const unsigned char *)a)[i] != ((const unsigned char *)b)[i]) return 1;
    return 0;
}

static inline unsigned char read_cr8(void)
{
    unsigned long long v;
    __asm__ volatile ("mov %%cr8, %0" : "=r"(v));
    return (unsigned char)v;
}

static unsigned int rd(Ext *x, unsigned int reg) { return *(volatile unsigned int *)(x->regs + reg); }
static void wr(Ext *x, unsigned int reg, unsigned int v) { *(volatile unsigned int *)(x->regs + reg) = v; }
static void wr64(Ext *x, unsigned int reg, unsigned long long v) { *(volatile unsigned long long *)(x->regs + reg) = v; }

static NT_STATUS complete(NT_IRP *irp, NT_STATUS st, unsigned long long info)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    IofCompleteRequest(irp, 0);
    return st;
}

/* The WDK's IoSkipCurrentIrpStackLocation, IoCopyCurrentIrpStackLocationToNext
 * and IoSetCompletionRoutine (inline) */
static void skip(NT_IRP *irp)
{
    irp->CurrentLocation++;
    irp->Tail.Overlay.CurrentStackLocation++;
}
static void copy_to_next(NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *cur = NtIrpCurrentStack(irp), *next = NtIrpNextStack(irp);
    mcopy(next, cur, __builtin_offsetof(NT_IO_STACK_LOCATION, CompletionRoutine));
    next->Control = 0;
}

static NT_API NT_STATUS start_done(NT_DEVICE_OBJECT *dev, NT_IRP *irp, void *ctx)
{
    (void)dev; (void)irp;
    KeSetEvent(ctx, 0, 0);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;              /* (the dispatch routine finishes it) */
}

/* Send @irp down and wait for the bus driver to finish it (as a START must
 * reach the PDO before the function driver touches the hardware) */
static NT_STATUS forward_and_wait(Ext *x, NT_IRP *irp)
{
    NT_KEVENT ev;
    KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    copy_to_next(irp);
    NT_IO_STACK_LOCATION *next = NtIrpNextStack(irp);
    next->CompletionRoutine = start_done;
    next->Context = &ev;
    next->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    if (IofCallDriver(x->lower, irp) == NT_STATUS_PENDING) KeWaitForSingleObject(&ev, 0, 0, 0, 0);
    return irp->IoStatus.Status;
}

/* ---- the interrupt ---- */

static NT_API unsigned char isr(void *interrupt, void *ctx)
{
    (void)interrupt;
    Ext *x = ctx;
    unsigned int status = rd(x, EDU_IRQ_STAT);
    if (!status) return 0;                                   /* (not ours: the line may be shared) */
    wr(x, EDU_IRQ_ACK, status);
    x->isr_status = status;
    x->isr_irql = read_cr8();
    x->interrupts++;
    if (status & EDU_DMA_IRQ) x->dma_interrupts++;
    KeInsertQueueDpc(&x->dpc, 0, 0);
    return 1;
}

/* The same interrupt as a message: MessageId says which of the device's messages (edu has one) */
static NT_API unsigned char message_isr(void *interrupt, void *ctx, unsigned int message_id)
{
    Ext *x = ctx;
    unsigned char mine = isr(interrupt, ctx);
    if (mine) x->message_interrupts++;
    if (message_id) x->bad_message_id++;
    return mine;
}

static NT_API void dpc(NT_KDPC *d, void *ctx, void *a1, void *a2)
{
    (void)d; (void)a1; (void)a2;
    Ext *x = ctx;
    x->dpc_irql = read_cr8();
    KeSetEvent(&x->done, 0, 0);
}

/* Wait (2 s at most) for the next interrupt's DPC */
static int wait_interrupt(Ext *x)
{
    long long timeout = -20000000;                           /* (100 ns units, relative) */
    return KeWaitForSingleObject(&x->done, 0, 0, 0, &timeout) == NT_STATUS_SUCCESS;
}

/* ---- starting and stopping the hardware ---- */

static NT_STATUS start_hardware(Ext *x, NT_CM_RESOURCE_LIST *raw, NT_CM_RESOURCE_LIST *xlat)
{
    EDUTEST_INFO *in = &x->info;
    if (!raw || !xlat || !xlat->Count) return NT_STATUS_DEVICE_NOT_READY;
    NT_CM_PARTIAL_RESOURCE_LIST *pl = &xlat->List[0].PartialResourceList, *rl = &raw->List[0].PartialResourceList;
    long long bar = 0;
    for (unsigned int i = 0; i < pl->Count; i++) {
        NT_CM_PARTIAL_RESOURCE_DESCRIPTOR *d = &pl->PartialDescriptors[i], *r = i < rl->Count ? &rl->PartialDescriptors[i] : d;
        switch (d->Type) {
        case NT_CmResourceTypeMemory:
            if (!in->memory++) { bar = d->u.Memory.Start; x->bar_len = d->u.Memory.Length; x->bar_raw = r->u.Memory.Start; }
            break;
        case NT_CmResourceTypePort: in->ports++; break;
        case NT_CmResourceTypeInterrupt:
            in->interrupts++;
            in->vector = d->u.Interrupt.Vector;
            in->irql = d->u.Interrupt.Level;
            in->raw_level = r->u.Interrupt.Level;
            in->latched = (d->Flags & NT_CM_RESOURCE_INTERRUPT_LATCHED) != 0;
            if (d->Flags & NT_CM_RESOURCE_INTERRUPT_MESSAGE) {
                in->message = 1;
                in->vector = d->u.MessageInterrupt.Translated.Vector;
                in->irql = d->u.MessageInterrupt.Translated.Level;
                in->message_count = r->u.MessageInterrupt.Raw.MessageCount;
            }
            break;
        }
    }
    if (!bar || !in->interrupts) return NT_STATUS_DEVICE_NOT_READY;
    in->bar_length = x->bar_len;
    in->bar_raw = x->bar_raw;
    x->regs = MmMapIoSpace(bar, x->bar_len, MmNonCached);
    if (!x->regs) return NT_STATUS_INSUFFICIENT_RESOURCES;
    in->ident = rd(x, EDU_IDENT);
    wr(x, EDU_LIVENESS, 0x12345678);
    in->liveness = rd(x, EDU_LIVENESS);
    DbgPrint("edutest: BAR at %llx (%u bytes), ident %08x", bar, x->bar_len, in->ident);

    NT_IO_CONNECT_INTERRUPT_PARAMETERS p;
    mzero(&p, sizeof(p));
    p.Version = NT_CONNECT_MESSAGE_BASED;                    /* (the line-based fallback when MSI is off) */
    p.MessageBased.PhysicalDeviceObject = x->pdo;
    p.MessageBased.ConnectionContext = &x->interrupt;
    p.MessageBased.MessageServiceRoutine = message_isr;
    p.MessageBased.ServiceContext = x;
    p.MessageBased.FallBackServiceRoutine = isr;
    NT_STATUS st = IoConnectInterruptEx(&p);
    if (!NT_SUCCESS_(st)) {
        DbgPrint("edutest: IoConnectInterruptEx failed (%08x)", st);
        MmUnmapIoSpace((void *)x->regs, x->bar_len);
        x->regs = 0;
        return st;
    }
    x->connect_version = in->connect_version = p.Version;
    if (p.Version == NT_CONNECT_MESSAGE_BASED && x->interrupt) {
        NT_IO_INTERRUPT_MESSAGE_INFO *t = x->interrupt;
        in->message_irql = t->UnifiedIrql;
        in->message_vector = t->MessageInfo[0].Vector;
        in->message_data = t->MessageInfo[0].MessageData;
        in->message_address = t->MessageInfo[0].MessageAddress;
        if (t->MessageCount != in->message_count) in->message_count = 0xFFFF;        /* (disagrees with the raw list) */
    }

    NT_DEVICE_DESCRIPTION dd;
    mzero(&dd, sizeof(dd));
    dd.Version = 3;
    dd.Master = 1;
    dd.ScatterGather = 1;
    dd.Dma32BitAddresses = 1;
    dd.InterfaceType = NT_PCIBus;
    dd.MaximumLength = 4096;
    dd.DmaAddressWidth = 28;                                 /* (edu's DMA mask) */
    x->dma = IoGetDmaAdapter(x->pdo, &dd, &x->dma_regs);
    if (!x->dma) DbgPrint("edutest: no DMA adapter");
    in->dma_registers = x->dma_regs;
    in->dma_version = x->dma ? x->dma->Version : 0;

    unsigned int got = 0;
    IoGetDeviceProperty(x->pdo, DevicePropertyLocationInformation, sizeof(in->location), in->location, &got);
    in->starts++;
    x->started = 1;
    IoSetDeviceInterfaceState(&x->iface, 1);
    DbgPrint("edutest: started, interrupt vector %u at IRQL %u, %u map registers", in->vector, in->irql, x->dma_regs);
    return NT_STATUS_SUCCESS;
}

static void stop_hardware(Ext *x)
{
    if (!x->started) return;
    x->started = 0;
    IoSetDeviceInterfaceState(&x->iface, 0);
    NT_IO_DISCONNECT_INTERRUPT_PARAMETERS d = { x->connect_version, x->interrupt };
    IoDisconnectInterruptEx(&d);
    if (x->dma) x->dma->DmaOperations->PutDmaAdapter(x->dma);
    x->dma = 0;
    MmUnmapIoSpace((void *)x->regs, x->bar_len);
    x->regs = 0;
    DbgPrint("edutest: hardware released");
}

/* ---- IRP_MJ_PNP ---- */

static NT_API NT_STATUS pnp(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    NT_STATUS st = IoAcquireRemoveLockEx(&x->lock, irp, "edutest.c", __LINE__, sizeof(x->lock));
    if (!NT_SUCCESS_(st)) return complete(irp, st, 0);
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE:
        st = forward_and_wait(x, irp);
        if (NT_SUCCESS_(st))
            st = start_hardware(x, sl->Parameters.StartDevice.AllocatedResources,
                                sl->Parameters.StartDevice.AllocatedResourcesTranslated);
        complete(irp, st, 0);
        IoReleaseRemoveLockEx(&x->lock, irp, sizeof(x->lock));
        return st;
    case NT_IRP_MN_QUERY_CAPABILITIES:
        st = forward_and_wait(x, irp);
        if (NT_SUCCESS_(st))
            x->info.capabilities_address = ((NT_DEVICE_CAPABILITIES *)sl->Parameters.Others.Argument1)->Address;
        complete(irp, st, irp->IoStatus.Information);
        IoReleaseRemoveLockEx(&x->lock, irp, sizeof(x->lock));
        return st;
    case NT_IRP_MN_QUERY_REMOVE_DEVICE:
        if (x->opens) {                                      /* (a program has it open: veto) */
            DbgPrint("edutest: removal vetoed, %d handle(s) open", x->opens);
            complete(irp, NT_STATUS_UNSUCCESSFUL, 0);
            IoReleaseRemoveLockEx(&x->lock, irp, sizeof(x->lock));
            return NT_STATUS_UNSUCCESSFUL;
        }
        DbgPrint("edutest: query-remove accepted");
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_CANCEL_REMOVE_DEVICE:
    case NT_IRP_MN_QUERY_STOP_DEVICE:
    case NT_IRP_MN_CANCEL_STOP_DEVICE:
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_STOP_DEVICE:
        stop_hardware(x);
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_SURPRISE_REMOVAL:
        DbgPrint("edutest: surprise removal");
        x->removed = 1;
        stop_hardware(x);
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        break;
    case NT_IRP_MN_REMOVE_DEVICE:
        x->removed = 1;
        stop_hardware(x);
        IoReleaseRemoveLockAndWaitEx(&x->lock, irp, sizeof(x->lock));
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        skip(irp);
        st = IofCallDriver(x->lower, irp);
        IoDetachDevice(x->lower);
        RtlFreeUnicodeString(&x->iface);
        DbgPrint("edutest: removed");
        IoDeleteDevice(dev);
        return st;
    }
    skip(irp);
    st = IofCallDriver(x->lower, irp);
    IoReleaseRemoveLockEx(&x->lock, irp, sizeof(x->lock));
    return st;
}

static NT_API NT_STATUS power(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    PoStartNextPowerIrp(irp);
    skip(irp);
    return PoCallDriver(x->lower, irp);
}

static NT_API NT_STATUS pass_down(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    skip(irp);
    return IofCallDriver(x->lower, irp);
}

/* ---- programs' requests ---- */

static NT_API NT_STATUS create_close(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    if (sl->MajorFunction == NT_IRP_MJ_CREATE) {
        if (x->removed || !x->started) return complete(irp, NT_STATUS_DELETE_PENDING, 0);
        x->opens++;
        x->info.opens++;
    } else if (sl->MajorFunction == NT_IRP_MJ_CLOSE) {
        x->opens--;
    }
    return complete(irp, NT_STATUS_SUCCESS, 0);
}

static NT_STATUS factorial(Ext *x, unsigned int n, EDUTEST_FACT *out)
{
    wr(x, EDU_STATUS, 0x80);                                 /* (interrupt when done) */
    wr(x, EDU_FACT, n);
    if (!wait_interrupt(x)) return NT_STATUS_TIMEOUT;
    out->value = rd(x, EDU_FACT);
    out->isr_status = x->isr_status;
    out->isr_irql = x->isr_irql;
    out->dpc_irql = x->dpc_irql;
    out->interrupts = x->interrupts;
    out->message_interrupts = x->message_interrupts;
    return NT_STATUS_SUCCESS;
}

/* One DMA transfer of @len bytes between @addr and the device's buffer at
 * @off, waiting for its interrupt */
static int dma_run(Ext *x, long long addr, unsigned int off, unsigned int len, int to_ram)
{
    wr64(x, to_ram ? EDU_DMA_SRC : EDU_DMA_DST, EDU_BUFFER + off);
    wr64(x, to_ram ? EDU_DMA_DST : EDU_DMA_SRC, (unsigned long long)addr);
    wr64(x, EDU_DMA_COUNT, len);
    wr64(x, EDU_DMA_CMD, 1 | (to_ram ? 2 : 0) | 4);
    return wait_interrupt(x) && (x->isr_status & EDU_DMA_IRQ);
}

static NT_API void sg_ready(NT_DEVICE_OBJECT *dev, NT_IRP *irp, NT_SCATTER_GATHER_LIST *sg, void *ctx)
{
    (void)dev; (void)irp;
    Ext *x = ctx;
    x->sg = sg;
    KeSetEvent(&x->ready, 0, 0);
}

static NT_API int channel_ready(NT_DEVICE_OBJECT *dev, NT_IRP *irp, void *base, void *ctx)
{
    (void)dev; (void)irp;
    Ext *x = ctx;
    x->map_base = base;
    KeSetEvent(&x->ready, 0, 0);
    return NT_DeallocateObjectKeepRegisters;                 /* (freed by FreeMapRegisters) */
}

static NT_STATUS dma(Ext *x, const unsigned char *in, unsigned int len, EDUTEST_DMA *r, unsigned char *out)
{
    if (!x->dma) return NT_STATUS_DEVICE_NOT_READY;
    NT_DMA_OPERATIONS *ops = x->dma->DmaOperations;
    long long zero = 0;
    unsigned char *src = ExAllocatePoolWithTag(NonPagedPoolNx, len, TAG_EDU);
    unsigned char *dst = ExAllocatePoolWithTag(NonPagedPoolNx, len, TAG_EDU);
    NT_MDL *m1 = src ? IoAllocateMdl(src, len, 0, 0, 0) : 0, *m2 = dst ? IoAllocateMdl(dst, len, 0, 0, 0) : 0;
    NT_STATUS st = NT_STATUS_INSUFFICIENT_RESOURCES;
    if (!m1 || !m2) { DbgPrint("edutest: dma: no buffers or MDLs"); goto out; }
    mcopy(src, in, len);
    mzero(dst, len);
    MmBuildMdlForNonPagedPool(m1);
    MmBuildMdlForNonPagedPool(m2);
    st = NT_STATUS_UNSUCCESSFUL;

    /* 1. to the device, through GetScatterGatherList's list */
    x->sg = 0;
    NT_STATUS sgst = ops->GetScatterGatherList(x->dma, x->self, m1, src, len, sg_ready, x, 1);
    KeWaitForSingleObject(&x->ready, 0, 0, 0, &zero);
    if (!NT_SUCCESS_(sgst) || !x->sg) { DbgPrint("edutest: dma: GetScatterGatherList failed (%08x)", sgst); goto out; }
    r->sg_elements = x->sg->NumberOfElements;
    r->sg_address = x->sg->Elements[0].Address;
    unsigned int off = 0;
    int ok = 1;
    for (unsigned int i = 0; i < x->sg->NumberOfElements && ok; i++) {
        ok = dma_run(x, x->sg->Elements[i].Address, off, x->sg->Elements[i].Length, 0);
        off += x->sg->Elements[i].Length;
    }
    ops->PutScatterGatherList(x->dma, x->sg, 1);
    if (!ok || off != len) { DbgPrint("edutest: dma: the transfer to the device failed (%u of %u bytes)", off, len); goto out; }

    /* 2. back into a common buffer */
    long long logical = 0;
    unsigned char *common = ops->AllocateCommonBuffer(x->dma, 4096, &logical, 0);
    if (common) {
        r->common_logical = logical;
        if (dma_run(x, logical, 0, len, 1)) r->common_ok = !mdiffer(common, in, len);
        ops->FreeCommonBuffer(x->dma, 4096, logical, common, 0);
    }

    /* 3. back through map registers (AllocateAdapterChannel, MapTransfer) */
    unsigned int pages = (unsigned int)((((unsigned long long)dst & 4095) + len + 4095) / 4096);
    x->map_base = 0;
    if (NT_SUCCESS_(ops->AllocateAdapterChannel(x->dma, x->self, pages, channel_ready, x))) {
        KeWaitForSingleObject(&x->ready, 0, 0, 0, &zero);
        ok = 1;
        for (off = 0; off < len && ok;) {
            unsigned int n = len - off;
            long long at = ops->MapTransfer(x->dma, m2, x->map_base, dst + off, &n, 0);
            if (!off) r->map_address = at;
            ok = n && dma_run(x, at, off, n, 1);
            ops->FlushAdapterBuffers(x->dma, m2, x->map_base, dst + off, n, 0);
            off += n;
        }
        ops->FreeMapRegisters(x->dma, x->map_base, pages);
        r->map_ok = ok && !mdiffer(dst, in, len);
    }
    mcopy(out, dst, len);
    r->interrupts = x->dma_interrupts;
    st = NT_STATUS_SUCCESS;
out:
    if (m1) IoFreeMdl(m1);
    if (m2) IoFreeMdl(m2);
    if (src) ExFreePoolWithTag(src, TAG_EDU);
    if (dst) ExFreePoolWithTag(dst, TAG_EDU);
    return st;
}

static NT_API NT_STATUS control(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    unsigned int in_len = sl->Parameters.DeviceIoControl.InputBufferLength;
    unsigned int out_len = sl->Parameters.DeviceIoControl.OutputBufferLength;
    unsigned char *buf = irp->AssociatedIrp.SystemBuffer;
    NT_STATUS st = IoAcquireRemoveLockEx(&x->lock, irp, "edutest.c", __LINE__, sizeof(x->lock));
    if (!NT_SUCCESS_(st)) return complete(irp, st, 0);
    unsigned long long info = 0;
    if (!x->started) st = NT_STATUS_DEVICE_NOT_READY;
    else switch (sl->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_EDUTEST_INFO:
        if (out_len < sizeof(EDUTEST_INFO)) { st = NT_STATUS_BUFFER_TOO_SMALL; break; }
        mcopy(buf, &x->info, sizeof(EDUTEST_INFO));
        info = sizeof(EDUTEST_INFO);
        break;
    case IOCTL_EDUTEST_FACTORIAL: {
        if (in_len < 4 || out_len < sizeof(EDUTEST_FACT)) { st = NT_STATUS_BUFFER_TOO_SMALL; break; }
        EDUTEST_FACT f;
        mzero(&f, sizeof(f));
        st = factorial(x, *(unsigned int *)buf, &f);
        mcopy(buf, &f, sizeof(f));
        info = NT_SUCCESS_(st) ? sizeof(f) : 0;
        break;
    }
    case IOCTL_EDUTEST_DMA: {
        if (!in_len || in_len > 4096) { st = NT_STATUS_INVALID_PARAMETER; break; }
        if (out_len < sizeof(EDUTEST_DMA) + in_len) { st = NT_STATUS_BUFFER_TOO_SMALL; break; }
        static unsigned char data[4096];                     /* (requests come one at a time) */
        EDUTEST_DMA r;
        mzero(&r, sizeof(r));
        mcopy(data, buf, in_len);
        st = dma(x, data, in_len, &r, buf + sizeof(r));
        mcopy(buf, &r, sizeof(r));
        info = NT_SUCCESS_(st) ? sizeof(r) + in_len : 0;
        break;
    }
    default:
        st = NT_STATUS_INVALID_DEVICE_REQUEST;
    }
    complete(irp, st, info);
    IoReleaseRemoveLockEx(&x->lock, irp, sizeof(x->lock));
    return st;
}

/* ---- the driver ---- */

static NT_API NT_STATUS add_device(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *pdo)
{
    static const NT_GUID guid = EDUTEST_GUID_INIT;
    NT_DEVICE_OBJECT *fdo;
    NT_STATUS st = IoCreateDevice(drv, sizeof(Ext), 0, NT_FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, 0, &fdo);
    if (!NT_SUCCESS_(st)) return st;
    Ext *x = fdo->DeviceExtension;
    mzero(x, sizeof(*x));
    x->self = fdo;
    x->pdo = pdo;
    IoInitializeRemoveLockEx(&x->lock, TAG_EDU, 0, 0, sizeof(x->lock));
    KeInitializeDpc(&x->dpc, dpc, x);
    KeInitializeEvent(&x->done, NT_SynchronizationEvent, 0);
    KeInitializeEvent(&x->ready, NT_SynchronizationEvent, 0);
    st = IoRegisterDeviceInterface(pdo, &guid, 0, &x->iface);
    if (!NT_SUCCESS_(st)) { IoDeleteDevice(fdo); return st; }
    x->lower = IoAttachDeviceToDeviceStack(fdo, pdo);
    if (!x->lower) { RtlFreeUnicodeString(&x->iface); IoDeleteDevice(fdo); return NT_STATUS_DEVICE_NOT_READY; }
    fdo->Flags |= NT_DO_BUFFERED_IO | NT_DO_POWER_PAGABLE;
    fdo->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    DbgPrint("edutest: AddDevice, interface %wZ", &x->iface);
    return NT_STATUS_SUCCESS;
}

static NT_API void unload(NT_DRIVER_OBJECT *drv)
{
    (void)drv;
    DbgPrint("edutest: unloaded");
}

NT_API NT_STATUS DriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *registry_path)
{
    DbgPrint("edutest: DriverEntry %wZ", registry_path);
    drv->DriverExtension->AddDevice = add_device;
    drv->MajorFunction[NT_IRP_MJ_PNP] = pnp;
    drv->MajorFunction[NT_IRP_MJ_POWER] = power;
    drv->MajorFunction[NT_IRP_MJ_SYSTEM_CONTROL] = pass_down;
    drv->MajorFunction[NT_IRP_MJ_CREATE] = create_close;
    drv->MajorFunction[NT_IRP_MJ_CLOSE] = create_close;
    drv->MajorFunction[NT_IRP_MJ_CLEANUP] = create_close;
    drv->MajorFunction[NT_IRP_MJ_DEVICE_CONTROL] = control;
    drv->DriverUnload = unload;
    return NT_STATUS_SUCCESS;
}
