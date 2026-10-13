/*
 * stack.cpp — WUDFHost.exe's device stack: IWudfDeviceStack2, its device
 * objects (IWudfDevice2) and file objects (IWudfFile2)
 *
 * The host runs one device: the stack is the UMDF drivers' device objects
 * (the framework's FxDevices are their contexts), top to bottom, above
 * the reflector's device in the kernel.  What the framework asks of the
 * rest of the system goes to the reflector over the host channel (device
 * interfaces), to the registry (the device's and the driver's keys, read
 * as Windows lays them out) or to the configuration manager (the
 * device's properties).  Hardware access, interrupts, USB and remote I/O
 * targets are not supported: those return E_NOTIMPL, which the framework
 * reports to the driver as a failure (docs/drivers.md).
 */
#include "host.h"

static const WCHAR k_Enum[] = L"SYSTEM\\CurrentControlSet\\Enum\\";
static const WCHAR k_Services[] = L"SYSTEM\\CurrentControlSet\\Services\\WUDF\\Services\\";

/* ---- a program's open of the device ---- */

CFile::CFile(ULONGLONG Id, PCWSTR Name, DWORD Pid) : m_Id(Id), m_Ref(1), m_Pid(Pid)
{
    size_t n = Name != NULL ? lstrlenW(Name) : 0;
    m_Name = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
    if (m_Name != NULL) {
        if (n) CopyMemory(m_Name, Name, n * sizeof(WCHAR));
        m_Name[n] = 0;
    }
    ZeroMemory(m_Fx, sizeof(m_Fx));
}

CFile::~CFile()
{
    if (m_Name != NULL) HeapFree(GetProcessHeap(), 0, m_Name);
}

HRESULT CFile::QueryInterface(REFIID riid, void **ppv)
{
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IWudfFile || riid == IID_IWudfFile2) {
        *ppv = static_cast<IWudfFile2 *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

IUnknown *CFile::GetFxContext(IWudfDevice *Device)
{
    for (int i = 0; i < 4; i++)
        if (m_Fx[i].Device == Device) return m_Fx[i].Context;
    return NULL;
}

void CFile::SetFxContext(IWudfDevice *Device, IUnknown *Context)
{
    for (int i = 0; i < 4; i++)
        if (m_Fx[i].Device == Device || m_Fx[i].Device == NULL) {
            m_Fx[i].Device = Device;
            m_Fx[i].Context = Context;
            return;
        }
    Log("a file has more than 4 devices' contexts");
}

/* ---- a device object ---- */

CDevice::CDevice(CDeviceStack *Stack, ULONG DriverId, IFxMessageDispatch *Dispatch, ULONG ExtensionSize)
    : m_Stack(Stack), m_Dispatch(Dispatch), m_Lower(NULL), m_Ref(1), m_DriverId(DriverId), m_Context(NULL),
      m_StackSize(1)
{
    m_Extension = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ExtensionSize ? ExtensionSize : 1);
    m_Dispatch->AddRef();
}

CDevice::~CDevice()
{
    m_Dispatch->Release();
    if (m_Extension != NULL) HeapFree(GetProcessHeap(), 0, m_Extension);
}

