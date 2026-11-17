/*
 * d3dkmt.c — gdi32's D3DKMT* thunks, the user-mode door to the WDDM
 * graphics kernel (adapters, devices, allocations, shared resources and
 * sync objects).  On Windows they call win32u's NtGdiDdDDI* system calls;
 * here each passes its D3DKMT_* structure, as Windows lays it out on x64,
 * to the kernel's NtNovaDxgkCtl (kernel/um/um_d3dkmt.c, step 3 of
 * docs/design/intel-gpu.md).  The kernel lists NovaOS's Basic Render
 * adapter, the software adapter Windows always has, and gives its
 * allocations memory, maps them for the CPU, and keeps its fences; a
 * driver's adapter with a GPU MMU also has GPU virtual addresses, paging
 * queues and residency, and one whose driver takes submissions has
 * contexts, D3DKMTSubmitCommand and fences the GPU signals and waits on.
 * Resources and fences made shareable go to other processes by NT handle
 * (D3DKMTShareObjects, D3DKMTOpenResourceFromNtHandle,
 * D3DKMTOpenSyncObjectFromNtHandle2).
 *
 * In a 32-bit program each structure is rebuilt here in its x64 layout
 * (pointers zero-extended, handles sign-extended, the arrays of structures
 * that hold pointers rebuilt too), as WoW64 does for Windows' own system
 * calls (ntdll_wow.c does it for NovaOS's), and what the kernel wrote is
 * copied back.  Those laid out alike on both (all their fields 4 or 8
 * bytes, the pointers' upper halves in the x86 padding) go as they are.
 *
 * The calls not implemented yet answer STATUS_NOT_SUPPORTED, as Windows
 * does where the kernel graphics subsystem is absent.  VKD3D-Proton 3.0
 * imports them (libs/vkd3d/d3dkmt.c: it opens the adapter by LUID to share
 * resources and fences across APIs and processes, and carries on without
 * when that fails).
 */
#define NOVA_BUILD_GDI32
#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);

#define STATUS_NOT_SUPPORTED ((LONG)0xC00000BB)
#define ST_INVALID ((LONG)0xC000000D)

#define KMT(name) GDIAPI LONG name(void *args) { (void)args; return STATUS_NOT_SUPPORTED; }

KMT(D3DKMTOpenAdapterFromHdc)
KMT(D3DKMTOpenAdapterFromGdiDisplayName)
KMT(D3DKMTDestroyKeyedMutex)
KMT(D3DKMTEscape)
KMT(D3DKMTOpenResource2)
KMT(D3DKMTOpenSyncObjectFromNtHandle)
KMT(D3DKMTQueryResourceInfo)

/* NtNovaDxgkCtl's operations, in this order */
enum {
    OP_ENUM_ADAPTERS2, OP_OPEN_ADAPTER_FROM_LUID, OP_CLOSE_ADAPTER, OP_QUERY_ADAPTER_INFO,
    OP_CREATE_DEVICE, OP_DESTROY_DEVICE, OP_CREATE_ALLOCATION2, OP_DESTROY_ALLOCATION2,
    OP_LOCK2, OP_UNLOCK2, OP_CREATE_SYNC2, OP_DESTROY_SYNC, OP_SIGNAL_FROM_CPU, OP_WAIT_FROM_CPU,
    OP_QUERY_VIDEO_MEMORY_INFO, OP_CREATE_PAGING_QUEUE, OP_DESTROY_PAGING_QUEUE, OP_RESERVE_GPU_VA,
    OP_MAP_GPU_VA, OP_FREE_GPU_VA, OP_MAKE_RESIDENT, OP_EVICT, OP_CREATE_CONTEXT_VIRTUAL, OP_DESTROY_CONTEXT,
    OP_SUBMIT_COMMAND, OP_SIGNAL_FROM_GPU2, OP_WAIT_FROM_GPU, OP_SHARE_OBJECTS, OP_QUERY_RESOURCE_INFO_FROM_NT,
    OP_OPEN_RESOURCE_FROM_NT, OP_OPEN_SYNC_FROM_NT2,
};

