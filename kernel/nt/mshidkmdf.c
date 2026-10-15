/*
 * mshidkmdf.c — the HID minidriver a KMDF HID driver sits under
 *
 * A KMDF driver cannot call HidRegisterMinidriver: the framework owns its
 * driver object's dispatch table, and hidclass would take that over.  So
 * on Windows a KMDF HID driver (Microsoft's vhidmini2 sample, a vendor's
 * touchpad or sensor driver) is installed as a lower filter under
 * mshidkmdf.sys, a pass-through minidriver: its INF says
 * "Include = MsHidKmdf.inf, Needs = MsHidKmdf.NT.Services", which makes
 * mshidkmdf the device's function driver, and adds its own driver below
 * it.  mshidkmdf registers with hidclass and hands every request it gets,
 * hidclass's IOCTL_HID_* among them, unchanged to the device below it:
 * the KMDF driver's queue.
 *
 * This is NovaOS's mshidkmdf.sys, built into the kernel as hidusb.c is
 * (the service "mshidkmdf"; NovaOS's MsHidKmdf.inf in C:\Windows\INF names
 * it, userland/setupapi/inf).
 */
#include "ntapi.h"
#include "hidclass.h"
#include "../lib/string.h"

/* IoSkipCurrentIrpStackLocation, then the device below: hidclass's FDO
 * extension names it */
static NT_API NT_STATUS mk_pass(NT_DEVICE_OBJECT *fdo, NT_IRP *irp)
{
    HidDeviceExtension *x = fdo->DeviceExtension;
    irp->CurrentLocation++;
    irp->Tail.Overlay.CurrentStackLocation++;
    return nt_IofCallDriver(x->NextDeviceObject, irp);
}

static NT_API NT_STATUS mk_add_device(NT_DRIVER_OBJECT *drv, NT_DEVICE_OBJECT *fdo)
{
    (void)drv;
    fdo->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    return NT_STATUS_SUCCESS;
}

NTSTATUS NT_API MsHidKmdfDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg)
{
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) drv->MajorFunction[i] = mk_pass;
    drv->DriverExtension->AddDevice = mk_add_device;
    HidMinidriverRegistration r;
    memset(&r, 0, sizeof(r));
    r.Revision = HID_REVISION;
    r.DriverObject = drv;
    r.RegistryPath = reg;
    r.DeviceExtensionSize = 0;
    r.DevicesArePolled = 0;
    return nt_HidRegisterMinidriver(&r);
}
