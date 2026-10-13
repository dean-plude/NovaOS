/*
 * host.h — WUDFHost.exe, NovaOS's UMDF 2 driver host: its objects
 *
 * The framework (Wudfx02000.dll) sees the host through the interfaces of
 * tools/umdf/inc/novaos_wudf_host.h; these are the host's
 * implementations of them:
 *
 *   CDeviceStack  IWudfDeviceStack2: one per host process (one device),
 *                 the framework's way to the reflector, the registry and
 *                 the rest of the device's stack (stack.cpp)
 *   CDevice       IWudfDevice2: a device object in the host's stack, the
 *                 framework's FxDevice its context (stack.cpp)
 *   CFile         IWudfFile2: a program's open of the device (stack.cpp)
 *   CIoIrp        IWudfIoIrp2 and CPnpIrp IWudfPnpIrp: a request, with a
 *                 stack location per device it passes through (irp.cpp)
 *
 * and main.cpp is the process: it loads the framework and the driver,
 * adds the device, and turns the reflector's messages (tools/umdf/wudfrd.h)
 * into requests.
 */
#pragma once
#include "fxmin.hpp"
#include "fxldrum.h"
#include "fxframeworkstubum.h"
#include "novaos_wudf_loader.h"
#include "../wudfrd.h"

void Log(const char *fmt, ...);
/* _snwprintf into @Out (@Chars of room), always terminated */
void Format(WCHAR *Out, size_t Chars, PCWSTR Fmt, ...);

class CDevice;
class CDeviceStack;
class CFile;

#define NOVA_COM_REFCOUNT                                                              \
    ULONG __stdcall AddRef(void) override { return (ULONG)InterlockedIncrement(&m_Ref); } \
    ULONG __stdcall Release(void) override                                             \
    {                                                                                  \
        LONG r = InterlockedDecrement(&m_Ref);                                         \
        if (r == 0) delete this;                                                       \
        return (ULONG)r;                                                               \
    }

/* ---- the channel to the reflector (main.cpp) ---- */

/* An IOCTL on the host's main channel handle: TRUE when it succeeded */
BOOL ChannelControl(DWORD code, const void *in, DWORD in_len, void *out, DWORD out_len, DWORD *got);

class CIrpCore;
/* A request from the reflector has completed: send its reply */
void ReflectorReply(CIrpCore *Irp);

/* ---- a program's open of the device ---- */

class CFile : public IWudfFile2 {
public:
    CFile(ULONGLONG Id, PCWSTR Name, DWORD Pid);
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override;
    NOVA_COM_REFCOUNT
    PCWSTR __stdcall GetName(void) override { return m_Name; }
    DWORD __stdcall GetInitiatorProcessId(void) override { return m_Pid; }
    PVOID __stdcall GetCreateContext(void) override { return NULL; }
    HANDLE __stdcall GetWeakRefHandle(void) override { return NULL; }

    /* the framework's FxFileObject for this open on @Device */
    IUnknown *GetFxContext(IWudfDevice *Device);
    void SetFxContext(IWudfDevice *Device, IUnknown *Context);

    ULONGLONG m_Id;                            /* (the reflector's name for the open) */
private:
    ~CFile();
    LONG m_Ref;
    WCHAR *m_Name;
    DWORD m_Pid;
    struct { IWudfDevice *Device; IUnknown *Context; } m_Fx[4];
};

/* ---- a device object ---- */

class CDevice : public IWudfDevice2 {
public:
    CDevice(CDeviceStack *Stack, ULONG DriverId, IFxMessageDispatch *Dispatch, ULONG ExtensionSize);
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override;
    NOVA_COM_REFCOUNT
    IWudfDevice *__stdcall GetAttachedDevice(void) override;
    /* (a user-mode device is always power-pageable: no non-paged power states) */
    ULONG __stdcall GetDeviceObjectWdmFlags(void) override { return DO_BUFFERED_IO | DO_POWER_PAGABLE; }
    IWudfDeviceStack *__stdcall GetDeviceStackInterface(void) override;
    PVOID __stdcall GetContext(void) override { return m_Context; }
    void __stdcall SetContext(PVOID Context) override { m_Context = Context; }
    PVOID __stdcall GetDeviceExtension(void) override { return m_Extension; }
    ULONG __stdcall GetStackSize2(void) override { return m_StackSize; }
    void __stdcall SetStackSize2(ULONG StackSize) override { m_StackSize = StackSize; }
    NTSTATUS __stdcall InitializeEventForRemoveLock(HANDLE *Event) override;

