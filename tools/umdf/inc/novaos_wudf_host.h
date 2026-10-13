/*
 * novaos_wudf_host.h — the interfaces between the UMDF 2 framework
 * (Wudfx02000.dll) and the host process it runs in (NovaOS stand-in for
 * Microsoft's internal headers; README.md)
 *
 * Microsoft published the framework but not its host (WUDFHost.exe) or
 * the private interfaces between the two.  These declarations are
 * NovaOS's own, written from what the framework source calls; NovaOS's
 * host (tools/umdf/host) implements them, so both sides are built from
 * this file.  The IIDs are NovaOS's.  Each method's comment says what the
 * host does for it.
 */
#pragma once

#define NOVA_WUDF_IID(n, x) \
    DEFINE_GUID(n, 0x6d1a2f47, 0x8b2e, 0x4c4e, 0x9f, 0x10, 0x4e, 0x6f, 0x76, 0x61, 0x4f, x)
NOVA_WUDF_IID(IID_IWudfHost, 0x10);
NOVA_WUDF_IID(IID_IWudfHost2, 0x11);
NOVA_WUDF_IID(IID_IWudfDeviceStack, 0x12);
NOVA_WUDF_IID(IID_IWudfDeviceStack2, 0x13);
NOVA_WUDF_IID(IID_IWudfDevice, 0x14);
NOVA_WUDF_IID(IID_IWudfDevice2, 0x15);
NOVA_WUDF_IID(IID_IWudfIrp, 0x16);
NOVA_WUDF_IID(IID_IWudfIoIrp, 0x17);
NOVA_WUDF_IID(IID_IWudfIoIrp2, 0x18);
NOVA_WUDF_IID(IID_IWudfPnpIrp, 0x19);
NOVA_WUDF_IID(IID_IWudfFile, 0x1a);
NOVA_WUDF_IID(IID_IWudfFile2, 0x1b);
NOVA_WUDF_IID(IID_IWudfCompanion, 0x1c);
NOVA_WUDF_IID(IID_IWudfTargetCallbackDeviceChange, 0x1d);
NOVA_WUDF_IID(IID_IWudfRemoteDispatcher, 0x1e);
NOVA_WUDF_IID(IID_IWudfIoDispatcher, 0x1f);
NOVA_WUDF_IID(IID_IFxMessageDispatch, 0x20);
NOVA_WUDF_IID(IID_IFxMessageDispatch2, 0x21);

/* USB capabilities the framework asks the host about (IWudfDeviceStack's
 * USB queries): only the framework and NovaOS's host exchange them */
NOVA_WUDF_IID(GUID_USB_CAPABILITY_DEVICE_CONNECTION_HIGH_SPEED_COMPATIBLE, 0x30);
NOVA_WUDF_IID(GUID_USB_CAPABILITY_DEVICE_CONNECTION_SUPER_SPEED_COMPATIBLE, 0x31);
NOVA_WUDF_IID(GUID_USB_CAPABILITY_SELECTIVE_SUSPEND, 0x32);
NOVA_WUDF_IID(GUID_USB_CAPABILITY_FUNCTION_SUSPEND, 0x33);

/* ---- types the interfaces name ---- */

struct IWudfCompanion;

typedef UINT64 WUDF_TARGET_CONTEXT;              /* a remote target's notification registration */
#define WUDF_TARGET_CONTEXT_INVALID ((WUDF_TARGET_CONTEXT)0)
typedef PVOID RD_INTERRUPT_CONTEXT;              /* the reflector's handle for one interrupt */

/* Where an I/O request's buffer was before a target retargeted it
 * (IWudfIoIrp::SwapCurrentBuffer / RestoreCurrentBuffer) */
typedef struct _WUDFX_IRP_BUFFER_INFO {
    PVOID InputBuffer;
    SIZE_T InputBufferCb;
    PVOID OutputBuffer;
    SIZE_T OutputBufferCb;
} WUDFX_IRP_BUFFER_INFO, *PWUDFX_IRP_BUFFER_INFO;

/* How a request goes down the stack (IWudfIoIrp::SetFlagsForNextStackLocation):
 * the WDF_REQUEST_SEND_OPTION_IMPERSONATE_* bits */
