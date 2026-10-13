/* wdmdefs.h — the kernel types user-mode framework code names (NovaOS
 * stand-in for the internal header; tools/umdf/inc/README.md)
 *
 * UMDF's public wudfwdm.h has most of them; the rest are what the
 * framework's mode-agnostic declarations (MxGeneral.h, ...) mention but
 * never use in user mode: opaque here. */
#pragma once
#include "wudfwdm.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ASSERT
#if DBG
#define ASSERT(e)            ((void)((e) ? 1 : (DebugBreak(), 0)))
#else
#define ASSERT(e)            ((void)0)
#endif
#endif
#ifndef ASSERTMSG
#define ASSERTMSG(m, e)      ASSERT(e)
#endif

#ifndef PASSIVE_LEVEL
#define PASSIVE_LEVEL  0
#define APC_LEVEL      1
#define DISPATCH_LEVEL 2
#endif

typedef struct _KDPC *PKDPC, *PRKDPC;
typedef struct _KINTERRUPT *PKINTERRUPT;
typedef struct _NPAGED_LOOKASIDE_LIST *PNPAGED_LOOKASIDE_LIST;
typedef struct _PAGED_LOOKASIDE_LIST *PPAGED_LOOKASIDE_LIST;
typedef struct _OBJECT_TYPE *POBJECT_TYPE;
typedef struct _OBJECT_HANDLE_INFORMATION *POBJECT_HANDLE_INFORMATION;
typedef LONG KPRIORITY;
typedef struct _IO_STACK_LOCATION *PIO_STACK_LOCATION;
typedef VOID (NTAPI *PINTERFACE_REFERENCE)(PVOID Context);
typedef VOID (NTAPI *PINTERFACE_DEREFERENCE)(PVOID Context);
typedef struct _INTERFACE {
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
} INTERFACE;
typedef ULONG64 POOL_FLAGS;
typedef enum _KWAIT_REASON { Executive = 0 } KWAIT_REASON;
typedef PVOID (NTAPI *PALLOCATE_FUNCTION)(ULONG PoolType, SIZE_T NumberOfBytes, ULONG Tag);
typedef VOID (NTAPI *PFREE_FUNCTION)(PVOID Buffer);

/* (SYSTEM_POWER_STATE, DEVICE_POWER_STATE: winnt.h) */
typedef union _POWER_STATE {
    SYSTEM_POWER_STATE SystemState;
    DEVICE_POWER_STATE DeviceState;
} POWER_STATE, *PPOWER_STATE;
typedef enum _POWER_STATE_TYPE { SystemPowerState = 0, DevicePowerState } POWER_STATE_TYPE, *PPOWER_STATE_TYPE;

typedef struct _DEVICE_CAPABILITIES {
    USHORT Size;
    USHORT Version;
    ULONG DeviceD1:1;
    ULONG DeviceD2:1;
    ULONG LockSupported:1;
    ULONG EjectSupported:1;
    ULONG Removable:1;
    ULONG DockDevice:1;
    ULONG UniqueID:1;
    ULONG SilentInstall:1;
    ULONG RawDeviceOK:1;
    ULONG SurpriseRemovalOK:1;
    ULONG WakeFromD0:1;
    ULONG WakeFromD1:1;
    ULONG WakeFromD2:1;
    ULONG WakeFromD3:1;
    ULONG HardwareDisabled:1;
    ULONG NonDynamic:1;
    ULONG WarmEjectSupported:1;
    ULONG NoDisplayInUI:1;
    ULONG Reserved1:1;
    ULONG WakeFromInterrupt:1;
    ULONG SecureDevice:1;
    ULONG ChildOfVgaEnabledBridge:1;
    ULONG DecodeIoOnBoot:1;
    ULONG Reserved:9;
    ULONG Address;
    ULONG UINumber;
    DEVICE_POWER_STATE DeviceState[PowerSystemMaximum];
    SYSTEM_POWER_STATE SystemWake;
    DEVICE_POWER_STATE DeviceWake;
    ULONG D1Latency;
    ULONG D2Latency;
    ULONG D3Latency;
} DEVICE_CAPABILITIES, *PDEVICE_CAPABILITIES;

