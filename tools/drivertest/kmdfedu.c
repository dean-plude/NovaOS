/*
 * kmdfedu.sys — a KMDF function driver for QEMU's edu device (-device edu,
 * PCI 1234:11e8), NovaOS's test of step 30.4 on real hardware resources
 *
 * The KMDF counterpart of edutest.sys (tools/drivertest/edutest.c), written
 * as the WDK's PCI samples (PLX9x5x, the KMDF DMA samples) write one, with
 * the framework doing the Plug and Play and power work the WDM driver does
 * by hand:
 *
 *   EvtDriverDeviceAdd       a device with a typed context, file-object
 *                            callbacks (to know when a handle is open), the
 *                            interface edutest.h names, a default queue for
 *                            device controls at PASSIVE_LEVEL and an
 *                            interrupt object (WdfInterruptCreate: the
 *                            framework connects it at D0 from the resources,
 *                            a message when MSISupported gave the device an
 *                            MSI message, else its line)
 *   EvtDevicePrepareHardware the memory BAR from the translated resource list
 *                            (MmMapIoSpace) and two DMA enablers for edu's
 *                            28-bit DMA: scatter/gather and packet
 *   EvtDeviceQueryRemove     refused while a handle is open
 *   EvtDeviceReleaseHardware unmaps the BAR
 *
 * The ISR (EvtInterruptIsr) acknowledges the device and queues the
 * interrupt's DPC (WdfInterruptQueueDpcForIsr), which wakes the waiting
 * request.  A DMA request moves the buffer to the device with a
 * scatter/gather DMA transaction (EvtProgramDma gets the list), back into a
 * common buffer (WdfCommonBufferCreate), and back again through a packet
 * transaction (map registers).  The control codes and their answers are
 * edutest.sys's, so the same program, edutest.exe, drives either driver.
 *
 * Built by tools/kmdf/build_kmdf.py against the KMDF 1.33 headers and the
 * client stub, as kmdftest.sys is; its package (kmdfedu.inf and a
 * test-signed catalog) is installed by the kmdfedu self-test with
 * "pnputil /add-driver kmdfedu.inf /install".
 */
#include <ntddk.h>
#include <wdf.h>
#include "edutest.h"

/* edu's registers (QEMU's docs/specs/edu.rst) */
#define EDU_IDENT     0x00
#define EDU_LIVENESS  0x04
#define EDU_FACT      0x08
#define EDU_STATUS    0x20                       /* bit 7: interrupt when the factorial is done */
#define EDU_IRQ_STAT  0x24
#define EDU_IRQ_ACK   0x64
#define EDU_DMA_SRC   0x80
#define EDU_DMA_DST   0x88
#define EDU_DMA_COUNT 0x90
#define EDU_DMA_CMD   0x98                       /* bit 0 start, bit 1 device to RAM, bit 2 interrupt when done */
#define EDU_BUFFER    0x40000                    /* the device's own 4 KiB, as its DMA sees it */
#define EDU_DMA_IRQ   0x100
#define EDU_DMA_WIDTH 28                         /* edu's DMA mask */
#define TAG_EDU       'udEK'

typedef struct {
    volatile UCHAR   *regs;                      /* the memory BAR, mapped */
    ULONG             bar_len;
    WDFINTERRUPT      interrupt;
    WDFDMAENABLER     sg, packet;                /* scatter/gather (to the device) and packet (map registers) */
    KEVENT            done;                      /* set by the interrupt's DPC */
    KEVENT            programmed;                /* EvtProgramDma got its list */
    PSCATTER_GATHER_LIST list;
    volatile ULONG    isr_status, isr_irql, dpc_irql, interrupts, dma_interrupts, message_interrupts;
    volatile LONG     opens;
    EDUTEST_INFO      info;
} EDU_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(EDU_CONTEXT, EduContext)

static ULONG rd(EDU_CONTEXT *x, ULONG reg) { return READ_REGISTER_ULONG((volatile ULONG *)(x->regs + reg)); }
static VOID wr(EDU_CONTEXT *x, ULONG reg, ULONG v) { WRITE_REGISTER_ULONG((volatile ULONG *)(x->regs + reg), v); }
static VOID wr64(EDU_CONTEXT *x, ULONG reg, ULONG64 v) { *(volatile ULONG64 *)(x->regs + reg) = v; }

