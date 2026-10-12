/*
 * dma.c — device memory for drivers: registers (MmMapIoSpace), physically
 * contiguous memory, and DMA adapters (IoGetDmaAdapter)
 *
 * A device's registers are mapped uncached at kernel addresses
 * (MmMapIoSpace, from the memory resources IRP_MN_START_DEVICE gave the
 * driver).  A bus-master device reaches memory by physical address, and
 * only as much of it as its address lines reach: the DMA adapter
 * IoGetDmaAdapter returns knows that limit from the DEVICE_DESCRIPTION
 * (DmaAddressWidth, else Dma64BitAddresses or 32 bits).  Through it the
 * driver gets common buffers below the limit (AllocateCommonBuffer(Ex)),
 * and the physical addresses of a request's buffer, its MDL's pages: as a
 * scatter/gather list (GetScatterGatherList, BuildScatterGatherList), or
 * piece by piece through map registers (AllocateAdapterChannel,
 * MapTransfer, FlushAdapterBuffers).  Where a page lies beyond the
 * device's reach the transfer goes through a bounce buffer below the
 * limit, copied before a write to the device and after a read from it,
 * which is what Windows' map registers are on such a machine.
 *
 * NovaOS has no IOMMU in use, so a logical address is the physical one.
 */
#include "ntapi.h"
#include "../ke/irql.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../arch/x86_64/paging.h"

#define PHYSMAP_LIMIT  (64ull << 30)            /* what the physmap (PHYSMAP_BASE) covers */
#define MAX_MAP_REGS   256u                     /* 1 MiB per transfer */

/* ---- registers ---- */

static void *map_io(UINT64 pa, UINT64 size, bool no_cache)
{
    if (!size) return NULL;
    UINT64 off = pa & (PAGE_SIZE - 1);
    size_t n = (size_t)((off + size + PAGE_SIZE - 1) / PAGE_SIZE);
    uintptr_t *f = kmalloc(n * sizeof(*f));
    if (!f) return NULL;
    for (size_t i = 0; i < n; i++) f[i] = (uintptr_t)(pa - off) + i * PAGE_SIZE;
    UINT8 *va = kernel_map_frames(f, n, no_cache);
    kfree(f);
    return va ? va + off : NULL;
}

NT_API void *nt_MmMapIoSpace(INT64 pa, UINT64 size, int cache)
{
    return map_io((UINT64)pa, size, cache != 1 /* MmCached */);
}

NT_API void *nt_MmMapIoSpaceEx(INT64 pa, UINT64 size, UINT32 protect)
{
    return map_io((UINT64)pa, size, (protect & 0x200 /* PAGE_NOCACHE */) || !(protect & 0x400 /* PAGE_WRITECOMBINE */));
}

NT_API void nt_MmUnmapIoSpace(void *va, UINT64 size)
{
    if (!va || !size) return;
    UINT64 off = (UINT64)(uintptr_t)va & (PAGE_SIZE - 1);
    kernel_unmap_frames(va, (size_t)((off + size + PAGE_SIZE - 1) / PAGE_SIZE));
}

/* ---- physically contiguous memory ---- */

typedef struct Contig {
    struct Contig *next;
    UINT64         pa;
    size_t         pages;
} Contig;

static KSpinLock g_lock = KSPINLOCK_INIT;
static Contig   *g_contig;

/* @size bytes of zeroed, contiguous memory from @low up to @high (the
 * highest address it may use): its kernel address, *@pa where it is */
static void *contig_alloc(UINT64 size, UINT64 low, UINT64 high, UINT64 *pa)
{
    size_t pages = (size_t)((size + PAGE_SIZE - 1) / PAGE_SIZE);
    UINT64 limit = high >= PHYSMAP_LIMIT - 1 ? PHYSMAP_LIMIT : high + 1;
    Contig *c = kzalloc(sizeof(*c));
    if (!c || !pages) { kfree(c); return NULL; }
    uintptr_t p = pmm_alloc_pages_below(pages, limit);
    if (p && p < low) {                        /* (rarely asked for: then the next run up) */
        pmm_free_pages(p, pages);
        p = 0;
    }
    if (!p) { kfree(c); return NULL; }
    c->pa = p;
    c->pages = pages;
    IrqState s = spin_lock_irqsave(&g_lock);
    c->next = g_contig;
    g_contig = c;
    spin_unlock_irqrestore(&g_lock, s);
    void *va = (void *)(uintptr_t)(PHYSMAP_BASE + p);
    memset(va, 0, pages * PAGE_SIZE);
    if (pa) *pa = p;
    return va;
}

