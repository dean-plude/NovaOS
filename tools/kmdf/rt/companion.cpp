/*
 * companion.cpp — driver companions, not available (tools/kmdf/inc/
 * FxCompanionLibrary.hpp): the framework's companion calls, which
 * Microsoft did not publish.  No device on NovaOS has a companion target,
 * so the PnP callbacks have no companion to call first and the companion
 * DDIs find none.
 */
#include "pnppriv.hpp"

NTSTATUS FxPnpDevicePrepareHardware::InvokeCompanionCallback(_In_ FxCompanionTarget *CompanionTarget)
{
    UNREFERENCED_PARAMETER(CompanionTarget);
    return STATUS_SUCCESS;
}

NTSTATUS FxPnpDeviceReleaseHardware::InvokeCompanionCallback(_In_ FxCompanionTarget *CompanionTarget)
{
    UNREFERENCED_PARAMETER(CompanionTarget);
    return STATUS_SUCCESS;
}

NTSTATUS FxPnpDeviceD0Entry::InvokeCompanionCallback(_In_ FxCompanionTarget *CompanionTarget)
{
    UNREFERENCED_PARAMETER(CompanionTarget);
    return STATUS_SUCCESS;
}

NTSTATUS FxPnpDeviceD0Exit::InvokeCompanionCallback(_In_ FxCompanionTarget *CompanionTarget)
{
    UNREFERENCED_PARAMETER(CompanionTarget);
    return STATUS_SUCCESS;
}

extern "C" {

_Must_inspect_result_
NTSTATUS
WDFEXPORT(WdfCompanionTargetSendTaskSynchronously)(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFCOMPANIONTARGET CompanionTarget,
    _In_ USHORT TaskQueueIdentifier,
    _In_ ULONG TaskOperationCode,
    _In_opt_ PWDF_MEMORY_DESCRIPTOR InputBuffer,
    _In_opt_ PWDF_MEMORY_DESCRIPTOR OutputBuffer,
    _In_opt_ PWDF_TASK_SEND_OPTIONS TaskOptions,
    _Out_ PULONG_PTR BytesReturned
    )
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(CompanionTarget);
    UNREFERENCED_PARAMETER(TaskQueueIdentifier);
    UNREFERENCED_PARAMETER(TaskOperationCode);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(TaskOptions);
    *BytesReturned = 0;
    return STATUS_NOT_SUPPORTED;
}

_Must_inspect_result_
PEPROCESS
WDFEXPORT(WdfCompanionTargetWdmGetCompanionProcess)(
    _In_ PWDF_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFCOMPANIONTARGET CompanionTarget
    )
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    UNREFERENCED_PARAMETER(CompanionTarget);
    return NULL;
}

} // extern "C"