/* ---- the interrupt ---- */

static BOOLEAN EvtInterruptIsr(WDFINTERRUPT interrupt, ULONG message_id)
{
    EDU_CONTEXT *x = EduContext(WdfInterruptGetDevice(interrupt));
    ULONG status = rd(x, EDU_IRQ_STAT);
    if (!status) return FALSE;                   /* (not ours: the line may be shared) */
    wr(x, EDU_IRQ_ACK, status);
    x->isr_status = status;
    x->isr_irql = KeGetCurrentIrql();
    x->interrupts++;
    if (status & EDU_DMA_IRQ) x->dma_interrupts++;
    if (x->info.wdf_message && message_id == 0) x->message_interrupts++;
    WdfInterruptQueueDpcForIsr(interrupt);
    return TRUE;
}

static VOID EvtInterruptDpc(WDFINTERRUPT interrupt, WDFOBJECT device)
{
    UNREFERENCED_PARAMETER(interrupt);
    EDU_CONTEXT *x = EduContext(device);
    x->dpc_irql = KeGetCurrentIrql();
    KeSetEvent(&x->done, IO_NO_INCREMENT, FALSE);
}

/* Wait (2 s at most) for the next interrupt's DPC */
static BOOLEAN wait_interrupt(EDU_CONTEXT *x)
{
    LARGE_INTEGER timeout;
    timeout.QuadPart = -20000000;                /* (100 ns units, relative) */
    return KeWaitForSingleObject(&x->done, Executive, KernelMode, FALSE, &timeout) == STATUS_SUCCESS;
}

/* ---- Plug and Play and power ---- */

static NTSTATUS EvtDevicePrepareHardware(WDFDEVICE device, WDFCMRESLIST raw, WDFCMRESLIST xlat)
{
    EDU_CONTEXT *x = EduContext(device);
    EDUTEST_INFO *in = &x->info;
    PHYSICAL_ADDRESS bar = { 0 };
    ULONG n = WdfCmResourceListGetCount(xlat), i;

    in->memory = in->ports = in->interrupts = in->message = 0;
    for (i = 0; i < n; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d = WdfCmResourceListGetDescriptor(xlat, i);
        PCM_PARTIAL_RESOURCE_DESCRIPTOR r = WdfCmResourceListGetDescriptor(raw, i);
        if (!d) continue;
        if (!r) r = d;
        switch (d->Type) {
        case CmResourceTypeMemory:
            if (!in->memory++) {
                bar = d->u.Memory.Start;
                x->bar_len = d->u.Memory.Length;
                in->bar_raw = r->u.Memory.Start.QuadPart;
            }
            break;
        case CmResourceTypePort:
            in->ports++;
            break;
        case CmResourceTypeInterrupt:
            in->interrupts++;
            in->vector = d->u.Interrupt.Vector;
            in->irql = (ULONG)d->u.Interrupt.Level;
            in->raw_level = r->u.Interrupt.Level;
            in->latched = (d->Flags & CM_RESOURCE_INTERRUPT_LATCHED) != 0;
            if (d->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) {
                in->message = 1;
                in->vector = d->u.MessageInterrupt.Translated.Vector;
                in->irql = d->u.MessageInterrupt.Translated.Level;
                in->message_count = r->u.MessageInterrupt.Raw.MessageCount;
            }
            break;
        }
    }
    if (!bar.QuadPart || !in->interrupts) return STATUS_DEVICE_CONFIGURATION_ERROR;
    in->bar_length = x->bar_len;
    x->regs = MmMapIoSpace(bar, x->bar_len, MmNonCached);
    if (!x->regs) return STATUS_INSUFFICIENT_RESOURCES;
    in->ident = rd(x, EDU_IDENT);
    wr(x, EDU_LIVENESS, 0x12345678);
    in->liveness = rd(x, EDU_LIVENESS);

    /* (once per device: the enablers live as long as it does) */
    if (!x->sg) {
        WDF_DMA_ENABLER_CONFIG cfg;
        NTSTATUS st;
        WDF_DMA_ENABLER_CONFIG_INIT(&cfg, WdfDmaProfileScatterGather, 4096);
        cfg.AddressWidthOverride = EDU_DMA_WIDTH;
        st = WdfDmaEnablerCreate(device, &cfg, WDF_NO_OBJECT_ATTRIBUTES, &x->sg);
        if (NT_SUCCESS(st)) {
            WDF_DMA_ENABLER_CONFIG_INIT(&cfg, WdfDmaProfilePacket, 4096);
            cfg.AddressWidthOverride = EDU_DMA_WIDTH;
            st = WdfDmaEnablerCreate(device, &cfg, WDF_NO_OBJECT_ATTRIBUTES, &x->packet);
        }
        if (!NT_SUCCESS(st)) {
            DbgPrint("kmdfedu: WdfDmaEnablerCreate failed (%08x)\n", st);
            MmUnmapIoSpace((PVOID)x->regs, x->bar_len);
            x->regs = NULL;
            return st;
        }
    }
    {
        PDMA_ADAPTER a = WdfDmaEnablerWdmGetDmaAdapter(x->packet, WdfDmaDirectionReadFromDevice);
        in->dma_version = a ? a->Version : 0;
        /* (the framework keeps the map registers' count to itself: the
         * fragment it maps at once is that many pages) */
        in->dma_registers = (ULONG)(WdfDmaEnablerGetFragmentLength(x->packet, WdfDmaDirectionReadFromDevice) / PAGE_SIZE);
    }
    {
        ULONG got = 0;
        WdfDeviceQueryProperty(device, DevicePropertyLocationInformation, sizeof in->location, in->location, &got);
    }
    in->starts++;
    DbgPrint("kmdfedu: EvtDevicePrepareHardware: BAR at %llx (%u bytes), ident %08x, %u resources\n",
             bar.QuadPart, x->bar_len, in->ident, n);
    return STATUS_SUCCESS;
}

