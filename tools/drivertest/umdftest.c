/*
 * umdftest.c — umdftest.dll, a UMDF 2 driver for QEMU's edu device
 *
 * The Phase 30.10 test driver: an ordinary UMDF 2 driver written against
 * the WDK's UMDF 2.33 headers (wdf.h) and linked with NovaOS's UMDF stub
 * (tools/umdf/stub.c).  Its package (umdftest.inf) installs the
 * reflector, WUDFRd.sys, as the device's function driver and this DLL as
 * its UMDF driver, so it runs in a driver host process (WUDFHost.exe):
 * DriverEntry creates the WDFDRIVER, EvtDriverDeviceAdd the device, its
 * interface and its queue, EvtDevicePrepareHardware looks at the
 * device's resources (the memory BAR and the interrupt; a UMDF driver
 * maps neither unless its INF asks for hardware access, and this one does
 * not), and the queue serves a program's reads, writes and device
 * controls from memory.  Each step says so on the debugger
 * (OutputDebugString: the kernel log), lines starting "umdftest: ".
 */
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <stdio.h>
#include <wdf.h>
#include "umdftest.h"

typedef struct {
    ULONG magic;                                /* UMDFTEST_MAGIC: the context is the device's */
    UMDFTEST_INFO info;
    BYTE store[UMDFTEST_STORE];
} DEVICE_CONTEXT;
#define UMDFTEST_MAGIC 0x46444d55               /* "UMDF" */
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceContext)

static const GUID g_Interface = UMDFTEST_GUID_INIT;
static ULONG g_Major, g_Minor;

static void Say(const char *fmt, ...)
{
    char line[300];
    va_list ap;
    int n = _snprintf(line, sizeof(line) - 2, "umdftest: ");
    va_start(ap, fmt);
    int m = _vsnprintf(line + n, sizeof(line) - 2 - n, fmt, ap);
    va_end(ap);
    n = m < 0 ? (int)sizeof(line) - 2 : n + m;
    line[n] = '\n';
    line[n + 1] = 0;
    OutputDebugStringA(line);
}

static NTSTATUS PrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw, WDFCMRESLIST Translated)
{
    DEVICE_CONTEXT *ctx = DeviceContext(Device);
    ctx->info.prepare_hardware++;
    ctx->info.resources_raw = WdfCmResourceListGetCount(Raw);
    ULONG n = WdfCmResourceListGetCount(Translated);
    for (ULONG i = 0; i < n; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d = WdfCmResourceListGetDescriptor(Translated, i);
        if (d == NULL) continue;
        if (d->Type == CmResourceTypeMemory) {
            if (!ctx->info.memory) ctx->info.memory_length = d->u.Memory.Length;
            ctx->info.memory++;
        } else if (d->Type == CmResourceTypeInterrupt) {
            ctx->info.interrupts++;
        }
    }
    Say("EvtDevicePrepareHardware: %lu resources, %u memory range(s) (%u bytes), %u interrupt(s)",
        ctx->info.resources_raw, ctx->info.memory, ctx->info.memory_length, ctx->info.interrupts);
    return STATUS_SUCCESS;
}

static NTSTATUS D0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    DEVICE_CONTEXT *ctx = DeviceContext(Device);
    ctx->info.d0_entries++;
    Say("EvtDeviceD0Entry (from WDF power state %d)", (int)PreviousState);
    return STATUS_SUCCESS;
}

static VOID FileCreate(WDFDEVICE Device, WDFREQUEST Request, WDFFILEOBJECT FileObject)
{
    (void)FileObject;
    DEVICE_CONTEXT *ctx = DeviceContext(Device);
    ctx->info.creates++;
    Say("EvtDeviceFileCreate from process %lu", WdfRequestGetRequestorProcessId(Request));
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

static VOID FileClose(WDFFILEOBJECT FileObject)
{
    DEVICE_CONTEXT *ctx = DeviceContext(WdfFileObjectGetDevice(FileObject));
    ctx->info.closes++;
    Say("EvtFileClose");
}

static VOID IoRead(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
    DEVICE_CONTEXT *ctx = DeviceContext(WdfIoQueueGetDevice(Queue));
    PVOID out;
    size_t cap;
    NTSTATUS status = WdfRequestRetrieveOutputBuffer(Request, 1, &out, &cap);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    size_t n = Length < cap ? Length : cap;
    if (n > ctx->info.stored) n = ctx->info.stored;
    CopyMemory(out, ctx->store, n);
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, n);
}

