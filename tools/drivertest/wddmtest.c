/*
 * wddmtest.sys — a WDDM render miniport whose GPU is software, NovaOS's
 * test of step 3 of docs/design/intel-gpu.md
 *
 * Written as a full WDDM driver is, Intel's among them: its DriverEntry
 * hands DxgkInitialize a DRIVER_INITIALIZATION_DATA, and everything else
 * is a DxgkDdi* function dxgkrnl calls (here kernel/nt/dxgkrnl.c).
 * DxgkInitialize is displib.lib's, a static library of the WDK; this file
 * has its own, doing what displib's does: open \Device\DxgKrnl, ask it for
 * its initialisation function with IOCTL_VIDEO_DDI_FUNC_REGISTER, call it.
 * Like Microsoft's Basic Render Driver it sits on a root-enumerated device
 * (hardware ID root\WddmTest, made by "kmttest create") and has no outputs.
 *
 *   StartDevice        no sources, no children: render only
 *   QueryAdapterInfo   WDDM 2.0 caps (DXGKQAITYPE_DRIVERCAPS) and one
 *                      memory segment of 256 MiB populated from system
 *                      memory, as an integrated GPU's is
 *                      (DXGKQAITYPE_QUERYSEGMENT4, asked twice)
 *   CreateDevice       a device context per D3DKMTCreateDevice
 *   CreateAllocation   reads the user-mode driver's private data (the size
 *                      wanted: NOVA_KMT_ALLOCATION's layout) and rounds it
 *                      up to the GPU's 64 KiB pages, in segment 1
 *   OpenAllocation     finds its allocation from dxgkrnl's handle
 *                      (DxgkCbGetHandleData) and gives the device its own
 *                      handle for it; CloseAllocation, DestroyAllocation
 *   its GPU MMU        GpuMmuSupported: two levels of page tables in system
 *                      memory (DXGKQAITYPE_GPUMMUCAPS, PAGETABLELEVELDESC),
 *                      a 30-bit (1 GiB) GPU address space per process, the
 *                      root sized by GetRootPageTableSize; entries are its
 *                      own 8-byte format (the page's physical address, bit
 *                      0 valid, bit 1 read-only), which BuildPagingBuffer's
 *                      UPDATE_PAGE_TABLE writes with the CPU from VidMm's
 *                      DXGK_PTEs; CreateProcess and DestroyProcess keep a
 *                      process object that learns its root table
 *
 * After each mapping it reads the mapping's first word back through its
 * own page tables, as its GPU would: the self-test checks that word.  The
 * next parts of step 3 give its GPU the rest: a command format a kernel
 * thread runs, its interrupt, fences, preemption and TDR.  Its DbgPrint lines and counts
 * are what the wddm self-test (tests/selftest/devices/wddm) checks.  Built
 * by tools/drivertest/mksys.py with its package (wddmtest.inf, a
 * test-signed catalog) into C:\Tests\WddmTest.
 */
#include "../../kernel/nt/dxgk.h"

#define IMPORT __declspec(dllimport) NT_API
IMPORT unsigned int DbgPrint(const char *fmt, ...);
IMPORT NT_STATUS IoGetDeviceObjectPointer(NT_UNICODE_STRING *name, unsigned int access, NT_FILE_OBJECT **file,
                                          NT_DEVICE_OBJECT **dev);
IMPORT NT_IRP   *IoBuildDeviceIoControlRequest(unsigned int code, NT_DEVICE_OBJECT *dev, void *in, unsigned int in_len,
                                               void *out, unsigned int out_len, unsigned char internal, NT_KEVENT *ev,
                                               NT_IO_STATUS_BLOCK *iosb);
IMPORT NT_STATUS IofCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp);
IMPORT void      KeInitializeEvent(NT_KEVENT *ev, int type, unsigned char state);
IMPORT NT_STATUS KeWaitForSingleObject(void *obj, int reason, char mode, unsigned char alertable, long long *timeout);
IMPORT void      ObfDereferenceObject(void *obj);
IMPORT void     *ExAllocatePoolWithTag(int type, unsigned long long size, unsigned int tag);
IMPORT void      ExFreePoolWithTag(void *p, unsigned int tag);
IMPORT void     *MmMapIoSpaceEx(long long pa, unsigned long long size, unsigned int protect);
IMPORT void      MmUnmapIoSpace(void *va, unsigned long long size);