static NTSTATUS EvtDeviceReleaseHardware(WDFDEVICE device, WDFCMRESLIST xlat)
{
    EDU_CONTEXT *x = EduContext(device);
    UNREFERENCED_PARAMETER(xlat);
    if (x->regs) MmUnmapIoSpace((PVOID)x->regs, x->bar_len);
    x->regs = NULL;
    DbgPrint("kmdfedu: hardware released\n");
    return STATUS_SUCCESS;
}

static NTSTATUS EvtDeviceD0EntryPostInterruptsEnabled(WDFDEVICE device, WDF_POWER_DEVICE_STATE previous)
{
    EDU_CONTEXT *x = EduContext(device);
    WDF_INTERRUPT_INFO ii;
    WDF_INTERRUPT_INFO_INIT(&ii);
    WdfInterruptGetInfo(x->interrupt, &ii);
    x->info.wdf_message = ii.MessageSignaled;
    x->info.message_vector = ii.Vector;
    x->info.message_irql = ii.Irql;
    DbgPrint("kmdfedu: started from D%d, %s interrupt vector %u at IRQL %u\n", (int)previous - 1,
             ii.MessageSignaled ? "message" : "line", ii.Vector, ii.Irql);
    return STATUS_SUCCESS;
}

static NTSTATUS EvtDeviceQueryRemove(WDFDEVICE device)
{
    EDU_CONTEXT *x = EduContext(device);
    if (x->opens) {                              /* (a program has it open: veto) */
        DbgPrint("kmdfedu: removal vetoed, %d handle(s) open\n", x->opens);
        return STATUS_UNSUCCESSFUL;
    }
    DbgPrint("kmdfedu: query-remove accepted\n");
    return STATUS_SUCCESS;
}

static VOID EvtDeviceCleanup(WDFOBJECT device)
{
    UNREFERENCED_PARAMETER(device);
    DbgPrint("kmdfedu: device deleted\n");
}

static VOID EvtFileCreate(WDFDEVICE device, WDFREQUEST request, WDFFILEOBJECT file)
{
    EDU_CONTEXT *x = EduContext(device);
    UNREFERENCED_PARAMETER(file);
    InterlockedIncrement(&x->opens);
    x->info.opens++;
    WdfRequestComplete(request, STATUS_SUCCESS);
}

static VOID EvtFileClose(WDFFILEOBJECT file)
{
    InterlockedDecrement(&EduContext(WdfFileObjectGetDevice(file))->opens);
}

/* ---- programs' requests ---- */