typedef enum _WUDFX_IRP_STACK_FLAGS {
    WudfxIrpStackFlagsNone = 0,
    WudfxIrpStackFlagsImpersonateClient = 0x8,
    WudfxIrpStackFlagsImpersonationIgnoreFailure = 0x10
} WUDFX_IRP_STACK_FLAGS;

/* A device's INF-chosen policies (UmdfDirectHardwareAccess, UmdfRegisterAccessMode,
 * UmdfFileObjectPolicy, UmdfFsContextUsePolicy) */
typedef enum _WDF_DIRECT_HARDWARE_ACCESS_TYPE {
    WdfRejectDirectHardwareAccess = 0,
    WdfAllowDirectHardwareAccess,
    WdfDirectHardwareAccessMax
} WDF_DIRECT_HARDWARE_ACCESS_TYPE;
typedef enum _WDF_REGISTER_ACCESS_MODE_TYPE {
    WdfRegisterAccessUsingSystemCall = 0,
    WdfRegisterAccessUsingUserModeMapping,
    WdfRegisterAccessMax
} WDF_REGISTER_ACCESS_MODE_TYPE;
typedef enum _WDF_FILE_OBJECT_POLICY_TYPE {
    WdfRejectNullAndUnknownFileObjects = 0,
    WdfAllowNullAndUnknownFileObjects,
    WdfFileObjectPolicyMax
} WDF_FILE_OBJECT_POLICY_TYPE;
typedef enum _WDF_FS_CONTEXT_USE_POLICY_TYPE {
    WdfDefaultFsContextUsePolicy = 0,       /* (FsContext free for the driver) */
    WdfCanUseFsContext2,
    WdfCannotUseFsContexts,
    WdfFsContextUsePolicyMax
} WDF_FS_CONTEXT_USE_POLICY_TYPE;

typedef enum _WDF_KERNEL_MODE_CLIENT_POLICY_TYPE {
    WdfRejectKernelModeClients = 0,
    WdfAllowKernelModeClients,
    WdfKernelModeClientPolicyMax
} WDF_KERNEL_MODE_CLIENT_POLICY_TYPE;
typedef enum _WDF_METHOD_NEITHER_ACTION_TYPE {
    WdfMethodNeitherAction_Fail = 0,
    WdfMethodNeitherAction_Copy,
    WdfMethodNeitherActionMax
} WDF_METHOD_NEITHER_ACTION_TYPE;
typedef enum _WDF_PROCESS_SHARING_TYPE {
    WdfProcessSharingDisabled = 0,
    WdfProcessSharingEnabled,
    WdfProcessSharingMax
} WDF_PROCESS_SHARING_TYPE;

/* The version the driver was built against (UmdfLibraryVersion; also the
 * framework's own, Microsoft_WDF_UMDF_Version) */
typedef struct _UMDF_VERSION_DATA {
    ULONG MajorNumber;
    ULONG MinorNumber;
    ULONG ServiceNumber;
} UMDF_VERSION_DATA, *PUMDF_VERSION_DATA;

/* What the host hands DRIVER_OBJECT_UM::DriverLoadContext while a driver
 * initialises or adds a device */
#define WUDF_DRIVER_TYPE_FUNCTION  0
#define WUDF_DRIVER_TYPE_COMPANION 1
typedef struct _WUDF_DRIVER_LOAD_CONTEXT {
    ULONG DriverType;
    IWudfDeviceStack *DeviceStack;
    IWudfCompanion *Companion;
} WUDF_DRIVER_LOAD_CONTEXT;

/* The framework's interrupt object as the host knows it */
typedef PVOID WUDF_INTERRUPT_CONTEXT;
typedef enum _InterruptControlType {
    InterruptControlTypeEnable = 0,
    InterruptControlTypeDisable,
    InterruptControlTypeDisconnect,
    InterruptControlTypeFlushQueuedDpcs,
    InterruptControlTypeResetInterruptInfo,
    InterruptControlTypeRevokeResources
} InterruptControlType;

