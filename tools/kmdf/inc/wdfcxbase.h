/*
 * wdfcxbase.h — NovaOS stand-in for the WDK header of this name
 *
 * How a client driver binds to a class extension (a framework library
 * such as NetAdapterCx, loaded through WdfLdr.sys like Wdf01000.sys):
 * the client's stub lists a WDF_CLASS_BIND_INFO per extension, and
 * WdfVersionBindClass fills its function table from the extension's
 * WDF_CLASS_LIBRARY_INFO.  Written from Microsoft's documentation and
 * from how the published WDF source uses these types (fxldr.h walks the
 * bind-info array); see tools/kmdf/inc/README.md.
 */
#pragma once

typedef ULONG WDF_MAJOR_VERSION;
typedef ULONG WDF_MINOR_VERSION;
typedef ULONG WDF_BUILD_NUMBER;

typedef struct _WDF_CLASS_VERSION {
    WDF_MAJOR_VERSION Major;
    WDF_MINOR_VERSION Minor;
    WDF_BUILD_NUMBER  Build;
} WDF_CLASS_VERSION;

typedef VOID (*PFN_WDF_CLASS_EXPORT)(VOID);

struct _WDF_CLASS_BIND_INFO;
typedef struct _WDF_DRIVER_GLOBALS *PWDF_COMPONENT_GLOBALS_CX;

typedef NTSTATUS (*PFN_WDF_CLIENT_BIND_CLASS)(
    _In_ struct _WDF_CLASS_BIND_INFO *ClassInfo,
    _In_ PWDF_COMPONENT_GLOBALS_CX ClientGlobals,
    _In_ PWDF_COMPONENT_GLOBALS_CX ClassGlobals);
typedef VOID (*PFN_WDF_CLIENT_UNBIND_CLASS)(
    _In_ struct _WDF_CLASS_BIND_INFO *ClassInfo,
    _In_ PWDF_COMPONENT_GLOBALS_CX ClientGlobals,
    _In_ PWDF_COMPONENT_GLOBALS_CX ClassGlobals);

typedef struct _WDF_CLASS_BIND_INFO {
    ULONG Size;
    PWCHAR ClassName;
    WDF_CLASS_VERSION Version;
    PFN_WDF_CLASS_EXPORT *FunctionTable;
    ULONG FunctionTableCount;
    PVOID ClassBindInfo;
    PFN_WDF_CLIENT_BIND_CLASS ClientBindClass;
    PFN_WDF_CLIENT_UNBIND_CLASS ClientUnbindClass;
    PVOID ClassModule;                  /* (the loader's, for its own bookkeeping) */
} WDF_CLASS_BIND_INFO, *PWDF_CLASS_BIND_INFO;

/* A client built against a newer extension than the one present says
 * which minor version it needs at least (as WDF_BIND_INFO2 for KMDF) */
typedef struct _WDF_CLASS_BIND_INFO2 {
    WDF_CLASS_BIND_INFO V1;
    WDF_MINOR_VERSION *MinimumVersionRequired;
    BOOLEAN *ClientVersionHigherThanClass;
    ULONG *FunctionTableCountPtr;
    PVOID Reserved;
} WDF_CLASS_BIND_INFO2, *PWDF_CLASS_BIND_INFO2;

typedef NTSTATUS (*PFN_WDF_CLASS_LIBRARY_INITIALIZE)(VOID);
typedef VOID (*PFN_WDF_CLASS_LIBRARY_DEINITIALIZE)(VOID);
typedef NTSTATUS (*PFN_WDF_CLASS_LIBRARY_BIND_CLIENT)(
    _In_ PWDF_CLASS_BIND_INFO ClassInfo,
    _In_ PWDF_COMPONENT_GLOBALS_CX ClientGlobals,
    _Out_ PVOID *ClassBindInfo);
typedef VOID (*PFN_WDF_CLASS_LIBRARY_UNBIND_CLIENT)(
    _In_ PWDF_CLASS_BIND_INFO ClassInfo,
    _In_ PWDF_COMPONENT_GLOBALS_CX ClientGlobals);

typedef struct _WDF_CLASS_LIBRARY_INFO {
    ULONG Size;
    WDF_CLASS_VERSION Version;
    PFN_WDF_CLASS_LIBRARY_INITIALIZE ClassLibraryInitialize;
    PFN_WDF_CLASS_LIBRARY_DEINITIALIZE ClassLibraryDeinitialize;
    PFN_WDF_CLASS_LIBRARY_BIND_CLIENT ClassLibraryBindClient;
    PFN_WDF_CLASS_LIBRARY_UNBIND_CLIENT ClassLibraryUnbindClient;
} WDF_CLASS_LIBRARY_INFO, *PWDF_CLASS_LIBRARY_INFO;

typedef struct _WDF_CLASS_EXTENSION_DESCRIPTOR *PWDF_CLASS_EXTENSION_DESCRIPTOR;
typedef struct _WDF_CLASS_EXTENSION_DESCRIPTOR {
    PWDF_CLASS_EXTENSION_DESCRIPTOR Next;
    ULONG Size;
    PVOID Bind;
    PVOID Unbind;
} WDF_CLASS_EXTENSION_DESCRIPTOR;
