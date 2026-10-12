/*
 * wdfldr.c — WdfLdr.sys, the KMDF loader, written for NovaOS
 *
 * Microsoft published the framework (Wdf01000.sys, built by build_kmdf.py)
 * but not its loader.  This is NovaOS's, implementing the interface the
 * published fxldr.h declares, the one the framework and every KMDF client
 * driver call:
 *
 *   WdfRegisterLibrary   the framework, from its DriverEntry: "I am
 *                        Wdf01000, version 1.33, here are my entry points";
 *                        the loader commissions it (LibraryCommission)
 *   WdfVersionBind       a client, before its own DriverEntry (the stub
 *                        every KMDF driver links, tools/kmdf/stub.c): the
 *                        loader starts the framework service the client's
 *                        major version names (Wdf01000 for KMDF 1.x) if it
 *                        is not running, and registers the client with it
 *                        (LibraryRegisterClient), which fills the client's
 *                        function table and WDF_DRIVER_GLOBALS
 *   WdfVersionUnbind     a client, after its DriverUnload
 *   WdfLdrQueryInterface the same three routines as an interface
 *   WdfLdrDiagnosticsValueByNameAsULONG
 *                        a value from the loader's diagnostics key
 *                        (HKLM\SYSTEM\CurrentControlSet\Control\Wdf\Kmdf\
 *                        Diagnostics), e.g. DbgPrintOn
 *
 * Class extension libraries (WdfRegisterClassLibrary, WdfVersionBindClass:
 * Microsoft's UcmCx, SerCx and the like, which bind beside the framework)
 * are not supported yet: NovaOS ships none, and a driver that needs one
 * fails to bind with STATUS_NOT_SUPPORTED, logged by name.
 *
 * Clients import these from WDFLDR.SYS, so the kernel loads this image as
 * an export driver (kernel/nt/ldr.c) and calls DllInitialize first.
 */
#include <ntddk.h>
#include <ntstrsafe.h>
#include <wdf.h>
#include <fxldr.h>

#define LDR_TAG 'rLxF'
#define DPFLTR_IHVDRIVER_ID 77
#define DIAGNOSTICS_KEY L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Wdf\\Kmdf\\Diagnostics"
#define SERVICES_KEY L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\"

/* A registered framework library */
typedef struct _LIBRARY_MODULE {
    LIST_ENTRY Link;
    PWDF_LIBRARY_INFO Info;
    LONG Clients;
    UNICODE_STRING Service;              /* its service name: Wdf01000 */
    WCHAR ServiceBuffer[64];
} LIBRARY_MODULE;

static LIST_ENTRY g_Libraries;
static FAST_MUTEX g_Lock;
static BOOLEAN g_Initialized;

static VOID Initialize(VOID)
{
    if (!g_Initialized) {
        InitializeListHead(&g_Libraries);
        ExInitializeFastMutex(&g_Lock);
        g_Initialized = TRUE;
    }
}

/* The last component of a service key path: "...\Services\Wdf01000" */
static VOID ServiceName(PCUNICODE_STRING Path, PUNICODE_STRING Name)
{
    USHORT n = Path->Length / sizeof(WCHAR), i = n;

    while (i > 0 && Path->Buffer[i - 1] != L'\\') {
        i--;
    }
    Name->Buffer = Path->Buffer + i;
    Name->Length = Name->MaximumLength = (USHORT)((n - i) * sizeof(WCHAR));
}

static LIBRARY_MODULE *FindLocked(PCUNICODE_STRING Service)
{
    for (PLIST_ENTRY e = g_Libraries.Flink; e != &g_Libraries; e = e->Flink) {
        LIBRARY_MODULE *lib = CONTAINING_RECORD(e, LIBRARY_MODULE, Link);
        if (RtlEqualUnicodeString(&lib->Service, Service, TRUE)) {
            return lib;
        }
    }
    return NULL;
}

static LIBRARY_MODULE *Find(PCUNICODE_STRING Service)
{
    LIBRARY_MODULE *lib;

    ExAcquireFastMutex(&g_Lock);
    lib = FindLocked(Service);
    ExReleaseFastMutex(&g_Lock);
    return lib;
}