typedef enum _DEVICE_WAKE_DEPTH {
    DeviceWakeDepthNotWakeable = 0,
    DeviceWakeDepthD0,
    DeviceWakeDepthD1,
    DeviceWakeDepthD2,
    DeviceWakeDepthD3hot,
    DeviceWakeDepthD3cold,
    DeviceWakeDepthMaximum
} DEVICE_WAKE_DEPTH, *PDEVICE_WAKE_DEPTH;

/* From mingw-w64's wdm.h (public domain), as the framework's PnP code
 * names them */
typedef enum _DEVICE_USAGE_NOTIFICATION_TYPE {
  DeviceUsageTypeUndefined,
  DeviceUsageTypePaging,
  DeviceUsageTypeHibernation,
  DeviceUsageTypeDumpFile,
  DeviceUsageTypeBoot,
  DeviceUsageTypePostDisplay,
  DeviceUsageTypeGuestAssigned
} DEVICE_USAGE_NOTIFICATION_TYPE;

typedef struct _PNP_BUS_INFORMATION {
  GUID BusTypeGuid;
  INTERFACE_TYPE LegacyBusType;
  ULONG BusNumber;
} PNP_BUS_INFORMATION, *PPNP_BUS_INFORMATION;

typedef struct _IO_RESOURCE_DESCRIPTOR {
  UCHAR Option;
  UCHAR Type;
  UCHAR ShareDisposition;
  UCHAR Spare1;
  USHORT Flags;
  USHORT Spare2;
  union {
    struct {
      ULONG Length;
      ULONG Alignment;
      PHYSICAL_ADDRESS MinimumAddress;
      PHYSICAL_ADDRESS MaximumAddress;
    } Port;
    struct {
      ULONG Length;
      ULONG Alignment;
      PHYSICAL_ADDRESS MinimumAddress;
      PHYSICAL_ADDRESS MaximumAddress;
    } Memory;
    struct {
      ULONG MinimumVector;
      ULONG MaximumVector;
    } Interrupt;
    struct {
      ULONG MinimumChannel;
      ULONG MaximumChannel;
    } Dma;
    struct {
      ULONG Length;
      ULONG Alignment;
      PHYSICAL_ADDRESS MinimumAddress;
      PHYSICAL_ADDRESS MaximumAddress;
    } Generic;
    struct {
      ULONG Data[3];
    } DevicePrivate;
    struct {
      ULONG Length;
      ULONG MinBusNumber;
      ULONG MaxBusNumber;
      ULONG Reserved;
    } BusNumber;
    struct {
      ULONG Priority;
      ULONG Reserved1;
      ULONG Reserved2;
    } ConfigData;
  } u;
} IO_RESOURCE_DESCRIPTOR, *PIO_RESOURCE_DESCRIPTOR;

typedef struct _IO_RESOURCE_LIST {
  USHORT Version;
  USHORT Revision;
  ULONG Count;
  IO_RESOURCE_DESCRIPTOR Descriptors[1];
} IO_RESOURCE_LIST, *PIO_RESOURCE_LIST;

typedef struct _IO_RESOURCE_REQUIREMENTS_LIST {
  ULONG ListSize;
  INTERFACE_TYPE InterfaceType;
  ULONG BusNumber;
  ULONG SlotNumber;
  ULONG Reserved[3];
  ULONG AlternativeLists;
  IO_RESOURCE_LIST List[1];
} IO_RESOURCE_REQUIREMENTS_LIST, *PIO_RESOURCE_REQUIREMENTS_LIST;

typedef struct _DEVICE_RELATIONS {
  ULONG Count;
  PDEVICE_OBJECT Objects[1];
} DEVICE_RELATIONS, *PDEVICE_RELATIONS;

typedef enum _DEVICE_TEXT_TYPE {
  DeviceTextDescription,
  DeviceTextLocationInformation
} DEVICE_TEXT_TYPE, *PDEVICE_TEXT_TYPE;

typedef enum _BUS_QUERY_ID_TYPE {
  BusQueryDeviceID,
  BusQueryHardwareIDs,
  BusQueryCompatibleIDs,
  BusQueryInstanceID,
  BusQueryDeviceSerialNumber,
  BusQueryContainerID
} BUS_QUERY_ID_TYPE, *PBUS_QUERY_ID_TYPE;

