/*
 * wdfldr.h — NovaOS stand-in for the WDK header of this name
 *
 * The loader side of KMDF: what WdfLdr.sys exports (fxldr.h, published
 * with the WDF source, declares the routines; this header gives the types
 * they use).  NovaOS's WdfLdr.sys (tools/kmdf/wdfldr.c) implements them.
 * Written from Microsoft's documentation and from the published WDF
 * source's use; see tools/kmdf/inc/README.md.
 */
#pragma once
#include "wdfcxbase.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What a library hands each client: its WDF_DRIVER_GLOBALS */
typedef struct _WDF_DRIVER_GLOBALS WDF_COMPONENT_GLOBALS, *PWDF_COMPONENT_GLOBALS;

typedef struct _WDF_BIND_INFO *PWDF_BIND_INFO;

typedef struct _WDF_INTERFACE_HEADER {
    const GUID *InterfaceType;
    ULONG InterfaceSize;
} WDF_INTERFACE_HEADER, *PWDF_INTERFACE_HEADER;

typedef NTSTATUS (*PWDF_LDR_DIAGNOSTICS_VALUE_BY_NAME_AS_ULONG)(
    _In_ PUNICODE_STRING ValueName,
    _Out_ PULONG Value);

NTSTATUS WdfLdrDiagnosticsValueByNameAsULONG(
    _In_ PUNICODE_STRING ValueName,
    _Out_ PULONG Value);

NTSTATUS WdfLdrQueryInterface(
    _In_ PWDF_INTERFACE_HEADER LoaderInterface);

NTSTATUS WdfVersionBindClass(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ struct _WDF_BIND_INFO *BindInfo,
    _In_ PWDF_COMPONENT_GLOBALS ComponentGlobals,
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo);

VOID WdfVersionUnbindClass(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ struct _WDF_BIND_INFO *BindInfo,
    _In_ PWDF_COMPONENT_GLOBALS ComponentGlobals,
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo);

NTSTATUS WdfRegisterClassLibrary(
    _In_ PWDF_CLASS_LIBRARY_INFO ClassLibraryInfo,
    _In_ PUNICODE_STRING SourceString,
    _In_ PUNICODE_STRING ObjectName);

#ifdef __cplusplus
}
#endif

/* The library's name in WdfLdr's diagnostics (Wdf01000) */
#ifndef KMDF_DEFAULT_NAME
#define KMDF_DEFAULT_NAME "Wdf01000"
#endif
