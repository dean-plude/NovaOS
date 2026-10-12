/*
 * rt.c — what the user-mode framework links that NovaOS's system DLLs do
 * not export: the kernel's debug print and registry query, which the shared
 * framework code calls in both modes, and ETW's TraceMessageVa (uuid.c
 * has COM's IID_IUnknown)
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "wdmdefs.h"

/* DbgPrintEx: errors and warnings to the debug output, as WPP messages are
 * filtered in wpp.c */
static ULONG __cdecl NovaDbgPrintEx(ULONG ComponentId, ULONG Level, PCSTR Format, ...)
{
    char msg[512];
    va_list ap;
    (void)ComponentId;
    if (Level > DPFLTR_WARNING_LEVEL) {
        return 0;
    }
    va_start(ap, Format);
    _vsnprintf(msg, sizeof(msg) - 1, Format, ap);
    va_end(ap);
    msg[sizeof(msg) - 1] = 0;
    OutputDebugStringA(msg);
    return 0;
}
void *__imp_DbgPrintEx = (void *)NovaDbgPrintEx;

/* RtlQueryRegistryValuesEx for the one form the framework uses (globals.cpp
 * reading its verifier settings): RTL_REGISTRY_HANDLE with
 * RTL_QUERY_REGISTRY_DIRECT entries of DWORD values, the handle being a
 * key the framework opened with RegOpenKeyEx; a value that is not there
 * takes its entry's default. */
static NTSTATUS NTAPI NovaRtlQueryRegistryValuesEx(ULONG RelativeTo, PCWSTR Path, PRTL_QUERY_REGISTRY_TABLE Table,
                                                   PVOID Context, PVOID Environment)
{
    (void)Context; (void)Environment;
    if ((RelativeTo & 0xFFFF) != RTL_REGISTRY_HANDLE) {
        return STATUS_NOT_SUPPORTED;
    }
    for (; Table->QueryRoutine != NULL || Table->Name != NULL; Table++) {
        if (!(Table->Flags & RTL_QUERY_REGISTRY_DIRECT) || Table->Name == NULL || Table->EntryContext == NULL) {
            return STATUS_NOT_SUPPORTED;
        }
        DWORD type = 0, value = 0, size = sizeof(value);
        if (RegQueryValueExW((HKEY)Path, Table->Name, NULL, &type, (BYTE *)&value, &size) == ERROR_SUCCESS &&
            type == REG_DWORD && size == sizeof(value)) {
            *(ULONG *)Table->EntryContext = value;
        } else if (Table->DefaultType == REG_DWORD && Table->DefaultData != NULL) {
            *(ULONG *)Table->EntryContext = *(ULONG *)Table->DefaultData;
        } else if (Table->Flags & RTL_QUERY_REGISTRY_REQUIRED) {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
    }
    return STATUS_SUCCESS;
}
void *__imp_RtlQueryRegistryValuesEx = (void *)NovaRtlQueryRegistryValuesEx;

/* No ETW session records the framework's trace messages (wpp.c prints the
 * ones that matter) */
ULONG TraceMessageVa(ULONG64 LoggerHandle, ULONG MessageFlags, LPCGUID MessageGuid, USHORT MessageNumber,
                     va_list MessageArgList)
{
    (void)LoggerHandle; (void)MessageFlags; (void)MessageGuid; (void)MessageNumber; (void)MessageArgList;
    return ERROR_SUCCESS;
}
