/*
 * wdmsec.c — the WDK's wdmsec.lib, as the framework links it:
 * IoCreateDeviceSecure (here WdmlibIoCreateDeviceSecure, the name the
 * library's header maps it to) and the SDDL strings it is given
 *
 * IoCreateDeviceSecure creates the device object and sets the security
 * descriptor the SDDL string describes, unless the registry's device-class
 * key overrides it.  NovaOS's I/O manager does not check access to device
 * objects yet (any opener gets the access it asks for, as with the default
 * descriptor's SYSTEM and Administrators entries for an administrator), so
 * the string is kept by nobody and the device is created plainly.
 */
#include <ntddk.h>
#include <wdmsec.h>

const UNICODE_STRING SDDL_DEVOBJ_SYS_ALL_ADM_ALL = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");

NTSTATUS NTAPI WdmlibIoCreateDeviceSecure(PDRIVER_OBJECT DriverObject, ULONG DeviceExtensionSize, PUNICODE_STRING DeviceName,
                                          DEVICE_TYPE DeviceType, ULONG DeviceCharacteristics, BOOLEAN Exclusive,
                                          PCUNICODE_STRING DefaultSDDLString, LPCGUID DeviceClassGuid,
                                          PDEVICE_OBJECT *DeviceObject)
{
    (void)DeviceClassGuid;
    if (DefaultSDDLString == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return IoCreateDevice(DriverObject, DeviceExtensionSize, DeviceName, DeviceType, DeviceCharacteristics, Exclusive,
                          DeviceObject);
}
