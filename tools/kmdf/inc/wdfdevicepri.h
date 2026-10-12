/*
 * wdfdevicepri.h — NovaOS stand-in for the WDK header of this name
 *
 * The device DDIs the WDK keeps private (a device sending requests to its own queues).  Microsoft's published WDF source uses these declarations
 * but does not include the header; the DDI typedefs below are taken from
 * the framework's own definitions of each DDI (framework/shared/
 * enhancedverif/km/vfwdfdynamics.cpp, MIT), the rest written from how the
 * framework uses them.  See tools/kmdf/inc/README.md.
 */
#pragma once

typedef
WDFIOTARGET
(NTAPI *PFN_WDFDEVICEGETSELFIOTARGET)(
    _In_
    PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDEVICE Device
    );

typedef
VOID
(NTAPI *PFN_WDFDEVICEINITALLOWSELFIOTARGET)(
    _In_
    PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_
    PWDFDEVICE_INIT DeviceInit
    );

