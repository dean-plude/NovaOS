/*
 * wdmtest.sys — NovaOS's first Windows kernel-mode test driver (step 30.1)
 *
 * A WDM driver as a Windows driver is written: DriverEntry creates
 * \Device\WdmTest (buffered I/O) and its \DosDevices\WdmTest link, and
 * serves IRP_MJ_CREATE, CLOSE, CLEANUP, READ, WRITE and DEVICE_CONTROL:
 * a write keeps up to 256 bytes that the next reads return, and the
 * control codes in wdmtest.h reverse bytes (buffered), add one to each
 * (neither I/O, on the caller's own buffers) and report the objects as the
 * driver sees them.  DriverUnload removes the link and the device.
 *
 * It is built by tools/drivertest/mksys.py with clang and lld-link
 * /driver against kernel/nt/exports.h's ntoskrnl.exe, and uses NovaOS's
 * own wdm.h for the structures (the WDK is not needed).  The wdmtest self-
 * test installs it with "sc create WdmTest type= kernel" and drives it
 * with userland/programs/wdmtest.c.
 */
#include "../../kernel/nt/wdm.h"
#include "wdmtest.h"

#define IMPORT __declspec(dllimport) NT_API
IMPORT NT_STATUS IoCreateDevice(NT_DRIVER_OBJECT *drv, unsigned int ext_size, NT_UNICODE_STRING *name, unsigned int type,
                                unsigned int characteristics, unsigned char exclusive, NT_DEVICE_OBJECT **out);
IMPORT void      IoDeleteDevice(NT_DEVICE_OBJECT *dev);
IMPORT NT_STATUS IoCreateSymbolicLink(NT_UNICODE_STRING *link, NT_UNICODE_STRING *target);
IMPORT NT_STATUS IoDeleteSymbolicLink(NT_UNICODE_STRING *link);
IMPORT void      IofCompleteRequest(NT_IRP *irp, char boost);
IMPORT void      RtlInitUnicodeString(NT_UNICODE_STRING *u, const NT_WCHAR *s);
IMPORT void      RtlCopyUnicodeString(NT_UNICODE_STRING *dst, const NT_UNICODE_STRING *src);
IMPORT unsigned int DbgPrint(const char *fmt, ...);

typedef struct {
    unsigned int  opens;
    unsigned int  len;
    unsigned char data[256];
    NT_UNICODE_STRING registry_path;
    NT_WCHAR      registry_buf[128];
} Ext;

static NT_STATUS complete(NT_IRP *irp, NT_STATUS st, unsigned long long info)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    IofCompleteRequest(irp, 0);
    return st;
}

static void wcopy(unsigned short *dst, int cap, const NT_UNICODE_STRING *u)
{
    int n = u && u->Buffer ? u->Length / 2 : 0;
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; i++) dst[i] = u->Buffer[i];
    dst[n] = 0;
}

static NT_API NT_STATUS create_close(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    if (sl->MajorFunction == NT_IRP_MJ_CREATE) {
        ((Ext *)dev->DeviceExtension)->opens++;
        DbgPrint("wdmtest: open \"%wZ\"", &sl->FileObject->FileName);
    }
    return complete(irp, NT_STATUS_SUCCESS, 0);
}

static NT_API NT_STATUS read_write(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    Ext *x = dev->DeviceExtension;
    unsigned char *buf = irp->AssociatedIrp.SystemBuffer;
    unsigned int n = sl->Parameters.Read.Length;
    if (n && !buf) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
    if (sl->MajorFunction == NT_IRP_MJ_WRITE) {
        if (n > sizeof(x->data)) n = sizeof(x->data);
        for (unsigned int i = 0; i < n; i++) x->data[i] = buf[i];
        x->len = n;
        return complete(irp, NT_STATUS_SUCCESS, n);
    }
    if (!x->len) return complete(irp, NT_STATUS_END_OF_FILE, 0);
    if (n > x->len) n = x->len;
    for (unsigned int i = 0; i < n; i++) buf[i] = x->data[i];
    return complete(irp, NT_STATUS_SUCCESS, n);
}