    CDeviceStack *m_Stack;
    IFxMessageDispatch *m_Dispatch;
    CDevice *m_Lower;                          /* the device below in the host's stack, or NULL */
private:
    ~CDevice();
    LONG m_Ref;
    ULONG m_DriverId;
    PVOID m_Context;
    BYTE *m_Extension;
    ULONG m_StackSize;
};

/* ---- requests ---- */

/* Where a request came from, and what to do when it completes */
enum IrpOrigin {
    IrpFromReflector,                          /* a message: the reply goes back */
    IrpFromDriver,                             /* AllocateIoIrp: the framework's own */
    IrpFromHost,                               /* RequestPowerIrp: the requester's callback */
};

#define IRP_STACK_MAX 8

/* One stack location */
struct IrpLocation {
    UCHAR Major, Minor;
    UMINT::WDF_REQUEST_TYPE Type;
    ULONG Length;                              /* read, write */
    LONGLONG Offset;
    ULONG Key;
    ULONG Code, InputLength, OutputLength;     /* device control */
    CFile *File;
    WUDFX_IRP_STACK_FLAGS Flags;
    PVOID Argument[4];
    PWUDF_IO_COMPLETION_ROUTINE Routine;
    PVOID RoutineContext;
    CDevice *Device;                           /* the device this location is for */
    BOOLEAN Pending;
};

/* What every request has, I/O or Plug and Play (irp.cpp) */
class CIrpCore {
public:
    CIrpCore(IWudfIrp *Self, BOOL IsPnp, IrpOrigin Origin, CDevice *Owner);
    virtual ~CIrpCore();

    /* Hand the request to the top of @Stack's devices with location 0 as
     * filled in (a request from the reflector) */
    void Start(CDevice *Top);

    void CoreComplete(void);
    NTSTATUS CoreForward(void);
    void CoreCopyToNext(void);
    void CoreSkip(void);
    void CoreSetNext(void);
    void CoreClearNext(void);
    BOOL CoreCancel(void);
    PWUDF_DRIVER_CANCEL CoreSetCancel(PWUDF_DRIVER_CANCEL Routine);

    IWudfIrp *m_Self;
    BOOL m_IsPnp;
    IrpOrigin m_Origin;
    CDevice *m_Owner;                          /* (IrpFromDriver: the device that allocated it) */
    CDevice *m_Active;                         /* the device that has the request now */
    IrpLocation m_Loc[IRP_STACK_MAX];
    int m_Cur;                                 /* the current location (-1: before the first) */
    BOOL m_ToSelf;
    NTSTATUS m_Status;
    ULONG_PTR m_Information;
    PVOID m_Context[4];
    LIST_ENTRY m_ListEntry;
    PWUDF_DRIVER_CANCEL m_CancelRoutine;
    BOOL m_Canceled, m_PendingReturned;
    DWORD m_Pid;

    /* IrpFromReflector: the message it answers */
    ULONGLONG m_MessageId;
    UINT m_MessageType;
    /* buffers (I/O): what the request brings and what it returns */
    BYTE *m_In;
    SIZE_T m_InLength;
    BYTE *m_Out;
    SIZE_T m_OutLength;
    /* IrpFromHost: RequestPowerIrp's requester */
    PREQUEST_POWER_COMPLETE m_PowerComplete;
    PVOID m_PowerContext;
    /* what the request owns: its data, and a reference on its file */
    BYTE *m_Owned;
    CFile *m_FileRef;

protected:
    void DispatchTo(CDevice *Device);
    void CompleteAtBottom(void);
    void Finish(void);
};

class CIoIrp : public IWudfIoIrp2, public CIrpCore {
public:
    CIoIrp(IrpOrigin Origin, CDevice *Owner);
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override;
    ULONG __stdcall AddRef(void) override { return (ULONG)InterlockedIncrement(&m_Ref); }
    ULONG __stdcall Release(void) override;