static NTSTATUS factorial(EDU_CONTEXT *x, ULONG n, EDUTEST_FACT *out)
{
    KeClearEvent(&x->done);
    wr(x, EDU_STATUS, 0x80);                     /* (interrupt when done) */
    wr(x, EDU_FACT, n);
    if (!wait_interrupt(x)) return STATUS_IO_TIMEOUT;
    out->value = rd(x, EDU_FACT);
    out->isr_status = x->isr_status;
    out->isr_irql = x->isr_irql;
    out->dpc_irql = x->dpc_irql;
    out->interrupts = x->interrupts;
    out->message_interrupts = x->message_interrupts;
    return STATUS_SUCCESS;
}

/* One DMA transfer of @len bytes between @addr and the device's buffer at
 * @off, waiting for its interrupt */
static BOOLEAN dma_run(EDU_CONTEXT *x, ULONG64 addr, ULONG off, ULONG len, BOOLEAN to_ram)
{
    KeClearEvent(&x->done);
    wr64(x, to_ram ? EDU_DMA_SRC : EDU_DMA_DST, EDU_BUFFER + off);
    wr64(x, to_ram ? EDU_DMA_DST : EDU_DMA_SRC, addr);
    wr64(x, EDU_DMA_COUNT, len);
    wr64(x, EDU_DMA_CMD, 1 | (to_ram ? 2 : 0) | 4);
    return wait_interrupt(x) && (x->isr_status & EDU_DMA_IRQ);
}

/* The framework hands the transaction's list to the driver (at
 * DISPATCH_LEVEL); edu takes one element per command, each waited for, so
 * the request's thread runs them */
static BOOLEAN EvtProgramDma(WDFDMATRANSACTION t, WDFDEVICE device, WDFCONTEXT ctx, WDF_DMA_DIRECTION dir,
                             PSCATTER_GATHER_LIST list)
{
    EDU_CONTEXT *x = EduContext(device);
    UNREFERENCED_PARAMETER(t);
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(dir);
    x->list = list;
    KeSetEvent(&x->programmed, IO_NO_INCREMENT, FALSE);
    return TRUE;
}

/* One DMA transaction on @enabler over @buf: every list the framework
 * programs goes to (or from) the device's buffer in order.  The first
 * list's element count and address go to @elements and @first. */
static BOOLEAN transfer(EDU_CONTEXT *x, WDFDMAENABLER enabler, UCHAR *buf, ULONG len, BOOLEAN to_ram,
                        ULONG *elements, ULONG64 *first)
{
    WDFDMATRANSACTION t;
    PMDL mdl = IoAllocateMdl(buf, len, FALSE, FALSE, NULL);
    LARGE_INTEGER timeout;
    NTSTATUS st;
    BOOLEAN ok = FALSE, finished = FALSE;
    ULONG off = 0, i;

    if (!mdl) return FALSE;
    MmBuildMdlForNonPagedPool(mdl);
    st = WdfDmaTransactionCreate(enabler, WDF_NO_OBJECT_ATTRIBUTES, &t);
    if (!NT_SUCCESS(st)) { IoFreeMdl(mdl); return FALSE; }
    st = WdfDmaTransactionInitialize(t, EvtProgramDma, to_ram ? WdfDmaDirectionReadFromDevice : WdfDmaDirectionWriteToDevice,
                                     mdl, buf, len);
    KeClearEvent(&x->programmed);
    x->list = NULL;
    if (NT_SUCCESS(st)) st = WdfDmaTransactionExecute(t, NULL);
    if (!NT_SUCCESS(st)) {
        DbgPrint("kmdfedu: DMA transaction failed (%08x)\n", st);
        goto out;
    }
    ok = TRUE;
    while (ok && !finished) {
        timeout.QuadPart = -20000000;
        if (KeWaitForSingleObject(&x->programmed, Executive, KernelMode, FALSE, &timeout) != STATUS_SUCCESS || !x->list) {
            ok = FALSE;
            break;
        }
        PSCATTER_GATHER_LIST list = x->list;
        x->list = NULL;
        KeClearEvent(&x->programmed);
        if (elements && !*elements) {
            *elements = list->NumberOfElements;
            *first = (ULONG64)list->Elements[0].Address.QuadPart;
        }
        for (i = 0; i < list->NumberOfElements && ok; i++) {
            ok = dma_run(x, (ULONG64)list->Elements[i].Address.QuadPart, off, list->Elements[i].Length, to_ram);
            off += list->Elements[i].Length;
        }
        /* (FALSE: more of the buffer remains, and the framework programs it next) */
        finished = WdfDmaTransactionDmaCompleted(t, &st);
        if (finished && !NT_SUCCESS(st)) ok = FALSE;
    }
    if (finished) WdfDmaTransactionRelease(t);
    ok = ok && off == len;
out:
    WdfObjectDelete(t);
    IoFreeMdl(mdl);
    return ok;
}