/* The S0-idle and Sx-wake WMI controls the reflector shows for a device */
typedef enum _WmiIdleWakeInstanceUpdate {
    AddS0IdleInstance = 0,
    AddSxWakeInstance,
    RemoveS0IdleInstance,
    RemoveSxWakeInstance
} WmiIdleWakeInstanceUpdate;
enum RdWmiPowerAction {
    ActionInvalid = 0,
    QueryS0Idle,
    SetS0IdleEnable,
    SetS0IdleDisable,
    QuerySxWake,
    SetSxWakeEnable,
    SetSxWakeDisable
};

/* The device's hardware key root, for the property store (UMDF 1's name) */
#define WDF_PROPERTY_STORE_HARDWARE_KEY_ROOT L"Device Parameters"

/* ---- the framework's side: the host dispatches to it (fxmessagedispatchum.hpp) ---- */

struct IFxMessageDispatch : public IUnknown {
    virtual void __stdcall DispatchPnP(IWudfIrp *pIrp) = 0;
    virtual void __stdcall CreateFile(IWudfIoIrp *pCreateIrp) = 0;
    virtual void __stdcall DeviceControl(IWudfIoIrp *pIrp, IUnknown *pFxContext) = 0;
    virtual void __stdcall ReadFile(IWudfIoIrp *pIrp, IUnknown *pFxContext) = 0;
    virtual void __stdcall WriteFile(IWudfIoIrp *pIrp, IUnknown *pFxContext) = 0;
    virtual void __stdcall CleanupFile(IWudfIoIrp *pIrp, IUnknown *pFxContext) = 0;
    virtual void __stdcall CloseFile(IWudfIoIrp *pIrp, IUnknown *pFxContext) = 0;
    virtual VOID __stdcall GetPreferredTransferMode(UMINT::WDF_DEVICE_IO_BUFFER_RETRIEVAL *RetrievalMode,
                                                    UMINT::WDF_DEVICE_IO_TYPE *RWPreference,
                                                    UMINT::WDF_DEVICE_IO_TYPE *IoctlPreference) = 0;
    virtual void __stdcall FlushBuffers(IWudfIoIrp *pIrp, IUnknown *pFxContext) = 0;
    virtual void __stdcall QueryInformationFile(IWudfIoIrp *pIrp, IUnknown *pFxContext) = 0;
    virtual void __stdcall SetInformationFile(IWudfIoIrp *pIrp, IUnknown *pFxContext) = 0;
    virtual NTSTATUS __stdcall ProcessWmiPowerQueryOrSetData(RdWmiPowerAction Action, BOOLEAN *QueryResult) = 0;
    virtual WUDF_INTERFACE_CONTEXT __stdcall RemoteInterfaceArrival(LPCGUID pDeviceInterfaceGuid, PCWSTR pSymbolicLink) = 0;
    virtual void __stdcall RemoteInterfaceRemoval(WUDF_INTERFACE_CONTEXT RemoteInterfaceID) = 0;
    virtual BOOL __stdcall TransportQueryID(DWORD Id, PVOID DataBuffer, SIZE_T cbDataBufferSize) = 0;
    virtual ULONG __stdcall GetDirectTransferThreshold(VOID) = 0;
};

struct IFxMessageDispatch2 : public IFxMessageDispatch {
    virtual void __stdcall PoFxDevicePowerRequired(VOID) = 0;
    virtual void __stdcall PoFxDevicePowerNotRequired(VOID) = 0;
    virtual void __stdcall PoFxDirectedPowerUp(VOID) = 0;
    virtual void __stdcall PoFxDirectedPowerDown(VOID) = 0;
};

/* ---- the host's side ---- */

/* The host process (one per device stack) */
struct IWudfHost : public IUnknown {
};

struct IWudfHost2 : public IWudfHost {
    /* the IRP whose list entry (IWudfIrp::GetListEntry) this is */
    virtual IWudfIrp *__stdcall GetIrpFromListEntry(PLIST_ENTRY ListEntry) = 0;
    virtual BOOL __stdcall IsCurrentThreadImpersonated(void) = 0;
};

/* A file object: one CreateFile of the device, or the host's own open of
 * the next-lower device (IWudfDeviceStack::CreateWdfFile) */
