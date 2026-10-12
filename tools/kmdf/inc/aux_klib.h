/*
 * aux_klib.h — the WDK's auxiliary kernel-mode library (aux_klib.lib)
 *
 * Declarations as Microsoft documents them; NovaOS's implementation is in
 * tools/kmdf/auxklib.c (the framework uses it only to name the driver that
 * caused a bug check).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define AUX_KLIB_MODULE_PATH_LEN 256

typedef struct _AUX_MODULE_BASIC_INFO {
    PVOID ImageBase;
} AUX_MODULE_BASIC_INFO, *PAUX_MODULE_BASIC_INFO;

typedef struct _AUX_MODULE_EXTENDED_INFO {
    AUX_MODULE_BASIC_INFO BasicInfo;
    ULONG ImageSize;
    USHORT FileNameOffset;
    UCHAR FullPathName[AUX_KLIB_MODULE_PATH_LEN];
} AUX_MODULE_EXTENDED_INFO, *PAUX_MODULE_EXTENDED_INFO;

typedef struct _KBUGCHECK_DATA {
    ULONG BugCheckDataSize;
    ULONG BugCheckCode;
    ULONG_PTR Parameter1;
    ULONG_PTR Parameter2;
    ULONG_PTR Parameter3;
    ULONG_PTR Parameter4;
} KBUGCHECK_DATA, *PKBUGCHECK_DATA;

NTSTATUS NTAPI AuxKlibInitialize(VOID);
NTSTATUS NTAPI AuxKlibQueryModuleInformation(_Inout_ PULONG BufferSize, _In_ ULONG ElementSize,
                                             _Out_writes_bytes_opt_(*BufferSize) PVOID QueryInfo);
NTSTATUS NTAPI AuxKlibGetBugCheckData(_Inout_ PKBUGCHECK_DATA BugCheckData);

#ifdef __cplusplus
}
#endif
