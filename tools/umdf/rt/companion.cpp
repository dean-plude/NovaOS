/*
 * companion.cpp — driver companions, not available: the companion DDIs
 * whose implementation Microsoft did not publish (the verifier's thunk
 * table, vfwdfdynamics.cpp, names them).  No device on NovaOS has a
 * companion, so creating one fails with STATUS_NOT_SUPPORTED.
 */
#include "fxmin.hpp"

extern "C" {

_Must_inspect_result_
NTSTATUS
WDFEXPORT(WdfCompanionCreate)(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _Inout_ PWDFDEVICE_INIT *DeviceInit,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES DeviceAttributes,
    _Out_ WDFCOMPANION *Companion
    )
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(DeviceInit);
    UNREFERENCED_PARAMETER(DeviceAttributes);
    *Companion = NULL;
    return STATUS_NOT_SUPPORTED;
}

HANDLE
WDFEXPORT(WdfCompanionWdmGetSecureDeviceHandle)(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFCOMPANION Companion
    )
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(Companion);
    return NULL;
}

_Must_inspect_result_
NTSTATUS
WDFEXPORT(WdfCompanionCreateTaskQueue)(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFCOMPANION Companion,
    _In_ PWDF_TASK_QUEUE_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES QueueAttributes,
    _Out_opt_ WDFTASKQUEUE *Queue
    )
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(Companion);
    UNREFERENCED_PARAMETER(Config);
    UNREFERENCED_PARAMETER(QueueAttributes);
    if (Queue != NULL) {
        *Queue = NULL;
    }
    return STATUS_NOT_SUPPORTED;
}

} // extern "C"