static bool contig_free(UINT64 pa)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    Contig *found = NULL;
    for (Contig **pp = &g_contig; *pp; pp = &(*pp)->next)
        if ((*pp)->pa == pa) { found = *pp; *pp = found->next; break; }
    spin_unlock_irqrestore(&g_lock, s);
    if (!found) return false;
    pmm_free_pages(found->pa, found->pages);
    kfree(found);
    return true;
}

static UINT64 pa_of(const void *va) { return (UINT64)(uintptr_t)va - PHYSMAP_BASE; }

NT_API void *nt_MmAllocateContiguousMemorySpecifyCache(UINT64 size, INT64 low, INT64 high, INT64 boundary, int cache)
{
    (void)boundary; (void)cache;               /* (a run never crosses a boundary its own size or larger: pages are from 0) */
    return contig_alloc(size, (UINT64)low, (UINT64)high, NULL);
}

NT_API void *nt_MmAllocateContiguousMemory(UINT64 size, INT64 high)
{
    return contig_alloc(size, 0, (UINT64)high, NULL);
}

NT_API void nt_MmFreeContiguousMemory(void *va)
{
    if (va && !contig_free(pa_of(va))) kprintf("[NT] MmFreeContiguousMemory(%p): not contiguous memory\n", va);
}

NT_API void nt_MmFreeContiguousMemorySpecifyCache(void *va, UINT64 size, int cache)
{
    (void)size; (void)cache;
    nt_MmFreeContiguousMemory(va);
}

/* ---- DMA adapters ---- */

typedef struct {
    NT_DMA_ADAPTER a;
    UINT64         limit;                      /* the first address the device cannot reach */
    UINT32         regs;                       /* map registers it may hold */
    void          *channel;                    /* the map registers an adapter channel kept */
} Adapter;

/* Map registers (AllocateAdapterChannel's MapRegisterBase): a bounce
 * buffer of that many pages below the limit */
typedef struct {
    UINT32  count;
    UINT64  pa;
    UINT8  *va;
} MapRegs;

/* A scatter/gather list NovaOS made, and its bounce buffer if it has one */
typedef struct Sg {
    struct Sg *next;
    NT_SCATTER_GATHER_LIST *list;
    bool       owned;                          /* GetScatterGatherList allocated it */
    UINT8     *bounce;                         /* (kernel address of the bounce buffer, or NULL) */
    UINT64     bounce_pa;
    NT_MDL    *mdl;
    UINT64     offset;                         /* the transfer's start in the MDL's buffer */
    UINT32     len;
} Sg;

static Sg *g_sg;

/* The byte at @off of an MDL chain's buffer: the frame's kernel address
 * and how many bytes follow it on that page (0: past the end) */
static UINT8 *mdl_byte(NT_MDL *m, UINT64 off, UINT32 *left)
{
    for (; m; m = m->Next) {
        if (off < m->ByteCount) {
            UINT64 at = m->ByteOffset + off;
            UINT64 pfn = NtMdlPfns(m)[at / PAGE_SIZE];
            UINT32 rest = (UINT32)(PAGE_SIZE - (at & (PAGE_SIZE - 1)));
            if (rest > m->ByteCount - off) rest = (UINT32)(m->ByteCount - off);
            *left = rest;
            return (UINT8 *)(uintptr_t)(PHYSMAP_BASE + pfn * PAGE_SIZE + (at & (PAGE_SIZE - 1)));
        }
        off -= m->ByteCount;
    }
    *left = 0;
    return NULL;
}

