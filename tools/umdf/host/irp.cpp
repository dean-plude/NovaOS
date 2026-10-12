/*
 * irp.cpp — WUDFHost.exe's requests: IWudfIoIrp2 and IWudfPnpIrp
 *
 * A request carries a stack location per device it passes through, as a
 * WDM IRP does.  m_Cur is the location of the device that has the request
 * (-1 before the first: a request the framework allocated, whose caller
 * fills in location 0 as "the next" and forwards it); a completion
 * routine lives in the location below the device that set it, and runs
 * as the request climbs back past that device, which may stop the climb
 * with STATUS_MORE_PROCESSING_REQUIRED and complete the request again
 * later.  A request that falls off the bottom of the host's stack (no
 * lower device) is done for the host: Plug and Play and the
 * create/cleanup/close of a file succeed there (the reflector has already
 * passed them down the kernel's stack), anything else is not supported.
 * When the climb ends above location 0 the request has completed:
 * Finish replies to the reflector, or calls a power request's requester.
 */
#include "host.h"

/* ---- what every request has ---- */

CIrpCore::CIrpCore(IWudfIrp *Self, BOOL IsPnp, IrpOrigin Origin, CDevice *Owner)
    : m_Self(Self), m_IsPnp(IsPnp), m_Origin(Origin), m_Owner(Owner), m_Active(Owner), m_Cur(-1),
      m_ToSelf(FALSE), m_Status(STATUS_SUCCESS), m_Information(0), m_CancelRoutine(NULL), m_Canceled(FALSE),
      m_PendingReturned(FALSE), m_Pid(GetCurrentProcessId()), m_MessageId(0), m_MessageType(0), m_In(NULL),
      m_InLength(0), m_Out(NULL), m_OutLength(0), m_PowerComplete(NULL), m_PowerContext(NULL), m_Owned(NULL),
      m_FileRef(NULL)
{
    ZeroMemory(m_Loc, sizeof(m_Loc));
    ZeroMemory(m_Context, sizeof(m_Context));
    InitializeListHead(&m_ListEntry);
}

CIrpCore::~CIrpCore()
{
    if (m_FileRef != NULL) m_FileRef->Release();
    if (m_Owned != NULL) HeapFree(GetProcessHeap(), 0, m_Owned);
}

void CIrpCore::Start(CDevice *Top)
{
    m_Self->AddRef();                           /* (it may complete inside the dispatch) */
    m_Cur = 0;
    m_Loc[0].Device = Top;
    m_Active = Top;
    if (Top != NULL) DispatchTo(Top);
    else CompleteAtBottom();
    m_Self->Release();
}

void CIrpCore::DispatchTo(CDevice *Device)
{
    IFxMessageDispatch *d = Device->m_Dispatch;
    if (m_IsPnp) {
        d->DispatchPnP(m_Self);
        return;
    }
    IWudfIoIrp *io = static_cast<IWudfIoIrp *>(m_Self);
    IrpLocation &l = m_Loc[m_Cur];
    IUnknown *fx = l.File != NULL ? l.File->GetFxContext(Device) : NULL;
    switch (l.Type) {
    case UMINT::WdfRequestCreate: d->CreateFile(io); break;
    case UMINT::WdfRequestRead: d->ReadFile(io, fx); break;
    case UMINT::WdfRequestWrite: d->WriteFile(io, fx); break;
    case UMINT::WdfRequestDeviceIoControl:
    case UMINT::WdfRequestInternalIoctl: d->DeviceControl(io, fx); break;
    case UMINT::WdfRequestCleanup: d->CleanupFile(io, fx); break;
    case UMINT::WdfRequestClose: d->CloseFile(io, fx); break;
    case UMINT::WdfRequestFlushBuffers: d->FlushBuffers(io, fx); break;
    case UMINT::WdfRequestQueryInformation: d->QueryInformationFile(io, fx); break;
    case UMINT::WdfRequestSetInformation: d->SetInformationFile(io, fx); break;
    default:
        m_Status = STATUS_INVALID_DEVICE_REQUEST;
        m_Information = 0;
        CoreComplete();
        break;
    }
}

