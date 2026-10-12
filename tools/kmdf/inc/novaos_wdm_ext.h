/*
 * novaos_wdm_ext.h — WDK declarations newer than mingw-w64's DDK headers
 *
 * mingw-w64's wdm.h follows the Windows 7 WDK; the KMDF source is written
 * against the Windows 10 one.  These are the later routines, types and
 * constants it uses, declared as Microsoft documents them (learn.microsoft.com
 * kernel-mode driver reference).  tools/kmdf/inc/wdm.h includes this after
 * mingw-w64's wdm.h.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ExAllocatePool2 (Windows 10 2004) */
typedef ULONG64 POOL_FLAGS;
#define POOL_FLAG_REQUIRED_START          0x0000000000000001ULL
#define POOL_FLAG_USE_QUOTA               0x0000000000000001ULL
#define POOL_FLAG_UNINITIALIZED           0x0000000000000002ULL
#define POOL_FLAG_SESSION                 0x0000000000000004ULL
#define POOL_FLAG_CACHE_ALIGNED           0x0000000000000008ULL
#define POOL_FLAG_RESERVED1               0x0000000000000010ULL
#define POOL_FLAG_RAISE_ON_FAILURE        0x0000000000000020ULL
#define POOL_FLAG_NON_PAGED               0x0000000000000040ULL
#define POOL_FLAG_NON_PAGED_EXECUTE       0x0000000000000080ULL
#define POOL_FLAG_PAGED                   0x0000000000000100ULL
#define POOL_FLAG_RESERVED2               0x0000000000000200ULL
#define POOL_FLAG_RESERVED3               0x0000000000000400ULL
#define POOL_FLAG_REQUIRED_END            0x0000000080000000ULL
#define POOL_FLAG_OPTIONAL_START          0x0000000100000000ULL
#define POOL_FLAG_SPECIAL_POOL            0x0000000100000000ULL
#define POOL_FLAG_OPTIONAL_END            0x8000000000000000ULL

NTKERNELAPI PVOID NTAPI ExAllocatePool2(_In_ POOL_FLAGS Flags, _In_ SIZE_T NumberOfBytes, _In_ ULONG Tag);

/* POOL_NX_OPTIN: NovaOS's non-paged pool is never executable, so a
 * driver's opt-in changes nothing */
#define DrvRtPoolNxOptIn 0x00000001
FORCEINLINE VOID ExInitializeDriverRuntime(_In_ ULONG RuntimeFlags) { (VOID)RuntimeFlags; }

#ifndef MdlMappingNoExecute
#define MdlMappingNoExecute 0x40000000
#endif
#ifndef MdlMappingNoWrite
#define MdlMappingNoWrite   0x80000000
#endif

NTKERNELAPI VOID NTAPI KeQuerySystemTimePrecise(_Out_ PLARGE_INTEGER CurrentTime);
#ifndef KeGetCurrentProcessorIndex
#define KeGetCurrentProcessorIndex() KeGetCurrentProcessorNumberEx(NULL)
#endif

/* Interrupt active/inactive reporting (Windows 8) */
#define IO_REPORT_INTERRUPT_ACTIVE_STATE_PARAMETERS_VERSION_FULLY_SPECIFIED 0x1
typedef struct _IO_REPORT_INTERRUPT_ACTIVE_STATE_PARAMETERS {
    ULONG Version;
    union {
        PVOID Generic;
        PKINTERRUPT InterruptObject;
        PKINTERRUPT *InterruptMessageTable;
    } ConnectionContext;
} IO_REPORT_INTERRUPT_ACTIVE_STATE_PARAMETERS, *PIO_REPORT_INTERRUPT_ACTIVE_STATE_PARAMETERS;

NTKERNELAPI VOID NTAPI IoReportInterruptActive(_In_ PIO_REPORT_INTERRUPT_ACTIVE_STATE_PARAMETERS Parameters);
NTKERNELAPI VOID NTAPI IoReportInterruptInactive(_In_ PIO_REPORT_INTERRUPT_ACTIVE_STATE_PARAMETERS Parameters);