static LONG kmt(ULONG op, void *args) { return args ? NtNovaDxgkCtl(op, args) : ST_INVALID; }

/* D3DKMTShareObjects' arguments, packed for the kernel (all 64-bit) */
typedef struct { UINT count, access; UINT64 handles, oa, out; } SHARE64;

#ifdef _WIN64
#define CALL(op, args) return kmt(op, (void *)(args))
#else
/* -----------------------------------------------------------------------
 * 32-bit programs: the x64 layouts (um_d3dkmt.c's), and each structure's
 * trip there and back
 * ----------------------------------------------------------------------- */
typedef UINT64 U64;
#define P(p) ((U64)(ULONG_PTR)(p))                      /* pointers: zero-extended */
#define H(h) ((U64)(LONGLONG)(LONG_PTR)(h))             /* handles: sign-extended */
#define PTR(v) ((void *)(ULONG_PTR)(v))

typedef struct { UINT n, pad; U64 adapters; } ENUM64;
typedef struct { UINT h, type; U64 data; UINT size, pad; } QAI64;
typedef struct { UINT adapter, pad0, flags, device; U64 cmd; UINT cmd_size, pad1; U64 alist; UINT alist_size, pad2;
                 U64 plist; UINT plist_size, pad3; } CDEV64;
typedef struct { UINT alloc, pad; U64 section, priv; UINT priv_size, source, flags, pad2; U64 gpu_va, priority, reserved[5]; } AI64;
typedef struct { UINT device, resource, global_share, pad; U64 rt; UINT rt_size, pad2; U64 priv; UINT priv_size, num;
                 U64 info; UINT flags, pad3; U64 rt_handle; } CA64;
typedef struct { UINT device, resource; U64 list; UINT count, flags; } DA64;
typedef struct { UINT device, alloc, flags, pad; U64 data; } LOCK64;
typedef struct { UINT device, count; U64 handles, values; UINT flags, pad; } SIG64;
typedef struct { UINT device, count; U64 handles, values, event; UINT flags, pad; } WAIT64;
typedef struct { U64 process; UINT adapter, group; U64 budget, usage, reservation, available; UINT index, pad; } VMI64;
typedef struct { UINT device; INT priority; UINT queue, sync; U64 cpu_va; UINT index, pad; } PQ64;
typedef struct { UINT queue, num; U64 list, priorities; UINT flags, pad; U64 fence, trim; } MR64;
typedef struct { UINT device, num; U64 list; UINT flags, pad; U64 trim; } EV64;
typedef struct { UINT device, node, affinity, flags; U64 priv; UINT priv_size, hint, context, pad; } CCV64;
typedef struct { U64 commands; UINT length, flags; U64 history; UINT nbroadcast, broadcast[64]; U64 priv;
                 UINT priv_size, nprimaries, primaries[16], nhistory, pad; U64 history_array; } SUB64;
typedef struct { UINT count, pad; U64 handles; UINT flags, ncontexts; U64 contexts; U64 u[8]; } SGPU64;
typedef struct { UINT context, count; U64 handles; U64 u[8]; } WGPU64;
typedef struct { UINT alloc, pad; U64 priv; UINT priv_size, pad2; U64 gpu_va, reserved[6]; } OAI64;
typedef struct { UINT device, pad; U64 nt; UINT num, pad2; U64 info; UINT rt_size, pad3; U64 rt; UINT rpriv_size, pad4;
                 U64 rpriv; UINT total_size, pad5; U64 total; UINT resource, keyed_mutex; U64 km_rt; UINT km_rt_size, sync; } ORES64;
typedef struct { UINT device, pad; U64 nt, rt; UINT rt_size, total_size, rpriv_size, num; } QRI64;
typedef struct { U64 nt; UINT device, flags, sync, pad; U64 cpu_va, gpu_va; UINT affinity, pad2; U64 rest[5]; } OSYNC64;
typedef struct { ULONG Length, pad; U64 RootDirectory, ObjectName; ULONG Attributes, pad2; U64 Sd, Sqos; } OA64;

#define MAX_LIST 64                     /* (the kernel's: allocations one call may name) */