HRESULT CDevice::QueryInterface(REFIID riid, void **ppv)
{
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IWudfDevice || riid == IID_IWudfDevice2) {
        *ppv = static_cast<IWudfDevice2 *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

IWudfDevice *CDevice::GetAttachedDevice(void)
{
    return m_Lower;
}

IWudfDeviceStack *CDevice::GetDeviceStackInterface(void)
{
    return m_Stack;
}

NTSTATUS CDevice::InitializeEventForRemoveLock(HANDLE *Event)
{
    /* (a notification event, as the kernel's remove lock has; the
     * framework closes it with the device's extension) */
    *Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    return *Event != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

/* ---- the device's stack ---- */

CDeviceStack *g_Stack;

/* A string property of the device from the configuration manager (cfgmgr32
 * is loaded when first needed: the host itself does not link it) */
typedef DWORD(WINAPI *PFN_CM_LOCATE)(DWORD *, PCWSTR, ULONG);
typedef DWORD(WINAPI *PFN_CM_GETPROP)(DWORD, const DEVPROPKEY *, DEVPROPTYPE *, PBYTE, PULONG, ULONG);
static PFN_CM_LOCATE s_Locate;
static PFN_CM_GETPROP s_GetProperty;

static DWORD DeviceProperty(PCWSTR Instance, const DEVPROPKEY *Key, DEVPROPTYPE *Type, PVOID Buffer, ULONG *Length)
{
    if (s_Locate == NULL) {
        HMODULE cm = LoadLibraryW(L"cfgmgr32.dll");
        if (cm == NULL) return ERROR_NOT_FOUND;
        s_GetProperty = (PFN_CM_GETPROP)(void *)GetProcAddress(cm, "CM_Get_DevNode_PropertyW");
        s_Locate = (PFN_CM_LOCATE)(void *)GetProcAddress(cm, "CM_Locate_DevNodeW");
        if (s_Locate == NULL || s_GetProperty == NULL) {
            s_Locate = NULL;
            return ERROR_NOT_FOUND;
        }
    }
    DWORD dn;
    if (s_Locate(&dn, Instance, 0) != 0) return ERROR_NOT_FOUND;
    DWORD cr = s_GetProperty(dn, Key, Type, (PBYTE)Buffer, Length, 0);
    if (cr == 0) return ERROR_SUCCESS;
    return cr == 0x1A /* CR_BUFFER_SMALL */ ? ERROR_INSUFFICIENT_BUFFER : ERROR_NOT_FOUND;
}

static void StringProperty(PCWSTR Instance, const DEVPROPKEY *Key, WCHAR *Out, ULONG Chars)
{
    DEVPROPTYPE type;
    ULONG len = (Chars - 2) * sizeof(WCHAR);
    ZeroMemory(Out, Chars * sizeof(WCHAR));
    if (DeviceProperty(Instance, Key, &type, Out, &len) != ERROR_SUCCESS) ZeroMemory(Out, Chars * sizeof(WCHAR));
}

CDeviceStack::CDeviceStack(PCWSTR InstanceId, PCWSTR ServiceName, const UMDF_VERSION_DATA *DriverVersion)
    : m_HardwareKey(NULL), m_DriverVersion(*DriverVersion), m_Top(NULL), m_Ppo(NULL), m_PowerState(PowerDeviceD0),
      m_NextLink(0)
{
    lstrcpynW(m_Instance, InstanceId, ARRAYSIZE(m_Instance));
    lstrcpynW(m_Service, ServiceName, ARRAYSIZE(m_Service));
    WCHAR key[300];
    Format(key, ARRAYSIZE(key), L"%s%s\\Device Parameters", k_Enum, m_Instance);
    RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &m_HardwareKey, NULL);
    StringProperty(m_Instance, &DEVPKEY_Device_HardwareIds, m_HardwareIds, ARRAYSIZE(m_HardwareIds));
    StringProperty(m_Instance, &DEVPKEY_Device_Class, m_Class, ARRAYSIZE(m_Class));
    StringProperty(m_Instance, &DEVPKEY_Device_EnumeratorName, m_Enumerator, ARRAYSIZE(m_Enumerator));
    StringProperty(m_Instance, &DEVPKEY_Device_Manufacturer, m_Manufacturer, ARRAYSIZE(m_Manufacturer));
    ZeroMemory(m_Links, sizeof(m_Links));
    m_Directory[0] = 0;
}

HRESULT CDeviceStack::QueryInterface(REFIID riid, void **ppv)
{
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IWudfDeviceStack || riid == IID_IWudfDeviceStack2) {
        *ppv = static_cast<IWudfDeviceStack2 *>(this);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

HRESULT CDeviceStack::CreateDevice2(ULONG DriverId, IFxMessageDispatch *Dispatcher, ULONG DeviceExtensionSize,
                                    IWudfDevice2 **Device)
{
    if (Device == NULL || Dispatcher == NULL) return E_POINTER;
    CDevice *d = new CDevice(this, DriverId, Dispatcher, DeviceExtensionSize);
    if (d == NULL) return E_OUTOFMEMORY;
    *Device = d;                                /* (the caller's reference) */
    return S_OK;
}

HRESULT CDeviceStack::AttachDevice(IWudfDevice *Device, IWudfDevice **AttachedDevice)
{
    CDevice *d = static_cast<CDevice *>(static_cast<IWudfDevice2 *>(Device));
    d->AddRef();                                /* (the stack's reference, until DetachDevice) */
    d->m_Lower = m_Top;
    d->SetStackSize2(m_Top != NULL ? m_Top->GetStackSize2() + 1 : 1);
    if (AttachedDevice != NULL) {
        *AttachedDevice = m_Top;
        if (m_Top != NULL) m_Top->AddRef();
    }
    m_Top = d;
    return S_OK;
}

HRESULT CDeviceStack::DetachDevice(IWudfDevice *Device)
{
    CDevice *d = static_cast<CDevice *>(static_cast<IWudfDevice2 *>(Device));
    CDevice **p = &m_Top;
    while (*p != NULL && *p != d) p = &(*p)->m_Lower;
    if (*p == NULL) return E_INVALIDARG;
    *p = d->m_Lower;
    if (m_Ppo == Device) m_Ppo = NULL;
    d->Release();
    return S_OK;
}

void CDeviceStack::GetDeviceStackPreferredTransferMode(UMINT::WDF_DEVICE_IO_BUFFER_RETRIEVAL *RetrievalMode,
                                                       UMINT::WDF_DEVICE_IO_TYPE *RWPreference,
                                                       UMINT::WDF_DEVICE_IO_TYPE *IoctlPreference)
{
    /* (the reflector copies every request's data: buffered I/O only) */
    if (RetrievalMode != NULL) *RetrievalMode = UMINT::WdfDeviceIoBufferRetrievalDeferred;
    if (RWPreference != NULL) *RWPreference = UMINT::WdfDeviceIoBuffered;
    if (IoctlPreference != NULL) *IoctlPreference = UMINT::WdfDeviceIoBuffered;
}

HRESULT CDeviceStack::GetStackCapabilities(PSTACK_DEVICE_CAPABILITIES Capabilities)
{
    ZeroMemory(Capabilities, sizeof(*Capabilities));
    DEVICE_CAPABILITIES *c = &Capabilities->DeviceCaps;
    c->Size = sizeof(*c);
    c->Version = 1;
    c->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    for (int s = PowerSystemSleeping1; s < PowerSystemMaximum; s++) c->DeviceState[s] = PowerDeviceD3;
    c->SystemWake = PowerSystemUnspecified;
    c->DeviceWake = PowerDeviceUnspecified;
    c->Address = 0xFFFFFFFF;
    c->UINumber = 0xFFFFFFFF;
    for (int s = 0; s <= PowerSystemHibernate; s++) Capabilities->DeepestWakeableDstate[s] = DeviceWakeDepthNotWakeable;
    return S_OK;
}

HRESULT CDeviceStack::AllocateIoIrp(IWudfDevice *DeviceObject, ULONG StackSize, IWudfIoIrp **Irp)
{
    (void)StackSize;
    if (Irp == NULL) return E_POINTER;
    CIoIrp *i = new CIoIrp(IrpFromDriver, static_cast<CDevice *>(static_cast<IWudfDevice2 *>(DeviceObject)));
    if (i == NULL) return E_OUTOFMEMORY;
    i->AddRef();                                /* (FxIrp::FreeIrp releases twice: Release, then Deallocate) */
    *Irp = i;
    return S_OK;
}

static DWORD WINAPI PowerThread(LPVOID Param)
{
    CPnpIrp *irp = (CPnpIrp *)Param;
    irp->Start(g_Stack->Top());
    irp->Release();                             /* (the thread's reference) */
    return 0;
}

HRESULT CDeviceStack::RequestPowerIrp(UCHAR MinorFunction, POWER_STATE PowerState,
                                      PREQUEST_POWER_COMPLETE CompletionFunction, PVOID Context)
{
    /* A device power request (PoRequestPowerIrp): sent to the top of the
     * stack on a thread of its own, as the kernel's would arrive, and the
     * requester called when it completes */
    CPnpIrp *irp = new CPnpIrp(IrpFromHost, IRP_MJ_POWER, MinorFunction);
    if (irp == NULL) return E_OUTOFMEMORY;
    irp->m_PowerType = DevicePowerState;
    irp->m_PowerState = PowerState;
    irp->m_PowerComplete = CompletionFunction;
    irp->m_PowerContext = Context;
    irp->m_Owner = static_cast<CDevice *>(static_cast<IWudfDevice2 *>(m_Ppo != NULL ? m_Ppo : m_Top));
    irp->AddRef();                              /* (one for Finish, one for the thread) */
    HANDLE t = CreateThread(NULL, 0, PowerThread, irp, 0, NULL);
    if (t == NULL) {
        irp->Release();
        irp->Release();
        return HRESULT_FROM_WIN32(GetLastError());
    }
    CloseHandle(t);
    return S_OK;
}

HRESULT CDeviceStack::Interface(UINT Op, LPCGUID Guid, PCWSTR Reference, WCHAR *Link, DWORD LinkChars)
{
    WUDFRD_IFACE f;
    ZeroMemory(&f, sizeof(f));
    f.op = Op;
    CopyMemory(f.guid, Guid, sizeof(f.guid));
    if (Reference != NULL) {
        int n = lstrlenW(Reference);
        if (n >= (int)ARRAYSIZE(f.ref)) return E_INVALIDARG;
        CopyMemory(f.ref, Reference, n * sizeof(WCHAR));
    }
    WCHAR scratch[260];
    if (Link == NULL) {
        Link = scratch;
        LinkChars = ARRAYSIZE(scratch);
    }
    DWORD got = 0;
    if (!ChannelControl(IOCTL_WUDFRD_IFACE, &f, sizeof(f), Link, (LinkChars - 1) * sizeof(WCHAR), &got))
        return HRESULT_FROM_WIN32(GetLastError());
    Link[got / sizeof(WCHAR)] = 0;
    return S_OK;
}

HRESULT CDeviceStack::CreateDeviceInterface(LPCGUID InterfaceClassGuid, PCWSTR ReferenceString)
{
    return Interface(WUDFRD_IFACE_REGISTER, InterfaceClassGuid, ReferenceString, NULL, 0);
}

HRESULT CDeviceStack::SetDeviceInterfaceState(LPCGUID InterfaceClassGuid, PCWSTR ReferenceString, BOOL Enable)
{
    return Interface(Enable ? WUDFRD_IFACE_ENABLE : WUDFRD_IFACE_DISABLE, InterfaceClassGuid, ReferenceString, NULL, 0);
}

HRESULT CDeviceStack::GetInterfaceSymbolicLink(LPCGUID InterfaceClassGuid, PCWSTR ReferenceString,
                                               PCWSTR *SymbolicLink)
{
    /* (the framework keeps the string as long as the device: the host
     * keeps the last few) */
    WCHAR *link = m_Links[InterlockedIncrement((LONG *)&m_NextLink) % ARRAYSIZE(m_Links)];
    HRESULT hr = Interface(WUDFRD_IFACE_REGISTER, InterfaceClassGuid, ReferenceString, link, ARRAYSIZE(m_Links[0]));
    *SymbolicLink = SUCCEEDED(hr) ? link : NULL;
    return hr;
}

HRESULT CDeviceStack::CreateSymbolicLink(PCWSTR SymbolicLink, PCWSTR Reserved)
{
    (void)SymbolicLink;
    (void)Reserved;
    return E_NOTIMPL;
}

/* Open or create @Path under HKLM (@Volatile: a key that does not survive
 * a restart) */
static HRESULT OpenKey(PCWSTR Path, BOOL Create, BOOL Volatile, REGSAM Access, PHKEY Key,
                       UMINT::WDF_PROPERTY_STORE_DISPOSITION *Disposition)
{
    LONG err;
    DWORD disp = REG_OPENED_EXISTING_KEY;
    if (Create)
        err = RegCreateKeyExW(HKEY_LOCAL_MACHINE, Path, 0, NULL, Volatile ? REG_OPTION_VOLATILE : 0, Access, NULL,
                              Key, &disp);
    else
        err = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, Access, Key);
    if (err != ERROR_SUCCESS) {
        *Key = NULL;
        return HRESULT_FROM_WIN32(err);
    }
    if (Disposition != NULL)
        *Disposition = disp == REG_CREATED_NEW_KEY ? UMINT::CreatedNewStore : UMINT::OpenedExistingStore;
    return S_OK;
}

HRESULT CDeviceStack::CreateRegistryEntry(UMINT::PWDF_PROPERTY_STORE_ROOT Root,
                                          UMINT::WDF_PROPERTY_STORE_RETRIEVE_FLAGS Flags, REGSAM DesiredAccess,
                                          PCWSTR SubkeyPath, PHKEY Key,
                                          UMINT::WDF_PROPERTY_STORE_DISPOSITION *Disposition)
{
    WCHAR path[512];
    if (Root == NULL || Key == NULL) return E_POINTER;
    switch (Root->RootClass) {
    case UMINT::WdfPropertyStoreRootClassHardwareKey: {
        /* Enum\<instance>\Device Parameters, or a driver's subkey of it */
        PCWSTR sub = Root->Qualifier.HardwareKey.ServiceName;
        if (sub == NULL || !lstrcmpiW(sub, WDF_PROPERTY_STORE_HARDWARE_KEY_ROOT))
            Format(path, ARRAYSIZE(path), L"%s%s\\Device Parameters", k_Enum, m_Instance);
        else
            Format(path, ARRAYSIZE(path), L"%s%s\\Device Parameters\\%s", k_Enum, m_Instance, sub);
        break;
    }
    case UMINT::WdfPropertyStoreRootClassSoftwareKey: {
        /* the driver key: Control\Class\<the device's Driver value> */
        WCHAR driver[128];
        DWORD len = sizeof(driver) - sizeof(WCHAR), type;
        HKEY e;
        Format(path, ARRAYSIZE(path), L"%s%s", k_Enum, m_Instance);
        driver[0] = 0;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &e) == ERROR_SUCCESS) {
            if (RegQueryValueExW(e, L"Driver", NULL, &type, (BYTE *)driver, &len) != ERROR_SUCCESS || type != REG_SZ)
                len = 0;
            driver[len / sizeof(WCHAR)] = 0;
            RegCloseKey(e);
        }
        if (driver[0])
            Format(path, ARRAYSIZE(path), L"SYSTEM\\CurrentControlSet\\Control\\Class\\%s", driver);
        else
            Format(path, ARRAYSIZE(path), L"%s%s\\Driver Parameters", k_Enum, m_Instance);
        break;
    }
    case UMINT::WdfPropertyStoreRootDriverParametersKey:
        Format(path, ARRAYSIZE(path), L"%s%s\\Parameters", k_Services,
                  Root->Qualifier.ParametersKey.ServiceName ? Root->Qualifier.ParametersKey.ServiceName : m_Service);
        break;
    case UMINT::WdfPropertyStoreRootDriverPersistentStateKey:
        Format(path, ARRAYSIZE(path), L"%s%s\\State", k_Services,
                  Root->Qualifier.ParametersKey.ServiceName ? Root->Qualifier.ParametersKey.ServiceName : m_Service);
        break;
    case UMINT::WdfPropertyStoreRootClassLegacyHardwareKey:
        Format(path, ARRAYSIZE(path), L"HARDWARE\\DEVICEMAP\\%s",
                  Root->Qualifier.LegacyHardwareKey.LegacyMapName ? Root->Qualifier.LegacyHardwareKey.LegacyMapName
                                                                  : m_Service);
        break;
    default:
        return E_NOTIMPL;
    }
    if (SubkeyPath != NULL && SubkeyPath[0] && lstrcmpW(SubkeyPath, L"\\")) {
        int n = lstrlenW(path);
        if (n + 1 + lstrlenW(SubkeyPath) >= (int)ARRAYSIZE(path)) return E_INVALIDARG;
        path[n] = L'\\';
        lstrcpyW(path + n + 1, SubkeyPath[0] == L'\\' ? SubkeyPath + 1 : SubkeyPath);
    }
    BOOL create = (Flags & (UMINT::WdfPropertyStoreCreateIfMissing | UMINT::WdfPropertyStoreCreateVolatile)) != 0;
    return OpenKey(path, create, (Flags & UMINT::WdfPropertyStoreCreateVolatile) != 0,
                   DesiredAccess ? DesiredAccess : KEY_READ, Key, Disposition);
}