#define NonPagedPoolNx      512
#define TAG_WDT             0x54644457u           /* 'WDdT' */
#define FILE_ALL_ACCESS     0x001F01FFu
#define SEGMENT_SIZE        (256ull << 20)
#define GPU_PAGE            65536ull
#define PRIORITY_NORMAL     0x78000000u           /* D3DDDI_ALLOCATIONPRIORITY_NORMAL */
#define VA_BITS             30                    /* its GPU virtual addresses */
#define PT_BITS             9                     /* each level's index: 512 8-byte entries, a 4 KiB table */
#define PTE_VALID           1ull
#define PTE_READ_ONLY       2ull
#define PAGE_READWRITE      0x04

/* The user-mode side's private data for an allocation (userland's
 * NOVA_KMT_ALLOCATION) */
typedef struct { unsigned long long Size; unsigned int Flags, Reserved; } AllocSpec;

typedef struct {
    NT_DEVICE_OBJECT  *pdo;
    DXGKRNL_INTERFACE  cb;
    int                devices, allocations, processes;   /* (live: counted for the log) */
    unsigned long long bytes;
} Gpu;

typedef struct { Gpu *gpu; unsigned long long *root; } Process;   /* (root: its root table, as the CPU sees it) */

typedef struct { Gpu *gpu; void *dxgk; } Device;
typedef struct { unsigned long long size; } Allocation;
typedef struct { Allocation *alloc; Device *dev; } Opened;   /* an allocation as a device has it */

static void mzero(void *p, unsigned long long n) { for (unsigned char *b = p; n--; ) *b++ = 0; }
static void *alloc(unsigned long long n) { void *p = ExAllocatePoolWithTag(NonPagedPoolNx, n, TAG_WDT); if (p) mzero(p, n); return p; }
static void release(void *p) { if (p) ExFreePoolWithTag(p, TAG_WDT); }

/* -----------------------------------------------------------------------
 * Plug and Play
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS wt_add_device(NT_DEVICE_OBJECT *pdo, void **ctx)
{
    if (!pdo || !ctx) return NT_STATUS_INVALID_PARAMETER;
    Gpu *g = alloc(sizeof(*g));
    if (!g) return NT_STATUS_NO_MEMORY;
    g->pdo = pdo;
    *ctx = g;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_start_device(void *ctx, DXGK_START_INFO *si, DXGKRNL_INTERFACE *cb, unsigned int *sources,
                                        unsigned int *children)
{
    Gpu *g = ctx;
    if (!g || !si || !cb || !sources || !children) return NT_STATUS_INVALID_PARAMETER;
    g->cb = *cb;
    DXGK_DEVICE_INFO info;
    mzero(&info, sizeof(info));
    if (!NT_SUCCESS_(cb->DxgkCbGetDeviceInformation(cb->DeviceHandle, &info))) return NT_STATUS_UNSUCCESSFUL;
    *sources = 0;
    *children = 0;
    DbgPrint("wddmtest: started, LUID %x, a software GPU in %llu MiB of RAM\n", si->AdapterLuid.LowPart,
             (unsigned long long)info.SystemMemorySize >> 20);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_stop_device(void *ctx)
{
    Gpu *g = ctx;
    DbgPrint("wddmtest: stopped (%d device(s), %d allocation(s) left)\n", g->devices, g->allocations);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_remove_device(void *ctx)
{
    release(ctx);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_child_relations(void *ctx, DXGK_CHILD_DESCRIPTOR *rel, unsigned int size)
{
    (void)ctx; (void)rel; (void)size;
    return NT_STATUS_SUCCESS;                             /* (no outputs) */
}

static NT_API NT_STATUS wt_child_status(void *ctx, DXGK_CHILD_STATUS *st, unsigned char nondestructive)
{
    (void)ctx; (void)st; (void)nondestructive;
    return NT_STATUS_INVALID_PARAMETER;
}