struct IWudfFile : public IUnknown {
    virtual PCWSTR __stdcall GetName(void) = 0;
    virtual DWORD __stdcall GetInitiatorProcessId(void) = 0;
    virtual PVOID __stdcall GetCreateContext(void) = 0;
};

struct IWudfFile2 : public IWudfFile {
    virtual HANDLE __stdcall GetWeakRefHandle(void) = 0;
};

/* A device object in the host's stack for the device; the framework's
 * FxDevice is its context and its message dispatcher gets the requests */
struct IWudfDevice : public IUnknown {
    virtual IWudfDevice *__stdcall GetAttachedDevice(void) = 0;
    virtual ULONG __stdcall GetDeviceObjectWdmFlags(void) = 0;
    virtual IWudfDeviceStack *__stdcall GetDeviceStackInterface(void) = 0;
};

struct IWudfDevice2 : public IWudfDevice {
    virtual PVOID __stdcall GetContext(void) = 0;
    virtual void __stdcall SetContext(PVOID Context) = 0;
    virtual PVOID __stdcall GetDeviceExtension(void) = 0;
    virtual ULONG __stdcall GetStackSize2(void) = 0;
    virtual void __stdcall SetStackSize2(ULONG StackSize) = 0;
    virtual NTSTATUS __stdcall InitializeEventForRemoveLock(HANDLE *Event) = 0;
};

/* A request travelling the host's stack */
struct IWudfIrp : public IUnknown {
    virtual void __stdcall CompleteRequest(void) = 0;
    virtual NTSTATUS __stdcall Forward(void) = 0;
    virtual void __stdcall SetCompletionRoutine(PWUDF_IO_COMPLETION_ROUTINE CompletionRoutine, PVOID Context) = 0;
    virtual PWUDF_DRIVER_CANCEL __stdcall SetCancelRoutine(PWUDF_DRIVER_CANCEL CancelRoutine) = 0;
    virtual BOOL __stdcall Cancel(void) = 0;
    virtual BOOL __stdcall IsCanceled(void) = 0;
    virtual void __stdcall CopyCurrentIrpStackLocationToNext(void) = 0;
    virtual void __stdcall SkipCurrentIrpStackLocation(void) = 0;
    virtual void __stdcall SetNextIrpStackLocation(void) = 0;
    virtual void __stdcall ClearNextStackLocation(void) = 0;
    virtual void __stdcall MarkIrpPending(void) = 0;
    virtual BOOLEAN __stdcall PendingReturned(void) = 0;
    virtual void __stdcall PropagatePendingReturned(void) = 0;
    virtual void __stdcall SetStatus(NTSTATUS Status) = 0;
    virtual NTSTATUS __stdcall GetStatus(void) = 0;
    virtual void __stdcall SetInformation(ULONG_PTR Information) = 0;
    virtual ULONG_PTR __stdcall GetInformation(void) = 0;
    virtual void __stdcall SetContext(ULONG Index, PVOID Value) = 0;
    virtual PVOID __stdcall GetContext(ULONG Index) = 0;
    virtual PLIST_ENTRY __stdcall GetListEntry(void) = 0;
    virtual DWORD __stdcall GetRequestorProcessId(void) = 0;
    virtual HRESULT __stdcall ImpersonateRequestorProcess(SECURITY_IMPERSONATION_LEVEL ImpersonationLevel) = 0;
    virtual void __stdcall RevertImpersonation(void) = 0;
};