void CIrpCore::CompleteAtBottom(void)
{
    if (m_IsPnp) {
        m_Status = STATUS_SUCCESS;
    } else {
        switch (m_Loc[m_Cur].Type) {
        case UMINT::WdfRequestCreate:
        case UMINT::WdfRequestCleanup:
        case UMINT::WdfRequestClose:
            m_Status = STATUS_SUCCESS;
            break;
        default:
            m_Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }
        m_Information = 0;
    }
    CoreComplete();
}

NTSTATUS CIrpCore::CoreForward(void)
{
    int next = m_Cur + 1;
    if (next >= IRP_STACK_MAX) {
        Log("a request went deeper than %d devices", IRP_STACK_MAX);
        m_Status = STATUS_INVALID_DEVICE_STATE;
        CoreComplete();
        return STATUS_SUCCESS;
    }
    CDevice *from = m_Active != NULL ? m_Active : m_Owner;
    CDevice *to = m_ToSelf ? from : (m_Active != NULL ? m_Active->m_Lower : NULL);
    m_ToSelf = FALSE;
    m_Cur = next;
    m_Loc[next].Device = to;
    m_Loc[next].Pending = FALSE;
    m_Active = to;
    if (to != NULL) DispatchTo(to);
    else CompleteAtBottom();
    return STATUS_SUCCESS;
}

void CIrpCore::CoreComplete(void)
{
    for (;;) {
        if (m_Cur < 0) {
            Finish();
            return;
        }
        IrpLocation &l = m_Loc[m_Cur];
        PWUDF_IO_COMPLETION_ROUTINE routine = l.Routine;
        PVOID context = l.RoutineContext;
        l.Routine = NULL;
        m_PendingReturned = l.Pending;
        m_Cur--;
        m_Active = m_Cur >= 0 ? m_Loc[m_Cur].Device : m_Owner;
        if (routine != NULL && routine(m_Active, m_Self, context) == STATUS_MORE_PROCESSING_REQUIRED) return;
    }
}

void CIrpCore::Finish(void)
{
    switch (m_Origin) {
    case IrpFromReflector:
        ReflectorReply(this);
        m_Self->Release();                      /* (the reference the message's dispatch took) */
        break;
    case IrpFromHost: {
        IO_STATUS_BLOCK iosb;
        iosb.Status = m_Status;
        iosb.Information = m_Information;
        POWER_STATE state;
        state.DeviceState = static_cast<CPnpIrp *>(this)->m_PowerState.DeviceState;
        if (m_PowerComplete != NULL)
            m_PowerComplete(m_Owner, m_Loc[0].Minor, state, m_PowerContext, &iosb);
        m_Self->Release();
        break;
    }
    case IrpFromDriver:
        /* (its completion routine owns it: FxIrp::FreeIrp releases it) */
        break;
    }
}

void CIrpCore::CoreCopyToNext(void)
{
    if (m_Cur < 0 || m_Cur + 1 >= IRP_STACK_MAX) return;
    IrpLocation &n = m_Loc[m_Cur + 1];
    n = m_Loc[m_Cur];
    n.Routine = NULL;
    n.RoutineContext = NULL;
    n.Pending = FALSE;
    n.Device = NULL;
}

void CIrpCore::CoreSkip(void)
{
    if (m_Cur >= 0) m_Cur--;
}

void CIrpCore::CoreSetNext(void)
{
    if (m_Cur + 1 >= IRP_STACK_MAX) return;
    m_Cur++;
    m_Loc[m_Cur].Device = m_Active;
}

void CIrpCore::CoreClearNext(void)
{
    if (m_Cur + 1 >= IRP_STACK_MAX) return;
    IrpLocation &n = m_Loc[m_Cur + 1];
    ZeroMemory(&n, sizeof(n));
}

BOOL CIrpCore::CoreCancel(void)
{
    m_Canceled = TRUE;
    PWUDF_DRIVER_CANCEL routine = (PWUDF_DRIVER_CANCEL)InterlockedExchangePointer((PVOID *)&m_CancelRoutine, NULL);
    if (routine == NULL) return FALSE;
    routine(m_Active, m_Self);
    return TRUE;
}