static NT_API NT_STATUS control(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    Ext *x = dev->DeviceExtension;
    unsigned int in = sl->Parameters.DeviceIoControl.InputBufferLength, out = sl->Parameters.DeviceIoControl.OutputBufferLength;
    unsigned char *sys = irp->AssociatedIrp.SystemBuffer;
    switch (sl->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_WDMTEST_REVERSE: {
        if (out < in) return complete(irp, NT_STATUS_BUFFER_TOO_SMALL, 0);
        for (unsigned int i = 0; i < in / 2; i++) {
            unsigned char t = sys[i];
            sys[i] = sys[in - 1 - i];
            sys[in - 1 - i] = t;
        }
        return complete(irp, NT_STATUS_SUCCESS, in);
    }
    case IOCTL_WDMTEST_NEITHER: {                      /* (the caller's own buffers: this runs in its process) */
        const unsigned char *src = sl->Parameters.DeviceIoControl.Type3InputBuffer;
        unsigned char *dst = irp->UserBuffer;
        if (out < in || !src || !dst) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
        for (unsigned int i = 0; i < in; i++) dst[i] = (unsigned char)(src[i] + 1);
        return complete(irp, NT_STATUS_SUCCESS, in);
    }
    case IOCTL_WDMTEST_INFO: {
        if (out < sizeof(WDMTEST_INFO)) return complete(irp, NT_STATUS_BUFFER_TOO_SMALL, 0);
        WDMTEST_INFO *i = (WDMTEST_INFO *)sys;
        NT_DRIVER_OBJECT *d = dev->DriverObject;
        i->driver_type = d->Type;
        i->driver_size = d->Size;
        i->device_type = dev->Type;
        i->device_size = (short)dev->Size;
        i->irp_type = irp->Type;
        i->irp_stack_count = irp->StackCount;
        i->irp_current_location = irp->CurrentLocation;
        i->file_type = sl->FileObject->Type;
        i->file_size = sl->FileObject->Size;
        i->requestor_mode = (unsigned char)irp->RequestorMode;
        i->buffered = sys != 0 && (dev->Flags & NT_DO_BUFFERED_IO) != 0;
        i->opens = x->opens;
        wcopy(i->driver_name, 64, &d->DriverName);
        wcopy(i->registry_path, 96, &x->registry_path);
        wcopy(i->service_key, 32, &d->DriverExtension->ServiceKeyName);
        wcopy(i->file_name, 32, &sl->FileObject->FileName);
        return complete(irp, NT_STATUS_SUCCESS, sizeof(WDMTEST_INFO));
    }
    default:
        return complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

static NT_API void unload(NT_DRIVER_OBJECT *drv)
{
    NT_UNICODE_STRING link;
    RtlInitUnicodeString(&link, WDMTEST_LINK);
    IoDeleteSymbolicLink(&link);
    IoDeleteDevice(drv->DeviceObject);
    DbgPrint("wdmtest: unloaded");
}

NT_API NT_STATUS DriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *registry_path)
{
    NT_UNICODE_STRING name, link;
    NT_DEVICE_OBJECT *dev;
    DbgPrint("wdmtest: DriverEntry %wZ", registry_path);
    RtlInitUnicodeString(&name, WDMTEST_DEVICE);
    NT_STATUS st = IoCreateDevice(drv, sizeof(Ext), &name, NT_FILE_DEVICE_UNKNOWN, 0, 0, &dev);
    if (!NT_SUCCESS_(st)) return st;
    dev->Flags |= NT_DO_BUFFERED_IO;
    Ext *x = dev->DeviceExtension;
    x->registry_path.Buffer = x->registry_buf;
    x->registry_path.MaximumLength = sizeof(x->registry_buf);
    RtlCopyUnicodeString(&x->registry_path, registry_path);   /* (RegistryPath is only lent for the call) */
    RtlInitUnicodeString(&link, WDMTEST_LINK);
    st = IoCreateSymbolicLink(&link, &name);
    if (!NT_SUCCESS_(st)) { IoDeleteDevice(dev); return st; }
    drv->MajorFunction[NT_IRP_MJ_CREATE] = create_close;
    drv->MajorFunction[NT_IRP_MJ_CLOSE] = create_close;
    drv->MajorFunction[NT_IRP_MJ_CLEANUP] = create_close;
    drv->MajorFunction[NT_IRP_MJ_READ] = read_write;
    drv->MajorFunction[NT_IRP_MJ_WRITE] = read_write;
    drv->MajorFunction[NT_IRP_MJ_DEVICE_CONTROL] = control;
    drv->DriverUnload = unload;
    dev->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    return NT_STATUS_SUCCESS;
}
