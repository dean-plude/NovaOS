/*
 * ntstrsafe.h — mingw-w64's ntstrsafe.h plus the UNICODE_STRING routines
 * it leaves out (RtlUnicodeString*), written as the WDK's header does:
 * inline, over the counted-buffer routines it does have
 */
#pragma once
#include_next <ntstrsafe.h>

#ifndef NTSTRSAFE_UNICODE_STRING_MAX_CCH
#define NTSTRSAFE_UNICODE_STRING_MAX_CCH 32767
#endif

static __inline NTSTATUS RtlUnicodeStringVPrintf(_Inout_ PUNICODE_STRING DestinationString,
                                                 _In_ NTSTRSAFE_PCWSTR pszFormat, _In_ va_list argList)
{
    NTSTATUS status;
    size_t cch = DestinationString->MaximumLength / sizeof(WCHAR);
    size_t len = 0;

    if (DestinationString->Buffer == NULL || cch == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    /* (one WCHAR for the terminator the counted routines write; a
     * UNICODE_STRING needs none, but the WDK's version gives up the same) */
    status = RtlStringCchVPrintfW(DestinationString->Buffer, cch, pszFormat, argList);
    if (NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW) {
        RtlStringCchLengthW(DestinationString->Buffer, cch, &len);
        DestinationString->Length = (USHORT)(len * sizeof(WCHAR));
    }
    return status;
}

static __inline NTSTATUS RtlUnicodeStringPrintf(_Inout_ PUNICODE_STRING DestinationString,
                                                _In_ NTSTRSAFE_PCWSTR pszFormat, ...)
{
    NTSTATUS status;
    va_list args;

    va_start(args, pszFormat);
    status = RtlUnicodeStringVPrintf(DestinationString, pszFormat, args);
    va_end(args);
    return status;
}

static __inline NTSTATUS RtlUnicodeStringCopy(_Inout_ PUNICODE_STRING DestinationString,
                                              _In_ PCUNICODE_STRING SourceString)
{
    USHORT n = SourceString->Length;
    NTSTATUS status = STATUS_SUCCESS;

    if (n > DestinationString->MaximumLength) {
        n = DestinationString->MaximumLength & ~1;
        status = STATUS_BUFFER_OVERFLOW;
    }
    if (n != 0) {
        RtlMoveMemory(DestinationString->Buffer, SourceString->Buffer, n);
    }
    DestinationString->Length = n;
    return status;
}