/* Copy between @len bytes of an MDL chain's buffer from @off and @buf */
static void mdl_copy(NT_MDL *m, UINT64 off, UINT8 *buf, UINT32 len, bool to_mdl)
{
    while (len) {
        UINT32 n;
        UINT8 *p = mdl_byte(m, off, &n);
        if (!p) return;
        if (n > len) n = len;
        if (to_mdl) memcpy(p, buf, n); else memcpy(buf, p, n);
        off += n;
        buf += n;
        len -= n;
    }
}

/* Where @va lies in the buffer @m (the first MDL of a chain) describes */
static UINT64 mdl_offset(NT_MDL *m, void *va)
{
    return (UINT64)(uintptr_t)va - ((UINT64)(uintptr_t)m->StartVa + m->ByteOffset);
}

static NT_API void put_adapter(NT_DMA_ADAPTER *a) { kfree(a); }

static NT_API void *alloc_common_ex(NT_DMA_ADAPTER *a, long long *max, UINT32 len, long long *logical, UINT8 cached, UINT32 node)
{
    (void)cached; (void)node;
    Adapter *ad = (Adapter *)a;
    UINT64 high = ad->limit - 1;
    if (max && (UINT64)*max < high) high = (UINT64)*max;
    UINT64 pa;
    void *va = contig_alloc(len, 0, high, &pa);
    if (va && logical) *logical = (INT64)pa;
    if (!va) kprintf("[NT] DMA: no %u-byte common buffer below 0x%llx\n", len, (unsigned long long)high + 1);
    return va;
}

static NT_API void *alloc_common(NT_DMA_ADAPTER *a, UINT32 len, long long *logical, UINT8 cached)
{
    return alloc_common_ex(a, NULL, len, logical, cached, 0);
}

static NT_API void free_common(NT_DMA_ADAPTER *a, UINT32 len, long long logical, void *va, UINT8 cached)
{
    (void)a; (void)len; (void)va; (void)cached;
    contig_free((UINT64)logical);
}

static MapRegs *regs_new(Adapter *ad, UINT32 count)
{
    MapRegs *r = kzalloc(sizeof(*r));
    if (!r) return NULL;
    r->count = count ? count : 1;
    r->va = contig_alloc((UINT64)r->count * PAGE_SIZE, 0, ad->limit - 1, &r->pa);
    if (!r->va) { kfree(r); return NULL; }
    return r;
}

static void regs_free(MapRegs *r)
{
    if (!r) return;
    contig_free(r->pa);
    kfree(r);
}

static NT_API NT_STATUS alloc_channel(NT_DMA_ADAPTER *a, NT_DEVICE_OBJECT *dev, UINT32 count, NT_DRIVER_CONTROL fn,
                                      void *ctx)
{
    Adapter *ad = (Adapter *)a;
    if (count > ad->regs) return NT_STATUS_INSUFFICIENT_RESOURCES;
    MapRegs *r = regs_new(ad, count);
    if (!r) return NT_STATUS_INSUFFICIENT_RESOURCES;
    UINT8 old = KeIrql();
    if (old < DISPATCH_LEVEL) KeIrqlSet(DISPATCH_LEVEL);
    int action = fn(dev, dev->CurrentIrp, r, ctx);
    KeIrqlSet(old);
    if (action == NT_KeepObject) ad->channel = r;
    else if (action == NT_DeallocateObject) regs_free(r);
    return NT_STATUS_SUCCESS;
}

static NT_API void free_channel(NT_DMA_ADAPTER *a)
{
    Adapter *ad = (Adapter *)a;
    regs_free(ad->channel);
    ad->channel = NULL;
}

static NT_API void free_regs(NT_DMA_ADAPTER *a, void *base, UINT32 count)
{
    (void)a; (void)count;
    regs_free(base);
}

/* A transfer through map registers: the device's address for the bytes
 * from @va (*@len of them at most; *@len how many it covers).  Through the
 * registers' bounce buffer, filled now for a write to the device. */