static VOID IoWrite(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
    DEVICE_CONTEXT *ctx = DeviceContext(WdfIoQueueGetDevice(Queue));
    PVOID in;
    size_t cap;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(Request, 1, &in, &cap);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    size_t n = Length < cap ? Length : cap;
    if (n > UMDFTEST_STORE) n = UMDFTEST_STORE;
    CopyMemory(ctx->store, in, n);
    ctx->info.stored = (unsigned int)n;
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, n);
}

static VOID IoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutputLength, size_t InputLength, ULONG Code)
{
    DEVICE_CONTEXT *ctx = DeviceContext(WdfIoQueueGetDevice(Queue));
    PVOID in = NULL, out = NULL;
    size_t inCap = 0, outCap = 0;
    NTSTATUS status;
    switch (Code) {
    case IOCTL_UMDFTEST_REVERSE: {
        if (InputLength == 0 || OutputLength < InputLength) {
            WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
            return;
        }
        status = WdfRequestRetrieveInputBuffer(Request, InputLength, &in, &inCap);
        if (NT_SUCCESS(status)) status = WdfRequestRetrieveOutputBuffer(Request, InputLength, &out, &outCap);
        if (!NT_SUCCESS(status)) {
            WdfRequestComplete(Request, status);
            return;
        }
        const BYTE *s = in;
        BYTE *d = out;
        for (size_t i = 0; i < InputLength; i++) d[i] = s[InputLength - 1 - i];
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, InputLength);
        return;
    }
    case IOCTL_UMDFTEST_INFO: {
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(UMDFTEST_INFO), &out, &outCap);
        if (!NT_SUCCESS(status)) {
            WdfRequestComplete(Request, status);
            return;
        }
        UMDFTEST_INFO *info = out;
        *info = ctx->info;
        info->major = g_Major;
        info->minor = g_Minor;
        info->host_pid = GetCurrentProcessId();
        info->requestor_pid = WdfRequestGetRequestorProcessId(Request);
        info->context_ok = ctx->magic == UMDFTEST_MAGIC;
        {
            PWSTR rp = WdfDriverGetRegistryPath(WdfGetDriver());
            size_t k = 0;
            for (; rp != NULL && rp[k] && k < ARRAYSIZE(info->registry_path) - 1; k++) info->registry_path[k] = rp[k];
            info->registry_path[k] = 0;
        }
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(UMDFTEST_INFO));
        return;
    }
    default:
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
}

static NTSTATUS DeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    (void)Driver;
    NTSTATUS status;
    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware = PrepareHardware;
    pnp.EvtDeviceD0Entry = D0Entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp);

    WDF_FILEOBJECT_CONFIG files;
    WDF_FILEOBJECT_CONFIG_INIT(&files, FileCreate, FileClose, WDF_NO_EVENT_CALLBACK);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &files, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    WDFDEVICE device;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        Say("WdfDeviceCreate failed (0x%08lx)", status);
        return status;
    }
    DEVICE_CONTEXT *ctx = DeviceContext(device);
    ctx->magic = UMDFTEST_MAGIC;
    ctx->info.device_adds++;

    status = WdfDeviceCreateDeviceInterface(device, &g_Interface, NULL);
    if (!NT_SUCCESS(status)) {
        Say("WdfDeviceCreateDeviceInterface failed (0x%08lx)", status);
        return status;
    }
    WDF_IO_QUEUE_CONFIG queue;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue, WdfIoQueueDispatchParallel);
    queue.EvtIoRead = IoRead;
    queue.EvtIoWrite = IoWrite;
    queue.EvtIoDeviceControl = IoDeviceControl;
    status = WdfIoQueueCreate(device, &queue, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        Say("WdfIoQueueCreate failed (0x%08lx)", status);
        return status;
    }
    Say("EvtDriverDeviceAdd: device, interface and queue created in host process %lu", GetCurrentProcessId());
    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, DeviceAdd);
    NTSTATUS status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        Say("WdfDriverCreate failed (0x%08lx)", status);
        return status;
    }
    WDF_DRIVER_VERSION_AVAILABLE_PARAMS v;
    WDF_DRIVER_VERSION_AVAILABLE_PARAMS_INIT(&v, 2, 33);
    if (WdfDriverIsVersionAvailable(WdfGetDriver(), &v)) {
        g_Major = 2;
        g_Minor = 33;
    }
    Say("DriverEntry: UMDF %lu.%lu, registry path %ls", g_Major, g_Minor, WdfDriverGetRegistryPath(WdfGetDriver()));
    return STATUS_SUCCESS;
}