NTSTATUS
WdfRegisterLibrary(
    __in PWDF_LIBRARY_INFO LibraryInfo,
    __in PUNICODE_STRING ServicePath,
    __in PCUNICODE_STRING LibraryDeviceName
    )
{
    UNICODE_STRING name;
    LIBRARY_MODULE *lib;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(LibraryDeviceName);
    Initialize();
    if (LibraryInfo == NULL || LibraryInfo->Size != sizeof(WDF_LIBRARY_INFO) || ServicePath == NULL ||
        LibraryInfo->LibraryCommission == NULL || LibraryInfo->LibraryRegisterClient == NULL ||
        LibraryInfo->LibraryUnregisterClient == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    ServiceName(ServicePath, &name);
    if (name.Length == 0 || name.Length > sizeof(lib->ServiceBuffer) - sizeof(WCHAR)) {
        return STATUS_INVALID_PARAMETER;
    }
    lib = (LIBRARY_MODULE *)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*lib), LDR_TAG);
    if (lib == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    lib->Info = LibraryInfo;
    RtlCopyMemory(lib->ServiceBuffer, name.Buffer, name.Length);
    lib->Service.Buffer = lib->ServiceBuffer;
    lib->Service.Length = name.Length;
    lib->Service.MaximumLength = sizeof(lib->ServiceBuffer);

    status = LibraryInfo->LibraryCommission();
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "WdfLdr: %wZ failed to commission: 0x%08x\n", &lib->Service,
                   status);
        ExFreePoolWithTag(lib, LDR_TAG);
        return status;
    }
    ExAcquireFastMutex(&g_Lock);
    if (FindLocked(&lib->Service) != NULL) {
        ExReleaseFastMutex(&g_Lock);
        LibraryInfo->LibraryDecommission();
        ExFreePoolWithTag(lib, LDR_TAG);
        return STATUS_OBJECT_NAME_COLLISION;
    }
    InsertTailList(&g_Libraries, &lib->Link);
    ExReleaseFastMutex(&g_Lock);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "WdfLdr: %wZ registered, KMDF %u.%u\n", &lib->Service,
               LibraryInfo->Version.Major, LibraryInfo->Version.Minor);
    return STATUS_SUCCESS;
}

/* The framework library a client of @BindInfo's major version binds to,
 * started if it is not running */
static NTSTATUS LibraryFor(PWDF_BIND_INFO BindInfo, LIBRARY_MODULE **Library)
{
    WCHAR serviceBuffer[16], pathBuffer[128];
    UNICODE_STRING service, path;
    NTSTATUS status;

    RtlInitEmptyUnicodeString(&service, serviceBuffer, sizeof(serviceBuffer));
    status = RtlUnicodeStringPrintf(&service, L"Wdf%02u000", BindInfo->Version.Major);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    *Library = Find(&service);
    if (*Library != NULL) {
        return STATUS_SUCCESS;
    }
    RtlInitEmptyUnicodeString(&path, pathBuffer, sizeof(pathBuffer));
    status = RtlUnicodeStringPrintf(&path, SERVICES_KEY L"%wZ", &service);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    /* (its DriverEntry calls WdfRegisterLibrary; the lock is not held here) */
    status = ZwLoadDriver(&path);
    if (!NT_SUCCESS(status) && status != STATUS_IMAGE_ALREADY_LOADED) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "WdfLdr: cannot start %wZ: 0x%08x\n", &service, status);
        return status;
    }
    *Library = Find(&service);
    return *Library != NULL ? STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS
WdfVersionBind(
    __in    PDRIVER_OBJECT DriverObject,
    __in    PUNICODE_STRING RegistryPath,
    __inout PWDF_BIND_INFO BindInfo,
    __out   PWDF_COMPONENT_GLOBALS* ComponentGlobals
    )
{
    CLIENT_INFO client;
    LIBRARY_MODULE *lib;
    PVOID context;
    NTSTATUS status;

    Initialize();
    if (DriverObject == NULL || RegistryPath == NULL || BindInfo == NULL || ComponentGlobals == NULL ||
        BindInfo->FuncTable == NULL || BindInfo->Size < sizeof(WDF_BIND_INFO)) {
        return STATUS_INVALID_PARAMETER;
    }
    *ComponentGlobals = NULL;
    status = LibraryFor(BindInfo, &lib);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    client.Size = sizeof(client);
    client.RegistryPath = RegistryPath;
    client.DriverObject = DriverObject;
    context = &client;
    status = lib->Info->LibraryRegisterClient(BindInfo, ComponentGlobals, &context);
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "WdfLdr: %wZ (KMDF %u.%u) cannot bind to %wZ: 0x%08x\n", RegistryPath, BindInfo->Version.Major,
                   BindInfo->Version.Minor, &lib->Service, status);
        return status;
    }
    BindInfo->Module = (PLIBRARY_MODULE)lib;
    InterlockedIncrement(&lib->Clients);
    return STATUS_SUCCESS;
}