#define CALL(op, args) return wow_##op((void *)(args))

/* Laid out alike on x86 and x64: OPENADAPTERFROMLUID, CLOSEADAPTER,
 * DESTROYDEVICE, UNLOCK2, DESTROYSYNCHRONIZATIONOBJECT, DESTROYPAGINGQUEUE,
 * DESTROYCONTEXT (handles and LUIDs only), RESERVE-, MAP- and
 * FREEGPUVIRTUALADDRESS (8-byte fields), CREATESYNCHRONIZATIONOBJECT2 (its
 * one pointer, the monitored fence's CPU address, at 24 with padding after
 * it on x86: the kernel's 64-bit write fits) */
#define OFF(t, m) __builtin_offsetof(t, m)
_Static_assert(sizeof(D3DKMT_OPENADAPTERFROMLUID) == 12 && sizeof(D3DKMT_ADAPTERINFO) == 20, "alike on x86");
_Static_assert(sizeof(D3DDDI_MAPGPUVIRTUALADDRESS) == 104 && OFF(D3DDDI_MAPGPUVIRTUALADDRESS, VirtualAddress) == 88 &&
               sizeof(D3DDDI_RESERVEGPUVIRTUALADDRESS) == 72 && OFF(D3DDDI_RESERVEGPUVIRTUALADDRESS, VirtualAddress) == 56 &&
               sizeof(D3DKMT_FREEGPUVIRTUALADDRESS) == 24, "GPU virtual address structures: alike on x86");
_Static_assert(sizeof(D3DKMT_CREATESYNCHRONIZATIONOBJECT2) == 96 &&
               OFF(D3DKMT_CREATESYNCHRONIZATIONOBJECT2, Info.MonitoredFence.FenceValueCPUVirtualAddress) == 24 &&
               OFF(D3DKMT_CREATESYNCHRONIZATIONOBJECT2, Info.MonitoredFence.FenceValueGPUVirtualAddress) == 32 &&
               OFF(D3DKMT_CREATESYNCHRONIZATIONOBJECT2, hSyncObject) == 88, "D3DKMT_CREATESYNCHRONIZATIONOBJECT2: alike on x86");
_Static_assert(sizeof(D3DKMT_OPENRESOURCEFROMNTHANDLE) == 60 && sizeof(D3DDDI_OPENALLOCATIONINFO2) == 48 &&
               sizeof(D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2) == 80 && sizeof(D3DKMT_SUBMITCOMMAND) == 368 &&
               sizeof(D3DDDI_ALLOCATIONINFO2) == 56 && sizeof(D3DKMT_CREATEALLOCATION) == 44,
               "the x86 layouts translated");
_Static_assert(sizeof(ORES64) == 104 && sizeof(OAI64) == 80 && sizeof(OSYNC64) == 88 && sizeof(SUB64) == 384 &&
               sizeof(SGPU64) == 96 && sizeof(WGPU64) == 80 && sizeof(CA64) == 72 && sizeof(AI64) == 96 &&
               sizeof(CDEV64) == 64 && sizeof(VMI64) == 56 && sizeof(MR64) == 48 && sizeof(CCV64) == 40 &&
               sizeof(QRI64) == 40 && sizeof(PQ64) == 32, "the x64 layouts (um_d3dkmt.c's)");

#define SAME(op) static LONG wow_##op(void *a) { return kmt(op, a); }
SAME(OP_OPEN_ADAPTER_FROM_LUID) SAME(OP_CLOSE_ADAPTER) SAME(OP_DESTROY_DEVICE) SAME(OP_UNLOCK2) SAME(OP_DESTROY_SYNC)
SAME(OP_DESTROY_PAGING_QUEUE) SAME(OP_DESTROY_CONTEXT) SAME(OP_RESERVE_GPU_VA) SAME(OP_MAP_GPU_VA) SAME(OP_FREE_GPU_VA)
SAME(OP_CREATE_SYNC2)