static NT_API long long map_transfer(NT_DMA_ADAPTER *a, NT_MDL *m, void *base, void *va, UINT32 *len, UINT8 write)
{
    Adapter *ad = (Adapter *)a;
    MapRegs *r = base;
    UINT64 off = mdl_offset(m, va);
    UINT32 run;
    UINT8 *p = mdl_byte(m, off, &run);
    if (!p || !len) return 0;
    UINT64 pa = (UINT64)(uintptr_t)p - PHYSMAP_BASE;
    /* how far the pages run on contiguously */
    while (run < *len) {
        UINT32 more;
        UINT8 *q = mdl_byte(m, off + run, &more);
        if (!q || (UINT64)(uintptr_t)q - PHYSMAP_BASE != pa + run) break;
        run += more;
    }
    if (run > *len) run = *len;
    if (pa + run <= ad->limit) { *len = run; return (INT64)pa; }
    UINT32 cap = r ? r->count * PAGE_SIZE : 0;
    if (!cap) { *len = 0; return 0; }
    if (*len > cap) *len = cap;
    if (write) mdl_copy(m, off, r->va, *len, false);
    return (INT64)r->pa;
}

static NT_API UINT8 flush_buffers(NT_DMA_ADAPTER *a, NT_MDL *m, void *base, void *va, UINT32 len, UINT8 write)
{
    Adapter *ad = (Adapter *)a;
    MapRegs *r = base;
    if (!m || !r || write) return 1;
    /* a read from the device that went through the bounce buffer: copy it out */
    UINT64 off = mdl_offset(m, va);
    UINT32 run;
    UINT8 *p = mdl_byte(m, off, &run);
    if (!p) return 1;
    UINT64 pa = (UINT64)(uintptr_t)p - PHYSMAP_BASE;
    bool direct = true;
    for (UINT32 done = 0; done < len && direct;) {
        UINT32 n;
        UINT8 *q = mdl_byte(m, off + done, &n);
        direct = q && (UINT64)(uintptr_t)q - PHYSMAP_BASE == pa + done && pa + done + n <= ad->limit;
        done += n;
    }
    if (!direct) mdl_copy(m, off, r->va, len > r->count * PAGE_SIZE ? r->count * PAGE_SIZE : len, true);
    return 1;
}

static NT_API UINT32 dma_alignment(NT_DMA_ADAPTER *a) { (void)a; return 1; }
static NT_API UINT32 read_counter(NT_DMA_ADAPTER *a) { (void)a; return 0; }

/* The pages of @len bytes from @va in @m: how many list elements, and
 * whether each lies below @limit */
static UINT32 sg_count(NT_MDL *m, UINT64 off, UINT32 len, UINT64 limit, bool *reachable)
{
    UINT32 n = 0;
    UINT64 last_end = ~0ull;
    *reachable = true;
    for (UINT32 done = 0; done < len;) {
        UINT32 run;
        UINT8 *p = mdl_byte(m, off + done, &run);
        if (!p) break;
        if (run > len - done) run = len - done;
        UINT64 pa = (UINT64)(uintptr_t)p - PHYSMAP_BASE;
        if (pa + run > limit) *reachable = false;
        if (pa != last_end) n++;
        last_end = pa + run;
        done += run;
    }
    return n;
}

static NT_API NT_STATUS calc_sg(NT_DMA_ADAPTER *a, NT_MDL *m, void *va, UINT32 len, UINT32 *size, UINT32 *regs)
{
    (void)a; (void)m;
    UINT32 pages = (UINT32)((((UINT64)(uintptr_t)va & (PAGE_SIZE - 1)) + len + PAGE_SIZE - 1) / PAGE_SIZE);
    if (size) *size = (UINT32)(sizeof(NT_SCATTER_GATHER_LIST) + (pages + 1) * sizeof(NT_SCATTER_GATHER_ELEMENT));
    if (regs) *regs = pages;
    return NT_STATUS_SUCCESS;
}

/* Fill @list for @len bytes from @va, through a bounce buffer when a page
 * is out of the device's reach; call @fn with it */