NTSTATUS
WdfVersionUnbind(
    __in PUNICODE_STRING RegistryPath,
    __in PWDF_BIND_INFO BindInfo,
    __in PWDF_COMPONENT_GLOBALS ComponentGlobals
    )
{
    LIBRARY_MODULE *lib;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);
    if (BindInfo == NULL || BindInfo->Module == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    lib = (LIBRARY_MODULE *)BindInfo->Module;
    status = lib->Info->LibraryUnregisterClient(BindInfo, ComponentGlobals);
    BindInfo->Module = NULL;
    InterlockedDecrement(&lib->Clients);
    return status;
}

NTSTATUS
WdfRegisterClassLibrary(
    _In_ PWDF_CLASS_LIBRARY_INFO ClassLibraryInfo,
    _In_ PUNICODE_STRING SourceString,
    _In_ PUNICODE_STRING ObjectName
    )
{
    UNREFERENCED_PARAMETER(ClassLibraryInfo);
    UNREFERENCED_PARAMETER(ObjectName);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "WdfLdr: %wZ: class extension libraries are not supported\n",
               SourceString);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
WdfVersionBindClass(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PWDF_BIND_INFO BindInfo,
    _In_ PWDF_COMPONENT_GLOBALS ComponentGlobals,
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(BindInfo);
    UNREFERENCED_PARAMETER(ComponentGlobals);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
               "WdfLdr: %wZ needs the class extension %ws, which NovaOS does not have\n", RegistryPath,
               ClassBindInfo != NULL ? ClassBindInfo->ClassName : L"?");
    return STATUS_NOT_SUPPORTED;
}

VOID
WdfVersionUnbindClass(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PWDF_BIND_INFO BindInfo,
    _In_ PWDF_COMPONENT_GLOBALS ComponentGlobals,
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(BindInfo);
    UNREFERENCED_PARAMETER(ComponentGlobals);
    UNREFERENCED_PARAMETER(ClassBindInfo);
}

NTSTATUS
WdfLdrQueryInterface(
    _In_ PWDF_INTERFACE_HEADER LoaderInterface
    )
{
    PWDF_LOADER_INTERFACE i = (PWDF_LOADER_INTERFACE)LoaderInterface;

    if (LoaderInterface == NULL || LoaderInterface->InterfaceType == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!IsEqualGUID(LoaderInterface->InterfaceType, &GUID_WDF_LOADER_INTERFACE_STANDARD) ||
        LoaderInterface->InterfaceSize < sizeof(WDF_LOADER_INTERFACE)) {
        return STATUS_NOINTERFACE;
    }
    i->RegisterLibrary = WdfRegisterLibrary;
    i->VersionBind = WdfVersionBind;
    i->VersionUnbind = WdfVersionUnbind;
    i->DiagnosticsValueByNameAsULONG = WdfLdrDiagnosticsValueByNameAsULONG;
    return STATUS_SUCCESS;
}

NTSTATUS
WdfLdrDiagnosticsValueByNameAsULONG(
    _In_ PUNICODE_STRING ValueName,
    _Out_ PULONG Value
    )
{
    UNICODE_STRING keyName = RTL_CONSTANT_STRING(DIAGNOSTICS_KEY);
    OBJECT_ATTRIBUTES attributes;
    UCHAR buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)buffer;
    HANDLE key;
    ULONG length;
    NTSTATUS status;

    if (ValueName == NULL || Value == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Value = 0;
    InitializeObjectAttributes(&attributes, &keyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    status = ZwOpenKey(&key, KEY_READ, &attributes);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = ZwQueryValueKey(key, ValueName, KeyValuePartialInformation, info, sizeof(buffer), &length);
    ZwClose(key);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (info->Type != REG_DWORD || info->DataLength != sizeof(ULONG)) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    *Value = *(PULONG)info->Data;
    return STATUS_SUCCESS;
}

/* Called by the kernel's loader when it loads this image for a client's
 * imports, before the client's DriverEntry */
NTSTATUS DllInitialize(_In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    Initialize();
    return STATUS_SUCCESS;
}

NTSTATUS DllUnload(VOID)
{
    /* (stays loaded while a framework is registered with it) */
    return IsListEmpty(&g_Libraries) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/* Started as a service (Windows starts WdfLdr at boot): nothing else to do */
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    Initialize();
    return STATUS_SUCCESS;
}