HRESULT CDeviceStack::GetUnifiedPropertyData(UMINT::PWDF_PROPERTY_STORE_ROOT Root, const DEVPROPKEY *PropertyKey,
                                             LCID Lcid, ULONG Flags, ULONG BufferLength, DEVPROPTYPE *PropertyType,
                                             ULONG *ResultLength, PVOID PropertyBuffer)
{
    (void)Lcid;
    (void)Flags;
    if (Root == NULL || Root->RootClass != UMINT::WdfPropertyStoreRootClassHardwareKey) return E_NOTIMPL;
    ULONG len = PropertyBuffer != NULL ? BufferLength : 0;
    DEVPROPTYPE type = 0;
    DWORD err = DeviceProperty(m_Instance, PropertyKey, &type, PropertyBuffer, &len);
    if (ResultLength != NULL) *ResultLength = len;
    if (PropertyType != NULL) *PropertyType = type;
    return err == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(err);
}

HRESULT CDeviceStack::SetUnifiedPropertyData(UMINT::PWDF_PROPERTY_STORE_ROOT Root, const DEVPROPKEY *PropertyKey,
                                             LCID Lcid, ULONG Flags, DEVPROPTYPE PropertyType, ULONG BufferLength,
                                             PVOID PropertyBuffer)
{
    (void)Root;
    (void)PropertyKey;
    (void)Lcid;
    (void)Flags;
    (void)PropertyType;
    (void)BufferLength;
    (void)PropertyBuffer;
    return E_NOTIMPL;
}

