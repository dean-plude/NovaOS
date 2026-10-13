/*
 * main.cpp — WUDFHost.exe, NovaOS's UMDF 2 driver host
 *
 *   WUDFHost.exe <device instance ID>
 *
 * The UMDF service (wudfsvc) starts one host per device whose function
 * driver is the reflector, WUDFRd.sys, once the device has started.  The
 * host opens the device's host channel (tools/umdf/wudfrd.h), loads the
 * framework (Wudfx02000.dll) and the device's UMDF driver (the first
 * service of the device's UmdfServiceOrder, its DLL the service's
 * ImagePath), binds the two, calls the driver's DriverEntry and its
 * AddDevice, and then turns the reflector's messages into requests on the
 * device's stack: Plug and Play (start, removal) and the I/O of the
 * programs that open the device.  When the device is removed the
 * reflector says so and the host exits.
 *
 * Log lines go to the debugger (OutputDebugString: the kernel log on
 * NovaOS), starting "WUDFHost: ".
 */
#include "host.h"
#include <stdarg.h>
#include <stdio.h>

void Log(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    lstrcpyA(line, "WUDFHost: ");
    int n = lstrlenA(line);
    int m = _vsnprintf(line + n, sizeof(line) - 2 - n, fmt, ap);
    va_end(ap);
    n = m < 0 ? (int)sizeof(line) - 2 : n + m;
    line[n] = '\n';
    line[n + 1] = 0;
    OutputDebugStringA(line);
}

void Format(WCHAR *Out, size_t Chars, PCWSTR Fmt, ...)
{
    va_list ap;
    va_start(ap, Fmt);
    int n = _vsnwprintf(Out, Chars - 1, Fmt, ap);
    va_end(ap);
    Out[n < 0 ? Chars - 1 : n] = 0;
}

/* ---- the channel ---- */

static WCHAR g_ChannelPath[400];
static HANDLE g_Channel;                        /* HELLO, REPLY and IFACE (none of them waits) */

BOOL ChannelControl(DWORD code, const void *in, DWORD in_len, void *out, DWORD out_len, DWORD *got)
{
    DWORD dummy;
    return DeviceIoControl(g_Channel, code, (void *)in, in_len, out, out_len, got != NULL ? got : &dummy, NULL);
}

static HANDLE OpenChannel(void)
{
    return CreateFileW(g_ChannelPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, 0, NULL);
}

/* ---- the host and platform objects the framework holds ---- */

class CHost : public IWudfHost2 {
public:
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IWudfHost || riid == IID_IWudfHost2) {
            *ppv = static_cast<IWudfHost2 *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef(void) override { return 2; }
    ULONG __stdcall Release(void) override { return 1; }
    IWudfIrp *__stdcall GetIrpFromListEntry(PLIST_ENTRY ListEntry) override
    {
        return CONTAINING_RECORD(ListEntry, CIrpCore, m_ListEntry)->m_Self;
    }
    BOOL __stdcall IsCurrentThreadImpersonated(void) override { return FALSE; }
};

static DWORD g_ActivationSlot;

class CPlatform : public IUMDFPlatform, public IUMDFPlatformModule {
public:
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IUMDFPlatform) {
            *ppv = static_cast<IUMDFPlatform *>(this);
            return S_OK;
        }
        if (riid == IID_IUMDFPlatformModule) {
            *ppv = static_cast<IUMDFPlatformModule *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef(void) override { return 2; }
    ULONG __stdcall Release(void) override { return 1; }
    /* the calling thread's list of framework calls in progress */
    PWDF_ACTIVATION_FRAME *__stdcall GetActivationListHead(void) override
    {
        PWDF_ACTIVATION_FRAME *head = (PWDF_ACTIVATION_FRAME *)TlsGetValue(g_ActivationSlot);
        if (head == NULL) {
            head = (PWDF_ACTIVATION_FRAME *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*head));
            TlsSetValue(g_ActivationSlot, head);
        }
        return head;
    }
};

static CHost g_Host;
static CPlatform g_Platform;

/* ---- binding the driver to the framework ---- */

static PWDF_LIBRARY_INFO g_Library;

