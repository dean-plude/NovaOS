/*
 * stub.c — the KMDF client stub, NovaOS's WdfDriverEntry.lib
 *
 * Every KMDF driver links this from the WDK.  Its entry point is
 * FxDriverEntry, not the driver's DriverEntry: it binds the driver to the
 * framework through WdfLdr.sys (WdfVersionBind, which fills WdfFunctions
 * with the framework's function table and WdfDriverGlobals with the
 * driver's globals), then calls DriverEntry.  When the driver unloads it
 * unbinds again, after the framework's own unload routine.
 *
 * Microsoft does not publish the stub; this one is written to the loader
 * interface of the published fxldr.h and the globals wdf.h declares, so a
 * driver built against the WDK's headers links it unchanged.
 */
#include <ntddk.h>
#include <wdf.h>
#include <fxldr.h>

/* The globals wdf.h and wdffuncenum.h declare for every client */
PWDF_DRIVER_GLOBALS WdfDriverGlobals;
const WDFFUNC *WdfFunctions_01033;
WDFFUNC WdfDriverMiniportUnloadOverride;
BOOLEAN WdfClientVersionHigherThanFramework;
ULONG WdfFunctionCount = WdfFunctionTableNumEntries;
ULONG WdfStructureCount = WDF_STRUCTURE_TABLE_NUM_ENTRIES;
WDF_STRUCT_INFO WdfStructures;

static WDF_BIND_INFO2 WdfBindInfo = {
    {
        sizeof(WDF_BIND_INFO2),
        L"KmdfLibrary",
        { KMDF_VERSION_MAJOR, KMDF_VERSION_MINOR, 0 },
        WdfFunctionTableNumEntries,
        (WDFFUNC *)&WdfFunctions_01033,        /* (the framework stores its table's address here) */
        NULL
    },
    &WdfMinimumVersionRequired,
    &WdfClientVersionHigherThanFramework,
    &WdfFunctionCount,
    &WdfStructureCount,
    &WdfStructures,
};

DRIVER_INITIALIZE DriverEntry;
DRIVER_INITIALIZE FxDriverEntry;

static PDRIVER_UNLOAD g_ClientUnload;
static UNICODE_STRING g_RegistryPath;

static VOID FxStubDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    if (g_ClientUnload != NULL) {
        g_ClientUnload(DriverObject);
    }
    WdfVersionUnbind(&g_RegistryPath, &WdfBindInfo.V1, (PWDF_COMPONENT_GLOBALS)WdfDriverGlobals);
    ExFreePool(g_RegistryPath.Buffer);
}

NTSTATUS FxDriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    /* (kept for the unbind: the I/O manager's copy lives only during DriverEntry) */
    g_RegistryPath.Buffer = (PWCH)ExAllocatePool2(POOL_FLAG_PAGED, RegistryPath->MaximumLength + sizeof(WCHAR), 'butS');
    if (g_RegistryPath.Buffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    g_RegistryPath.MaximumLength = RegistryPath->MaximumLength + sizeof(WCHAR);
    RtlCopyUnicodeString(&g_RegistryPath, RegistryPath);

    status = WdfVersionBind(DriverObject, &g_RegistryPath, &WdfBindInfo.V1, (PWDF_COMPONENT_GLOBALS *)&WdfDriverGlobals);
    if (!NT_SUCCESS(status)) {
        ExFreePool(g_RegistryPath.Buffer);
        return status;
    }
    status = DriverEntry(DriverObject, RegistryPath);
    if (!NT_SUCCESS(status)) {
        WdfVersionUnbind(&g_RegistryPath, &WdfBindInfo.V1, (PWDF_COMPONENT_GLOBALS)WdfDriverGlobals);
        ExFreePool(g_RegistryPath.Buffer);
        return status;
    }
    /* WdfDriverCreate set the framework's unload routine; the unbind follows it */
    if (DriverObject->DriverUnload != NULL) {
        g_ClientUnload = DriverObject->DriverUnload;
        DriverObject->DriverUnload = FxStubDriverUnload;
    }
    return status;
}
