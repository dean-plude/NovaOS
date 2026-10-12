/*
 * FxCompanionLibrary.hpp — driver companions, not available
 *
 * A Windows 10 driver can declare a "companion": a user-mode or secure
 * (VBS) process the framework starts beside each device and talks to
 * through WDFCOMPANIONTARGET.  Microsoft did not publish this part of the
 * framework.  NovaOS has no companions: no device ever needs one, so the
 * framework never creates a companion target (FxCompanionTarget.hpp).
 */
#pragma once

class FxCompanionLibrary {
public:
    static NTSTATUS _CreateAndInitialize(_Out_ FxCompanionLibrary **Library)
    {
        *Library = new FxCompanionLibrary();
        return (*Library != NULL) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
    }

    BOOLEAN IsCompanionRequiredForDevice(_In_ FxDevice *Device, _Out_ PCWSTR *CompanionName)
    {
        UNREFERENCED_PARAMETER(Device);
        *CompanionName = NULL;
        return FALSE;
    }

    PVOID operator new(size_t Size)
    {
        return ExAllocatePool2(POOL_FLAG_NON_PAGED, Size, 'pCxF');
    }

    VOID operator delete(PVOID Pointer)
    {
        if (Pointer != NULL) {
            ExFreePool(Pointer);
        }
    }
};