static NTSTATUS RegisterLibrary(PVOID Context, PWDF_LIBRARY_INFO LibraryInfo)
{
    (void)Context;
    g_Library = LibraryInfo;
    return STATUS_SUCCESS;
}

static NTSTATUS VersionBind(PVOID Context, PCUNICODE_STRING RegistryPath, PVOID BindInfo, PVOID *ComponentGlobals)
{
    (void)Context;
    CLIENT_INFO client;
    client.Size = sizeof(client);
    client.RegistryPath = (PUNICODE_STRING)RegistryPath;
    PVOID context = &client;
    return g_Library->LibraryRegisterClient((PWDF_BIND_INFO)BindInfo, (PWDF_COMPONENT_GLOBALS *)ComponentGlobals,
                                            &context);
}

static NTSTATUS VersionUnbind(PVOID Context, PCUNICODE_STRING RegistryPath, PVOID BindInfo, PVOID ComponentGlobals)
{
    (void)Context;
    (void)RegistryPath;
    return g_Library->LibraryUnregisterClient((PWDF_BIND_INFO)BindInfo, (PWDF_COMPONENT_GLOBALS)ComponentGlobals);
}

static NOVA_WUDF_LOADER g_Loader = { sizeof(NOVA_WUDF_LOADER), NOVA_WUDF_LOADER_VERSION, VersionBind, VersionUnbind };

typedef NTSTATUS (*PFN_FX_FRAMEWORK_ENTRY_UM)(PWUDF_LOADER_FX_INTERFACE, PVOID);
typedef NTSTATUS (*PFN_FX_DRIVER_ENTRY_UM)(PVOID, PVOID, PVOID, PUNICODE_STRING);

/* ---- the programs' opens ---- */

static CRITICAL_SECTION g_FilesLock;
struct FileEntry {
    FileEntry *next;
    CFile *file;
};
static FileEntry *g_Files;

static CFile *FindFile(ULONGLONG Id)
{
    CFile *f = NULL;
    EnterCriticalSection(&g_FilesLock);
    for (FileEntry *e = g_Files; e != NULL; e = e->next)
        if (e->file->m_Id == Id) {
            f = e->file;
            f->AddRef();
            break;
        }
    LeaveCriticalSection(&g_FilesLock);
    return f;
}

static void AddFile(CFile *File)
{
    FileEntry *e = (FileEntry *)HeapAlloc(GetProcessHeap(), 0, sizeof(*e));
    if (e == NULL) return;
    File->AddRef();
    e->file = File;
    EnterCriticalSection(&g_FilesLock);
    e->next = g_Files;
    g_Files = e;
    LeaveCriticalSection(&g_FilesLock);
}

static void DropFile(ULONGLONG Id)
{
    FileEntry *gone = NULL;
    EnterCriticalSection(&g_FilesLock);
    for (FileEntry **p = &g_Files; *p != NULL; p = &(*p)->next)
        if ((*p)->file->m_Id == Id) {
            gone = *p;
            *p = gone->next;
            break;
        }
    LeaveCriticalSection(&g_FilesLock);
    if (gone != NULL) {
        gone->file->Release();
        HeapFree(GetProcessHeap(), 0, gone);
    }
}

/* ---- messages ---- */

static volatile LONG g_Removed;

void ReflectorReply(CIrpCore *Irp)
{
    SIZE_T data = 0;
    if ((Irp->m_MessageType == WUDFRD_MSG_READ || Irp->m_MessageType == WUDFRD_MSG_IOCTL) && NT_SUCCESS(Irp->m_Status)) {
        data = Irp->m_Information < Irp->m_OutLength ? Irp->m_Information : Irp->m_OutLength;
        if (Irp->m_Out == NULL) data = 0;
    }
    BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, sizeof(WUDFRD_REPLY) + data);
    if (buf != NULL) {
        WUDFRD_REPLY *r = (WUDFRD_REPLY *)buf;
        r->id = Irp->m_MessageId;
        r->status = Irp->m_Status;
        r->data_len = (unsigned)data;
        r->information = Irp->m_Information;
        if (data) CopyMemory(buf + sizeof(*r), Irp->m_Out, data);
        if (!ChannelControl(IOCTL_WUDFRD_REPLY, buf, (DWORD)(sizeof(*r) + data), NULL, 0, NULL))
            Log("the reply to message %llu failed (%lu)", Irp->m_MessageId, GetLastError());
        HeapFree(GetProcessHeap(), 0, buf);
    }
    CFile *f = Irp->m_FileRef;
    if (f != NULL && (Irp->m_MessageType == WUDFRD_MSG_CLOSE ||
                      (Irp->m_MessageType == WUDFRD_MSG_CREATE && !NT_SUCCESS(Irp->m_Status))))
        DropFile(f->m_Id);
    if (Irp->m_MessageType == WUDFRD_MSG_REMOVE) InterlockedExchange(&g_Removed, 1);
}

