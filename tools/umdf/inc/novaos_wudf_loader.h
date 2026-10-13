/*
 * novaos_wudf_loader.h — how a UMDF 2 driver binds to the framework on
 * NovaOS: the loader interface the host (WUDFHost.exe) passes to the
 * driver's FxDriverEntryUm, and the stub (tools/umdf/stub.c) uses
 *
 * Every UMDF 2 driver exports FxDriverEntryUm (wdf.h) from the WDK's stub
 * library, which binds the driver to the framework through an interface
 * the host passes it, then calls the driver's DriverEntry.  Microsoft
 * published neither the stub nor that interface; this is NovaOS's, so a
 * UMDF driver runs on NovaOS when it is linked with NovaOS's stub
 * (docs/drivers.md says what that means for Windows' own UMDF binaries).
 */
#pragma once

#define NOVA_WUDF_LOADER_VERSION 1

typedef struct _NOVA_WUDF_LOADER {
    ULONG Size;                         /* sizeof(NOVA_WUDF_LOADER) */
    ULONG Version;                      /* NOVA_WUDF_LOADER_VERSION */
    /* WdfVersionBind: the framework's LibraryRegisterClient for the
     * driver's bind information (the function table, its globals) */
    NTSTATUS (*VersionBind)(PVOID Context, PCUNICODE_STRING RegistryPath, PVOID BindInfo, PVOID *ComponentGlobals);
    /* WdfVersionUnbind: LibraryUnregisterClient */
    NTSTATUS (*VersionUnbind)(PVOID Context, PCUNICODE_STRING RegistryPath, PVOID BindInfo, PVOID ComponentGlobals);
} NOVA_WUDF_LOADER;
