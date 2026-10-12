/*
 * kmdftest.sys — NovaOS's KMDF test driver (step 30.4)
 *
 * A non-PnP KMDF driver as the WDK samples write one (NONPNP): DriverEntry
 * calls WdfDriverCreate with WdfDriverInitNonPnpDriver, then makes a
 * control device \Device\KmdfTest with its \DosDevices\KmdfTest link, a
 * typed device context, a file-create callback and a sequential default
 * queue.  A write keeps up to 256 bytes that the next reads return; the
 * control codes in kmdftest.h reverse bytes and report what the driver saw
 * of the framework.  EvtDriverUnload has nothing to free: the framework
 * deletes the control device itself.
 *
 * It is built by tools/kmdf/build_kmdf.py against the KMDF 1.33 headers
 * from Microsoft's Windows-Driver-Frameworks source, linked with the KMDF
 * client stub (tools/kmdf/stub.c: FxDriverEntry binds to Wdf01000.sys
 * through WdfLdr.sys) and runs on Microsoft's own framework.  The
 * kmdftest self-test installs it with "sc create KmdfTest type= kernel"
 * and drives it with userland/programs/kmdftest.c.
 */
#include <ntddk.h>
#include <wdf.h>
#include "kmdftest.h"

typedef struct {
    ULONG  magic;
    ULONG  len;
    UCHAR  data[256];
    KMDFTEST_INFO info;
} KMDFTEST_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(KMDFTEST_CONTEXT, KmdfTestContext)

#define KMDFTEST_MAGIC 0x4B6D6446u               /* 'KmdF' */

static KMDFTEST_INFO g_boot;                     /* filled in DriverEntry, copied to the context */

static VOID EvtFileCreate(WDFDEVICE device, WDFREQUEST request, WDFFILEOBJECT file)
{
    UNREFERENCED_PARAMETER(file);
    KmdfTestContext(device)->info.creates++;
    WdfRequestComplete(request, STATUS_SUCCESS);
}

static VOID EvtIoWrite(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    KMDFTEST_CONTEXT *ctx = KmdfTestContext(WdfIoQueueGetDevice(queue));
    PVOID in;
    NTSTATUS st = WdfRequestRetrieveInputBuffer(request, 1, &in, NULL);
    if (!NT_SUCCESS(st)) { WdfRequestComplete(request, st); return; }
    if (length > sizeof ctx->data) length = sizeof ctx->data;
    RtlCopyMemory(ctx->data, in, length);
    ctx->len = (ULONG)length;
    ctx->info.writes++;
    WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, length);
}

static VOID EvtIoRead(WDFQUEUE queue, WDFREQUEST request, size_t length)
{
    KMDFTEST_CONTEXT *ctx = KmdfTestContext(WdfIoQueueGetDevice(queue));
    PVOID out;
    NTSTATUS st = WdfRequestRetrieveOutputBuffer(request, 1, &out, NULL);
    if (!NT_SUCCESS(st)) { WdfRequestComplete(request, st); return; }
    if (length > ctx->len) length = ctx->len;
    RtlCopyMemory(out, ctx->data, length);
    WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, length);
}

static VOID EvtIoDeviceControl(WDFQUEUE queue, WDFREQUEST request, size_t out_len, size_t in_len, ULONG code)
{
    KMDFTEST_CONTEXT *ctx = KmdfTestContext(WdfIoQueueGetDevice(queue));
    UCHAR *in, *out;
    NTSTATUS st;
    size_t i;

    switch (code) {
    case IOCTL_KMDFTEST_REVERSE:
        if (out_len < in_len) { WdfRequestComplete(request, STATUS_BUFFER_TOO_SMALL); return; }
        st = WdfRequestRetrieveInputBuffer(request, 1, (PVOID *)&in, NULL);
        if (NT_SUCCESS(st)) st = WdfRequestRetrieveOutputBuffer(request, in_len, (PVOID *)&out, NULL);
        if (!NT_SUCCESS(st)) { WdfRequestComplete(request, st); return; }
        /* buffered I/O: one system buffer for both, so reverse in place */
        for (i = 0; i < in_len / 2; i++) {
            UCHAR t = in[i];
            in[i] = in[in_len - 1 - i];
            in[in_len - 1 - i] = t;
        }
        if (out != in) RtlCopyMemory(out, in, in_len);
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, in_len);
        return;
    case IOCTL_KMDFTEST_INFO:
        st = WdfRequestRetrieveOutputBuffer(request, sizeof(KMDFTEST_INFO), (PVOID *)&out, NULL);
        if (!NT_SUCCESS(st)) { WdfRequestComplete(request, st); return; }
        ctx->info.context = ctx->magic == KMDFTEST_MAGIC;
        ctx->info.passive = KeGetCurrentIrql() == PASSIVE_LEVEL;
        RtlCopyMemory(out, &ctx->info, sizeof ctx->info);
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, sizeof ctx->info);
        return;
    default:
        WdfRequestComplete(request, STATUS_INVALID_DEVICE_REQUEST);
    }
}