void CDeviceStack::GetPdoProperties(PCWSTR *HardwareIds, PCWSTR *SetupClass, PCWSTR *BusEnumerator,
                                    PCWSTR *Manufacturer)
{
    if (HardwareIds != NULL) *HardwareIds = m_HardwareIds;
    if (SetupClass != NULL) *SetupClass = m_Class;
    if (BusEnumerator != NULL) *BusEnumerator = m_Enumerator;
    if (Manufacturer != NULL) *Manufacturer = m_Manufacturer;
}

HRESULT CDeviceStack::PostEvent(REFGUID EventGuid, WDF_EVENT_TYPE EventType, PBYTE Data, ULONG DataSizeCb)
{
    (void)EventGuid;
    (void)EventType;
    (void)Data;
    (void)DataSizeCb;
    return E_NOTIMPL;
}

HRESULT CDeviceStack::UpdateIdleWakeWmiInstance(WmiIdleWakeInstanceUpdate UpdateType)
{
    (void)UpdateType;
    return S_OK;
}

HRESULT CDeviceStack::CreateWdfFile(IWudfDevice *DeviceObject, IWudfDevice *RelatedDevice, PCWSTR FileName,
                                    IWudfFile **File)
{
    (void)DeviceObject;
    (void)RelatedDevice;
    (void)FileName;
    if (File != NULL) *File = NULL;
    return E_NOTIMPL;
}

