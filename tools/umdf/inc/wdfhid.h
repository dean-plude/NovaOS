/* wdfhid.h — UMDF's HID DDIs (NovaOS stand-in for the WDK header, README.md):
 * a UMDF driver can expose a HID interface (the host's reflector then
 * stands in for the HID minidriver) */
#pragma once
typedef VOID (__stdcall *PFN_WDFDEVICEINITENABLEHIDINTERFACE)(PWDF_DRIVER_GLOBALS DriverGlobals, PWDFDEVICE_INIT DeviceInit);
typedef NTSTATUS (__stdcall *PFN_WDFDEVICEHIDNOTIFYPRESENCE)(PWDF_DRIVER_GLOBALS DriverGlobals, WDFDEVICE Device,
                                                            BOOLEAN IsPresent);