static const char *const k_MessageNames[] = {
    "?", "start", "query-remove", "cancel-remove", "remove", "surprise-removal",
    "create", "cleanup", "close", "read", "write", "device control",
};

/* Turn one message into a request and send it down the stack */
static void Dispatch(const WUDFRD_MSG *Msg, const BYTE *Data)
{
    static const UCHAR pnp[] = { 0, IRP_MN_START_DEVICE, IRP_MN_QUERY_REMOVE_DEVICE, IRP_MN_CANCEL_REMOVE_DEVICE,
                                 IRP_MN_REMOVE_DEVICE, IRP_MN_SURPRISE_REMOVAL };
    if (Msg->type >= WUDFRD_MSG_START && Msg->type <= WUDFRD_MSG_SURPRISE_REMOVAL) {
        Log("%s", k_MessageNames[Msg->type]);
        CPnpIrp *irp = new CPnpIrp(IrpFromReflector, IRP_MJ_PNP, pnp[Msg->type]);
        irp->m_MessageId = Msg->id;
        irp->m_MessageType = Msg->type;
        if (Msg->type == WUDFRD_MSG_START && Msg->data_len >= Msg->raw_len && Msg->raw_len) {
            SIZE_T xl = Msg->data_len - Msg->raw_len;
            irp->m_Raw = (PCM_RESOURCE_LIST)HeapAlloc(GetProcessHeap(), 0, Msg->raw_len);
            irp->m_Translated = (PCM_RESOURCE_LIST)HeapAlloc(GetProcessHeap(), 0, xl ? xl : 1);
            if (irp->m_Raw != NULL) CopyMemory(irp->m_Raw, Data, Msg->raw_len);
            if (irp->m_Translated != NULL && xl) CopyMemory(irp->m_Translated, Data + Msg->raw_len, xl);
        }
        irp->Start(g_Stack->Top());
        return;
    }

    CFile *file;
    if (Msg->type == WUDFRD_MSG_CREATE) {
        WCHAR name[260];
        DWORD n = Msg->data_len / sizeof(WCHAR);
        if (n >= ARRAYSIZE(name)) n = ARRAYSIZE(name) - 1;
        CopyMemory(name, Data, n * sizeof(WCHAR));
        name[n] = 0;
        file = new CFile(Msg->file, name, Msg->pid);
        AddFile(file);
    } else {
        file = FindFile(Msg->file);
    }
    CIoIrp *irp = new CIoIrp(IrpFromReflector, NULL);
    irp->m_MessageId = Msg->id;
    irp->m_MessageType = Msg->type;
    irp->m_Pid = Msg->pid;
    irp->m_FileRef = file;
    if (file == NULL) {
        /* (an open the host never saw: it started after the program opened) */
        irp->m_Status = STATUS_INVALID_HANDLE;
        ReflectorReply(irp);
        irp->Release();
        return;
    }
    IrpLocation &l = irp->m_Loc[0];
    l.File = file;
    SIZE_T in = 0, out = 0;
    switch (Msg->type) {
    case WUDFRD_MSG_CREATE:
        l.Type = UMINT::WdfRequestCreate;
        l.Major = IRP_MJ_CREATE;
        irp->m_CreateOptions = Msg->options;
        irp->m_ShareAccess = (USHORT)Msg->share;
        irp->m_Security.DesiredAccess = Msg->access;
        break;
    case WUDFRD_MSG_CLEANUP:
        l.Type = UMINT::WdfRequestCleanup;
        l.Major = IRP_MJ_CLEANUP;
        break;
    case WUDFRD_MSG_CLOSE:
        l.Type = UMINT::WdfRequestClose;
        l.Major = IRP_MJ_CLOSE;
        break;
    case WUDFRD_MSG_READ:
        l.Type = UMINT::WdfRequestRead;
        l.Major = IRP_MJ_READ;
        l.Length = Msg->out_len;
        l.Offset = Msg->offset;
        out = Msg->out_len;
        break;
    case WUDFRD_MSG_WRITE:
        l.Type = UMINT::WdfRequestWrite;
        l.Major = IRP_MJ_WRITE;
        l.Length = Msg->in_len;
        l.Offset = Msg->offset;
        in = Msg->data_len;
        break;
    case WUDFRD_MSG_IOCTL:
        l.Type = UMINT::WdfRequestDeviceIoControl;
        l.Major = IRP_MJ_DEVICE_CONTROL;
        l.Code = Msg->code;
        l.InputLength = Msg->in_len;
        l.OutputLength = Msg->out_len;
        in = Msg->data_len;
        out = Msg->out_len;
        break;
    default:
        irp->m_Status = STATUS_INVALID_DEVICE_REQUEST;
        ReflectorReply(irp);
        irp->Release();
        return;
    }
    /* the request's buffers: what it brings, then room for what it returns */
    if (in + out) {
        irp->m_Owned = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, in + out);
        if (irp->m_Owned == NULL) {
            irp->m_Status = STATUS_INSUFFICIENT_RESOURCES;
            ReflectorReply(irp);
            irp->Release();
            return;
        }
        if (in) {
            CopyMemory(irp->m_Owned, Data, in);
            irp->m_In = irp->m_Owned;
            irp->m_InLength = in;
        }
        if (out) {
            irp->m_Out = irp->m_Owned + in;
            irp->m_OutLength = out;
        }
    }
    irp->Start(g_Stack->Top());
}