void CDeviceStack::CloseFile(IWudfFile *File)
{
    if (File != NULL) File->Release();
}

HRESULT CDeviceStack::CreateRemoteDispatcher(IWudfIoDispatcher **IoDispatcher, IWudfRemoteDispatcher **RemoteDispatcher)
{
    if (IoDispatcher != NULL) *IoDispatcher = NULL;
    if (RemoteDispatcher != NULL) *RemoteDispatcher = NULL;
    return E_NOTIMPL;
}

HRESULT CDeviceStack::RegisterTargetDeviceNotification(IWudfTargetCallbackDeviceChange *Callback, HANDLE TargetHandle,
                                                       WUDF_TARGET_CONTEXT *RegistrationId)
{
    (void)Callback;
    (void)TargetHandle;
    if (RegistrationId != NULL) *RegistrationId = WUDF_TARGET_CONTEXT_INVALID;
    return E_NOTIMPL;
}

HRESULT CDeviceStack::MapIoSpace(PHYSICAL_ADDRESS PhysicalAddress, SIZE_T NumberOfBytes, MEMORY_CACHING_TYPE CacheType,
                                 PVOID *PseudoBaseAddress, PVOID *UserModeBaseAddress)
{
    (void)PhysicalAddress;
    (void)NumberOfBytes;
    (void)CacheType;
    if (PseudoBaseAddress != NULL) *PseudoBaseAddress = NULL;
    if (UserModeBaseAddress != NULL) *UserModeBaseAddress = NULL;
    return E_NOTIMPL;
}