static LONG wow_OP_ENUM_ADAPTERS2(D3DKMT_ENUMADAPTERS2 *a)
{
    if (!a) return ST_INVALID;
    ENUM64 k = { a->NumAdapters, 0, P(a->pAdapters) };      /* (D3DKMT_ADAPTERINFO: alike) */
    LONG st = kmt(OP_ENUM_ADAPTERS2, &k);
    a->NumAdapters = k.n;
    return st;
}

static LONG wow_OP_QUERY_ADAPTER_INFO(const D3DKMT_QUERYADAPTERINFO *a)
{
    if (!a) return ST_INVALID;
    QAI64 k = { a->hAdapter, (UINT)a->Type, P(a->pPrivateDriverData), a->PrivateDriverDataSize, 0 };
    return kmt(OP_QUERY_ADAPTER_INFO, &k);
}

static LONG wow_OP_CREATE_DEVICE(D3DKMT_CREATEDEVICE *a)
{
    if (!a) return ST_INVALID;
    CDEV64 k = { a->hAdapter, 0, a->Flags, 0, P(a->pCommandBuffer), a->CommandBufferSize, 0, P(a->pAllocationList),
                 a->AllocationListSize, 0, P(a->pPatchLocationList), a->PatchLocationListSize, 0 };
    LONG st = kmt(OP_CREATE_DEVICE, &k);
    if (!st) a->hDevice = k.device;
    return st;
}

static LONG wow_OP_CREATE_ALLOCATION2(D3DKMT_CREATEALLOCATION *a)
{
    if (!a || !a->NumAllocations || a->NumAllocations > MAX_LIST || !a->pAllocationInfo2) return ST_INVALID;
    AI64 info[MAX_LIST];
    D3DDDI_ALLOCATIONINFO2 *ai = a->pAllocationInfo2;
    memset(info, 0, sizeof(info[0]) * a->NumAllocations);
    for (UINT i = 0; i < a->NumAllocations; i++) {
        info[i].section = H(ai[i].hSection);
        info[i].priv = P(ai[i].pPrivateDriverData);
        info[i].priv_size = ai[i].PrivateDriverDataSize;
        info[i].source = ai[i].VidPnSourceId;
        info[i].flags = ai[i].Flags;
        info[i].priority = ai[i].Priority;
    }
    CA64 k = { a->hDevice, a->hResource, a->hGlobalShare, 0, P(a->pPrivateRuntimeData), a->PrivateRuntimeDataSize, 0,
               P(a->pPrivateDriverData), a->PrivateDriverDataSize, a->NumAllocations, P(info), a->Flags, 0,
               H(a->hPrivateRuntimeResourceHandle) };
    LONG st = kmt(OP_CREATE_ALLOCATION2, &k);
    if (st) return st;
    for (UINT i = 0; i < a->NumAllocations; i++) {
        ai[i].hAllocation = info[i].alloc;
        ai[i].GpuVirtualAddress = info[i].gpu_va;
    }
    a->hResource = k.resource;
    a->hGlobalShare = k.global_share;
    return st;
}

static LONG wow_OP_DESTROY_ALLOCATION2(const D3DKMT_DESTROYALLOCATION2 *a)
{
    if (!a) return ST_INVALID;
    DA64 k = { a->hDevice, a->hResource, P(a->phAllocationList), a->AllocationCount, a->Flags };
    return kmt(OP_DESTROY_ALLOCATION2, &k);
}

static LONG wow_OP_LOCK2(D3DKMT_LOCK2 *a)
{
    if (!a) return ST_INVALID;
    LOCK64 k = { a->hDevice, a->hAllocation, a->Flags, 0, 0 };
    LONG st = kmt(OP_LOCK2, &k);
    if (!st) a->pData = PTR(k.data);
    return st;
}

static LONG wow_OP_SIGNAL_FROM_CPU(const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *a)
{
    if (!a) return ST_INVALID;
    SIG64 k = { a->hDevice, a->ObjectCount, P(a->ObjectHandleArray), P(a->FenceValueArray), a->Flags, 0 };
    return kmt(OP_SIGNAL_FROM_CPU, &k);
}

