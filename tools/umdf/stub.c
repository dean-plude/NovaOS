/*
 * stub.c — the UMDF 2 client stub: what the WDK's WdfDriverStubUm.lib is
 * to a UMDF driver, NovaOS's
 *
 * A UMDF 2 driver is a DLL whose entry point for the host is
 * FxDriverEntryUm (wdf.h), from the stub library every driver links; the
 * stub binds the driver to the framework through the loader interface the
 * host passes it (NOVA_WUDF_LOADER, tools/umdf/inc/novaos_wudf_loader.h),
 * which fills WdfFunctions with the framework's function table and
 * WdfDriverGlobals with the driver's globals, then calls DriverEntry.
 * When the framework unloads the driver the stub unbinds it again.
 *
 * Microsoft published neither the stub nor the interface: a UMDF driver
 * runs on NovaOS when it is linked with this stub (docs/drivers.md).
 */
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <wdf.h>
#include <fxldr.h>
#include "novaos_wudf_loader.h"

/* The start of the framework's DRIVER_OBJECT_UM (fxldrum.h, a C++
 * header), as far as the stub uses it */
typedef struct _NOVA_DRIVER_OBJECT_UM {
    ULONG Size;
    ULONG Flags;
    UNICODE_STRING DriverName;
    PVOID FxDriver;
    PVOID DriverLoadContext;
    TP_CALLBACK_ENVIRON ThreadPoolEnv;
    PTP_CLEANUP_GROUP ThreadPoolGroup;
    LONG IfrSequenceNumber;
    PVOID AddDevice;
    VOID (*DriverUnload)(struct _NOVA_DRIVER_OBJECT_UM *DriverObject);
} NOVA_DRIVER_OBJECT_UM, *PNOVA_DRIVER_OBJECT_UM;
typedef VOID (*PFN_DRIVER_UNLOAD_UM)(PNOVA_DRIVER_OBJECT_UM DriverObject);

/* The globals wdf.h and wdffuncenum.h declare for every client */
PWDF_DRIVER_GLOBALS WdfDriverGlobals;
const WDFFUNC *WdfFunctions_02033;
BOOLEAN WdfClientVersionHigherThanFramework;
ULONG WdfFunctionCount = WdfFunctionTableNumEntries;
ULONG WdfStructureCount = WDF_STRUCTURE_TABLE_NUM_ENTRIES;
WDF_STRUCT_INFO WdfStructures;

static WDF_BIND_INFO2 WdfBindInfo = {
    {
        sizeof(WDF_BIND_INFO2),
        L"UmdfLibrary",
        { 2, 33, 0 },
        WdfFunctionTableNumEntries,
        (WDFFUNC *)&WdfFunctions_02033,         /* (the framework stores its table's address here) */
        NULL
    },
    &WdfMinimumVersionRequired,
    &WdfClientVersionHigherThanFramework,
    &WdfFunctionCount,
    &WdfStructureCount,
    &WdfStructures,
};

DRIVER_INITIALIZE DriverEntry;

static NOVA_WUDF_LOADER *g_Loader;
static PVOID g_LoaderContext;
static UNICODE_STRING g_RegistryPath;
static WCHAR g_RegistryPathBuffer[260];
static PFN_DRIVER_UNLOAD_UM g_ClientUnload;

static VOID FxStubDriverUnload(PNOVA_DRIVER_OBJECT_UM DriverObject)
{
    if (g_ClientUnload != NULL) {
        g_ClientUnload(DriverObject);
    }
    g_Loader->VersionUnbind(g_LoaderContext, &g_RegistryPath, &WdfBindInfo.V1, WdfDriverGlobals);
}

__declspec(dllexport) NTSTATUS FxDriverEntryUm(PVOID LoaderInterface, PVOID Context, PVOID DriverObject,
                                               PUNICODE_STRING RegistryPath)
{
    NOVA_WUDF_LOADER *loader = (NOVA_WUDF_LOADER *)LoaderInterface;
    PNOVA_DRIVER_OBJECT_UM driver = (PNOVA_DRIVER_OBJECT_UM)DriverObject;
    NTSTATUS status;

    if (loader == NULL || loader->Size < sizeof(*loader) || loader->Version != NOVA_WUDF_LOADER_VERSION ||
        RegistryPath == NULL || RegistryPath->Length >= sizeof(g_RegistryPathBuffer)) {
        return STATUS_INVALID_PARAMETER;
    }
    g_Loader = loader;
    g_LoaderContext = Context;

    /* (kept for the unbind: the host's copy lives only during this call) */
    CopyMemory(g_RegistryPathBuffer, RegistryPath->Buffer, RegistryPath->Length);
    g_RegistryPathBuffer[RegistryPath->Length / sizeof(WCHAR)] = 0;
    g_RegistryPath.Buffer = g_RegistryPathBuffer;
    g_RegistryPath.Length = RegistryPath->Length;
    g_RegistryPath.MaximumLength = sizeof(g_RegistryPathBuffer);

    status = loader->VersionBind(Context, &g_RegistryPath, &WdfBindInfo.V1, (PVOID *)&WdfDriverGlobals);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = DriverEntry((PDRIVER_OBJECT)DriverObject, &g_RegistryPath);
    if (!NT_SUCCESS(status)) {
        loader->VersionUnbind(Context, &g_RegistryPath, &WdfBindInfo.V1, WdfDriverGlobals);
        return status;
    }
    /* WdfDriverCreate set the framework's unload routine; the unbind follows it */
    if (driver->DriverUnload != NULL) {
        g_ClientUnload = driver->DriverUnload;
        driver->DriverUnload = FxStubDriverUnload;
    }
    return status;
}

/* The DLL's own entry point: nothing to do, the host calls FxDriverEntryUm */
BOOL WINAPI FxStubDllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    (void)Reserved;
    if (Reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(Instance);
    }
    return TRUE;
}