    /* IWudfIrp */
    void __stdcall CompleteRequest(void) override { CoreComplete(); }
    NTSTATUS __stdcall Forward(void) override { return CoreForward(); }
    void __stdcall SetCompletionRoutine(PWUDF_IO_COMPLETION_ROUTINE CompletionRoutine, PVOID Context) override;
    PWUDF_DRIVER_CANCEL __stdcall SetCancelRoutine(PWUDF_DRIVER_CANCEL CancelRoutine) override { return CoreSetCancel(CancelRoutine); }
    BOOL __stdcall Cancel(void) override { return CoreCancel(); }
    BOOL __stdcall IsCanceled(void) override { return m_Canceled; }
    void __stdcall CopyCurrentIrpStackLocationToNext(void) override { CoreCopyToNext(); }
    void __stdcall SkipCurrentIrpStackLocation(void) override { CoreSkip(); }
    void __stdcall SetNextIrpStackLocation(void) override { CoreSetNext(); }
    void __stdcall ClearNextStackLocation(void) override { CoreClearNext(); }
    void __stdcall MarkIrpPending(void) override;
    BOOLEAN __stdcall PendingReturned(void) override { return (BOOLEAN)m_PendingReturned; }
    void __stdcall PropagatePendingReturned(void) override;
    void __stdcall SetStatus(NTSTATUS Status) override { m_Status = Status; }
    NTSTATUS __stdcall GetStatus(void) override { return m_Status; }
    void __stdcall SetInformation(ULONG_PTR Information) override { m_Information = Information; }
    ULONG_PTR __stdcall GetInformation(void) override { return m_Information; }
    void __stdcall SetContext(ULONG Index, PVOID Value) override { if (Index < 4) m_Context[Index] = Value; }
    PVOID __stdcall GetContext(ULONG Index) override { return Index < 4 ? m_Context[Index] : NULL; }
    PLIST_ENTRY __stdcall GetListEntry(void) override { return &m_ListEntry; }
    DWORD __stdcall GetRequestorProcessId(void) override { return m_Pid; }
    HRESULT __stdcall ImpersonateRequestorProcess(SECURITY_IMPERSONATION_LEVEL ImpersonationLevel) override;
    void __stdcall RevertImpersonation(void) override {}

    /* IWudfIoIrp */
    UMINT::WDF_REQUEST_TYPE __stdcall GetType(void) override;
    KPROCESSOR_MODE __stdcall GetRequestorMode(void) override { return m_Origin == IrpFromReflector ? UserMode : KernelMode; }
    IWudfFile *__stdcall GetFile(void) override;
    HRESULT __stdcall RetrieveBuffers(SIZE_T *InputBufferCb, PVOID *InputBuffer, SIZE_T *OutputBufferCb,
                                      PVOID *OutputBuffer) override;
    void __stdcall GetReadParameters(ULONG *Length, LONGLONG *DeviceOffset, ULONG *Key) override;
    void __stdcall GetWriteParameters(ULONG *Length, LONGLONG *DeviceOffset, ULONG *Key) override;
    void __stdcall GetDeviceIoControlParameters(ULONG *IoControlCode, ULONG *InputBufferLength,
                                                ULONG *OutputBufferLength) override;
    UMINT::WDF_DEVICE_IO_TYPE __stdcall GetTransferMode(void) override { return UMINT::WdfDeviceIoBuffered; }
    BOOL __stdcall IsFrom32BitProcess(void) override { return FALSE; }
    BOOL __stdcall IsDriverCreated(void) override { return m_Origin == IrpFromDriver; }
    BOOL __stdcall GetUserModeDriverInitiatedIo(void) override { return m_DriverInitiated; }
    void __stdcall SetUserModeDriverInitiatedIo(BOOL IsUserModeDriverInitiated) override { m_DriverInitiated = IsUserModeDriverInitiated; }
    BOOL __stdcall IsActivityIdSet(void) override { return m_ActivitySet; }
    LPCGUID __stdcall GetActivityId(void) override { return &m_Activity; }
    void __stdcall SetActivityId(LPCGUID ActivityId) override;
    void __stdcall GetFrameworkRelatedFileObjectContext(IWudfDevice *DeviceObject, IUnknown **Context) override;
    void __stdcall SetFrameworkFileObjectContext(IWudfDevice *DeviceObject, IUnknown *Context) override;
    void __stdcall Reuse(NTSTATUS Status) override;
    void __stdcall Deallocate(void) override { Release(); }
    void __stdcall SetTypeForNextStackLocation(UMINT::WDF_REQUEST_TYPE Type) override;
    void __stdcall SetFileForNextIrpStackLocation(IWudfFile *File) override;
    void __stdcall SetFlagsForNextStackLocation(WUDFX_IRP_STACK_FLAGS Flags) override;
    void __stdcall SetReadParametersForNextStackLocation(ULONG Length, PLONGLONG DeviceOffset, ULONG Key) override;
    void __stdcall SetWriteParametersForNextStackLocation(ULONG Length, PLONGLONG DeviceOffset, ULONG Key) override;
    void __stdcall SetDeviceIoControlParametersForNextStackLocation(ULONG IoControlCode, ULONG InputBufferLength,
                                                                    ULONG OutputBufferLength) override;
    void __stdcall SetOtherParametersForNextStackLocation(PVOID *Argument1, PVOID *Argument2, PVOID *Argument3,
                                                          PVOID *Argument4) override;
    void __stdcall SwapCurrentBuffer(bool IsIoctl, SIZE_T NewInputBufferCb, PVOID NewInputBuffer,
                                     SIZE_T NewOutputBufferCb, PVOID NewOutputBuffer,
                                     PWUDFX_IRP_BUFFER_INFO OriginalBufferInfo) override;
    void __stdcall RestoreCurrentBuffer(PWUDFX_IRP_BUFFER_INFO OriginalBufferInfo) override;