static VOID EvtDriverUnload(WDFDRIVER driver)
{
    UNREFERENCED_PARAMETER(driver);
    DbgPrint("KmdfTest: unloading\n");
}

static NTSTATUS CreateControlDevice(WDFDRIVER driver)
{
    DECLARE_CONST_UNICODE_STRING(name, KMDFTEST_DEVICE);
    DECLARE_CONST_UNICODE_STRING(link, KMDFTEST_LINK);
    WDF_FILEOBJECT_CONFIG file;
    WDF_OBJECT_ATTRIBUTES attr;
    WDF_IO_QUEUE_CONFIG qcfg;
    PWDFDEVICE_INIT init;
    WDFDEVICE device;
    KMDFTEST_CONTEXT *ctx;
    NTSTATUS st;

    init = WdfControlDeviceInitAllocate(driver, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
    if (!init) return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetIoType(init, WdfDeviceIoBuffered);
    st = WdfDeviceInitAssignName(init, &name);
    if (!NT_SUCCESS(st)) { WdfDeviceInitFree(init); return st; }
    WDF_FILEOBJECT_CONFIG_INIT(&file, EvtFileCreate, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK);
    WdfDeviceInitSetFileObjectConfig(init, &file, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, KMDFTEST_CONTEXT);
    st = WdfDeviceCreate(&init, &attr, &device);
    if (!NT_SUCCESS(st)) { WdfDeviceInitFree(init); return st; }
    ctx = KmdfTestContext(device);
    ctx->magic = KMDFTEST_MAGIC;
    ctx->info = g_boot;

    st = WdfDeviceCreateSymbolicLink(device, &link);
    if (!NT_SUCCESS(st)) goto fail;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchSequential);
    qcfg.EvtIoRead = EvtIoRead;
    qcfg.EvtIoWrite = EvtIoWrite;
    qcfg.EvtIoDeviceControl = EvtIoDeviceControl;
    st = WdfIoQueueCreate(device, &qcfg, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(st)) goto fail;
    ctx->info.device_created = 1;
    WdfControlFinishInitializing(device);
    return STATUS_SUCCESS;
fail:
    WdfObjectDelete(device);
    return st;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING registry_path)
{
    WDF_DRIVER_CONFIG cfg;
    WDFDRIVER driver;
    WDFSTRING path;
    UNICODE_STRING s;
    NTSTATUS st;

    WDF_DRIVER_CONFIG_INIT(&cfg, WDF_NO_EVENT_CALLBACK);
    cfg.DriverInitFlags |= WdfDriverInitNonPnpDriver;
    cfg.EvtDriverUnload = EvtDriverUnload;
    st = WdfDriverCreate(drv, registry_path, WDF_NO_OBJECT_ATTRIBUTES, &cfg, &driver);
    if (!NT_SUCCESS(st)) {
        DbgPrint("KmdfTest: WdfDriverCreate failed %08x\n", st);
        return st;
    }
    g_boot.driver_entry = 1;
    g_boot.major = 1;
    g_boot.minor = WdfDriverIsVersionAvailable(driver, &(WDF_DRIVER_VERSION_AVAILABLE_PARAMS){
        sizeof(WDF_DRIVER_VERSION_AVAILABLE_PARAMS), 1, 33 }) ? 33 : 0;
    if (NT_SUCCESS(WdfStringCreate(NULL, WDF_NO_OBJECT_ATTRIBUTES, &path))) {
        if (NT_SUCCESS(WdfDriverRetrieveVersionString(driver, path))) { /* exercises WDFSTRING */ }
        WdfObjectDelete(path);
    }
    s.Buffer = (PWCH)WdfDriverGetRegistryPath(driver);
    if (s.Buffer) {
        size_t n = wcslen(s.Buffer);
        if (n >= RTL_NUMBER_OF(g_boot.registry_path)) n = RTL_NUMBER_OF(g_boot.registry_path) - 1;
        RtlCopyMemory(g_boot.registry_path, s.Buffer, n * sizeof(WCHAR));
    }
    st = CreateControlDevice(driver);
    DbgPrint("KmdfTest: DriverEntry %08x, KMDF 1.%u\n", st, g_boot.minor);
    return st;
}