static LONG wow_OP_WAIT_FROM_CPU(const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *a)
{
    if (!a) return ST_INVALID;
    WAIT64 k = { a->hDevice, a->ObjectCount, P(a->ObjectHandleArray), P(a->FenceValueArray),
                 a->hAsyncEvent ? H(a->hAsyncEvent) : 0, a->Flags, 0 };
    return kmt(OP_WAIT_FROM_CPU, &k);
}

static LONG wow_OP_QUERY_VIDEO_MEMORY_INFO(D3DKMT_QUERYVIDEOMEMORYINFO *a)
{
    if (!a) return ST_INVALID;
    VMI64 k = { a->hProcess ? H(a->hProcess) : 0, a->hAdapter, (UINT)a->MemorySegmentGroup, 0, 0, 0, 0,
                a->PhysicalAdapterIndex, 0 };
    LONG st = kmt(OP_QUERY_VIDEO_MEMORY_INFO, &k);
    if (st) return st;
    a->Budget = k.budget;
    a->CurrentUsage = k.usage;
    a->CurrentReservation = k.reservation;
    a->AvailableForReservation = k.available;
    return st;
}

static LONG wow_OP_CREATE_PAGING_QUEUE(D3DKMT_CREATEPAGINGQUEUE *a)
{
    if (!a) return ST_INVALID;
    PQ64 k = { a->hDevice, (INT)a->Priority, 0, 0, 0, a->PhysicalAdapterIndex, 0 };
    LONG st = kmt(OP_CREATE_PAGING_QUEUE, &k);
    if (st) return st;
    a->hPagingQueue = k.queue;
    a->hSyncObject = k.sync;
    a->FenceValueCPUVirtualAddress = PTR(k.cpu_va);
    return st;
}

static LONG wow_OP_MAKE_RESIDENT(D3DDDI_MAKERESIDENT *a)
{
    if (!a) return ST_INVALID;
    MR64 k = { a->hPagingQueue, a->NumAllocations, P(a->AllocationList), P(a->PriorityList), a->Flags, 0, 0, 0 };
    LONG st = kmt(OP_MAKE_RESIDENT, &k);
    if (st) return st;
    a->PagingFenceValue = k.fence;
    a->NumBytesToTrim = k.trim;
    return st;
}

static LONG wow_OP_EVICT(D3DKMT_EVICT *a)
{
    if (!a) return ST_INVALID;
    EV64 k = { a->hDevice, a->NumAllocations, P(a->AllocationList), a->Flags, 0, 0 };
    LONG st = kmt(OP_EVICT, &k);
    if (!st) a->NumBytesToTrim = k.trim;
    return st;
}

static LONG wow_OP_CREATE_CONTEXT_VIRTUAL(D3DKMT_CREATECONTEXTVIRTUAL *a)
{
    if (!a) return ST_INVALID;
    CCV64 k = { a->hDevice, a->NodeOrdinal, a->EngineAffinity, a->Flags.Value, P(a->pPrivateDriverData),
                a->PrivateDriverDataSize, (UINT)a->ClientHint, 0, 0 };
    LONG st = kmt(OP_CREATE_CONTEXT_VIRTUAL, &k);
    if (!st) a->hContext = k.context;
    return st;
}

static LONG wow_OP_SUBMIT_COMMAND(const D3DKMT_SUBMITCOMMAND *a)
{
    if (!a) return ST_INVALID;
    SUB64 k;
    memset(&k, 0, sizeof(k));
    k.commands = a->Commands;
    k.length = a->CommandLength;
    memcpy(&k.flags, &a->Flags, 4);
    k.history = a->PresentHistoryToken;
    k.nbroadcast = a->BroadcastContextCount;
    memcpy(k.broadcast, a->BroadcastContext, sizeof(k.broadcast));
    k.priv = P(a->pPrivateDriverData);
    k.priv_size = a->PrivateDriverDataSize;
    k.nprimaries = a->NumPrimaries;
    memcpy(k.primaries, a->WrittenPrimaries, sizeof(k.primaries));
    k.nhistory = a->NumHistoryBuffers;
    k.history_array = P(a->HistoryBufferArray);
    return kmt(OP_SUBMIT_COMMAND, &k);
}