    /* IWudfIoIrp2 */
    void __stdcall GetCreateParameters2(ULONG *Options, USHORT *FileAttributes, USHORT *ShareAccess,
                                        PIO_SECURITY_CONTEXT *SecurityContext) override;
    void __stdcall PrepareToForwardToSelf(void) override { m_ToSelf = TRUE; }

    /* CREATE's parameters */
    IO_SECURITY_CONTEXT m_Security;
    ULONG m_CreateOptions;
    USHORT m_ShareAccess;

private:
    LONG m_Ref;
    BOOL m_DriverInitiated, m_ActivitySet;
    GUID m_Activity;
    IrpLocation &Next(void);
};

class CPnpIrp : public IWudfPnpIrp, public CIrpCore {
public:
    CPnpIrp(IrpOrigin Origin, UCHAR Major, UCHAR Minor);
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override;
    ULONG __stdcall AddRef(void) override { return (ULONG)InterlockedIncrement(&m_Ref); }
    ULONG __stdcall Release(void) override;

    /* IWudfIrp */
    void __stdcall CompleteRequest(void) override { CoreComplete(); }
    NTSTATUS __stdcall Forward(void) override { return CoreForward(); }
    void __stdcall SetCompletionRoutine(PWUDF_IO_COMPLETION_ROUTINE CompletionRoutine, PVOID Context) override;
    PWUDF_DRIVER_CANCEL __stdcall SetCancelRoutine(PWUDF_DRIVER_CANCEL CancelRoutine) override { return CoreSetCancel(CancelRoutine); }
    BOOL __stdcall Cancel(void) override { return CoreCancel(); }
    BOOL __stdcall IsCanceled(void) override { return m_Canceled; }
    void __stdcall CopyCurrentIrpStackLocationToNext(void) override { CoreCopyToNext(); }
    void __stdcall SkipCurrentIrpStackLocation(void) override { CoreSkip(); }
    void __stdcall SetNextIrpStackLocation(void) override { CoreSetNext(); }
    void __stdcall ClearNextStackLocation(void) override { CoreClearNext(); }
    void __stdcall MarkIrpPending(void) override;
    BOOLEAN __stdcall PendingReturned(void) override { return (BOOLEAN)m_PendingReturned; }
    void __stdcall PropagatePendingReturned(void) override;
    void __stdcall SetStatus(NTSTATUS Status) override { m_Status = Status; }
    NTSTATUS __stdcall GetStatus(void) override { return m_Status; }
    void __stdcall SetInformation(ULONG_PTR Information) override { m_Information = Information; }
    ULONG_PTR __stdcall GetInformation(void) override { return m_Information; }
    void __stdcall SetContext(ULONG Index, PVOID Value) override { if (Index < 4) m_Context[Index] = Value; }
    PVOID __stdcall GetContext(ULONG Index) override { return Index < 4 ? m_Context[Index] : NULL; }
    PLIST_ENTRY __stdcall GetListEntry(void) override { return &m_ListEntry; }
    DWORD __stdcall GetRequestorProcessId(void) override { return m_Pid; }
    HRESULT __stdcall ImpersonateRequestorProcess(SECURITY_IMPERSONATION_LEVEL ImpersonationLevel) override;
    void __stdcall RevertImpersonation(void) override {}