static NT_API void wt_unload(void) { DbgPrint("wddmtest: unloaded\n"); }

/* -----------------------------------------------------------------------
 * The adapter's description
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS wt_query_adapter_info(void *adapter, const DXGKARG_QUERYADAPTERINFO *q)
{
    (void)adapter;
    if (!q || !q->pOutputData) return NT_STATUS_INVALID_PARAMETER;
    switch (q->Type) {
    case DXGKQAITYPE_DRIVERCAPS: {
        if (q->OutputDataSize < sizeof(DXGK_DRIVERCAPS)) return NT_STATUS_BUFFER_TOO_SMALL;
        DXGK_DRIVERCAPS *caps = q->pOutputData;
        mzero(caps, sizeof(*caps));
        caps->HighestAcceptableAddress = ~0ull;
        caps->MaxAllocationListSlotId = 16;
        caps->MemoryManagementCaps.Value = DXGK_VIDMMCAPS_GPUMMU;
        caps->GpuEngineTopology.NbAsymetricProcessingNodes = 1;
        caps->WDDMVersion = DXGKDDI_WDDMv2;
        caps->SupportNonVGA = 1;
        return NT_STATUS_SUCCESS;
    }
    case DXGKQAITYPE_GPUMMUCAPS: {
        if (q->OutputDataSize < sizeof(DXGK_GPUMMUCAPS)) return NT_STATUS_BUFFER_TOO_SMALL;
        DXGK_GPUMMUCAPS *mmu = q->pOutputData;
        mzero(mmu, sizeof(*mmu));
        mmu->Flags = 1;                                   /* ReadOnlyMemorySupported */
        mmu->PageTableUpdateMode = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;   /* (a software GPU: the CPU writes them) */
        mmu->VirtualAddressBitCount = VA_BITS;
        mmu->LeafPageTableSizeFor64KPagesInBytes = 4096;
        mmu->PageTableLevelCount = 2;
        return NT_STATUS_SUCCESS;
    }
    case DXGKQAITYPE_PAGETABLELEVELDESC: {
        if (q->OutputDataSize < sizeof(DXGK_PAGE_TABLE_LEVEL_DESC) || !q->pInputData) return NT_STATUS_INVALID_PARAMETER;
        unsigned int level = *(const unsigned int *)q->pInputData;
        if (level > 1) return NT_STATUS_INVALID_PARAMETER;
        DXGK_PAGE_TABLE_LEVEL_DESC *d = q->pOutputData;
        mzero(d, sizeof(*d));
        d->PageTableIndexBitCount = PT_BITS;              /* (the root's: its size comes from GetRootPageTableSize) */
        d->PageTableSizeInBytes = d->PageTableAlignmentInBytes = 4096;   /* segment 0: system memory */
        return NT_STATUS_SUCCESS;
    }
    case DXGKQAITYPE_QUERYSEGMENT4: {
        if (q->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT4)) return NT_STATUS_BUFFER_TOO_SMALL;
        DXGK_QUERYSEGMENTOUT4 *out = q->pOutputData;
        if (!out->NbSegment) { out->NbSegment = 1; return NT_STATUS_SUCCESS; }   /* (the count first) */
        if (!out->pSegmentDescriptor || out->SegmentDescriptorStride < sizeof(DXGK_SEGMENTDESCRIPTOR4))
            return NT_STATUS_INVALID_PARAMETER;
        DXGK_SEGMENTDESCRIPTOR4 *s = (DXGK_SEGMENTDESCRIPTOR4 *)out->pSegmentDescriptor;
        mzero(s, sizeof(*s));
        s->Flags = DXGK_SEGMENT_CPU_VISIBLE | DXGK_SEGMENT_CACHE_COHERENT | DXGK_SEGMENT_POPULATED_FROM_SYSMEM |
                   DXGK_SEGMENT_USE_64KB_PAGES;
        s->Size = SEGMENT_SIZE;
        s->CommitLimit = SEGMENT_SIZE;
        out->NbSegment = 1;
        out->PagingBufferSegmentId = 0;
        out->PagingBufferSize = 64 * 1024;
        out->PagingBufferPrivateDataSize = 0;
        return NT_STATUS_SUCCESS;
    }
    default:
        return NT_STATUS_NOT_SUPPORTED;
    }
}