/* An I/O request (create, read, write, device control, ...) */
struct IWudfIoIrp : public IWudfIrp {
    virtual UMINT::WDF_REQUEST_TYPE __stdcall GetType(void) = 0;
    virtual KPROCESSOR_MODE __stdcall GetRequestorMode(void) = 0;
    virtual IWudfFile *__stdcall GetFile(void) = 0;
    virtual HRESULT __stdcall RetrieveBuffers(SIZE_T *InputBufferCb, PVOID *InputBuffer, SIZE_T *OutputBufferCb,
                                              PVOID *OutputBuffer) = 0;
    virtual void __stdcall GetReadParameters(ULONG *Length, LONGLONG *DeviceOffset, ULONG *Key) = 0;
    virtual void __stdcall GetWriteParameters(ULONG *Length, LONGLONG *DeviceOffset, ULONG *Key) = 0;
    virtual void __stdcall GetDeviceIoControlParameters(ULONG *IoControlCode, ULONG *InputBufferLength,
                                                        ULONG *OutputBufferLength) = 0;
    virtual UMINT::WDF_DEVICE_IO_TYPE __stdcall GetTransferMode(void) = 0;
    virtual BOOL __stdcall IsFrom32BitProcess(void) = 0;
    virtual BOOL __stdcall IsDriverCreated(void) = 0;
    virtual BOOL __stdcall GetUserModeDriverInitiatedIo(void) = 0;
    virtual void __stdcall SetUserModeDriverInitiatedIo(BOOL IsUserModeDriverInitiated) = 0;
    virtual BOOL __stdcall IsActivityIdSet(void) = 0;
    virtual LPCGUID __stdcall GetActivityId(void) = 0;
    virtual void __stdcall SetActivityId(LPCGUID ActivityId) = 0;
    virtual void __stdcall GetFrameworkRelatedFileObjectContext(IWudfDevice *DeviceObject, IUnknown **Context) = 0;
    virtual void __stdcall SetFrameworkFileObjectContext(IWudfDevice *DeviceObject, IUnknown *Context) = 0;
    virtual void __stdcall Reuse(NTSTATUS Status) = 0;
    virtual void __stdcall Deallocate(void) = 0;
    virtual void __stdcall SetTypeForNextStackLocation(UMINT::WDF_REQUEST_TYPE Type) = 0;
    virtual void __stdcall SetFileForNextIrpStackLocation(IWudfFile *File) = 0;
    virtual void __stdcall SetFlagsForNextStackLocation(WUDFX_IRP_STACK_FLAGS Flags) = 0;
    virtual void __stdcall SetReadParametersForNextStackLocation(ULONG Length, PLONGLONG DeviceOffset, ULONG Key) = 0;
    virtual void __stdcall SetWriteParametersForNextStackLocation(ULONG Length, PLONGLONG DeviceOffset, ULONG Key) = 0;
    virtual void __stdcall SetDeviceIoControlParametersForNextStackLocation(ULONG IoControlCode, ULONG InputBufferLength,
                                                                            ULONG OutputBufferLength) = 0;
    virtual void __stdcall SetOtherParametersForNextStackLocation(PVOID *Argument1, PVOID *Argument2, PVOID *Argument3,
                                                                  PVOID *Argument4) = 0;
    virtual void __stdcall SwapCurrentBuffer(bool IsIoctl, SIZE_T NewInputBufferCb, PVOID NewInputBuffer,
                                             SIZE_T NewOutputBufferCb, PVOID NewOutputBuffer,
                                             PWUDFX_IRP_BUFFER_INFO OriginalBufferInfo) = 0;
    virtual void __stdcall RestoreCurrentBuffer(PWUDFX_IRP_BUFFER_INFO OriginalBufferInfo) = 0;
};

struct IWudfIoIrp2 : public IWudfIoIrp {
    using IWudfIoIrp::GetReadParameters;
    using IWudfIoIrp::GetWriteParameters;
    using IWudfIoIrp::GetDeviceIoControlParameters;
    virtual void __stdcall GetCreateParameters2(ULONG *Options, USHORT *FileAttributes, USHORT *ShareAccess,
                                                PIO_SECURITY_CONTEXT *SecurityContext) = 0;
    virtual void __stdcall PrepareToForwardToSelf(void) = 0;
};