/* A driver's own registry keys (Windows 10 1803) */
typedef enum _DRIVER_REGKEY_TYPE {
    DriverRegKeyParameters,
    DriverRegKeyPersistentState,
    DriverRegKeySharedPersistentState
} DRIVER_REGKEY_TYPE, *PDRIVER_REGKEY_TYPE;

NTKERNELAPI NTSTATUS NTAPI IoOpenDriverRegistryKey(_In_ PDRIVER_OBJECT DriverObject, _In_ DRIVER_REGKEY_TYPE RegKeyType,
                                                   _In_ ACCESS_MASK DesiredAccess, _In_ ULONG Flags, _Out_ PHANDLE DriverRegKey);
NTKERNELAPI NTSTATUS NTAPI IoQueryFullDriverPath(_In_ PDRIVER_OBJECT DriverObject, _Out_ PUNICODE_STRING FullPath);
NTKERNELAPI NTSTATUS NTAPI IoGetActivityIdIrp(_In_ PIRP Irp, _Out_ LPGUID Guid);
NTKERNELAPI NTSTATUS NTAPI IoSetActivityIdIrp(_Inout_ PIRP Irp, _In_opt_ LPCGUID Guid);
NTKERNELAPI PEPROCESS NTAPI IoGetInitiatorProcess(_In_ PFILE_OBJECT FileObject);
NTKERNELAPI NTSTATUS NTAPI RtlQueryRegistryValuesEx(_In_ ULONG RelativeTo, _In_ PCWSTR Path,
                                                    _Inout_ PRTL_QUERY_REGISTRY_TABLE QueryTable,
                                                    _In_opt_ PVOID Context, _In_opt_ PVOID Environment);

/* Registry type checks for RTL_QUERY_REGISTRY_DIRECT (Windows 8) */
#ifndef RTL_QUERY_REGISTRY_TYPECHECK
#define RTL_QUERY_REGISTRY_TYPECHECK       0x00000100
#define RTL_QUERY_REGISTRY_TYPECHECK_SHIFT 24
#define RTL_QUERY_REGISTRY_TYPECHECK_MASK  (0xff << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT)
#endif

#ifndef POOL_NX_ALLOCATION
#define POOL_NX_ALLOCATION 512
#endif
#ifndef NonPagedPoolNx
#define NonPagedPoolNx ((POOL_TYPE)512)
#endif

#ifndef CM_RESOURCE_INTERRUPT_WAKE_HINT
#define CM_RESOURCE_INTERRUPT_WAKE_HINT 0x0020
#endif

/* Bug check codes newer than mingw-w64's list */
#ifndef WDF_VIOLATION
#define WDF_VIOLATION ((ULONG)0x0000010D)
#endif

/* D3cold support interface (Windows 8), asked of the bus driver with
 * IRP_MN_QUERY_INTERFACE */
DEFINE_GUID(GUID_D3COLD_SUPPORT_INTERFACE, 0xb38290e5, 0x3cd0, 0x4f9d, 0x99, 0x37, 0xf5, 0xfe, 0x2b, 0x44, 0xd4, 0x7a);
#define D3COLD_SUPPORT_INTERFACE_VERSION 1

typedef enum _DEVICE_WAKE_DEPTH {
    DeviceWakeDepthNotWakeable = 0,
    DeviceWakeDepthD0,
    DeviceWakeDepthD1,
    DeviceWakeDepthD2,
    DeviceWakeDepthD3hot,
    DeviceWakeDepthD3cold,
    DeviceWakeDepthMaximum
} DEVICE_WAKE_DEPTH, *PDEVICE_WAKE_DEPTH;

typedef enum _D3COLD_LAST_TRANSITION_STATUS {
    LastDStateTransitionStatusUnknown = 0,
    LastDStateTransitionD3hot,
    LastDStateTransitionD3cold
} D3COLD_LAST_TRANSITION_STATUS, *PD3COLD_LAST_TRANSITION_STATUS;