    /* IWudfPnpIrp */
    UCHAR __stdcall GetMajorFunction(void) override;
    UCHAR __stdcall GetMinorFunction(void) override;
    PCM_RESOURCE_LIST __stdcall GetParameterAllocatedResources(void) override { return m_Raw; }
    PCM_RESOURCE_LIST __stdcall GetParameterAllocatedResourcesTranslated(void) override { return m_Translated; }
    PDEVICE_CAPABILITIES __stdcall GetDeviceCapabilities(void) override { return &m_Capabilities; }
    POWER_STATE_TYPE __stdcall GetPowerType(void) override { return m_PowerType; }
    POWER_STATE __stdcall GetPowerState(void) override { return m_PowerState; }
    DEVICE_POWER_STATE __stdcall GetPowerStateDeviceState(void) override { return m_PowerState.DeviceState; }
    SYSTEM_POWER_STATE __stdcall GetPowerStateSystemState(void) override { return m_PowerState.SystemState; }
    POWER_ACTION __stdcall GetPowerAction(void) override { return PowerActionNone; }
    ULONG __stdcall GetSystemPowerStateContext(void) override { return 0; }

    PCM_RESOURCE_LIST m_Raw, m_Translated;     /* START: (owned) */
    DEVICE_CAPABILITIES m_Capabilities;
    POWER_STATE_TYPE m_PowerType;
    POWER_STATE m_PowerState;

private:
    ~CPnpIrp() override;
    LONG m_Ref;
};

/* ---- the device's stack (stack.cpp) ---- */

class CDeviceStack : public IWudfDeviceStack2 {
public:
    CDeviceStack(PCWSTR InstanceId, PCWSTR ServiceName, const UMDF_VERSION_DATA *DriverVersion);
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override;
    ULONG __stdcall AddRef(void) override { return 2; }          /* (lives as long as the process) */
    ULONG __stdcall Release(void) override { return 1; }