static NT_STATUS build_sg(Adapter *ad, NT_DEVICE_OBJECT *dev, NT_MDL *m, void *va, UINT32 len, NT_DRIVER_LIST_CONTROL fn,
                          void *ctx, UINT8 write, NT_SCATTER_GATHER_LIST *list, UINT32 cap, bool owned)
{
    if (!m || !len) return NT_STATUS_INVALID_PARAMETER;
    UINT64 off = mdl_offset(m, va);
    bool reachable;
    UINT32 n = sg_count(m, off, len, ad->limit, &reachable);
    Sg *rec = kzalloc(sizeof(*rec));
    if (!rec) return NT_STATUS_INSUFFICIENT_RESOURCES;
    if (!reachable) n = 1;
    if (!list) {
        cap = (UINT32)(sizeof(NT_SCATTER_GATHER_LIST) + n * sizeof(NT_SCATTER_GATHER_ELEMENT));
        list = kzalloc(cap);
        if (!list) { kfree(rec); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    }
    if (cap < sizeof(NT_SCATTER_GATHER_LIST) + n * sizeof(NT_SCATTER_GATHER_ELEMENT)) {
        kfree(rec);
        return NT_STATUS_BUFFER_TOO_SMALL;
    }
    rec->list = list;
    rec->owned = owned;
    rec->mdl = m;
    rec->offset = off;
    rec->len = len;
    if (!reachable) {
        rec->bounce = contig_alloc(len, 0, ad->limit - 1, &rec->bounce_pa);
        if (!rec->bounce) {
            if (owned) kfree(list);
            kfree(rec);
            return NT_STATUS_INSUFFICIENT_RESOURCES;
        }
        if (write) mdl_copy(m, off, rec->bounce, len, false);
        list->NumberOfElements = 1;
        list->Elements[0].Address = (INT64)rec->bounce_pa;
        list->Elements[0].Length = len;
    } else {
        UINT32 k = 0;
        UINT64 last_end = ~0ull;
        for (UINT32 done = 0; done < len;) {
            UINT32 run;
            UINT8 *p = mdl_byte(m, off + done, &run);
            if (!p) break;
            if (run > len - done) run = len - done;
            UINT64 pa = (UINT64)(uintptr_t)p - PHYSMAP_BASE;
            if (pa == last_end) list->Elements[k - 1].Length += run;
            else { list->Elements[k].Address = (INT64)pa; list->Elements[k].Length = run; k++; }
            last_end = pa + run;
            done += run;
        }
        list->NumberOfElements = k;
    }
    IrqState s = spin_lock_irqsave(&g_lock);
    rec->next = g_sg;
    g_sg = rec;
    spin_unlock_irqrestore(&g_lock, s);
    UINT8 old = KeIrql();
    if (old < DISPATCH_LEVEL) KeIrqlSet(DISPATCH_LEVEL);
    fn(dev, dev->CurrentIrp, list, ctx);
    KeIrqlSet(old);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS get_sg(NT_DMA_ADAPTER *a, NT_DEVICE_OBJECT *dev, NT_MDL *m, void *va, UINT32 len,
                               NT_DRIVER_LIST_CONTROL fn, void *ctx, UINT8 write)
{
    return build_sg((Adapter *)a, dev, m, va, len, fn, ctx, write, NULL, 0, true);
}

static NT_API NT_STATUS build_sg_into(NT_DMA_ADAPTER *a, NT_DEVICE_OBJECT *dev, NT_MDL *m, void *va, UINT32 len,
                                      NT_DRIVER_LIST_CONTROL fn, void *ctx, UINT8 write, void *buf, UINT32 cap)
{
    if (!buf) return NT_STATUS_INVALID_PARAMETER;
    return build_sg((Adapter *)a, dev, m, va, len, fn, ctx, write, buf, cap, false);
}

static NT_API void put_sg(NT_DMA_ADAPTER *a, NT_SCATTER_GATHER_LIST *list, UINT8 write)
{
    (void)a;
    IrqState s = spin_lock_irqsave(&g_lock);
    Sg *rec = NULL;
    for (Sg **pp = &g_sg; *pp; pp = &(*pp)->next)
        if ((*pp)->list == list) { rec = *pp; *pp = rec->next; break; }
    spin_unlock_irqrestore(&g_lock, s);
    if (!rec) return;
    if (rec->bounce) {
        if (!write) mdl_copy(rec->mdl, rec->offset, rec->bounce, rec->len, true);
        contig_free(rec->bounce_pa);
    }
    if (rec->owned) kfree(list);
    kfree(rec);
}

/* What NovaOS does not have yet (DMA version 2 and 3 functions beyond
 * AllocateCommonBufferEx): logged, and refused */
static NT_API NT_STATUS dma_missing(void)
{
    kprintf("[NT] DMA: a version 2 or 3 DMA operation NovaOS does not implement yet was called\n");
    return NT_STATUS_NOT_SUPPORTED;
}

static NT_DMA_OPERATIONS g_ops = {
    .Size = sizeof(NT_DMA_OPERATIONS),
    .PutDmaAdapter = put_adapter,
    .AllocateCommonBuffer = alloc_common,
    .FreeCommonBuffer = free_common,
    .AllocateAdapterChannel = alloc_channel,
    .FlushAdapterBuffers = flush_buffers,
    .FreeAdapterChannel = free_channel,
    .FreeMapRegisters = free_regs,
    .MapTransfer = map_transfer,
    .GetDmaAlignment = dma_alignment,
    .ReadDmaCounter = read_counter,
    .GetScatterGatherList = get_sg,
    .PutScatterGatherList = put_sg,
    .CalculateScatterGatherList = calc_sg,
    .BuildScatterGatherList = build_sg_into,
    .AllocateCommonBufferEx = alloc_common_ex,
};

NT_API NT_DMA_ADAPTER *nt_IoGetDmaAdapter(NT_DEVICE_OBJECT *pdo, NT_DEVICE_DESCRIPTION *d, UINT32 *regs)
{
    static bool filled;
    if (!filled) {                                   /* (the operations NovaOS lacks) */
        void **slot[] = { &g_ops.BuildMdlFromScatterGatherList, &g_ops.GetDmaAdapterInfo, &g_ops.GetDmaTransferInfo,
                          &g_ops.InitializeDmaTransferContext, &g_ops.AllocateAdapterChannelEx,
                          &g_ops.ConfigureAdapterChannel, &g_ops.CancelAdapterChannel, &g_ops.MapTransferEx,
                          &g_ops.GetScatterGatherListEx, &g_ops.BuildScatterGatherListEx, &g_ops.FlushAdapterBuffersEx,
                          &g_ops.FreeAdapterObject, &g_ops.CancelMappedTransfer, &g_ops.AllocateDomainCommonBuffer,
                          &g_ops.FlushDmaBuffer, &g_ops.JoinDmaDomain, &g_ops.LeaveDmaDomain, &g_ops.GetDmaDomain };
        for (unsigned i = 0; i < sizeof(slot) / sizeof(slot[0]); i++) *slot[i] = (void *)dma_missing;
        filled = true;
    }
    (void)pdo;
    if (!d || d->Version > 3) return NULL;
    Adapter *ad = kzalloc(sizeof(*ad));
    if (!ad) return NULL;
    UINT32 bits = d->Version >= 3 && d->DmaAddressWidth ? d->DmaAddressWidth : d->Dma64BitAddresses ? 64 : 32;
    ad->limit = bits >= 36 ? PHYSMAP_LIMIT : 1ull << bits;
    UINT32 n = d->MaximumLength ? d->MaximumLength / PAGE_SIZE + 1 : MAX_MAP_REGS;
    ad->regs = n > MAX_MAP_REGS ? MAX_MAP_REGS : n;
    ad->a.Version = (unsigned short)(d->Version >= 3 ? 3 : d->Version == 2 ? 2 : 1);
    ad->a.Size = (unsigned short)sizeof(NT_DMA_ADAPTER);
    ad->a.DmaOperations = &g_ops;
    if (regs) *regs = ad->regs;
    kprintf("[NT] DMA adapter: version %u, %u-bit addresses, %u map registers\n", ad->a.Version, bits, ad->regs);
    return &ad->a;
}