typedef VOID (NTAPI SET_D3COLD_SUPPORT)(_In_reads_opt_(_Inexpressible_("varies")) PVOID Context, _In_ BOOLEAN D3ColdSupport);
typedef SET_D3COLD_SUPPORT *PSET_D3COLD_SUPPORT;
typedef NTSTATUS (NTAPI GET_IDLE_WAKE_INFO)(PVOID Context, SYSTEM_POWER_STATE SystemPowerState, PDEVICE_WAKE_DEPTH DeepestWakeableDstate);
typedef GET_IDLE_WAKE_INFO *PGET_IDLE_WAKE_INFO;
typedef NTSTATUS (NTAPI GET_D3COLD_CAPABILITY)(PVOID Context, PBOOLEAN D3ColdSupported);
typedef GET_D3COLD_CAPABILITY *PGET_D3COLD_CAPABILITY;
typedef VOID (NTAPI GET_D3COLD_LAST_TRANSITION_STATUS)(PVOID Context, PD3COLD_LAST_TRANSITION_STATUS LastTransitionStatus);
typedef GET_D3COLD_LAST_TRANSITION_STATUS *PGET_D3COLD_LAST_TRANSITION_STATUS;

typedef struct _D3COLD_SUPPORT_INTERFACE {
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PSET_D3COLD_SUPPORT SetD3ColdSupport;
    PGET_IDLE_WAKE_INFO GetIdleWakeInfo;
    PGET_D3COLD_CAPABILITY GetD3ColdCapability;
    PGET_D3COLD_CAPABILITY GetBusDriverD3ColdSupport;
    PGET_D3COLD_LAST_TRANSITION_STATUS GetLastTransitionStatus;
} D3COLD_SUPPORT_INTERFACE, *PD3COLD_SUPPORT_INTERFACE;

/* Runtime power management framework, PoFx (Windows 8; version 3 adds
 * directed power transitions) */
#define PO_FX_VERSION_V3 3
#define PO_FX_UNKNOWN_POWER 0xFFFFFFFF
#define PO_FX_UNKNOWN_TIME  0xFFFFFFFFFFFFFFFFULL
#define PO_FX_FLAG_BLOCKING 0x1
#define PO_FX_FLAG_ASYNC_ONLY 0x2
#define PO_FX_DEVICE_FLAG_RESERVED_1            0x0000000000000001ULL
#define PO_FX_DEVICE_FLAG_DFX_CHILDREN_OPTIONAL 0x0000000000000002ULL
#define PO_FX_DEVICE_FLAG_DISABLE_FAST_RESUME   0x0000000000000004ULL

typedef VOID (NTAPI PO_FX_DEVICE_POWER_REQUIRED_CALLBACK)(_In_ PVOID Context);
typedef PO_FX_DEVICE_POWER_REQUIRED_CALLBACK *PPO_FX_DEVICE_POWER_REQUIRED_CALLBACK;
typedef VOID (NTAPI PO_FX_DEVICE_POWER_NOT_REQUIRED_CALLBACK)(_In_ PVOID Context);
typedef PO_FX_DEVICE_POWER_NOT_REQUIRED_CALLBACK *PPO_FX_DEVICE_POWER_NOT_REQUIRED_CALLBACK;
typedef VOID (NTAPI PO_FX_DIRECTED_POWER_UP_CALLBACK)(_In_ PVOID Context, _In_ ULONG Flags);
typedef PO_FX_DIRECTED_POWER_UP_CALLBACK *PPO_FX_DIRECTED_POWER_UP_CALLBACK;
typedef VOID (NTAPI PO_FX_DIRECTED_POWER_DOWN_CALLBACK)(_In_ PVOID Context, _In_ ULONG Flags);
typedef PO_FX_DIRECTED_POWER_DOWN_CALLBACK *PPO_FX_DIRECTED_POWER_DOWN_CALLBACK;

