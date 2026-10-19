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
 * own page tables, as its GPU would: the self-test checks that word.
 *
 *   its GPU            (step 3's fourth part) a kernel thread that runs DMA
 *                      buffers in the order SubmitCommandVirtual queues
 *                      them, reading each through the context's page tables
 *                      from the root SetRootPageTable gave (physical
 *                      addresses only, as a GPU has); its commands are
 *                      32-bit words: WRITE va value, COPY src dst words,
 *                      FENCE va value64 (a monitored fence's GPU address:
 *                      the GPU writes the value there), NOP.  A buffer done,
 *                      it raises its "interrupt": with no interrupt line
 *                      (a root device) the thread calls the driver's
 *                      interrupt routine itself, which reports the buffer's
 *                      fence ID (DXGK_INTERRUPT_DMA_COMPLETED, or DMA_FAULTED
 *                      for a write to a read-only or unmapped page; and
 *                      MONITORED_FENCE_SIGNALED after a FENCE) and queues the
 *                      DPC, whose routine tells dxgkrnl (DxgkCbNotifyDpc)
 *   preemption, TDR    (the fifth part) PreemptionCaps at primitive
 *                      boundaries: PreemptCommand makes the GPU stop
 *                      between two commands (or inside a stoppable SPIN),
 *                      remember where the context was, drop the buffers it
 *                      had not started and report DMA_PREEMPTED; a buffer
 *                      given again resumes where it stopped.
 *                      SupportPerEngineTDR: ResetEngine stops whatever it
 *                      runs (a SPIN that cannot be stopped included), drops
 *                      its queue and says the last fence ID it aborted
 *                      (ResetFromTimeout and RestartFromTimeout do the same
 *                      for the adapter).  SPIN flags is the command that
 *                      provokes them: it runs until the GPU is preempted
 *                      (flags bit 0, "a long draw") or reset (a shader in
 *                      an endless loop)
 *
 * Its DbgPrint lines and counts
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
IMPORT NT_STATUS PsCreateSystemThread(void **thread, unsigned int access, void *attrs, void *process, void *client,
                                      void (NT_API *routine)(void *), void *ctx);
IMPORT NT_STATUS PsTerminateSystemThread(NT_STATUS st);
IMPORT NT_STATUS ZwClose(void *h);
IMPORT int       KeSetEvent(NT_KEVENT *ev, int increment, unsigned char wait);
IMPORT unsigned char KeAcquireSpinLockRaiseToDpc(NT_KSPIN_LOCK *l);
IMPORT void      KeReleaseSpinLock(NT_KSPIN_LOCK *l, unsigned char old);

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
#define RING                32                    /* DMA buffers its GPU holds */
#define GPU_WRITE           1u                    /* its commands */
#define GPU_COPY            2u
#define GPU_FENCE           3u
#define GPU_SPIN            4u                    /* SPIN flags: until preempted (bit 0) or reset */
#define SPIN_PREEMPTIBLE    1u
#define GPU_NOP             0u
#define ST_FAULT            ((NT_STATUS)0xC01E0003)   /* (STATUS_GRAPHICS_... : the faulted buffer's status) */

/* The user-mode side's private data for an allocation (userland's
 * NOVA_KMT_ALLOCATION) */
typedef struct { unsigned long long Size; unsigned int Flags, Reserved; } AllocSpec;

struct Context;
typedef struct { struct Context *ctx; unsigned long long va; unsigned int size, fence; } Job;
enum { RAN, FAULTED, PREEMPTED, RESET };          /* how a DMA buffer's run ended */

typedef struct {
    NT_DEVICE_OBJECT  *pdo;
    DXGKRNL_INTERFACE  cb;
    int                devices, allocations, processes, contexts;   /* (live: counted for the log) */
    unsigned long long bytes;
    /* its GPU: the ring the thread takes DMA buffers from, and its
     * "interrupt status register" (what the interrupt routine reports) */
    NT_KSPIN_LOCK      ring_lock;
    Job                ring[RING];
    unsigned int       head, tail;
    NT_KEVENT          work, exited, reset_done;
    volatile int       stop, running;
    volatile unsigned int irq_done, irq_fault, irq_fence, irq_preempt;   /* (a fence ID; 0: none) */
    /* preemption and reset, asked for by dxgkrnl (PreemptCommand, ResetEngine) */
    volatile unsigned int preempt_fence, reset;
    unsigned int       last_done, last_given;     /* fence IDs: the last it finished, the last it was given */
} Gpu;

typedef struct { Gpu *gpu; unsigned long long *root; } Process;   /* (root: its root table, as the CPU sees it) */

typedef struct { Gpu *gpu; void *dxgk; } Device;
/* (root: its root table's physical address; resume_*: where a preemption
 * stopped it, inside the buffer at resume_buf) */
typedef struct Context { Gpu *gpu; unsigned int node; unsigned long long root, resume_buf, resume_at; } Context;
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

static NT_API void gpu_thread(void *arg);

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
    KeInitializeEvent(&g->work, 1 /* SynchronizationEvent */, 0);
    KeInitializeEvent(&g->exited, 0 /* NotificationEvent */, 0);
    KeInitializeEvent(&g->reset_done, 1, 0);
    g->stop = 0;
    g->head = g->tail = 0;
    void *th;
    if (!NT_SUCCESS_(PsCreateSystemThread(&th, 0, 0, 0, 0, gpu_thread, g))) return NT_STATUS_INSUFFICIENT_RESOURCES;
    ZwClose(th);
    g->running = 1;
    DbgPrint("wddmtest: started, LUID %x, a software GPU in %llu MiB of RAM\n", si->AdapterLuid.LowPart,
             (unsigned long long)info.SystemMemorySize >> 20);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_stop_device(void *ctx)
{
    Gpu *g = ctx;
    if (g->running) {                                     /* (its GPU's thread, told to stop) */
        g->stop = 1;
        KeSetEvent(&g->work, 0, 0);
        KeWaitForSingleObject(&g->exited, 0, 0, 0, 0);
        g->running = 0;
    }
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
        caps->PreemptionCaps.Graphics = D3DKMDT_GRAPHICS_PREEMPTION_PRIMITIVE_BOUNDARY;   /* (between commands) */
        caps->PreemptionCaps.Compute = D3DKMDT_COMPUTE_PREEMPTION_DISPATCH_BOUNDARY;
        caps->SupportPerEngineTDR = 1;
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
 * Its GPU: contexts, submission, the thread that runs DMA buffers, and its
 * interrupt
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS wt_create_context(void *device, DXGKARG_CREATECONTEXT *a)
{
    Device *d = device;
    if (a->NodeOrdinal != 0 || !(a->Flags & DXGK_CONTEXT_VIRTUAL_ADDRESSING)) return NT_STATUS_INVALID_PARAMETER;
    Context *c = alloc(sizeof(*c));
    if (!c) return NT_STATUS_NO_MEMORY;
    c->gpu = d->gpu;
    c->node = a->NodeOrdinal;
    a->hContext = c;
    a->ContextInfo.DmaBufferSize = 4096;
    a->ContextInfo.DmaBufferPrivateDataSize = 16;
    a->ContextInfo.Caps = 1;                              /* NoPatchingRequired */
    d->gpu->contexts++;
    DbgPrint("wddmtest: context created on node %u (%d)\n", c->node, d->gpu->contexts);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS wt_destroy_context(void *context)
{
    Context *c = context;
    c->gpu->contexts--;
    DbgPrint("wddmtest: context destroyed (%d left)\n", c->gpu->contexts);
    release(c);
    return NT_STATUS_SUCCESS;
}

static NT_API void wt_set_root(void *adapter, const DXGKARG_SETROOTPAGETABLE *a)
{
    (void)adapter;
    Context *c = a->hContext;
    c->root = a->Address.SegmentOffset;
    DbgPrint("wddmtest: context's root page table set (%u entries, segment %u)\n", a->NumEntries, a->Address.SegmentId);
}

static NT_API NT_STATUS wt_submit(void *adapter, const DXGKARG_SUBMITCOMMANDVIRTUAL *a)
{
    Gpu *g = adapter;
    if (!a->hContext || (a->Flags & DXGK_SUBMIT_PAGING)) return NT_STATUS_INVALID_PARAMETER;   /* (no paging buffers: the CPU writes its tables) */
    unsigned char irql = KeAcquireSpinLockRaiseToDpc(&g->ring_lock);
    int full = g->head - g->tail >= RING;
    if (!full) {
        g->ring[g->head++ % RING] = (Job){ a->hContext, a->DmaBufferVirtualAddress, a->DmaBufferSize, a->SubmissionFenceId };
        g->last_given = a->SubmissionFenceId;
    }
    KeReleaseSpinLock(&g->ring_lock, irql);
    if (full) return NT_STATUS_INSUFFICIENT_RESOURCES;
    KeSetEvent(&g->work, 0, 0);
    return NT_STATUS_SUCCESS;
}

/* The page holding GPU virtual address @va in the address space whose
 * root table is at physical @root, mapped for the CPU: its GPU's view, from
 * the page tables alone; NULL if unmapped (or read-only, for @write) */
static unsigned char *gpu_page(unsigned long long root, unsigned long long va, int write)
{
    if (!root || va >> VA_BITS) return 0;
    unsigned long long *t = MmMapIoSpaceEx((long long)root, 4096, PAGE_READWRITE), e;
    if (!t) return 0;
    e = t[va >> 21 & 511];
    MmUnmapIoSpace(t, 4096);
    if (!(e & PTE_VALID) || !(t = MmMapIoSpaceEx((long long)(e & ~0xFFFull), 4096, PAGE_READWRITE))) return 0;
    e = t[va >> 12 & 511];
    MmUnmapIoSpace(t, 4096);
    if (!(e & PTE_VALID) || (write && (e & PTE_READ_ONLY))) return 0;
    return MmMapIoSpaceEx((long long)(e & ~0xFFFull), 4096, PAGE_READWRITE);
}

static int gpu_load(unsigned long long root, unsigned long long va, unsigned int *v)
{
    unsigned char *p = (va & 3) ? 0 : gpu_page(root, va, 0);
    if (!p) return 0;
    *v = *(volatile unsigned int *)(p + (va & 0xFFF));
    MmUnmapIoSpace(p, 4096);
    return 1;
}

static int gpu_store(unsigned long long root, unsigned long long va, unsigned int v)
{
    unsigned char *p = (va & 3) ? 0 : gpu_page(root, va, 1);
    if (!p) return 0;
    *(volatile unsigned int *)(p + (va & 0xFFF)) = v;
    MmUnmapIoSpace(p, 4096);
    return 1;
}

static unsigned long long addr(const unsigned int *w) { return w[0] | (unsigned long long)w[1] << 32; }

/* One DMA buffer: its commands, fetched through the page tables, from
 * where a preemption stopped its context if it was stopped inside it.
 * RAN, PREEMPTED (where is kept in the context), RESET, or FAULTED (*fault:
 * the GPU address) */
static int gpu_run(Gpu *g, const Job *j, int *commands, unsigned long long *fault)
{
    Context *c = j->ctx;
    unsigned long long root = c->root, at = j->va, end = j->va + j->size;
    if (c->resume_buf == j->va && c->resume_at >= j->va && c->resume_at < end) {
        at = c->resume_at;
        DbgPrint("wddmtest: resumed at GPU VA 0x%llx (fence %u)\n", at, j->fence);
    }
    c->resume_buf = c->resume_at = 0;
    *commands = 0;
    *fault = 0;
    while (at < end) {
        unsigned int w[6] = { 0 }, n;
        if (g->reset) return RESET;
        if (g->preempt_fence) { c->resume_buf = j->va; c->resume_at = at; return PREEMPTED; }   /* (a primitive boundary) */
        if (!gpu_load(root, at, &w[0])) { *fault = at; return FAULTED; }
        n = w[0] == GPU_WRITE ? 4 : w[0] == GPU_COPY ? 6 : w[0] == GPU_FENCE ? 5 : w[0] == GPU_SPIN ? 2 : 1;
        if (w[0] > GPU_SPIN || at + 4ull * n > end) { *fault = at; return FAULTED; }   /* (an unknown or cut command: a fault too) */
        for (unsigned int i = 1; i < n; i++)
            if (!gpu_load(root, at + 4ull * i, &w[i])) { *fault = at + 4ull * i; return FAULTED; }
        if (w[0] == GPU_WRITE && !gpu_store(root, addr(&w[1]), w[3])) { *fault = addr(&w[1]); return FAULTED; }
        if (w[0] == GPU_COPY)
            for (unsigned int i = 0; i < w[5]; i++) {
                unsigned int v;
                if (!gpu_load(root, addr(&w[1]) + 4ull * i, &v)) { *fault = addr(&w[1]) + 4ull * i; return FAULTED; }
                if (!gpu_store(root, addr(&w[3]) + 4ull * i, v)) { *fault = addr(&w[3]) + 4ull * i; return FAULTED; }
            }
        if (w[0] == GPU_FENCE) {                          /* (the high word first: the value only rises) */
            if (!gpu_store(root, addr(&w[1]) + 4, w[4]) || !gpu_store(root, addr(&w[1]), w[3])) {
                *fault = addr(&w[1]);
                return FAULTED;
            }
            DbgPrint("wddmtest: GPU wrote fence value %llu\n", addr(&w[3]));
            g->irq_fence = 1;
        }
        if (w[0] == GPU_SPIN) {                           /* (its "work": the GPU's clock ticks, 10 ms each) */
            DbgPrint("wddmtest: GPU spinning at 0x%llx (fence %u, %s)\n", at, j->fence,
                     w[1] & SPIN_PREEMPTIBLE ? "until preempted" : "until reset");
            for (;;) {
                long long t = -10000ll * 10;
                if (g->reset) return RESET;
                if ((w[1] & SPIN_PREEMPTIBLE) && g->preempt_fence) { c->resume_buf = j->va; c->resume_at = at; return PREEMPTED; }
                KeWaitForSingleObject(&g->work, 0, 0, 0, &t);
            }
        }
        if (w[0] != GPU_NOP) (*commands)++;
        at += 4ull * n;
    }
    return RAN;
}

/* Its interrupt routine (DxgkDdiInterruptRoutine): what the GPU did, to dxgkrnl */
static NT_API unsigned char wt_interrupt(void *ctx, unsigned int msg)
{
    (void)msg;
    Gpu *g = ctx;
    unsigned int done = __atomic_exchange_n(&g->irq_done, 0, __ATOMIC_ACQ_REL);
    unsigned int fault = __atomic_exchange_n(&g->irq_fault, 0, __ATOMIC_ACQ_REL);
    unsigned int fence = __atomic_exchange_n(&g->irq_fence, 0, __ATOMIC_ACQ_REL);
    unsigned int preempt = __atomic_exchange_n(&g->irq_preempt, 0, __ATOMIC_ACQ_REL);
    if (!done && !fault && !fence && !preempt) return 0;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA d;
    if (fence) {
        mzero(&d, sizeof(d));
        d.InterruptType = DXGK_INTERRUPT_MONITORED_FENCE_SIGNALED;
        g->cb.DxgkCbNotifyInterrupt(g->cb.DeviceHandle, &d);
    }
    if (fault) {
        mzero(&d, sizeof(d));
        d.InterruptType = DXGK_INTERRUPT_DMA_FAULTED;
        d.DmaFaulted.FaultedFenceId = fault;
        d.DmaFaulted.Status = ST_FAULT;
        g->cb.DxgkCbNotifyInterrupt(g->cb.DeviceHandle, &d);
    }
    if (done) {
        mzero(&d, sizeof(d));
        d.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
        d.DmaCompleted.SubmissionFenceId = done;
        g->cb.DxgkCbNotifyInterrupt(g->cb.DeviceHandle, &d);
    }
    if (preempt) {                                        /* (after what it finished before it stopped) */
        mzero(&d, sizeof(d));
        d.InterruptType = DXGK_INTERRUPT_DMA_PREEMPTED;
        d.DmaPreempted.PreemptionFenceId = preempt;
        d.DmaPreempted.LastCompletedFenceId = g->last_done;
        g->cb.DxgkCbNotifyInterrupt(g->cb.DeviceHandle, &d);
    }
    g->cb.DxgkCbQueueDpc(g->cb.DeviceHandle);
    return 1;
}

static NT_API void wt_dpc(void *ctx)
{
    Gpu *g = ctx;
    g->cb.DxgkCbNotifyDpc(g->cb.DeviceHandle);
}

static NT_API unsigned char raise_interrupt(void *ctx) { return wt_interrupt(ctx, 0); }

/* Its interrupt raised (no interrupt line: the routine runs as the line would run it, under its lock) */
static void interrupt(Gpu *g)
{
    unsigned char r;
    g->cb.DxgkCbSynchronizeExecution(g->cb.DeviceHandle, raise_interrupt, g, 0, &r);
}

/* The buffers not started dropped (a preemption's, or a reset's): how many */
static unsigned int ring_drop(Gpu *g)
{
    unsigned char irql = KeAcquireSpinLockRaiseToDpc(&g->ring_lock);
    unsigned int n = g->head - g->tail;
    g->tail = g->head;
    KeReleaseSpinLock(&g->ring_lock, irql);
    return n;
}

static void preempted(Gpu *g)
{
    unsigned int f = g->preempt_fence, n = ring_drop(g);
    g->preempt_fence = 0;
    DbgPrint("wddmtest: preempted (fence %u): %u buffer(s) not started dropped, the last finished %u\n", f, n,
             g->last_done);
    __atomic_store_n(&g->irq_preempt, f, __ATOMIC_RELEASE);
    interrupt(g);
}

static void reset_done(Gpu *g)
{
    unsigned int n = ring_drop(g);
    g->preempt_fence = 0;
    g->reset = 0;
    DbgPrint("wddmtest: engine reset, %u buffer(s) not started dropped\n", n);
    KeSetEvent(&g->reset_done, 0, 0);
}

static NT_API void gpu_thread(void *arg)
{
    Gpu *g = arg;
    while (!g->stop) {
        KeWaitForSingleObject(&g->work, 0, 0, 0, 0);
        for (;;) {
            if (g->reset) reset_done(g);
            if (g->preempt_fence) preempted(g);
            unsigned char irql = KeAcquireSpinLockRaiseToDpc(&g->ring_lock);
            int any = g->tail != g->head;
            Job j = any ? g->ring[g->tail++ % RING] : (Job){ 0 };
            KeReleaseSpinLock(&g->ring_lock, irql);
            if (!any) break;
            int commands;
            unsigned long long fault;
            int how = gpu_run(g, &j, &commands, &fault);
            if (how == RESET) {
                DbgPrint("wddmtest: GPU stopped by a reset (fence %u)\n", j.fence);
                j.ctx->resume_buf = j.ctx->resume_at = 0;
                reset_done(g);
                continue;
            }
            if (how == PREEMPTED) {
                DbgPrint("wddmtest: GPU stopped at 0x%llx by a preemption (fence %u)\n", j.ctx->resume_at, j.fence);
                preempted(g);
                continue;
            }
            if (how == FAULTED) {
                DbgPrint("wddmtest: GPU page fault at 0x%llx (fence %u)\n", fault, j.fence);
                __atomic_store_n(&g->irq_fault, j.fence, __ATOMIC_RELEASE);
            } else {
                DbgPrint("wddmtest: ran %u bytes at GPU VA 0x%llx (fence %u): %d command(s)\n", j.size, j.va, j.fence,
                         commands);
                g->last_done = j.fence;
                __atomic_store_n(&g->irq_done, j.fence, __ATOMIC_RELEASE);
            }
            interrupt(g);
        }
    }
    KeSetEvent(&g->exited, 0, 0);
    PsTerminateSystemThread(0);
}

/* DxgkDdiPreemptCommand: the GPU stops at its next primitive boundary */
static NT_API NT_STATUS wt_preempt(void *adapter, const DXGKARG_PREEMPTCOMMAND *a)
{
    Gpu *g = adapter;
    if (a->NodeOrdinal != 0) return NT_STATUS_INVALID_PARAMETER;
    DbgPrint("wddmtest: preemption asked for (fence %u, the last finished %u)\n", a->PreemptionFenceId,
             a->LastCompletedFenceId);
    g->preempt_fence = a->PreemptionFenceId;
    KeSetEvent(&g->work, 0, 0);
    return NT_STATUS_SUCCESS;
}

/* DxgkDdiResetEngine: whatever the GPU runs stopped, its queue dropped */
static NT_API NT_STATUS wt_reset_engine(void *adapter, DXGKARG_RESETENGINE *a)
{
    Gpu *g = adapter;
    if (a->NodeOrdinal != 0) return NT_STATUS_INVALID_PARAMETER;
    long long t = -10000ll * 1000;
    g->reset = 1;
    KeSetEvent(&g->work, 0, 0);
    NT_STATUS st = KeWaitForSingleObject(&g->reset_done, 0, 0, 0, &t);
    a->LastAbortedFenceId = g->last_given;
    return st == 0 ? NT_STATUS_SUCCESS : NT_STATUS_UNSUCCESSFUL;
}

/* DxgkDdiResetFromTimeout, DxgkDdiRestartFromTimeout: the adapter's one engine */
static NT_API NT_STATUS wt_reset_from_timeout(void *adapter)
{
    DXGKARG_RESETENGINE r = { 0, 0, 0 };
    return wt_reset_engine(adapter, &r);
}

static NT_API NT_STATUS wt_restart_from_timeout(void *adapter) { (void)adapter; return NT_STATUS_SUCCESS; }

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
    init.DxgkDdiInterruptRoutine = wt_interrupt;
    init.DxgkDdiDpcRoutine = wt_dpc;
    init.DxgkDdiCreateContext = wt_create_context;
    init.DxgkDdiDestroyContext = wt_destroy_context;
    init.DxgkDdiSetRootPageTable = wt_set_root;
    init.DxgkDdiSubmitCommandVirtual = wt_submit;
    init.DxgkDdiPreemptCommand = wt_preempt;
    init.DxgkDdiResetEngine = wt_reset_engine;
    init.DxgkDdiResetFromTimeout = wt_reset_from_timeout;
    init.DxgkDdiRestartFromTimeout = wt_restart_from_timeout;
    NT_STATUS st = initialize(drv, reg, &init);
    DbgPrint("wddmtest: DxgkInitialize returned 0x%08x\n", (unsigned int)st);
    return st;
}