static NTSTATUS dma(EDU_CONTEXT *x, const UCHAR *in, ULONG len, EDUTEST_DMA *r, UCHAR *out)
{
    UCHAR *src = ExAllocatePool2(POOL_FLAG_NON_PAGED, len, TAG_EDU);
    UCHAR *dst = ExAllocatePool2(POOL_FLAG_NON_PAGED, len, TAG_EDU);
    ULONG map_elements = 0;
    WDFCOMMONBUFFER cb;
    NTSTATUS st = STATUS_INSUFFICIENT_RESOURCES;

    if (!src || !dst) goto out;
    RtlCopyMemory(src, in, len);
    RtlZeroMemory(dst, len);
    st = STATUS_UNSUCCESSFUL;

    /* 1. to the device: a scatter/gather transaction */
    if (!transfer(x, x->sg, src, len, FALSE, &r->sg_elements, (ULONG64 *)&r->sg_address)) {
        DbgPrint("kmdfedu: dma: the transfer to the device failed\n");
        goto out;
    }

    /* 2. back into a common buffer */
    if (NT_SUCCESS(WdfCommonBufferCreate(x->sg, 4096, WDF_NO_OBJECT_ATTRIBUTES, &cb))) {
        UCHAR *va = WdfCommonBufferGetAlignedVirtualAddress(cb);
        r->common_logical = WdfCommonBufferGetAlignedLogicalAddress(cb).QuadPart;
        if (dma_run(x, (ULONG64)r->common_logical, 0, len, TRUE)) r->common_ok = RtlCompareMemory(va, in, len) == len;
        WdfObjectDelete(cb);
    }

    /* 3. back through map registers: a packet transaction */
    r->map_ok = transfer(x, x->packet, dst, len, TRUE, &map_elements, (ULONG64 *)&r->map_address) &&
                RtlCompareMemory(dst, in, len) == len;
    RtlCopyMemory(out, dst, len);
    r->interrupts = x->dma_interrupts;
    st = STATUS_SUCCESS;
out:
    if (src) ExFreePoolWithTag(src, TAG_EDU);
    if (dst) ExFreePoolWithTag(dst, TAG_EDU);
    return st;
}