typedef struct _PO_FX_DEVICE_V1 {
    ULONG Version;
    ULONG ComponentCount;
    PPO_FX_COMPONENT_ACTIVE_CONDITION_CALLBACK ComponentActiveConditionCallback;
    PPO_FX_COMPONENT_IDLE_CONDITION_CALLBACK ComponentIdleConditionCallback;
    PPO_FX_COMPONENT_IDLE_STATE_CALLBACK ComponentIdleStateCallback;
    PPO_FX_DEVICE_POWER_REQUIRED_CALLBACK DevicePowerRequiredCallback;
    PPO_FX_DEVICE_POWER_NOT_REQUIRED_CALLBACK DevicePowerNotRequiredCallback;
    PPO_FX_POWER_CONTROL_CALLBACK PowerControlCallback;
    PVOID DeviceContext;
    PO_FX_COMPONENT_V1 Components[ANYSIZE_ARRAY];
} PO_FX_DEVICE_V1, *PPO_FX_DEVICE_V1;

typedef struct _PO_FX_DEVICE_V2 {
    ULONG Version;
    ULONGLONG Flags;
    PPO_FX_COMPONENT_ACTIVE_CONDITION_CALLBACK ComponentActiveConditionCallback;
    PPO_FX_COMPONENT_IDLE_CONDITION_CALLBACK ComponentIdleConditionCallback;
    PPO_FX_COMPONENT_IDLE_STATE_CALLBACK ComponentIdleStateCallback;
    PPO_FX_DEVICE_POWER_REQUIRED_CALLBACK DevicePowerRequiredCallback;
    PPO_FX_DEVICE_POWER_NOT_REQUIRED_CALLBACK DevicePowerNotRequiredCallback;
    PPO_FX_POWER_CONTROL_CALLBACK PowerControlCallback;
    PVOID DeviceContext;
    ULONG ComponentCount;
    PO_FX_COMPONENT_V2 Components[ANYSIZE_ARRAY];
} PO_FX_DEVICE_V2, *PPO_FX_DEVICE_V2;

typedef struct _PO_FX_DEVICE_V3 {
    ULONG Version;
    ULONGLONG Flags;
    PPO_FX_COMPONENT_ACTIVE_CONDITION_CALLBACK ComponentActiveConditionCallback;
    PPO_FX_COMPONENT_IDLE_CONDITION_CALLBACK ComponentIdleConditionCallback;
    PPO_FX_COMPONENT_IDLE_STATE_CALLBACK ComponentIdleStateCallback;
    PPO_FX_DEVICE_POWER_REQUIRED_CALLBACK DevicePowerRequiredCallback;
    PPO_FX_DEVICE_POWER_NOT_REQUIRED_CALLBACK DevicePowerNotRequiredCallback;
    PPO_FX_POWER_CONTROL_CALLBACK PowerControlCallback;
    PPO_FX_DIRECTED_POWER_UP_CALLBACK DirectedPowerUpCallback;
    PPO_FX_DIRECTED_POWER_DOWN_CALLBACK DirectedPowerDownCallback;
    ULONG DirectedFxTimeoutInSeconds;
    PVOID DeviceContext;
    ULONG ComponentCount;
    PO_FX_COMPONENT_V2 Components[ANYSIZE_ARRAY];
} PO_FX_DEVICE_V3, *PPO_FX_DEVICE_V3;

typedef PO_FX_DEVICE_V2 PO_FX_DEVICE, *PPO_FX_DEVICE;