/* A Plug and Play or power request */
struct IWudfPnpIrp : public IWudfIrp {
    virtual UCHAR __stdcall GetMajorFunction(void) = 0;
    virtual UCHAR __stdcall GetMinorFunction(void) = 0;
    virtual PCM_RESOURCE_LIST __stdcall GetParameterAllocatedResources(void) = 0;
    virtual PCM_RESOURCE_LIST __stdcall GetParameterAllocatedResourcesTranslated(void) = 0;
    virtual PDEVICE_CAPABILITIES __stdcall GetDeviceCapabilities(void) = 0;
    virtual POWER_STATE_TYPE __stdcall GetPowerType(void) = 0;
    virtual POWER_STATE __stdcall GetPowerState(void) = 0;
    virtual DEVICE_POWER_STATE __stdcall GetPowerStateDeviceState(void) = 0;
    virtual SYSTEM_POWER_STATE __stdcall GetPowerStateSystemState(void) = 0;
    virtual POWER_ACTION __stdcall GetPowerAction(void) = 0;
    virtual ULONG __stdcall GetSystemPowerStateContext(void) = 0;
};

/* A remote I/O target's notification sink and dispatcher (a driver's
 * WdfIoTargetOpen of another device) */
struct IWudfTargetCallbackDeviceChange : public IUnknown {
};

struct IWudfIoDispatcher : public IUnknown {
    virtual void __stdcall Dispatch(IWudfIoIrp *Irp, IUnknown *Context) = 0;
};

struct IWudfRemoteDispatcher : public IUnknown {
    virtual HRESULT __stdcall BindToHandle(HANDLE Handle) = 0;
    virtual void __stdcall CloseHandle(void) = 0;
    virtual HANDLE __stdcall GetHandle(void) = 0;
};

struct IWudfCompanion : public IUnknown {
    virtual NTSTATUS __stdcall AllocateIfrMemory(PCWSTR ServiceName, ULONG PageCount, BOOLEAN IsDriverIfr,
                                                 BOOLEAN StoreInMiniDump, PVOID *Buffer, ULONG *BufferCb) = 0;
};

/* The host's side of one device: everything the framework asks of the
 * reflector and the rest of the stack */