PWUDF_DRIVER_CANCEL CIrpCore::CoreSetCancel(PWUDF_DRIVER_CANCEL Routine)
{
    return (PWUDF_DRIVER_CANCEL)InterlockedExchangePointer((PVOID *)&m_CancelRoutine, (PVOID)Routine);
}

/* ---- I/O requests ---- */

CIoIrp::CIoIrp(IrpOrigin Origin, CDevice *Owner)
    : CIrpCore(static_cast<IWudfIrp *>(static_cast<IWudfIoIrp *>(this)), FALSE, Origin, Owner), m_CreateOptions(0),
      m_ShareAccess(0), m_Ref(1), m_DriverInitiated(FALSE), m_ActivitySet(FALSE)
{
    ZeroMemory(&m_Security, sizeof(m_Security));
    ZeroMemory(&m_Activity, sizeof(m_Activity));
}

HRESULT CIoIrp::QueryInterface(REFIID riid, void **ppv)
{
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IWudfIrp || riid == IID_IWudfIoIrp || riid == IID_IWudfIoIrp2) {
        *ppv = static_cast<IWudfIoIrp2 *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

ULONG CIoIrp::Release(void)
{
    LONG r = InterlockedDecrement(&m_Ref);
    if (r == 0) delete this;
    return (ULONG)r;
}

IrpLocation &CIoIrp::Next(void)
{
    int n = m_Cur + 1;
    if (n >= IRP_STACK_MAX) n = IRP_STACK_MAX - 1;
    return m_Loc[n];
}

/* the current location (location 0 before the first: the parameters the
 * creator of an allocated request filled in) */
#define CUR (m_Loc[m_Cur < 0 ? 0 : m_Cur])

void CIoIrp::SetCompletionRoutine(PWUDF_IO_COMPLETION_ROUTINE CompletionRoutine, PVOID Context)
{
    IrpLocation &n = Next();
    n.Routine = CompletionRoutine;
    n.RoutineContext = Context;
}

void CIoIrp::MarkIrpPending(void)
{
    if (m_Cur >= 0) m_Loc[m_Cur].Pending = TRUE;
}

void CIoIrp::PropagatePendingReturned(void)
{
    if (m_PendingReturned && m_Cur >= 0) m_Loc[m_Cur].Pending = TRUE;
}

HRESULT CIoIrp::ImpersonateRequestorProcess(SECURITY_IMPERSONATION_LEVEL ImpersonationLevel)
{
    (void)ImpersonationLevel;
    return E_NOTIMPL;
}

UMINT::WDF_REQUEST_TYPE CIoIrp::GetType(void)
{
    return CUR.Type;
}

IWudfFile *CIoIrp::GetFile(void)
{
    return CUR.File;
}

HRESULT CIoIrp::RetrieveBuffers(SIZE_T *InputBufferCb, PVOID *InputBuffer, SIZE_T *OutputBufferCb, PVOID *OutputBuffer)
{
    if (InputBufferCb != NULL) *InputBufferCb = m_InLength;
    if (InputBuffer != NULL) *InputBuffer = m_InLength ? m_In : NULL;
    if (OutputBufferCb != NULL) *OutputBufferCb = m_OutLength;
    if (OutputBuffer != NULL) *OutputBuffer = m_OutLength ? m_Out : NULL;
    return S_OK;
}

void CIoIrp::GetReadParameters(ULONG *Length, LONGLONG *DeviceOffset, ULONG *Key)
{
    if (Length != NULL) *Length = CUR.Length;
    if (DeviceOffset != NULL) *DeviceOffset = CUR.Offset;
    if (Key != NULL) *Key = CUR.Key;
}

void CIoIrp::GetWriteParameters(ULONG *Length, LONGLONG *DeviceOffset, ULONG *Key)
{
    GetReadParameters(Length, DeviceOffset, Key);
}

void CIoIrp::GetDeviceIoControlParameters(ULONG *IoControlCode, ULONG *InputBufferLength, ULONG *OutputBufferLength)
{
    if (IoControlCode != NULL) *IoControlCode = CUR.Code;
    if (InputBufferLength != NULL) *InputBufferLength = CUR.InputLength;
    if (OutputBufferLength != NULL) *OutputBufferLength = CUR.OutputLength;
}

void CIoIrp::SetActivityId(LPCGUID ActivityId)
{
    if (ActivityId == NULL) {
        m_ActivitySet = FALSE;
        return;
    }
    m_Activity = *ActivityId;
    m_ActivitySet = TRUE;
}

void CIoIrp::GetFrameworkRelatedFileObjectContext(IWudfDevice *DeviceObject, IUnknown **Context)
{
    /* (NovaOS's opens have no related file object) */
    (void)DeviceObject;
    if (Context != NULL) *Context = NULL;
}

void CIoIrp::SetFrameworkFileObjectContext(IWudfDevice *DeviceObject, IUnknown *Context)
{
    CFile *f = static_cast<CFile *>(CUR.File);
    if (f != NULL) f->SetFxContext(DeviceObject, Context);
}

void CIoIrp::Reuse(NTSTATUS Status)
{
    ZeroMemory(m_Loc, sizeof(m_Loc));
    m_Cur = -1;
    m_Active = m_Owner;
    m_ToSelf = FALSE;
    m_Status = Status;
    m_Information = 0;
    m_CancelRoutine = NULL;
    m_Canceled = FALSE;
    m_PendingReturned = FALSE;
    m_In = m_Out = NULL;
    m_InLength = m_OutLength = 0;
}

void CIoIrp::SetTypeForNextStackLocation(UMINT::WDF_REQUEST_TYPE Type)
{
    static const UCHAR major[] = {
        0xff, IRP_MJ_CREATE, IRP_MJ_CLEANUP, IRP_MJ_READ, IRP_MJ_WRITE, IRP_MJ_DEVICE_CONTROL, IRP_MJ_CLOSE,
        IRP_MJ_INTERNAL_DEVICE_CONTROL, IRP_MJ_FLUSH_BUFFERS, IRP_MJ_QUERY_INFORMATION, IRP_MJ_SET_INFORMATION,
    };
    IrpLocation &n = Next();
    n.Type = Type;
    n.Major = (unsigned)Type < sizeof(major) ? major[Type] : 0xff;
}

void CIoIrp::SetFileForNextIrpStackLocation(IWudfFile *File)
{
    Next().File = static_cast<CFile *>(static_cast<IWudfFile2 *>(File));
}

void CIoIrp::SetFlagsForNextStackLocation(WUDFX_IRP_STACK_FLAGS Flags)
{
    Next().Flags = Flags;
}

void CIoIrp::SetReadParametersForNextStackLocation(ULONG Length, PLONGLONG DeviceOffset, ULONG Key)
{
    IrpLocation &n = Next();
    n.Length = Length;
    n.Offset = DeviceOffset != NULL ? *DeviceOffset : 0;
    n.Key = Key;
}

void CIoIrp::SetWriteParametersForNextStackLocation(ULONG Length, PLONGLONG DeviceOffset, ULONG Key)
{
    SetReadParametersForNextStackLocation(Length, DeviceOffset, Key);
}

void CIoIrp::SetDeviceIoControlParametersForNextStackLocation(ULONG IoControlCode, ULONG InputBufferLength,
                                                              ULONG OutputBufferLength)
{
    IrpLocation &n = Next();
    n.Code = IoControlCode;
    n.InputLength = InputBufferLength;
    n.OutputLength = OutputBufferLength;
}

void CIoIrp::SetOtherParametersForNextStackLocation(PVOID *Argument1, PVOID *Argument2, PVOID *Argument3,
                                                    PVOID *Argument4)
{
    IrpLocation &n = Next();
    if (Argument1 != NULL) n.Argument[0] = *Argument1;
    if (Argument2 != NULL) n.Argument[1] = *Argument2;
    if (Argument3 != NULL) n.Argument[2] = *Argument3;
    if (Argument4 != NULL) n.Argument[3] = *Argument4;
}

void CIoIrp::SwapCurrentBuffer(bool IsIoctl, SIZE_T NewInputBufferCb, PVOID NewInputBuffer, SIZE_T NewOutputBufferCb,
                               PVOID NewOutputBuffer, PWUDFX_IRP_BUFFER_INFO OriginalBufferInfo)
{
    (void)IsIoctl;
    if (OriginalBufferInfo != NULL) {
        OriginalBufferInfo->InputBuffer = m_In;
        OriginalBufferInfo->InputBufferCb = m_InLength;
        OriginalBufferInfo->OutputBuffer = m_Out;
        OriginalBufferInfo->OutputBufferCb = m_OutLength;
    }
    m_In = (BYTE *)NewInputBuffer;
    m_InLength = NewInputBufferCb;
    m_Out = (BYTE *)NewOutputBuffer;
    m_OutLength = NewOutputBufferCb;
}

void CIoIrp::RestoreCurrentBuffer(PWUDFX_IRP_BUFFER_INFO OriginalBufferInfo)
{
    if (OriginalBufferInfo == NULL) return;
    m_In = (BYTE *)OriginalBufferInfo->InputBuffer;
    m_InLength = OriginalBufferInfo->InputBufferCb;
    m_Out = (BYTE *)OriginalBufferInfo->OutputBuffer;
    m_OutLength = OriginalBufferInfo->OutputBufferCb;
}

void CIoIrp::GetCreateParameters2(ULONG *Options, USHORT *FileAttributes, USHORT *ShareAccess,
                                  PIO_SECURITY_CONTEXT *SecurityContext)
{
    if (Options != NULL) *Options = m_CreateOptions;
    if (FileAttributes != NULL) *FileAttributes = 0;
    if (ShareAccess != NULL) *ShareAccess = m_ShareAccess;
    if (SecurityContext != NULL) *SecurityContext = &m_Security;
}

/* ---- Plug and Play and power requests ---- */

CPnpIrp::CPnpIrp(IrpOrigin Origin, UCHAR Major, UCHAR Minor)
    : CIrpCore(static_cast<IWudfIrp *>(static_cast<IWudfPnpIrp *>(this)), TRUE, Origin, NULL), m_Raw(NULL),
      m_Translated(NULL), m_PowerType(SystemPowerState), m_Ref(1)
{
    ZeroMemory(&m_Capabilities, sizeof(m_Capabilities));
    m_PowerState.SystemState = PowerSystemUnspecified;
    m_Loc[0].Major = Major;
    m_Loc[0].Minor = Minor;
}

CPnpIrp::~CPnpIrp()
{
    if (m_Raw != NULL) HeapFree(GetProcessHeap(), 0, m_Raw);
    if (m_Translated != NULL) HeapFree(GetProcessHeap(), 0, m_Translated);
}

HRESULT CPnpIrp::QueryInterface(REFIID riid, void **ppv)
{
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IWudfIrp || riid == IID_IWudfPnpIrp) {
        *ppv = static_cast<IWudfPnpIrp *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

ULONG CPnpIrp::Release(void)
{
    LONG r = InterlockedDecrement(&m_Ref);
    if (r == 0) delete this;
    return (ULONG)r;
}

void CPnpIrp::SetCompletionRoutine(PWUDF_IO_COMPLETION_ROUTINE CompletionRoutine, PVOID Context)
{
    int n = m_Cur + 1 < IRP_STACK_MAX ? m_Cur + 1 : IRP_STACK_MAX - 1;
    m_Loc[n].Routine = CompletionRoutine;
    m_Loc[n].RoutineContext = Context;
}

void CPnpIrp::MarkIrpPending(void)
{
    if (m_Cur >= 0) m_Loc[m_Cur].Pending = TRUE;
}

void CPnpIrp::PropagatePendingReturned(void)
{
    if (m_PendingReturned && m_Cur >= 0) m_Loc[m_Cur].Pending = TRUE;
}

HRESULT CPnpIrp::ImpersonateRequestorProcess(SECURITY_IMPERSONATION_LEVEL ImpersonationLevel)
{
    (void)ImpersonationLevel;
    return E_NOTIMPL;
}

UCHAR CPnpIrp::GetMajorFunction(void)
{
    return CUR.Major;
}

UCHAR CPnpIrp::GetMinorFunction(void)
{
    return CUR.Minor;
}