NTKERNELAPI NTSTATUS NTAPI PoFxRegisterDevice(_In_ PDEVICE_OBJECT Pdo, _In_ PPO_FX_DEVICE Device, _Out_ POHANDLE *Handle);
NTKERNELAPI VOID NTAPI PoFxStartDevicePowerManagement(_In_ POHANDLE Handle);
NTKERNELAPI VOID NTAPI PoFxUnregisterDevice(_In_ POHANDLE Handle);
NTKERNELAPI VOID NTAPI PoFxActivateComponent(_In_ POHANDLE Handle, _In_ ULONG Component, _In_ ULONG Flags);
NTKERNELAPI VOID NTAPI PoFxIdleComponent(_In_ POHANDLE Handle, _In_ ULONG Component, _In_ ULONG Flags);
NTKERNELAPI VOID NTAPI PoFxReportDevicePoweredOn(_In_ POHANDLE Handle);
NTKERNELAPI VOID NTAPI PoFxSetDeviceIdleTimeout(_In_ POHANDLE Handle, _In_ ULONGLONG IdleTimeout);
NTKERNELAPI VOID NTAPI PoFxCompleteIdleState(_In_ POHANDLE Handle, _In_ ULONG Component);
NTKERNELAPI VOID NTAPI PoFxCompleteIdleCondition(_In_ POHANDLE Handle, _In_ ULONG Component);
NTKERNELAPI VOID NTAPI PoFxCompleteDevicePowerNotRequired(_In_ POHANDLE Handle);
NTKERNELAPI VOID NTAPI PoFxCompleteDirectedPowerDown(_In_ POHANDLE Handle);

FORCEINLINE DEVICE_POWER_STATE MapWakeDepthToDstate(_In_ DEVICE_WAKE_DEPTH WakeDepth)
{
    static const DEVICE_POWER_STATE map[DeviceWakeDepthMaximum] = {
        PowerDeviceD0, PowerDeviceD0, PowerDeviceD1, PowerDeviceD2, PowerDeviceD3, PowerDeviceD3
    };
    if ((ULONG)WakeDepth >= DeviceWakeDepthMaximum) {
        return PowerDeviceUnspecified;
    }
    return map[WakeDepth];
}

#ifndef PNP_DEVICE_ASSIGNED_TO_GUEST
#define PNP_DEVICE_ASSIGNED_TO_GUEST 0x00000100
#endif

/* GUIDs of interfaces asked of the lower stack (wdmguid.h) */
DEFINE_GUID(GUID_REENUMERATE_SELF_INTERFACE_STANDARD, 0x2aeb0243, 0x6a6e, 0x486b, 0x82, 0xfc, 0xd8, 0x15, 0xf6, 0xb9, 0x70, 0x06);
DEFINE_GUID(GUID_SECURE_DRIVER_INTERFACE, 0x370f67e1, 0x4ff5, 0x4a94, 0x9a, 0x35, 0x06, 0xc5, 0xd9, 0xcc, 0x30, 0xe2);

/* WPP's kernel-mode message API (mingw-w64 leaves WmiTraceMessageVa out) */
#ifndef TRACE_MESSAGE_SEQUENCE
#define TRACE_MESSAGE_SEQUENCE              1
#define TRACE_MESSAGE_GUID                  2
#define TRACE_MESSAGE_COMPONENTID           4
#define TRACE_MESSAGE_TIMESTAMP             8
#define TRACE_MESSAGE_PERFORMANCE_TIMESTAMP 16
#define TRACE_MESSAGE_SYSTEMINFO            32
#endif
NTKERNELAPI NTSTATUS NTAPI WmiTraceMessageVa(_In_ TRACEHANDLE LoggerHandle, _In_ ULONG MessageFlags,
                                             _In_ LPCGUID MessageGuid, _In_ USHORT MessageNumber,
                                             _In_ va_list MessageArgList);

typedef const WCHAR *LPCWCHAR;

/* ntintsafe.h names mingw-w64 spells differently or leaves out */
#ifndef RtlSIZETAdd
#define RtlSIZETAdd RtlSizeTAdd
#endif
#ifndef USHORT_MAX
#define USHORT_MAX 0xffff
#endif
#ifndef ULONG_MAX
#define ULONG_MAX 0xffffffffUL
#endif

/* POWER_PLATFORM_INFORMATION (winnt.h; the kernel-mode headers lack it) */
#ifndef _POWER_PLATFORM_INFORMATION_DEFINED
#define _POWER_PLATFORM_INFORMATION_DEFINED
typedef struct _POWER_PLATFORM_INFORMATION {
    BOOLEAN AoAc;
} POWER_PLATFORM_INFORMATION, *PPOWER_PLATFORM_INFORMATION;
#endif

#ifdef __cplusplus
}
#endif