typedef enum _LOCK_OPERATION {
  IoReadAccess,
  IoWriteAccess,
  IoModifyAccess
} LOCK_OPERATION;

typedef struct _KEY_VALUE_PARTIAL_INFORMATION {
  ULONG TitleIndex;
  ULONG Type;
  ULONG DataLength;
  UCHAR Data[1];
} KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;

typedef struct _SYSTEM_POWER_STATE_CONTEXT {
  union {
    struct {
      ULONG Reserved1:8;
      ULONG TargetSystemState:4;
      ULONG EffectiveSystemState:4;
      ULONG CurrentSystemState:4;
      ULONG IgnoreHibernationPath:1;
      ULONG PseudoTransition:1;
      ULONG Reserved2:10;
    } ;
    ULONG ContextAsUlong;
  } ;
} SYSTEM_POWER_STATE_CONTEXT, *PSYSTEM_POWER_STATE_CONTEXT;


typedef ULONG PNP_DEVICE_STATE, *PPNP_DEVICE_STATE;
typedef VOID (NTAPI WORKER_THREAD_ROUTINE)(PVOID Parameter);
typedef WORKER_THREAD_ROUTINE *PWORKER_THREAD_ROUTINE;
typedef struct _WORK_QUEUE_ITEM {
    LIST_ENTRY List;
    PWORKER_THREAD_ROUTINE WorkerRoutine;
    volatile PVOID Parameter;
} WORK_QUEUE_ITEM, *PWORK_QUEUE_ITEM;
typedef struct _IO_CSQ *PIO_CSQ;
typedef const struct _WNF_STATE_NAME *PCWNF_STATE_NAME;
#define POOL_FLAG_NON_PAGED 0x0000000000000040ULL
#define POOL_FLAG_PAGED     0x0000000000000100ULL
#ifndef FILE_AUTOGENERATED_DEVICE_NAME
#define FILE_AUTOGENERATED_DEVICE_NAME 0x00000080
#endif

/* D3cold support (Windows 8), as tools/kmdf/inc/novaos_wdm_ext.h */
DEFINE_GUID(GUID_D3COLD_SUPPORT_INTERFACE, 0xb38290e5, 0x3cd0, 0x4f9d, 0x99, 0x37, 0xf5, 0xfe, 0x2b, 0x44, 0xd4, 0x7a);
#define D3COLD_SUPPORT_INTERFACE_VERSION 1
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

typedef VOID (NTAPI *PREENUMERATE_SELF)(PVOID Context);
DEFINE_GUID(GUID_REENUMERATE_SELF_INTERFACE_STANDARD, 0x2aeb0243, 0x6a6e, 0x486b, 0x82, 0xfc, 0xd8, 0x15, 0xf6, 0xb9, 0x70, 0x06);
typedef struct _REENUMERATE_SELF_INTERFACE_STANDARD {
  USHORT Size;
  USHORT Version;
  PVOID Context;
  PINTERFACE_REFERENCE InterfaceReference;
  PINTERFACE_DEREFERENCE InterfaceDereference;
  PREENUMERATE_SELF SurpriseRemoveAndReenumerateSelf;
} REENUMERATE_SELF_INTERFACE_STANDARD, *PREENUMERATE_SELF_INTERFACE_STANDARD;