    /* IWudfDeviceStack */
    HRESULT __stdcall AttachDevice(IWudfDevice *Device, IWudfDevice **AttachedDevice) override;
    void __stdcall SetPPO(IWudfDevice *Device) override { m_Ppo = Device; }
    PUMDF_VERSION_DATA __stdcall GetMinDriverVersion(void) override { return &m_DriverVersion; }
    BOOL __stdcall ShouldPreserveIrpCompletionStatusCompatibility(void) override { return FALSE; }
    HKEY __stdcall GetDeviceRegistryKey(void) override { return m_HardwareKey; }
    void __stdcall GetDeviceStackPreferredTransferMode(UMINT::WDF_DEVICE_IO_BUFFER_RETRIEVAL *RetrievalMode,
                                                      UMINT::WDF_DEVICE_IO_TYPE *RWPreference,
                                                      UMINT::WDF_DEVICE_IO_TYPE *IoctlPreference) override;
    HRESULT __stdcall GetStackCapabilities(PSTACK_DEVICE_CAPABILITIES Capabilities) override;
    void __stdcall SetD3ColdSupport(BOOLEAN UseD3Cold) override { (void)UseD3Cold; }
    HRESULT __stdcall AllocateIoIrp(IWudfDevice *DeviceObject, ULONG StackSize, IWudfIoIrp **Irp) override;
    HRESULT __stdcall RequestPowerIrp(UCHAR MinorFunction, POWER_STATE PowerState,
                                      PREQUEST_POWER_COMPLETE CompletionFunction, PVOID Context) override;
    HRESULT __stdcall CreateDeviceInterface(LPCGUID InterfaceClassGuid, PCWSTR ReferenceString) override;
    HRESULT __stdcall SetDeviceInterfaceState(LPCGUID InterfaceClassGuid, PCWSTR ReferenceString, BOOL Enable) override;
    HRESULT __stdcall CreateSymbolicLink(PCWSTR SymbolicLink, PCWSTR Reserved) override;
    HRESULT __stdcall CreateRegistryEntry(UMINT::PWDF_PROPERTY_STORE_ROOT Root,
                                          UMINT::WDF_PROPERTY_STORE_RETRIEVE_FLAGS Flags, REGSAM DesiredAccess,
                                          PCWSTR SubkeyPath, PHKEY Key,
                                          UMINT::WDF_PROPERTY_STORE_DISPOSITION *Disposition) override;
    HRESULT __stdcall GetUnifiedPropertyData(UMINT::PWDF_PROPERTY_STORE_ROOT Root, const DEVPROPKEY *PropertyKey,
                                             LCID Lcid, ULONG Flags, ULONG BufferLength, DEVPROPTYPE *PropertyType,
                                             ULONG *ResultLength, PVOID PropertyBuffer) override;
    HRESULT __stdcall SetUnifiedPropertyData(UMINT::PWDF_PROPERTY_STORE_ROOT Root, const DEVPROPKEY *PropertyKey,
                                             LCID Lcid, ULONG Flags, DEVPROPTYPE PropertyType, ULONG BufferLength,
                                             PVOID PropertyBuffer) override;
    void __stdcall GetPdoProperties(PCWSTR *HardwareIds, PCWSTR *SetupClass, PCWSTR *BusEnumerator,
                                    PCWSTR *Manufacturer) override;
    HRESULT __stdcall PostEvent(REFGUID EventGuid, WDF_EVENT_TYPE EventType, PBYTE Data, ULONG DataSizeCb) override;
    HRESULT __stdcall UpdateIdleWakeWmiInstance(WmiIdleWakeInstanceUpdate UpdateType) override;
    HRESULT __stdcall CreateWdfFile(IWudfDevice *DeviceObject, IWudfDevice *RelatedDevice, PCWSTR FileName,
                                    IWudfFile **File) override;
    void __stdcall CloseFile(IWudfFile *File) override;
    HRESULT __stdcall CreateRemoteDispatcher(IWudfIoDispatcher **IoDispatcher,
                                             IWudfRemoteDispatcher **RemoteDispatcher) override;
    HRESULT __stdcall RegisterTargetDeviceNotification(IWudfTargetCallbackDeviceChange *Callback, HANDLE TargetHandle,
                                                       WUDF_TARGET_CONTEXT *RegistrationId) override;
    void __stdcall UnregisterTargetDeviceNotification(WUDF_TARGET_CONTEXT RegistrationId) override { (void)RegistrationId; }
    HRESULT __stdcall MapIoSpace(PHYSICAL_ADDRESS PhysicalAddress, SIZE_T NumberOfBytes, MEMORY_CACHING_TYPE CacheType,
                                 PVOID *PseudoBaseAddress, PVOID *UserModeBaseAddress) override;
    void __stdcall UnmapIoSpace(PVOID PseudoBaseAddress, SIZE_T NumberOfBytes) override;
    void __stdcall ReadFromHardware(UMINT::WDF_DEVICE_HWACCESS_TARGET_TYPE Type,
                                    UMINT::WDF_DEVICE_HWACCESS_TARGET_SIZE Size, PVOID TargetAddress,
                                    ULONG_PTR *Value, PVOID Buffer, ULONG Count) override;
    void __stdcall WriteToHardware(UMINT::WDF_DEVICE_HWACCESS_TARGET_TYPE Type,
                                   UMINT::WDF_DEVICE_HWACCESS_TARGET_SIZE Size, PVOID TargetAddress, ULONG_PTR Value,
                                   PVOID Buffer, ULONG Count) override;
    HRESULT __stdcall InitializeInterrupt(WUDF_INTERRUPT_CONTEXT FxInterrupt, HANDLE InterruptEvent,
                                          BOOLEAN ShareVector, RD_INTERRUPT_CONTEXT *RdInterruptContext) override;
    HRESULT __stdcall AssignInterruptResources(RD_INTERRUPT_CONTEXT RdInterruptContext,
                                               PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescRaw,
                                               PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescTrans,
                                               PWDF_INTERRUPT_INFO InterruptInfo,
                                               BOOLEAN PassiveHandlingByRedirector) override;
    HRESULT __stdcall ControlInterrupt(RD_INTERRUPT_CONTEXT RdInterruptContext, InterruptControlType ControlType) override;
    HRESULT __stdcall SetInterruptPolicy(RD_INTERRUPT_CONTEXT RdInterruptContext, WDF_INTERRUPT_POLICY Policy,
                                         WDF_INTERRUPT_PRIORITY Priority, PGROUP_AFFINITY TargetProcessorSet) override;
    HRESULT __stdcall AcknowledgeInterrupt(RD_INTERRUPT_CONTEXT RdInterruptContext, BOOLEAN Claimed) override;
    HRESULT __stdcall InitializeUsbSS(void) override { return E_NOTIMPL; }
    void __stdcall SubmitUsbIdleNotification(PVOID CallbackInfo, PWUDF_IO_COMPLETION_ROUTINE Completion,
                                             PVOID Context) override;
    void __stdcall CancelUsbSS(void) override {}
    void __stdcall SignalUsbSSCallbackProcessingComplete(void) override {}
    HRESULT __stdcall HidNotifyPresence(BOOLEAN IsPresent) override { (void)IsPresent; return E_NOTIMPL; }
    void __stdcall SetHidInterfaceSupport(void) override {}
    void __stdcall SetPowerState(POWER_STATE_TYPE Type, DEVICE_POWER_STATE State, ULONG *OldState) override;
    void __stdcall InvalidateDeviceState(void) override {}
    HRESULT __stdcall DetachDevice(IWudfDevice *Device) override;