void CDeviceStack::UnmapIoSpace(PVOID PseudoBaseAddress, SIZE_T NumberOfBytes)
{
    (void)PseudoBaseAddress;
    (void)NumberOfBytes;
}

void CDeviceStack::ReadFromHardware(UMINT::WDF_DEVICE_HWACCESS_TARGET_TYPE Type,
                                    UMINT::WDF_DEVICE_HWACCESS_TARGET_SIZE Size, PVOID TargetAddress, ULONG_PTR *Value,
                                    PVOID Buffer, ULONG Count)
{
    /* (never reached: MapIoSpace failed, so the driver has no registers) */
    (void)Type;
    (void)Size;
    (void)TargetAddress;
    (void)Buffer;
    (void)Count;
    if (Value != NULL) *Value = 0;
}

void CDeviceStack::WriteToHardware(UMINT::WDF_DEVICE_HWACCESS_TARGET_TYPE Type,
                                   UMINT::WDF_DEVICE_HWACCESS_TARGET_SIZE Size, PVOID TargetAddress, ULONG_PTR Value,
                                   PVOID Buffer, ULONG Count)
{
    (void)Type;
    (void)Size;
    (void)TargetAddress;
    (void)Value;
    (void)Buffer;
    (void)Count;
}

HRESULT CDeviceStack::InitializeInterrupt(WUDF_INTERRUPT_CONTEXT FxInterrupt, HANDLE InterruptEvent,
                                          BOOLEAN ShareVector, RD_INTERRUPT_CONTEXT *RdInterruptContext)
{
    (void)FxInterrupt;
    (void)InterruptEvent;
    (void)ShareVector;
    if (RdInterruptContext != NULL) *RdInterruptContext = NULL;
    return E_NOTIMPL;
}