#ifndef IRP_MN_START_DEVICE
#define IRP_MN_START_DEVICE                 0x00
#define IRP_MN_QUERY_REMOVE_DEVICE          0x01
#define IRP_MN_REMOVE_DEVICE                0x02
#define IRP_MN_CANCEL_REMOVE_DEVICE         0x03
#define IRP_MN_STOP_DEVICE                  0x04
#define IRP_MN_QUERY_STOP_DEVICE            0x05
#define IRP_MN_CANCEL_STOP_DEVICE           0x06
#define IRP_MN_QUERY_DEVICE_RELATIONS       0x07
#define IRP_MN_QUERY_INTERFACE              0x08
#define IRP_MN_QUERY_CAPABILITIES           0x09
#define IRP_MN_QUERY_RESOURCES              0x0A
#define IRP_MN_QUERY_RESOURCE_REQUIREMENTS  0x0B
#define IRP_MN_QUERY_DEVICE_TEXT            0x0C
#define IRP_MN_FILTER_RESOURCE_REQUIREMENTS 0x0D
#define IRP_MN_READ_CONFIG                  0x0F
#define IRP_MN_WRITE_CONFIG                 0x10
#define IRP_MN_EJECT                        0x11
#define IRP_MN_SET_LOCK                     0x12
#define IRP_MN_QUERY_ID                     0x13
#define IRP_MN_QUERY_PNP_DEVICE_STATE       0x14
#define IRP_MN_QUERY_BUS_INFORMATION        0x15
#define IRP_MN_DEVICE_USAGE_NOTIFICATION    0x16
#define IRP_MN_SURPRISE_REMOVAL             0x17
#define IRP_MN_DEVICE_ENUMERATED            0x19
#define IRP_MN_WAIT_WAKE                    0x00
#define IRP_MN_POWER_SEQUENCE               0x01
#define IRP_MN_SET_POWER                    0x02
#define IRP_MN_QUERY_POWER                  0x03
#endif
#ifndef DO_BUFFERED_IO
#define DO_BUFFERED_IO 0x00000004
#define DO_DIRECT_IO   0x00000010
#endif
typedef struct _NPAGED_LOOKASIDE_LIST { PVOID Reserved[16]; } NPAGED_LOOKASIDE_LIST;

typedef enum _DEVICE_RELATION_TYPE {
  BusRelations,
  EjectionRelations,
  PowerRelations,
  RemovalRelations,
  TargetDeviceRelation,
  SingleBusRelations,
  TransportRelations
} DEVICE_RELATION_TYPE, *PDEVICE_RELATION_TYPE;


typedef struct _KEVENT *PKEVENT, *PRKEVENT;
typedef struct _IO_WORKITEM *PIO_WORKITEM;
typedef struct _PAGED_LOOKASIDE_LIST { PVOID Reserved[16]; } PAGED_LOOKASIDE_LIST;
#ifndef DO_DEVICE_INITIALIZING
#define DO_POWER_PAGABLE        0x00002000
#define DO_POWER_INRUSH         0x00004000
#define DO_DEVICE_INITIALIZING  0x00000080
#endif
#ifndef FILE_DEVICE_SECURE_OPEN
#define FILE_DEVICE_SECURE_OPEN 0x00000100
#endif
#define FO_STREAM_FILE          0x00100000
#define PNP_DEVICE_DISABLED                      0x00000001
#define PNP_DEVICE_DONT_DISPLAY_IN_UI            0x00000002
#define PNP_DEVICE_FAILED                        0x00000004
#define PNP_DEVICE_REMOVED                       0x00000008
#define PNP_DEVICE_RESOURCE_REQUIREMENTS_CHANGED 0x00000010
#define PNP_DEVICE_NOT_DISABLEABLE               0x00000020
#define PNP_DEVICE_DISCONNECTED                  0x00000040
#define PNP_DEVICE_ASSIGNED_TO_GUEST             0x00000100
#ifndef STATUS_CONTINUE_COMPLETION
#define STATUS_CONTINUE_COMPLETION STATUS_SUCCESS
#endif
#ifndef ALIGN_UP_BY
#define ALIGN_DOWN_BY(length, alignment) ((ULONG_PTR)(length) & ~((ULONG_PTR)(alignment) - 1))
#define ALIGN_UP_BY(length, alignment) (ALIGN_DOWN_BY(((ULONG_PTR)(length) + (alignment) - 1), alignment))
#define ALIGN_DOWN(length, type) ALIGN_DOWN_BY(length, sizeof(type))
#define ALIGN_UP(length, type) ALIGN_UP_BY(length, sizeof(type))
#endif
#ifndef USHORT_MAX
#define USHORT_MAX 0xffff
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
typedef enum _MM_PAGE_PRIORITY { LowPagePriority, NormalPagePriority = 16, HighPagePriority = 32 } MM_PAGE_PRIORITY;
#define MdlMappingNoExecute 0x40000000

/* RtlQueryRegistryValues' table (ntdll; the framework reads its
 * parameters with it in user mode too) */
typedef NTSTATUS (NTAPI *PRTL_QUERY_REGISTRY_ROUTINE)(PWSTR ValueName, ULONG ValueType, PVOID ValueData,
                                                      ULONG ValueLength, PVOID Context, PVOID EntryContext);
