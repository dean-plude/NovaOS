/*
 * wdfiotargetpri.h — NovaOS stand-in for the WDK header of this name
 *
 * The I/O-target DDIs the WDK keeps private.  Microsoft's published WDF source uses these declarations
 * but does not include the header; the DDI typedefs below are taken from
 * the framework's own definitions of each DDI (framework/shared/
 * enhancedverif/km/vfwdfdynamics.cpp, MIT), the rest written from how the
 * framework uses them.  See tools/kmdf/inc/README.md.
 */
#pragma once

typedef
NTSTATUS
(NTAPI *PFN_WDFIOTARGETSELFASSIGNDEFAULTIOQUEUE)(
    _In_
    PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFIOTARGET IoTarget,
    _In_
    WDFQUEUE Queue
    );

