/*
 * auxklib.c — the WDK's auxiliary kernel-mode library (aux_klib.lib), the
 * part the framework calls: the loaded-module list (read, as the library
 * does, from ZwQuerySystemInformation's SystemModuleInformation) and the
 * current bug check's data (none: NovaOS's bug checks stop the machine
 * before any driver's callback runs)
 */
#include <ntddk.h>
#include "aux_klib.h"

#define SystemModuleInformation 11

typedef struct _RTL_PROCESS_MODULE_INFORMATION {
    HANDLE Section;
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES {
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES;

NTSYSAPI NTSTATUS NTAPI ZwQuerySystemInformation(ULONG SystemInformationClass, PVOID SystemInformation,
                                                 ULONG SystemInformationLength, PULONG ReturnLength);

NTSTATUS NTAPI AuxKlibInitialize(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AuxKlibQueryModuleInformation(PULONG BufferSize, ULONG ElementSize, PVOID QueryInfo)
{
    RTL_PROCESS_MODULES *mods;
    ULONG need = 0, i, n;
    NTSTATUS status;

    if (BufferSize == NULL || (ElementSize != sizeof(AUX_MODULE_BASIC_INFO) &&
                               ElementSize != sizeof(AUX_MODULE_EXTENDED_INFO))) {
        return STATUS_INVALID_PARAMETER;
    }
    status = ZwQuerySystemInformation(SystemModuleInformation, NULL, 0, &need);
    if (need == 0) {
        return NT_SUCCESS(status) ? STATUS_UNSUCCESSFUL : status;
    }
    mods = (RTL_PROCESS_MODULES *)ExAllocatePool2(POOL_FLAG_PAGED, need, 'lKxA');
    if (mods == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = ZwQuerySystemInformation(SystemModuleInformation, mods, need, &need);
    if (!NT_SUCCESS(status)) {
        ExFreePool(mods);
        return status;
    }
    n = mods->NumberOfModules;
    if (QueryInfo == NULL || *BufferSize < n * ElementSize) {
        *BufferSize = n * ElementSize;
        ExFreePool(mods);
        return QueryInfo == NULL ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
    }
    for (i = 0; i < n; i++) {
        RTL_PROCESS_MODULE_INFORMATION *m = &mods->Modules[i];
        if (ElementSize == sizeof(AUX_MODULE_BASIC_INFO)) {
            ((AUX_MODULE_BASIC_INFO *)QueryInfo)[i].ImageBase = m->ImageBase;
        } else {
            AUX_MODULE_EXTENDED_INFO *e = &((AUX_MODULE_EXTENDED_INFO *)QueryInfo)[i];
            e->BasicInfo.ImageBase = m->ImageBase;
            e->ImageSize = m->ImageSize;
            e->FileNameOffset = m->OffsetToFileName;
            RtlCopyMemory(e->FullPathName, m->FullPathName, sizeof(e->FullPathName));
        }
    }
    *BufferSize = n * ElementSize;
    ExFreePool(mods);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AuxKlibGetBugCheckData(PKBUGCHECK_DATA BugCheckData)
{
    if (BugCheckData == NULL || BugCheckData->BugCheckDataSize != sizeof(KBUGCHECK_DATA)) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(&BugCheckData->BugCheckCode, sizeof(*BugCheckData) - sizeof(ULONG));
    return STATUS_SUCCESS;
}