typedef struct _RTL_QUERY_REGISTRY_TABLE {
    PRTL_QUERY_REGISTRY_ROUTINE QueryRoutine;
    ULONG Flags;
    PWSTR Name;
    PVOID EntryContext;
    ULONG DefaultType;
    PVOID DefaultData;
    ULONG DefaultLength;
} RTL_QUERY_REGISTRY_TABLE, *PRTL_QUERY_REGISTRY_TABLE;
#define RTL_QUERY_REGISTRY_SUBKEY     0x00000001
#define RTL_QUERY_REGISTRY_TOPKEY     0x00000002
#define RTL_QUERY_REGISTRY_REQUIRED   0x00000004
#define RTL_QUERY_REGISTRY_NOVALUE    0x00000008
#define RTL_QUERY_REGISTRY_NOEXPAND   0x00000010
#define RTL_QUERY_REGISTRY_DIRECT     0x00000020
#define RTL_QUERY_REGISTRY_DELETE     0x00000040
#define RTL_QUERY_REGISTRY_TYPECHECK  0x00000100
#define RTL_QUERY_REGISTRY_TYPECHECK_SHIFT 24
#define RTL_REGISTRY_ABSOLUTE   0
#define RTL_REGISTRY_HANDLE     0x40000000
#define RTL_REGISTRY_OPTIONAL   0x80000000
NTSYSAPI NTSTATUS NTAPI RtlQueryRegistryValues(ULONG RelativeTo, PCWSTR Path, PRTL_QUERY_REGISTRY_TABLE QueryTable,
                                              PVOID Context, PVOID Environment);
NTSYSAPI NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW VersionInformation);
NTSYSAPI VOID NTAPI RtlInitAnsiString(PANSI_STRING DestinationString, PCSZ SourceString);
NTSYSAPI NTSTATUS NTAPI RtlAnsiStringToUnicodeString(PUNICODE_STRING DestinationString, PCANSI_STRING SourceString,
                                                    BOOLEAN AllocateDestinationString);

/* ntintsafe.h's size addition, which mingw-w64 has only for kernel mode */
FORCEINLINE NTSTATUS NovaRtlSIZETAdd(SIZE_T Augend, SIZE_T Addend, SIZE_T *Result)
{
    if (Augend + Addend < Augend) { *Result = (SIZE_T)-1; return STATUS_INTEGER_OVERFLOW; }
    *Result = Augend + Addend;
    return STATUS_SUCCESS;
}
#ifndef RtlSIZETAdd
#define RtlSIZETAdd NovaRtlSIZETAdd
#endif

#define ExInitializeWorkItem(Item, Routine, Context) \
    ((Item)->WorkerRoutine = (Routine), (Item)->Parameter = (Context), (Item)->List.Flink = NULL)

/* The deepest D-state a device can wake from (wdm.h's inline) */
FORCEINLINE DEVICE_POWER_STATE MapWakeDepthToDstate(DEVICE_WAKE_DEPTH WakeDepth)
{
    const DEVICE_POWER_STATE map[] = { PowerDeviceUnspecified, PowerDeviceD0, PowerDeviceD1, PowerDeviceD2,
                                       PowerDeviceD3, PowerDeviceD3, PowerDeviceMaximum };
    return WakeDepth < 0 || WakeDepth > DeviceWakeDepthMaximum ? PowerDeviceUnspecified : map[WakeDepth];
}

/* The user-mode remove lock (Mx::MxAcquireRemoveLock, MxGeneralUm.h) */
typedef struct _WUDF_IO_REMOVE_LOCK {
    HANDLE RemoveEvent;
    volatile LONG IoCount;
    BOOLEAN Removed;
    DWORD ReleaseRemLockAndWaitStatus;
} WUDF_IO_REMOVE_LOCK, *PWUDF_IO_REMOVE_LOCK;

/* An NTSTATUS for a Win32 error, as winerror.h's HRESULT_FROM_WIN32 */
#ifndef NTSTATUS_FROM_WIN32
#define NTSTATUS_FROM_WIN32(x) ((NTSTATUS)(x) <= 0 ? ((NTSTATUS)(x)) : ((NTSTATUS)(((x) & 0x0000FFFF) | (7 << 16) | 0xC0000000)))
#endif

#ifdef __cplusplus
}
#endif