    /* IWudfDeviceStack2 */
    HRESULT __stdcall CreateDevice2(ULONG DriverId, IFxMessageDispatch *Dispatcher, ULONG DeviceExtensionSize,
                                    IWudfDevice2 **Device) override;
    HRESULT __stdcall GetDeviceDirectory(PCWSTR *Path) override;
    HRESULT __stdcall GetDriverDataDirectory(PCWSTR ServiceName, PCWSTR *Path) override;
    HRESULT __stdcall GetInterfaceSymbolicLink(LPCGUID InterfaceClassGuid, PCWSTR ReferenceString,
                                               PCWSTR *SymbolicLink) override;
    HRESULT __stdcall OpenUSBCommunicationChannel(IWudfDevice *Device, IWudfDevice *AttachedDevice,
                                                  IWudfFile **File) override;
    HRESULT __stdcall ConnectInterrupt(RD_INTERRUPT_CONTEXT RdInterruptContext,
                                       BOOLEAN *IsRdConnectingOrConnected) override;
    HRESULT __stdcall ReenumerateSelf(void) override { return E_NOTIMPL; }
    NTSTATUS __stdcall AllocateIfrMemory(PCWSTR ServiceName, ULONG PageCount, BOOLEAN IsDriverIfr,
                                         BOOLEAN StoreInMiniDump, PVOID *Buffer, ULONG *BufferCb) override;
    HRESULT __stdcall UpdateProcessKeepAliveCount(IWudfFile *File, BOOLEAN Increment) override;
    HRESULT __stdcall PoFxRegisterDevice(ULONG DeviceFlags, BOOLEAN DirectedTransitionsEnabled) override;
    void __stdcall PoFxUnregisterDevice(void) override {}
    void __stdcall PoFxStartDevicePowerManagement(void) override {}
    void __stdcall PoFxActivateComponent(void) override {}
    void __stdcall PoFxIdleComponent(void) override {}
    void __stdcall PoFxReportDevicePoweredOn(void) override {}
    void __stdcall PoFxSetDeviceIdleTimeout(ULONGLONG IdleTimeout) override { (void)IdleTimeout; }
    void __stdcall PoFxCompleteDevicePowerNotRequired(void) override {}
    void __stdcall PoFxSimulateDevicePowerRequiredInReflector(void) override {}
    void __stdcall PoFxCompleteDirectedPowerDown(void) override {}

    /* the host's own */
    CDevice *Top(void) { return m_Top; }
    HKEY m_HardwareKey;                        /* Enum\<instance>\Device Parameters */

private:
    HRESULT Interface(UINT Op, LPCGUID Guid, PCWSTR Reference, WCHAR *Link, DWORD LinkChars);
    WCHAR m_Instance[200], m_Service[64];
    UMDF_VERSION_DATA m_DriverVersion;
    CDevice *m_Top;
    IWudfDevice *m_Ppo;
    DEVICE_POWER_STATE m_PowerState;
    WCHAR m_HardwareIds[512], m_Class[64], m_Enumerator[32], m_Manufacturer[128];
    WCHAR m_Links[8][260];                     /* (GetInterfaceSymbolicLink's answers, kept) */
    int m_NextLink;
    WCHAR m_Directory[MAX_PATH];
};

extern CDeviceStack *g_Stack;