static VOID EvtIoDeviceControl(WDFQUEUE queue, WDFREQUEST request, size_t out_len, size_t in_len, ULONG code)
{
    EDU_CONTEXT *x = EduContext(WdfIoQueueGetDevice(queue));
    UCHAR *buf = NULL;
    ULONG_PTR info = 0;
    NTSTATUS st;

    /* (buffered I/O: one system buffer for both directions) */
    st = WdfRequestRetrieveOutputBuffer(request, 1, (PVOID *)&buf, NULL);
    if (!NT_SUCCESS(st)) { WdfRequestComplete(request, st); return; }
    switch (code) {
    case IOCTL_EDUTEST_INFO:
        if (out_len < sizeof(EDUTEST_INFO)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        RtlCopyMemory(buf, &x->info, sizeof(EDUTEST_INFO));
        info = sizeof(EDUTEST_INFO);
        break;
    case IOCTL_EDUTEST_FACTORIAL: {
        EDUTEST_FACT f;
        if (in_len < 4 || out_len < sizeof f) { st = STATUS_BUFFER_TOO_SMALL; break; }
        RtlZeroMemory(&f, sizeof f);
        st = factorial(x, *(ULONG *)buf, &f);
        RtlCopyMemory(buf, &f, sizeof f);
        info = NT_SUCCESS(st) ? sizeof f : 0;
        break;
    }
    case IOCTL_EDUTEST_DMA: {
        static UCHAR data[4096];                 /* (the queue is sequential: one request at a time) */
        EDUTEST_DMA r;
        if (!in_len || in_len > 4096) { st = STATUS_INVALID_PARAMETER; break; }
        if (out_len < sizeof r + in_len) { st = STATUS_BUFFER_TOO_SMALL; break; }
        RtlZeroMemory(&r, sizeof r);
        RtlCopyMemory(data, buf, in_len);
        st = dma(x, data, (ULONG)in_len, &r, buf + sizeof r);
        RtlCopyMemory(buf, &r, sizeof r);
        info = NT_SUCCESS(st) ? sizeof r + in_len : 0;
        break;
    }
    default:
        st = STATUS_INVALID_DEVICE_REQUEST;
    }
    WdfRequestCompleteWithInformation(request, st, info);
}

/* ---- the driver ---- */

static NTSTATUS EvtDriverDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT init)
{
    static const GUID guid = EDUTEST_GUID_INIT;
    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_FILEOBJECT_CONFIG file;
    WDF_OBJECT_ATTRIBUTES attr;
    WDF_INTERRUPT_CONFIG ic;
    WDF_IO_QUEUE_CONFIG qcfg;
    WDFDEVICE device;
    EDU_CONTEXT *x;
    NTSTATUS st;
    UNREFERENCED_PARAMETER(driver);

    WdfDeviceInitSetIoType(init, WdfDeviceIoBuffered);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
    pnp.EvtDeviceReleaseHardware = EvtDeviceReleaseHardware;
    pnp.EvtDeviceD0EntryPostInterruptsEnabled = EvtDeviceD0EntryPostInterruptsEnabled;
    pnp.EvtDeviceQueryRemove = EvtDeviceQueryRemove;
    WdfDeviceInitSetPnpPowerEventCallbacks(init, &pnp);
    WDF_FILEOBJECT_CONFIG_INIT(&file, EvtFileCreate, EvtFileClose, WDF_NO_EVENT_CALLBACK);
    WdfDeviceInitSetFileObjectConfig(init, &file, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, EDU_CONTEXT);
    attr.EvtCleanupCallback = EvtDeviceCleanup;
    st = WdfDeviceCreate(&init, &attr, &device);
    if (!NT_SUCCESS(st)) return st;
    x = EduContext(device);
    KeInitializeEvent(&x->done, SynchronizationEvent, FALSE);
    KeInitializeEvent(&x->programmed, NotificationEvent, FALSE);
    x->info.kmdf_minor = WdfDriverIsVersionAvailable(driver, &(WDF_DRIVER_VERSION_AVAILABLE_PARAMS){
        sizeof(WDF_DRIVER_VERSION_AVAILABLE_PARAMS), 1, 33 }) ? 33 : 1;

    st = WdfDeviceCreateDeviceInterface(device, &guid, NULL);
    if (!NT_SUCCESS(st)) return st;

    WDF_INTERRUPT_CONFIG_INIT(&ic, EvtInterruptIsr, EvtInterruptDpc);
    st = WdfInterruptCreate(device, &ic, WDF_NO_OBJECT_ATTRIBUTES, &x->interrupt);
    if (!NT_SUCCESS(st)) {
        DbgPrint("kmdfedu: WdfInterruptCreate failed (%08x)\n", st);
        return st;
    }

    /* (the requests wait for the device's interrupts: PASSIVE_LEVEL) */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchSequential);
    qcfg.EvtIoDeviceControl = EvtIoDeviceControl;
    WDF_OBJECT_ATTRIBUTES_INIT(&attr);
    attr.ExecutionLevel = WdfExecutionLevelPassive;
    st = WdfIoQueueCreate(device, &qcfg, &attr, WDF_NO_HANDLE);
    if (!NT_SUCCESS(st)) return st;
    DbgPrint("kmdfedu: EvtDriverDeviceAdd: device, interface, interrupt and queue created\n");
    return STATUS_SUCCESS;
}

static VOID EvtDriverUnload(WDFDRIVER driver)
{
    UNREFERENCED_PARAMETER(driver);
    DbgPrint("kmdfedu: unloaded\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING registry_path)
{
    WDF_DRIVER_CONFIG cfg;
    NTSTATUS st;

    WDF_DRIVER_CONFIG_INIT(&cfg, EvtDriverDeviceAdd);
    cfg.EvtDriverUnload = EvtDriverUnload;
    st = WdfDriverCreate(drv, registry_path, WDF_NO_OBJECT_ATTRIBUTES, &cfg, WDF_NO_HANDLE);
    DbgPrint("kmdfedu: DriverEntry %08x\n", st);
    return st;
}