/* -----------------------------------------------------------------------
 * Devices and allocations
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS wt_create_device(void *adapter, DXGKARG_CREATEDEVICE *a)
{
    Gpu *g = adapter;
    Device *d = alloc(sizeof(*d));
    if (!d) return NT_STATUS_NO_MEMORY;
    d->gpu = g;
    d->dxgk = a->hDevice;
    a->hDevice = d;
    g->devices++;
    DbgPrint("wddmtest: device created (%d)\n", g->devices);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_destroy_device(void *device)
{
    Device *d = device;
    d->gpu->devices--;
    DbgPrint("wddmtest: device destroyed (%d left)\n", d->gpu->devices);
    release(d);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_create_allocation(void *adapter, DXGKARG_CREATEALLOCATION *a)
{
    Gpu *g = adapter;
    if (a->Flags & 1) return NT_STATUS_NOT_SUPPORTED;     /* (resources: not yet) */
    for (unsigned int i = 0; i < a->NumAllocations; i++) {
        DXGK_ALLOCATIONINFO *ai = &a->pAllocationInfo[i];
        const AllocSpec *spec = ai->pPrivateDriverData;
        if (!spec || ai->PrivateDriverDataSize != sizeof(*spec) || !spec->Size || spec->Size > SEGMENT_SIZE) {
            while (i--) { release(a->pAllocationInfo[i].hAllocation); g->allocations--; }
            return NT_STATUS_INVALID_PARAMETER;
        }
        Allocation *al = alloc(sizeof(*al));
        if (!al) {
            while (i--) { release(a->pAllocationInfo[i].hAllocation); g->allocations--; }
            return NT_STATUS_NO_MEMORY;
        }
        al->size = (spec->Size + GPU_PAGE - 1) & ~(GPU_PAGE - 1);
        ai->Size = al->size;
        ai->Alignment = (unsigned int)GPU_PAGE;
        ai->SupportedReadSegmentSet = ai->SupportedWriteSegmentSet = 1;   /* segment 1 */
        ai->PreferredSegment = 1;
        ai->AllocationPriority = PRIORITY_NORMAL;
        ai->hAllocation = al;
        g->allocations++;
    }
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_destroy_allocation(void *adapter, const DXGKARG_DESTROYALLOCATION *a)
{
    Gpu *g = adapter;
    for (unsigned int i = 0; i < a->NumAllocations; i++) {
        release(a->pAllocationList[i]);
        g->allocations--;
    }
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_open_allocation(void *device, const DXGKARG_OPENALLOCATION *a)
{
    Device *d = device;
    for (unsigned int i = 0; i < a->NumAllocations; i++) {
        DXGK_OPENALLOCATIONINFO *oi = &a->pOpenAllocation[i];
        DXGKARGCB_GETHANDLEDATA q = { oi->hAllocation, DXGK_HANDLE_ALLOCATION, 0 };
        Allocation *al = d->gpu->cb.DxgkCbGetHandleData(&q);
        Opened *o = al ? alloc(sizeof(*o)) : 0;
        if (!o) {
            while (i--) release(a->pOpenAllocation[i].hDeviceSpecificAllocation);
            return al ? NT_STATUS_NO_MEMORY : NT_STATUS_INVALID_PARAMETER;
        }
        o->alloc = al;
        o->dev = d;
        oi->hDeviceSpecificAllocation = o;
        d->gpu->bytes += al->size;
        DbgPrint("wddmtest: allocation of %llu KiB opened (%llu KiB in use)\n", al->size >> 10, d->gpu->bytes >> 10);
    }
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_close_allocation(void *device, const DXGKARG_CLOSEALLOCATION *a)
{
    Device *d = device;
    for (unsigned int i = 0; i < a->NumAllocations; i++) {
        Opened *o = a->pOpenHandleList[i];
        d->gpu->bytes -= o->alloc->size;
        release(o);
    }
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * Its GPU MMU: processes, page tables
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS wt_create_process(void *ctx, DXGKARG_CREATEPROCESS *a)
{
    Gpu *g = ctx;
    Process *p = alloc(sizeof(*p));
    if (!p) return NT_STATUS_NO_MEMORY;
    p->gpu = g;
    a->hKmdProcess = p;
    g->processes++;
    DbgPrint("wddmtest: process created (%d)\n", g->processes);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_destroy_process(void *adapter, void *process)
{
    Gpu *g = adapter;
    g->processes--;
    DbgPrint("wddmtest: process destroyed (%d left)\n", g->processes);
    release(process);
    return NT_STATUS_SUCCESS;
}

static NT_API unsigned long long wt_root_size(void *adapter, DXGKARG_GETROOTPAGETABLESIZE *a)
{
    (void)adapter;
    unsigned int n = (a->NumberOfPte + 511) & ~511u;      /* (whole 4 KiB pages of entries) */
    a->NumberOfPte = n;
    return (unsigned long long)n * 8;
}

/* The 32-bit word at GPU virtual address @va of @p, read as the GPU reads
 * it: through the root and leaf tables to the page; 0xFFFFFFFF if unmapped */
static unsigned int gpu_read(Process *p, unsigned long long va)
{
    unsigned long long dir = p->root ? p->root[va >> 21 & 511] : 0, pte = 0;
    if (!(dir & PTE_VALID)) return 0xFFFFFFFFu;
    unsigned long long *leaf = MmMapIoSpaceEx((long long)(dir & ~0xFFFull), 4096, PAGE_READWRITE);
    if (leaf) { pte = leaf[va >> 12 & 511]; MmUnmapIoSpace(leaf, 4096); }
    if (!(pte & PTE_VALID)) return 0xFFFFFFFFu;
    unsigned char *page = MmMapIoSpaceEx((long long)(pte & ~0xFFFull), 4096, PAGE_READWRITE);
    if (!page) return 0xFFFFFFFFu;
    unsigned int v = *(volatile unsigned int *)(page + (va & 0xFFF));
    MmUnmapIoSpace(page, 4096);
    return v;
}

/* VidMm's page table updates, done here with the CPU (nothing goes in the
 * paging buffer) */
static NT_API NT_STATUS wt_build_paging_buffer(void *adapter, DXGKARG_BUILDPAGINGBUFFER *a)
{
    (void)adapter;
    if (a->Operation == DXGK_OPERATION_FLUSH_TLB) return NT_STATUS_SUCCESS;   /* (no TLB: it walks the tables each time) */
    if (a->Operation != DXGK_OPERATION_UPDATE_PAGE_TABLE) return NT_STATUS_NOT_SUPPORTED;
    DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *u = &a->UpdatePageTable;
    Process *p = u->hProcess;
    if (!p || u->UpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL || u->PageTableLevel > 1 ||
        u->StartIndex + u->NumPageTableEntries > 512)
        return NT_STATUS_INVALID_PARAMETER;
    unsigned long long *t = u->PageTableAddress.CpuVirtual;
    int valid = 0;
    for (unsigned int i = 0; i < u->NumPageTableEntries; i++) {
        const DXGK_PTE *e = &u->pPageTableEntries[u->Flags & DXGK_UPDATEPT_REPEAT ? 0 : i];
        unsigned long long own = 0;
        if (e->Flags & DXGK_PTE_VALID) {
            own = e->PageAddress << 12 | PTE_VALID | (e->Flags & DXGK_PTE_READ_ONLY ? PTE_READ_ONLY : 0);
            valid++;
        }
        t[u->StartIndex + i] = own;
    }
    if (u->PageTableLevel == 0 && (u->Flags & DXGK_UPDATEPT_INITIAL_UPDATE)) p->root = t;   /* (its root: made first) */
    if (u->PageTableLevel != 1 || !u->hAllocation) {
        if (u->PageTableLevel == 1 && !valid && !(u->Flags & DXGK_UPDATEPT_INITIAL_UPDATE))
            DbgPrint("wddmtest: unmapped %u page(s) at GPU VA 0x%llx\n", u->NumPageTableEntries, u->FirstPteVirtualAddress);
        return NT_STATUS_SUCCESS;
    }
    DbgPrint("wddmtest: mapped %d page(s) at GPU VA 0x%llx (allocation offset %llu KiB)\n", valid,
             u->FirstPteVirtualAddress, u->AllocationOffsetInBytes >> 10);
    DbgPrint("wddmtest: GPU read at 0x%llx: 0x%08x\n", u->FirstPteVirtualAddress, gpu_read(p, u->FirstPteVirtualAddress));
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * DriverEntry, and displib.lib's DxgkInitialize
 * ----------------------------------------------------------------------- */
static NT_STATUS initialize(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg, DRIVER_INITIALIZATION_DATA *init)
{
    static NT_WCHAR name[] = { '\\', 'D', 'e', 'v', 'i', 'c', 'e', '\\', 'D', 'x', 'g', 'K', 'r', 'n', 'l' };
    NT_UNICODE_STRING u = { sizeof(name), sizeof(name), name };
    NT_FILE_OBJECT *file;
    NT_DEVICE_OBJECT *dev;
    NT_STATUS st = IoGetDeviceObjectPointer(&u, FILE_ALL_ACCESS, &file, &dev);
    if (!NT_SUCCESS_(st)) return st;
    DXGKRNL_DPIINITIALIZE dpi = 0;
    NT_KEVENT ev;
    NT_IO_STATUS_BLOCK iosb;
    KeInitializeEvent(&ev, 0 /* NotificationEvent */, 0);
    NT_IRP *irp = IoBuildDeviceIoControlRequest(IOCTL_VIDEO_DDI_FUNC_REGISTER, dev, 0, 0, &dpi, sizeof(dpi), 1, &ev, &iosb);
    if (!irp) { ObfDereferenceObject(file); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    st = IofCallDriver(dev, irp);
    if (st == NT_STATUS_PENDING) { KeWaitForSingleObject(&ev, 0, 0, 0, 0); st = iosb.Status; }
    ObfDereferenceObject(file);
    if (!NT_SUCCESS_(st) || !dpi) return NT_SUCCESS_(st) ? NT_STATUS_UNSUCCESSFUL : st;
    return dpi(drv, reg, init);
}

NT_API NT_STATUS DriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg)
{
    static DRIVER_INITIALIZATION_DATA init;               /* (as a WDDM 2.0 driver is built: the table to that version) */
    mzero(&init, sizeof(init));
    init.Version = DXGKDDI_INTERFACE_VERSION_WDDM2_0;
    init.DxgkDdiAddDevice = wt_add_device;
    init.DxgkDdiStartDevice = wt_start_device;
    init.DxgkDdiStopDevice = wt_stop_device;
    init.DxgkDdiRemoveDevice = wt_remove_device;
    init.DxgkDdiQueryChildRelations = wt_child_relations;
    init.DxgkDdiQueryChildStatus = wt_child_status;
    init.DxgkDdiUnload = wt_unload;
    init.DxgkDdiQueryAdapterInfo = wt_query_adapter_info;
    init.DxgkDdiCreateDevice = wt_create_device;
    init.DxgkDdiDestroyDevice = wt_destroy_device;
    init.DxgkDdiCreateAllocation = wt_create_allocation;
    init.DxgkDdiDestroyAllocation = wt_destroy_allocation;
    init.DxgkDdiOpenAllocation = wt_open_allocation;
    init.DxgkDdiCloseAllocation = wt_close_allocation;
    init.DxgkDdiCreateProcess = wt_create_process;
    init.DxgkDdiDestroyProcess = wt_destroy_process;
    init.DxgkDdiGetRootPageTableSize = wt_root_size;
    init.DxgkDdiBuildPagingBuffer = wt_build_paging_buffer;
    NT_STATUS st = initialize(drv, reg, &init);
    DbgPrint("wddmtest: DxgkInitialize returned 0x%08x\n", (unsigned int)st);
    return st;
}