HRESULT CDeviceStack::AssignInterruptResources(RD_INTERRUPT_CONTEXT RdInterruptContext,
                                               PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescRaw,
                                               PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescTrans,
                                               PWDF_INTERRUPT_INFO InterruptInfo, BOOLEAN PassiveHandlingByRedirector)
{
    (void)RdInterruptContext;
    (void)CmDescRaw;
    (void)CmDescTrans;
    (void)InterruptInfo;
    (void)PassiveHandlingByRedirector;
    return E_NOTIMPL;
}

HRESULT CDeviceStack::ControlInterrupt(RD_INTERRUPT_CONTEXT RdInterruptContext, InterruptControlType ControlType)
{
    (void)RdInterruptContext;
    (void)ControlType;
    return E_NOTIMPL;
}

HRESULT CDeviceStack::SetInterruptPolicy(RD_INTERRUPT_CONTEXT RdInterruptContext, WDF_INTERRUPT_POLICY Policy,
                                         WDF_INTERRUPT_PRIORITY Priority, PGROUP_AFFINITY TargetProcessorSet)
{
    (void)RdInterruptContext;
    (void)Policy;
    (void)Priority;
    (void)TargetProcessorSet;
    return E_NOTIMPL;
}

HRESULT CDeviceStack::AcknowledgeInterrupt(RD_INTERRUPT_CONTEXT RdInterruptContext, BOOLEAN Claimed)
{
    (void)RdInterruptContext;
    (void)Claimed;
    return E_NOTIMPL;
}

