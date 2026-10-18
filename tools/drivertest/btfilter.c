/*
 * btfilter.sys — a lower filter on a USB Bluetooth radio, NovaOS's stand-in
 * for Intel's ibtusb.sys in step 28.2's self-test
 *
 * Intel's radios on Windows run bth.inf's bthusb as their function driver
 * with ibtusb under it as a lower filter (its INF: Include = bth.inf, Needs
 * = BthUsb.NT..., LowerFilters = ibtusb), which loads the firmware and then
 * passes the HCI traffic through.  btfilter.inf is laid out the same way,
 * and this driver does what such a filter does once the firmware is in: it
 * attaches under the function driver, passes every request down, and logs
 * the HCI commands it sees bthusb send (class requests to the device) so
 * the self-test knows the radio's traffic went through it.
 *
 * Built by tools/drivertest/mksys.py, as edutest.sys is.
 */
#include "../../kernel/nt/wdm.h"

#define NT_IRP_MJ_INTERNAL_DEVICE_CONTROL 0x0F
#define IOCTL_INTERNAL_USB_SUBMIT_URB     0x00220003
#define URB_FUNCTION_CLASS_DEVICE         0x001A

/* The start of the WDK's _URB_CONTROL_VENDOR_OR_CLASS_REQUEST (usb.h, x64) */
typedef struct {
    unsigned short Length, Function;
    unsigned int   Status;
    void          *UsbdDeviceHandle;
    unsigned int   UsbdFlags;
    void          *PipeHandle;
    unsigned int   TransferFlags;
    unsigned int   TransferBufferLength;
    void          *TransferBuffer;
} Urb;

#define IMPORT __declspec(dllimport) NT_API
IMPORT NT_STATUS IoCreateDevice(NT_DRIVER_OBJECT *drv, unsigned int ext_size, NT_UNICODE_STRING *name, unsigned int type,
                                unsigned int chars, unsigned char exclusive, NT_DEVICE_OBJECT **out);
IMPORT void      IoDeleteDevice(NT_DEVICE_OBJECT *dev);
IMPORT NT_DEVICE_OBJECT *IoAttachDeviceToDeviceStack(NT_DEVICE_OBJECT *src, NT_DEVICE_OBJECT *target);
IMPORT void      IoDetachDevice(NT_DEVICE_OBJECT *target);
IMPORT NT_STATUS IofCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp);
IMPORT void      PoStartNextPowerIrp(NT_IRP *irp);
IMPORT NT_STATUS PoCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp);
IMPORT unsigned int DbgPrint(const char *fmt, ...);

typedef struct {
    NT_DEVICE_OBJECT *lower;
    unsigned int commands;                  /* HCI commands seen */
} Ext;

static void skip(NT_IRP *irp)
{
    irp->CurrentLocation++;
    irp->Tail.Overlay.CurrentStackLocation++;
}

static NT_API NT_STATUS pass_down(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    skip(irp);
    return IofCallDriver(x->lower, irp);
}

/* The radio's URBs: an HCI command is a class request to the device whose
 * buffer is the command packet (opcode, length, parameters) */
static NT_API NT_STATUS internal_control(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    if (sl->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB) {
        Urb *u = sl->Parameters.Others.Argument1;
        if (u && u->Function == URB_FUNCTION_CLASS_DEVICE && u->TransferBuffer && u->TransferBufferLength >= 3) {
            const unsigned char *p = u->TransferBuffer;
            unsigned int op = p[0] | p[1] << 8;
            if (++x->commands <= 8 || op == 0x0C03)
                DbgPrint("btfilter: HCI command %04x (%u so far)", op, x->commands);
        }
    }
    return pass_down(dev, irp);
}

static NT_API NT_STATUS pnp(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    if (sl->MinorFunction == NT_IRP_MN_REMOVE_DEVICE) {
        NT_DEVICE_OBJECT *lower = x->lower;
        DbgPrint("btfilter: removed after %u HCI commands", x->commands);
        skip(irp);
        NT_STATUS st = IofCallDriver(lower, irp);
        IoDetachDevice(lower);
        IoDeleteDevice(dev);
        return st;
    }
    return pass_down(dev, irp);
}

static NT_API NT_STATUS power(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    Ext *x = dev->DeviceExtension;
    PoStartNextPowerIrp(irp);
    skip(irp);
    return PoCallDriver(x->lower, irp);
}

static NT_API NT_STATUS add_device(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *pdo)
{
    NT_DEVICE_OBJECT *fido;
    NT_STATUS st = IoCreateDevice(drv, sizeof(Ext), 0, NT_FILE_DEVICE_UNKNOWN, 0, 0, &fido);
    if (!NT_SUCCESS_(st)) return st;
    Ext *x = fido->DeviceExtension;
    x->commands = 0;
    x->lower = IoAttachDeviceToDeviceStack(fido, pdo);
    if (!x->lower) { IoDeleteDevice(fido); return NT_STATUS_DEVICE_NOT_READY; }
    /* a filter takes on the stack's I/O and power flags and its type */
    fido->Flags |= x->lower->Flags & (NT_DO_BUFFERED_IO | NT_DO_DIRECT_IO | NT_DO_POWER_PAGABLE);
    fido->DeviceType = x->lower->DeviceType;
    fido->Characteristics = x->lower->Characteristics;
    fido->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    DbgPrint("btfilter: AddDevice, under the radio's function driver");
    return NT_STATUS_SUCCESS;
}

NT_API NT_STATUS DriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *registry_path)
{
    (void)registry_path;
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) drv->MajorFunction[i] = pass_down;
    drv->MajorFunction[NT_IRP_MJ_INTERNAL_DEVICE_CONTROL] = internal_control;
    drv->MajorFunction[NT_IRP_MJ_PNP] = pnp;
    drv->MajorFunction[NT_IRP_MJ_POWER] = power;
    drv->DriverExtension->AddDevice = add_device;
    DbgPrint("btfilter: DriverEntry");
    return NT_STATUS_SUCCESS;
}