/* (their unions: a value, or a pointer to the values, which the kernel
 * reads by the fences' kind: each goes as its 8 bytes, a pointer's upper
 * half the zeros a caller's memset left) */
static LONG wow_OP_SIGNAL_FROM_GPU2(const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 *a)
{
    if (!a) return ST_INVALID;
    SGPU64 k = { a->ObjectCount, 0, P(a->ObjectHandleArray), a->Flags.Value, a->BroadcastContextCount,
                 P(a->BroadcastContextArray), { 0 } };
    memcpy(k.u, a->Reserved, sizeof(k.u));
    return kmt(OP_SIGNAL_FROM_GPU2, &k);
}

static LONG wow_OP_WAIT_FROM_GPU(const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU *a)
{
    if (!a) return ST_INVALID;
    WGPU64 k = { a->hContext, a->ObjectCount, P(a->ObjectHandleArray), { 0 } };
    memcpy(k.u, a->Reserved, sizeof(k.u));
    return kmt(OP_WAIT_FROM_GPU, &k);
}

static LONG wow_OP_QUERY_RESOURCE_INFO_FROM_NT(D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE *a)
{
    if (!a) return ST_INVALID;
    QRI64 k = { a->hDevice, 0, H(a->hNtHandle), P(a->pPrivateRuntimeData), a->PrivateRuntimeDataSize, 0, 0, 0 };
    LONG st = kmt(OP_QUERY_RESOURCE_INFO_FROM_NT, &k);
    if (st) return st;
    a->PrivateRuntimeDataSize = k.rt_size;
    a->TotalPrivateDriverDataSize = k.total_size;
    a->ResourcePrivateDriverDataSize = k.rpriv_size;
    a->NumAllocations = k.num;
    return st;
}

static LONG wow_OP_OPEN_RESOURCE_FROM_NT(D3DKMT_OPENRESOURCEFROMNTHANDLE *a)
{
    if (!a || a->NumAllocations > MAX_LIST || (a->NumAllocations && !a->pOpenAllocationInfo2)) return ST_INVALID;
    OAI64 info[MAX_LIST];
    memset(info, 0, sizeof(info));
    ORES64 k = { a->hDevice, 0, H(a->hNtHandle), a->NumAllocations, 0, P(info), a->PrivateRuntimeDataSize, 0,
                 P(a->pPrivateRuntimeData), a->ResourcePrivateDriverDataSize, 0, P(a->pResourcePrivateDriverData),
                 a->TotalPrivateDriverDataBufferSize, 0, P(a->pTotalPrivateDriverDataBuffer), 0, 0,
                 P(a->pKeyedMutexPrivateRuntimeData), a->KeyedMutexPrivateRuntimeDataSize, 0 };
    LONG st = kmt(OP_OPEN_RESOURCE_FROM_NT, &k);
    if (st) return st;
    for (UINT i = 0; i < a->NumAllocations; i++) {
        D3DDDI_OPENALLOCATIONINFO2 *o = &a->pOpenAllocationInfo2[i];
        o->hAllocation = info[i].alloc;
        o->pPrivateDriverData = PTR(info[i].priv);
        o->PrivateDriverDataSize = info[i].priv_size;
        o->GpuVirtualAddress = info[i].gpu_va;
    }
    a->hResource = k.resource;
    a->hKeyedMutex = k.keyed_mutex;
    a->hSyncObject = k.sync;
    return st;
}

static LONG wow_OP_OPEN_SYNC_FROM_NT2(D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 *a)
{
    if (!a) return ST_INVALID;
    OSYNC64 k;
    memset(&k, 0, sizeof(k));
    k.nt = H(a->hNtHandle);
    k.device = a->hDevice;
    k.flags = a->Flags;
    k.affinity = a->MonitoredFence.EngineAffinity;
    LONG st = kmt(OP_OPEN_SYNC_FROM_NT2, &k);
    if (st) return st;
    a->hSyncObject = k.sync;
    a->MonitoredFence.FenceValueCPUVirtualAddress = PTR(k.cpu_va);
    a->MonitoredFence.FenceValueGPUVirtualAddress = k.gpu_va;
    return st;
}
#endif