struct IWudfDeviceStack : public IUnknown {
    virtual HRESULT __stdcall AttachDevice(IWudfDevice *Device, IWudfDevice **AttachedDevice) = 0;
    virtual void __stdcall SetPPO(IWudfDevice *Device) = 0;
    virtual PUMDF_VERSION_DATA __stdcall GetMinDriverVersion(void) = 0;
    virtual BOOL __stdcall ShouldPreserveIrpCompletionStatusCompatibility(void) = 0;
    virtual HKEY __stdcall GetDeviceRegistryKey(void) = 0;
    virtual void __stdcall GetDeviceStackPreferredTransferMode(UMINT::WDF_DEVICE_IO_BUFFER_RETRIEVAL *RetrievalMode,
                                                              UMINT::WDF_DEVICE_IO_TYPE *RWPreference,
                                                              UMINT::WDF_DEVICE_IO_TYPE *IoctlPreference) = 0;
    virtual HRESULT __stdcall GetStackCapabilities(PSTACK_DEVICE_CAPABILITIES Capabilities) = 0;
    virtual void __stdcall SetD3ColdSupport(BOOLEAN UseD3Cold) = 0;
    virtual HRESULT __stdcall AllocateIoIrp(IWudfDevice *DeviceObject, ULONG StackSize, IWudfIoIrp **Irp) = 0;
    virtual HRESULT __stdcall RequestPowerIrp(UCHAR MinorFunction, POWER_STATE PowerState,
                                              PREQUEST_POWER_COMPLETE CompletionFunction, PVOID Context) = 0;
    virtual HRESULT __stdcall CreateDeviceInterface(LPCGUID InterfaceClassGuid, PCWSTR ReferenceString) = 0;
    virtual HRESULT __stdcall SetDeviceInterfaceState(LPCGUID InterfaceClassGuid, PCWSTR ReferenceString,
                                                      BOOL Enable) = 0;
    virtual HRESULT __stdcall CreateSymbolicLink(PCWSTR SymbolicLink, PCWSTR Reserved) = 0;
    virtual HRESULT __stdcall CreateRegistryEntry(UMINT::PWDF_PROPERTY_STORE_ROOT Root,
                                                  UMINT::WDF_PROPERTY_STORE_RETRIEVE_FLAGS Flags,
                                                  REGSAM DesiredAccess, PCWSTR SubkeyPath, PHKEY Key,
                                                  UMINT::WDF_PROPERTY_STORE_DISPOSITION *Disposition) = 0;
    virtual HRESULT __stdcall GetUnifiedPropertyData(UMINT::PWDF_PROPERTY_STORE_ROOT Root, const DEVPROPKEY *PropertyKey,
                                                     LCID Lcid, ULONG Flags, ULONG BufferLength,
                                                     DEVPROPTYPE *PropertyType, ULONG *ResultLength,
                                                     PVOID PropertyBuffer) = 0;
    virtual HRESULT __stdcall SetUnifiedPropertyData(UMINT::PWDF_PROPERTY_STORE_ROOT Root, const DEVPROPKEY *PropertyKey,
                                                     LCID Lcid, ULONG Flags, DEVPROPTYPE PropertyType,
                                                     ULONG BufferLength, PVOID PropertyBuffer) = 0;
    virtual void __stdcall GetPdoProperties(PCWSTR *HardwareIds, PCWSTR *SetupClass, PCWSTR *BusEnumerator,
                                            PCWSTR *Manufacturer) = 0;
    virtual HRESULT __stdcall PostEvent(REFGUID EventGuid, WDF_EVENT_TYPE EventType, PBYTE Data, ULONG DataSizeCb) = 0;
    virtual HRESULT __stdcall UpdateIdleWakeWmiInstance(WmiIdleWakeInstanceUpdate UpdateType) = 0;
    virtual HRESULT __stdcall CreateWdfFile(IWudfDevice *DeviceObject, IWudfDevice *RelatedDevice, PCWSTR FileName,
                                            IWudfFile **File) = 0;
    virtual void __stdcall CloseFile(IWudfFile *File) = 0;
    virtual HRESULT __stdcall CreateRemoteDispatcher(IWudfIoDispatcher **IoDispatcher,
                                                     IWudfRemoteDispatcher **RemoteDispatcher) = 0;
    virtual HRESULT __stdcall RegisterTargetDeviceNotification(IWudfTargetCallbackDeviceChange *Callback,
                                                               HANDLE TargetHandle,
                                                               WUDF_TARGET_CONTEXT *RegistrationId) = 0;
    virtual void __stdcall UnregisterTargetDeviceNotification(WUDF_TARGET_CONTEXT RegistrationId) = 0;
    virtual HRESULT __stdcall MapIoSpace(PHYSICAL_ADDRESS PhysicalAddress, SIZE_T NumberOfBytes,
                                         MEMORY_CACHING_TYPE CacheType, PVOID *PseudoBaseAddress,
                                         PVOID *UserModeBaseAddress) = 0;
    virtual void __stdcall UnmapIoSpace(PVOID PseudoBaseAddress, SIZE_T NumberOfBytes) = 0;
    virtual void __stdcall ReadFromHardware(UMINT::WDF_DEVICE_HWACCESS_TARGET_TYPE Type,
                                            UMINT::WDF_DEVICE_HWACCESS_TARGET_SIZE Size, PVOID TargetAddress,
                                            ULONG_PTR *Value, PVOID Buffer, ULONG Count) = 0;
    virtual void __stdcall WriteToHardware(UMINT::WDF_DEVICE_HWACCESS_TARGET_TYPE Type,
                                           UMINT::WDF_DEVICE_HWACCESS_TARGET_SIZE Size, PVOID TargetAddress,
                                           ULONG_PTR Value, PVOID Buffer, ULONG Count) = 0;
    virtual HRESULT __stdcall InitializeInterrupt(WUDF_INTERRUPT_CONTEXT FxInterrupt, HANDLE InterruptEvent,
                                                  BOOLEAN ShareVector, RD_INTERRUPT_CONTEXT *RdInterruptContext) = 0;
    virtual HRESULT __stdcall AssignInterruptResources(RD_INTERRUPT_CONTEXT RdInterruptContext,
                                                       PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescRaw,
                                                       PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescTrans,
                                                       PWDF_INTERRUPT_INFO InterruptInfo,
                                                       BOOLEAN PassiveHandlingByRedirector) = 0;
    virtual HRESULT __stdcall ControlInterrupt(RD_INTERRUPT_CONTEXT RdInterruptContext,
                                               InterruptControlType ControlType) = 0;
    virtual HRESULT __stdcall SetInterruptPolicy(RD_INTERRUPT_CONTEXT RdInterruptContext,
                                                 WDF_INTERRUPT_POLICY Policy, WDF_INTERRUPT_PRIORITY Priority,
                                                 PGROUP_AFFINITY TargetProcessorSet) = 0;
    virtual HRESULT __stdcall AcknowledgeInterrupt(RD_INTERRUPT_CONTEXT RdInterruptContext, BOOLEAN Claimed) = 0;
    virtual HRESULT __stdcall InitializeUsbSS(void) = 0;
    virtual void __stdcall SubmitUsbIdleNotification(PVOID CallbackInfo, PWUDF_IO_COMPLETION_ROUTINE Completion,
                                                     PVOID Context) = 0;
    virtual void __stdcall CancelUsbSS(void) = 0;
    virtual void __stdcall SignalUsbSSCallbackProcessingComplete(void) = 0;
    virtual HRESULT __stdcall HidNotifyPresence(BOOLEAN IsPresent) = 0;
    virtual void __stdcall SetHidInterfaceSupport(void) = 0;
    virtual void __stdcall SetPowerState(POWER_STATE_TYPE Type, DEVICE_POWER_STATE State, ULONG *OldState) = 0;
    virtual void __stdcall InvalidateDeviceState(void) = 0;
    virtual HRESULT __stdcall DetachDevice(IWudfDevice *Device) = 0;
};