/* A worker: asks the reflector for work on a handle of its own until the
 * device is gone */
static DWORD WINAPI Worker(LPVOID Param)
{
    (void)Param;
    HANDLE h = OpenChannel();
    if (h == INVALID_HANDLE_VALUE) {
        Log("a worker could not open the channel (%lu)", GetLastError());
        return 1;
    }
    DWORD size = sizeof(WUDFRD_MSG) + 2 * WUDFRD_MAX_DATA;
    BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
    for (;;) {
        DWORD got = 0;
        if (!DeviceIoControl(h, IOCTL_WUDFRD_GET, NULL, 0, buf, size, &got, NULL) || got < sizeof(WUDFRD_MSG)) break;
        WUDFRD_MSG *m = (WUDFRD_MSG *)buf;
        if (got < sizeof(WUDFRD_MSG) + m->data_len) {
            Log("a short message (%lu bytes)", got);
            continue;
        }
        Dispatch(m, buf + sizeof(WUDFRD_MSG));
    }
    HeapFree(GetProcessHeap(), 0, buf);
    CloseHandle(h);
    return 0;
}

/* ---- the process ---- */

/* The instance ID: the command line's first argument */
static BOOL InstanceArgument(WCHAR *Out, int Chars)
{
    const WCHAR *c = GetCommandLineW();
    if (*c == L'"') {
        for (c++; *c && *c != L'"'; c++)
            ;
        if (*c) c++;
    } else {
        while (*c && *c != L' ' && *c != L'\t') c++;
    }
    while (*c == L' ' || *c == L'\t') c++;
    BOOL quoted = *c == L'"';
    if (quoted) c++;
    int n = 0;
    while (*c && n < Chars - 1 && (quoted ? *c != L'"' : (*c != L' ' && *c != L'\t'))) Out[n++] = *c++;
    Out[n] = 0;
    return n > 0;
}