GDIAPI LONG D3DKMTEnumAdapters2(D3DKMT_ENUMADAPTERS2 *a)                 { CALL(OP_ENUM_ADAPTERS2, a); }
GDIAPI LONG D3DKMTOpenAdapterFromLuid(D3DKMT_OPENADAPTERFROMLUID *a)     { CALL(OP_OPEN_ADAPTER_FROM_LUID, a); }
GDIAPI LONG D3DKMTCloseAdapter(const D3DKMT_CLOSEADAPTER *a)             { CALL(OP_CLOSE_ADAPTER, a); }
GDIAPI LONG D3DKMTQueryAdapterInfo(const D3DKMT_QUERYADAPTERINFO *a)     { CALL(OP_QUERY_ADAPTER_INFO, a); }
GDIAPI LONG D3DKMTCreateDevice(D3DKMT_CREATEDEVICE *a)                   { CALL(OP_CREATE_DEVICE, a); }
GDIAPI LONG D3DKMTDestroyDevice(const D3DKMT_DESTROYDEVICE *a)           { CALL(OP_DESTROY_DEVICE, a); }
GDIAPI LONG D3DKMTCreateAllocation2(D3DKMT_CREATEALLOCATION *a)          { CALL(OP_CREATE_ALLOCATION2, a); }
GDIAPI LONG D3DKMTDestroyAllocation2(const D3DKMT_DESTROYALLOCATION2 *a) { CALL(OP_DESTROY_ALLOCATION2, a); }
GDIAPI LONG D3DKMTLock2(D3DKMT_LOCK2 *a)                                 { CALL(OP_LOCK2, a); }
GDIAPI LONG D3DKMTUnlock2(const D3DKMT_UNLOCK2 *a)                       { CALL(OP_UNLOCK2, a); }
GDIAPI LONG D3DKMTDestroySynchronizationObject(const D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *a) { CALL(OP_DESTROY_SYNC, a); }
GDIAPI LONG D3DKMTQueryVideoMemoryInfo(D3DKMT_QUERYVIDEOMEMORYINFO *a)   { CALL(OP_QUERY_VIDEO_MEMORY_INFO, a); }
GDIAPI LONG D3DKMTCreatePagingQueue(D3DKMT_CREATEPAGINGQUEUE *a)         { CALL(OP_CREATE_PAGING_QUEUE, a); }
GDIAPI LONG D3DKMTDestroyPagingQueue(D3DDDI_DESTROYPAGINGQUEUE *a)       { CALL(OP_DESTROY_PAGING_QUEUE, a); }
GDIAPI LONG D3DKMTReserveGpuVirtualAddress(D3DDDI_RESERVEGPUVIRTUALADDRESS *a) { CALL(OP_RESERVE_GPU_VA, a); }
GDIAPI LONG D3DKMTMapGpuVirtualAddress(D3DDDI_MAPGPUVIRTUALADDRESS *a)   { CALL(OP_MAP_GPU_VA, a); }
GDIAPI LONG D3DKMTFreeGpuVirtualAddress(const D3DKMT_FREEGPUVIRTUALADDRESS *a) { CALL(OP_FREE_GPU_VA, a); }
GDIAPI LONG D3DKMTMakeResident(D3DDDI_MAKERESIDENT *a)                   { CALL(OP_MAKE_RESIDENT, a); }
GDIAPI LONG D3DKMTEvict(D3DKMT_EVICT *a)                                 { CALL(OP_EVICT, a); }
GDIAPI LONG D3DKMTCreateContextVirtual(D3DKMT_CREATECONTEXTVIRTUAL *a)   { CALL(OP_CREATE_CONTEXT_VIRTUAL, a); }
GDIAPI LONG D3DKMTDestroyContext(const D3DKMT_DESTROYCONTEXT *a)         { CALL(OP_DESTROY_CONTEXT, a); }
GDIAPI LONG D3DKMTSubmitCommand(const D3DKMT_SUBMITCOMMAND *a)           { CALL(OP_SUBMIT_COMMAND, a); }
GDIAPI LONG D3DKMTSignalSynchronizationObjectFromGpu2(const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 *a)
{
    CALL(OP_SIGNAL_FROM_GPU2, a);
}