struct IWudfDeviceStack2 : public IWudfDeviceStack {
    virtual HRESULT __stdcall CreateDevice2(ULONG DriverId, IFxMessageDispatch *Dispatcher, ULONG DeviceExtensionSize,
                                            IWudfDevice2 **Device) = 0;
    virtual HRESULT __stdcall GetDeviceDirectory(PCWSTR *Path) = 0;
    virtual HRESULT __stdcall GetDriverDataDirectory(PCWSTR ServiceName, PCWSTR *Path) = 0;
    virtual HRESULT __stdcall GetInterfaceSymbolicLink(LPCGUID InterfaceClassGuid, PCWSTR ReferenceString,
                                                       PCWSTR *SymbolicLink) = 0;
    virtual HRESULT __stdcall OpenUSBCommunicationChannel(IWudfDevice *Device, IWudfDevice *AttachedDevice,
                                                          IWudfFile **File) = 0;
    virtual HRESULT __stdcall ConnectInterrupt(RD_INTERRUPT_CONTEXT RdInterruptContext,
                                               BOOLEAN *IsRdConnectingOrConnected) = 0;
    virtual HRESULT __stdcall ReenumerateSelf(void) = 0;
    virtual NTSTATUS __stdcall AllocateIfrMemory(PCWSTR ServiceName, ULONG PageCount, BOOLEAN IsDriverIfr,
                                                 BOOLEAN StoreInMiniDump, PVOID *Buffer, ULONG *BufferCb) = 0;
    virtual HRESULT __stdcall UpdateProcessKeepAliveCount(IWudfFile *File, BOOLEAN Increment) = 0;
    virtual HRESULT __stdcall PoFxRegisterDevice(ULONG DeviceFlags, BOOLEAN DirectedTransitionsEnabled) = 0;
    virtual void __stdcall PoFxUnregisterDevice(void) = 0;
    virtual void __stdcall PoFxStartDevicePowerManagement(void) = 0;
    virtual void __stdcall PoFxActivateComponent(void) = 0;
    virtual void __stdcall PoFxIdleComponent(void) = 0;
    virtual void __stdcall PoFxReportDevicePoweredOn(void) = 0;
    virtual void __stdcall PoFxSetDeviceIdleTimeout(ULONGLONG IdleTimeout) = 0;
    virtual void __stdcall PoFxCompleteDevicePowerNotRequired(void) = 0;
    virtual void __stdcall PoFxSimulateDevicePowerRequiredInReflector(void) = 0;
    virtual void __stdcall PoFxCompleteDirectedPowerDown(void) = 0;
};