/* A string value under HKLM\@Key */
static BOOL ReadString(PCWSTR Key, PCWSTR Value, WCHAR *Out, DWORD Chars, DWORD *Type)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Key, 0, KEY_READ, &k) != ERROR_SUCCESS) return FALSE;
    DWORD len = (Chars - 2) * sizeof(WCHAR), type = 0;
    ZeroMemory(Out, Chars * sizeof(WCHAR));
    LONG err = RegQueryValueExW(k, Value, NULL, &type, (BYTE *)Out, &len);
    RegCloseKey(k);
    if (Type != NULL) *Type = type;
    return err == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ);
}

static int Fail(const char *what, DWORD code)
{
    Log("%s (0x%08lx)", what, code);
    return 1;
}

int main(void)
{
    WCHAR instance[200];
    if (!InstanceArgument(instance, ARRAYSIZE(instance))) return Fail("usage: WUDFHost.exe <device instance ID>", 0);
    g_ActivationSlot = TlsAlloc();
    InitializeCriticalSection(&g_FilesLock);

    /* the channel: \\?\<instance, '\' as '#'>#{host GUID}\WudfHost */
    {
        WCHAR *p = g_ChannelPath;
        lstrcpyW(p, L"\\\\?\\");
        p += 4;
        for (const WCHAR *c = instance; *c; c++) *p++ = *c == L'\\' ? L'#' : *c;
        static const GUID host = WUDFRD_HOST_GUID_INIT;
        Format(p, ARRAYSIZE(g_ChannelPath) - (p - g_ChannelPath), L"#{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\\%s", host.Data1, host.Data2, host.Data3,
                  host.Data4[0], host.Data4[1], host.Data4[2], host.Data4[3], host.Data4[4], host.Data4[5],
                  host.Data4[6], host.Data4[7], WUDFRD_HOST_REF);
    }
    for (int tries = 0; (g_Channel = OpenChannel()) == INVALID_HANDLE_VALUE; tries++) {
        if (tries == 50) return Fail("the device's host channel did not open", GetLastError());
        Sleep(200);
    }
    WUDFRD_HELLO hello;
    if (!ChannelControl(IOCTL_WUDFRD_HELLO, NULL, 0, &hello, sizeof(hello), NULL))
        return Fail("the reflector refused the host", GetLastError());
    if (hello.version != WUDFRD_VERSION) return Fail("the reflector speaks another protocol version", hello.version);

    /* the driver: the device's first UMDF service, and its DLL */
    WCHAR key[400], service[64], image[MAX_PATH], expanded[MAX_PATH], version[32];
    Format(key, ARRAYSIZE(key), L"SYSTEM\\CurrentControlSet\\Enum\\%s\\Device Parameters", instance);
    if (!ReadString(key, L"UmdfServiceOrder", service, ARRAYSIZE(service), NULL) || !service[0])
        return Fail("the device has no UmdfServiceOrder", 0);
    WCHAR services[300];
    Format(services, ARRAYSIZE(services), L"SYSTEM\\CurrentControlSet\\Services\\WUDF\\Services\\%s", service);
    if (!ReadString(services, L"ImagePath", image, ARRAYSIZE(image), NULL))
        return Fail("the UMDF service has no ImagePath", 0);
    if (!ExpandEnvironmentStringsW(image, expanded, ARRAYSIZE(expanded))) lstrcpyW(expanded, image);
    UMDF_VERSION_DATA want = { 2, 0, 0 };
    if (ReadString(services, L"UmdfLibraryVersion", version, ARRAYSIZE(version), NULL)) {
        ULONG v[3] = { 0, 0, 0 };
        int i = 0;
        for (const WCHAR *c = version; *c && i < 3; c++) {
            if (*c == L'.') i++;
            else if (*c >= L'0' && *c <= L'9') v[i] = v[i] * 10 + (*c - L'0');
        }
        want.MajorNumber = v[0];
        want.MinorNumber = v[1];
        want.ServiceNumber = v[2];
    }
    Log("%ls: driver %ls (%ls), UMDF %lu.%lu", instance, service, expanded, want.MajorNumber, want.MinorNumber);
    if (want.MajorNumber != 2) return Fail("only UMDF 2 drivers run on NovaOS", want.MajorNumber);

    /* the framework */
    HMODULE fx = LoadLibraryW(L"Wudfx02000.dll");
    if (fx == NULL) return Fail("Wudfx02000.dll did not load", GetLastError());
    PFN_FX_FRAMEWORK_ENTRY_UM fxEntry = (PFN_FX_FRAMEWORK_ENTRY_UM)(void *)GetProcAddress(fx, "FxFrameworkEntryUm");
    if (fxEntry == NULL) return Fail("Wudfx02000.dll has no FxFrameworkEntryUm", 0);
    WUDF_LOADER_FX_INTERFACE fxi;
    fxi.Size = sizeof(fxi);
    fxi.RegisterLibrary = RegisterLibrary;
    fxi.pIWudfHost = &g_Host;
    fxi.pUMDFPlatform = &g_Platform;
    NTSTATUS st = fxEntry(&fxi, NULL);
    if (!NT_SUCCESS(st) || g_Library == NULL) return Fail("the framework did not register", st);
    st = g_Library->LibraryCommission();
    if (!NT_SUCCESS(st)) return Fail("the framework did not commission", st);
    if (want.MinorNumber > g_Library->Version.Minor)
        Log("the driver wants UMDF 2.%lu, the framework is 2.%lu", want.MinorNumber, g_Library->Version.Minor);

    /* the driver's DLL, its DriverEntry */
    g_Stack = new CDeviceStack(instance, service, &want);
    HMODULE drv = LoadLibraryW(expanded);
    if (drv == NULL) return Fail("the driver's DLL did not load", GetLastError());
    PFN_FX_DRIVER_ENTRY_UM entry = (PFN_FX_DRIVER_ENTRY_UM)(void *)GetProcAddress(drv, "FxDriverEntryUm");
    if (entry == NULL) return Fail("the driver has no FxDriverEntryUm (not a UMDF 2 driver)", 0);

    static DRIVER_OBJECT_UM driver;
    static WUDF_DRIVER_LOAD_CONTEXT load;
    load.DriverType = WUDF_DRIVER_TYPE_FUNCTION;
    load.DeviceStack = g_Stack;
    load.Companion = NULL;
    driver.Size = sizeof(driver);
    driver.DriverName.Buffer = service;
    driver.DriverName.Length = (USHORT)(lstrlenW(service) * sizeof(WCHAR));
    driver.DriverName.MaximumLength = driver.DriverName.Length + sizeof(WCHAR);
    driver.DriverLoadContext = &load;
    InitializeThreadpoolEnvironment(&driver.ThreadPoolEnv);
    driver.ThreadPoolGroup = CreateThreadpoolCleanupGroup();
    if (driver.ThreadPoolGroup != NULL)
        SetThreadpoolCallbackCleanupGroup(&driver.ThreadPoolEnv, driver.ThreadPoolGroup, NULL);

    UNICODE_STRING registryPath;
    registryPath.Buffer = services;
    registryPath.Length = (USHORT)(lstrlenW(services) * sizeof(WCHAR));
    registryPath.MaximumLength = registryPath.Length + sizeof(WCHAR);
    st = entry(&g_Loader, NULL, &driver, &registryPath);
    if (!NT_SUCCESS(st)) return Fail("the driver's DriverEntry failed", st);
    if (driver.AddDevice == NULL) return Fail("the driver did not create its WDFDRIVER", 0);

    /* the device */
    st = driver.AddDevice(&driver, NULL, &load, L"\\Device\\WUDFRd", g_Stack->m_HardwareKey, service, instance, 0);
    if (!NT_SUCCESS(st)) return Fail("the driver's AddDevice failed", st);
    if (g_Stack->Top() == NULL) return Fail("the driver's AddDevice created no device", 0);
    Log("%ls added", instance);

    /* the requests: two workers (a request a driver keeps waiting does
     * not hold up the next) */
    HANDLE workers[2];
    for (int i = 0; i < 2; i++) {
        workers[i] = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        if (workers[i] == NULL) return Fail("a worker did not start", GetLastError());
    }
    WaitForMultipleObjects(2, workers, TRUE, INFINITE);
    Log("%ls: %s", instance, g_Removed ? "removed, exiting" : "the reflector is gone, exiting");
    /* (the framework's objects go with the process, as on Windows when a
     * host exits after its last device) */
    ExitProcess(0);
}