GDIAPI LONG D3DKMTWaitForSynchronizationObjectFromGpu(const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU *a)
{
    CALL(OP_WAIT_FROM_GPU, a);
}

GDIAPI LONG D3DKMTCreateSynchronizationObject2(D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *a)
{
    CALL(OP_CREATE_SYNC2, a);
}

GDIAPI LONG D3DKMTSignalSynchronizationObjectFromCpu(const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *a)
{
    CALL(OP_SIGNAL_FROM_CPU, a);
}

GDIAPI LONG D3DKMTWaitForSynchronizationObjectFromCpu(const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *a)
{
    CALL(OP_WAIT_FROM_CPU, a);
}

GDIAPI LONG D3DKMTQueryResourceInfoFromNtHandle(D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE *a)
{
    CALL(OP_QUERY_RESOURCE_INFO_FROM_NT, a);
}

GDIAPI LONG D3DKMTOpenResourceFromNtHandle(D3DKMT_OPENRESOURCEFROMNTHANDLE *a) { CALL(OP_OPEN_RESOURCE_FROM_NT, a); }
GDIAPI LONG D3DKMTOpenSyncObjectFromNtHandle2(D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 *a) { CALL(OP_OPEN_SYNC_FROM_NT2, a); }

/* One resource or fence (a resource with its keyed mutex: not yet) to an NT
 * handle; @oa: its attributes (OBJ_INHERIT; a name: not yet) */
GDIAPI LONG D3DKMTShareObjects(UINT count, const D3DKMT_HANDLE *handles, OBJECT_ATTRIBUTES *oa, DWORD access,
                               HANDLE *out)
{
    if (!handles || !out) return ST_INVALID;
#ifdef _WIN64
    SHARE64 k = { count, access, (UINT64)handles, (UINT64)oa, (UINT64)out };
    return kmt(OP_SHARE_OBJECTS, &k);
#else
    OA64 oa64;
    memset(&oa64, 0, sizeof(oa64));
    if (oa) {
        oa64.Length = sizeof(oa64);
        oa64.RootDirectory = H(oa->RootDirectory);
        oa64.ObjectName = P(oa->ObjectName);            /* (refused: no names yet) */
        oa64.Attributes = oa->Attributes;
    }
    U64 h = 0;
    SHARE64 k = { count, access, P(handles), oa ? P(&oa64) : 0, P(&h) };
    LONG st = kmt(OP_SHARE_OBJECTS, &k);
    if (!st) *out = (HANDLE)(ULONG_PTR)h;
    return st;
#endif
}

/* The first version: the same structure without Flags */
typedef struct {
    D3DKMT_HANDLE hDevice, hResource;
    const D3DKMT_HANDLE *phAllocationList;
    UINT AllocationCount;
} D3DKMT_DESTROYALLOCATION;

GDIAPI LONG D3DKMTDestroyAllocation(const D3DKMT_DESTROYALLOCATION *a)
{
    if (!a) return (LONG)0xC000000D;
    D3DKMT_DESTROYALLOCATION2 v2 = { a->hDevice, a->hResource, a->phAllocationList, a->AllocationCount, 0 };
    return D3DKMTDestroyAllocation2(&v2);
}

/* A process's GPU scheduling priority (OBS raises its own to keep encoding
 * while a game loads the GPU): with no GPU scheduler there is nothing to
 * reorder, so the class is kept and reported back and the call succeeds */
static LONG g_sched_class;

GDIAPI LONG D3DKMTSetProcessSchedulingPriorityClass(HANDLE process, LONG cls)
{
    (void)process;
    if (cls < 0 || cls > 5) return (LONG)0xC000000D;    /* STATUS_INVALID_PARAMETER */
    g_sched_class = cls;
    return 0;
}

GDIAPI LONG D3DKMTGetProcessSchedulingPriorityClass(HANDLE process, LONG *cls)
{
    (void)process;
    if (!cls) return (LONG)0xC000000D;
    *cls = g_sched_class;
    return 0;
}