HRESULT CDeviceStack::ConnectInterrupt(RD_INTERRUPT_CONTEXT RdInterruptContext, BOOLEAN *IsRdConnectingOrConnected)
{
    (void)RdInterruptContext;
    if (IsRdConnectingOrConnected != NULL) *IsRdConnectingOrConnected = FALSE;
    return E_NOTIMPL;
}

void CDeviceStack::SubmitUsbIdleNotification(PVOID CallbackInfo, PWUDF_IO_COMPLETION_ROUTINE Completion, PVOID Context)
{
    (void)CallbackInfo;
    (void)Completion;
    (void)Context;
}

void CDeviceStack::SetPowerState(POWER_STATE_TYPE Type, DEVICE_POWER_STATE State, ULONG *OldState)
{
    if (OldState != NULL) *OldState = (ULONG)m_PowerState;
    if (Type == DevicePowerState) m_PowerState = State;
}

/* %SystemRoot%\System32\DriverData\UMDF\<Name>, created */
static HRESULT DataDirectory(PCWSTR Name, WCHAR *Out)
{
    WCHAR path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n + 20 + lstrlenW(Name) >= MAX_PATH) return E_FAIL;
    lstrcatW(path, L"\\DriverData");
    CreateDirectoryW(path, NULL);
    lstrcatW(path, L"\\UMDF");
    CreateDirectoryW(path, NULL);
    lstrcatW(path, L"\\");
    WCHAR *p = path + lstrlenW(path);
    lstrcpyW(p, Name);
    for (; *p; p++)
        if (*p == L'\\') *p = L'#';
    if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(GetLastError());
    lstrcpyW(Out, path);
    return S_OK;
}

HRESULT CDeviceStack::GetDeviceDirectory(PCWSTR *Path)
{
    HRESULT hr = m_Directory[0] ? S_OK : DataDirectory(m_Instance, m_Directory);
    *Path = SUCCEEDED(hr) ? m_Directory : NULL;
    return hr;
}

HRESULT CDeviceStack::GetDriverDataDirectory(PCWSTR ServiceName, PCWSTR *Path)
{
    static WCHAR dir[MAX_PATH];
    HRESULT hr = DataDirectory(ServiceName != NULL ? ServiceName : m_Service, dir);
    *Path = SUCCEEDED(hr) ? dir : NULL;
    return hr;
}

HRESULT CDeviceStack::OpenUSBCommunicationChannel(IWudfDevice *Device, IWudfDevice *AttachedDevice, IWudfFile **File)
{
    (void)Device;
    (void)AttachedDevice;
    if (File != NULL) *File = NULL;
    return E_NOTIMPL;
}

NTSTATUS CDeviceStack::AllocateIfrMemory(PCWSTR ServiceName, ULONG PageCount, BOOLEAN IsDriverIfr,
                                         BOOLEAN StoreInMiniDump, PVOID *Buffer, ULONG *BufferCb)
{
    /* (the in-flight recorder's log: plain memory, as there are no dumps) */
    (void)ServiceName;
    (void)IsDriverIfr;
    (void)StoreInMiniDump;
    if (PageCount == 0) PageCount = 1;
    *Buffer = VirtualAlloc(NULL, (SIZE_T)PageCount * 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    *BufferCb = *Buffer != NULL ? PageCount * 4096 : 0;
    return *Buffer != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

HRESULT CDeviceStack::UpdateProcessKeepAliveCount(IWudfFile *File, BOOLEAN Increment)
{
    (void)File;
    (void)Increment;
    return S_OK;
}

HRESULT CDeviceStack::PoFxRegisterDevice(ULONG DeviceFlags, BOOLEAN DirectedTransitionsEnabled)
{
    (void)DeviceFlags;
    (void)DirectedTransitionsEnabled;
    return S_OK;
}
