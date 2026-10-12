/*
 * FxCompanionTarget.hpp — a device's companion target (see
 * FxCompanionLibrary.hpp): the object type exists so WDFCOMPANIONTARGET
 * handles validate, but NovaOS never creates one (Init fails)
 */
#pragma once

class FxCompanionTarget : public FxNonPagedObject {
public:
    FxCompanionTarget(_In_ PFX_DRIVER_GLOBALS FxDriverGlobals, _In_ USHORT ObjectSize)
        : FxNonPagedObject(FX_TYPE_COMPANION_TARGET, ObjectSize, FxDriverGlobals)
    {
    }

    _Must_inspect_result_
    NTSTATUS Init(_In_ FxDevice *Device)
    {
        UNREFERENCED_PARAMETER(Device);
        return STATUS_NOT_SUPPORTED;
    }

    VOID QueryPnPDeviceStateNotification(VOID)
    {
    }

    _Must_inspect_result_
    NTSTATUS HandleQueryInterfaceForSecureDriver(_In_ FxIrp *Irp, _Out_ PBOOLEAN CompleteRequest)
    {
        *CompleteRequest = FALSE;
        return Irp->GetStatus();
    }
};
