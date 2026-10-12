/*
 * ex.c — the executive's pool for drivers: ExAllocatePool*, ExFreePool*
 *
 * Every pool type is the kernel heap (kmalloc: NovaOS's kernel memory is
 * never paged out, so paged and nonpaged pool are the same memory), with
 * kmalloc's alignment (16 bytes, whole pages from a page up).  Each block
 * is recorded in a table beside it with its tag, size and the driver that
 * allocated it (the caller's address), so that a free with the wrong tag
 * or of a block that is not pool is reported as Windows' BAD_POOL_CALLER
 * (0xC2) would be, and the blocks a driver still holds when it unloads
 * are named in the log.
 */
#include "nt.h"
#include "ntapi.h"
#include "../ke/irql.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../net/tls.h"

/* ExAllocatePool2's POOL_FLAGS */
#define POOL_FLAG_UNINITIALIZED   0x0002ull
#define POOL_FLAG_RAISE_ON_FAILURE 0x0020ull
#define POOL_FLAG_NON_PAGED       0x0040ull
#define POOL_FLAG_PAGED           0x0100ull
/* POOL_TYPE */
#define NonPagedPoolMustSucceed   2

#define BUCKETS 256

typedef struct PoolBlock {
    struct PoolBlock *next;
    void             *p;
    UINT64            size;
    UINT32            tag;
    NtDriver         *drv;
} PoolBlock;

static PoolBlock *g_pool[BUCKETS];
static KSpinLock  g_pool_lock = KSPINLOCK_INIT;

static unsigned bucket(const void *p) { return (unsigned)(((uintptr_t)p >> 4) * 0x9E3779B1u >> 24) % BUCKETS; }

static void tag_str(UINT32 tag, char out[5])
{
    for (int i = 0; i < 4; i++) {
        char c = (char)(tag >> (8 * i));
        out[i] = c >= 0x20 && c < 0x7F ? c : '.';
    }
    out[4] = 0;
}

static void *pool_alloc(UINT64 size, UINT32 tag, bool zero, void *caller)
{
    PoolBlock *b = kmalloc(sizeof(*b));
    void *p = b ? kmalloc(size < 16 ? 16 : size) : NULL;
    if (!p) { kfree(b); return NULL; }
    if (zero) memset(p, 0, size);
    b->p = p;
    b->size = size;
    b->tag = tag;
    b->drv = NtDrvByAddress((UINT64)(uintptr_t)caller);
    unsigned h = bucket(p);
    IrqState s = spin_lock_irqsave(&g_pool_lock);
    b->next = g_pool[h];
    g_pool[h] = b;
    spin_unlock_irqrestore(&g_pool_lock, s);
    return p;
}

static void pool_free(void *p, UINT32 tag, bool check_tag, void *caller)
{
    if (!p) {
        kprintf("[NT] BAD_POOL_CALLER (0xC2): ExFreePool(NULL) from %p\n", caller);
        return;
    }
    unsigned h = bucket(p);
    IrqState s = spin_lock_irqsave(&g_pool_lock);
    PoolBlock *b = NULL;
    for (PoolBlock **pp = &g_pool[h]; *pp; pp = &(*pp)->next)
        if ((*pp)->p == p) { b = *pp; *pp = b->next; break; }
    spin_unlock_irqrestore(&g_pool_lock, s);
    if (!b) {
        kprintf("[NT] BAD_POOL_CALLER (0xC2): %p freed from %p is not a pool block (freed twice?)\n", p, caller);
        return;
    }
    /* (bit 31: PROTECTED_POOL; tag 0 is ExFreePool's, unchecked, as
     * FTDI's ftser2k.sys frees with on Windows) */
    if (check_tag && tag && (tag & 0x7FFFFFFF) != (b->tag & 0x7FFFFFFF)) {
        char want[5], got[5];
        tag_str(b->tag, want);
        tag_str(tag, got);
        kprintf("[NT] BAD_POOL_CALLER (0xC2): %p allocated with tag '%s' freed with tag '%s'\n", p, want, got);
    }
    kfree(p);
    kfree(b);
}

NT_API void *nt_ExAllocatePoolWithTag(int type, UINT64 size, UINT32 tag)
{
    void *p = pool_alloc(size, tag, false, __builtin_return_address(0));
    if (!p && type == NonPagedPoolMustSucceed)
        KPANIC("*** STOP: 0x00000041 (MUST_SUCCEED_POOL_EMPTY): %llu bytes", (unsigned long long)size);
    return p;
}

NT_API void *nt_ExAllocatePool(int type, UINT64 size)
{
    (void)type;
    return pool_alloc(size, 0x656E6F4E /* 'None' */, false, __builtin_return_address(0));
}

NT_API void *nt_ExAllocatePoolWithTagPriority(int type, UINT64 size, UINT32 tag, int priority)
{
    (void)type; (void)priority;
    return pool_alloc(size, tag, false, __builtin_return_address(0));
}

NT_API void *nt_ExAllocatePoolWithQuotaTag(int type, UINT64 size, UINT32 tag)
{
    (void)type;
    return pool_alloc(size, tag, false, __builtin_return_address(0));
}

/* Windows 10 2004's: zeroed unless POOL_FLAG_UNINITIALIZED */
NT_API void *nt_ExAllocatePool2(UINT64 flags, UINT64 size, UINT32 tag)
{
    if (!tag || !(flags & (POOL_FLAG_NON_PAGED | POOL_FLAG_PAGED))) {
        kprintf("[NT] ExAllocatePool2: flags 0x%llx tag 0x%08x not valid\n", (unsigned long long)flags, tag);
        return NULL;
    }
    void *p = pool_alloc(size, tag, !(flags & POOL_FLAG_UNINITIALIZED), __builtin_return_address(0));
    if (!p && (flags & POOL_FLAG_RAISE_ON_FAILURE))
        kprintf("[NT] ExAllocatePool2: out of memory (%llu bytes), POOL_FLAG_RAISE_ON_FAILURE\n", (unsigned long long)size);
    return p;
}

NT_API void nt_ExFreePoolWithTag(void *p, UINT32 tag) { pool_free(p, tag, true, __builtin_return_address(0)); }
NT_API void nt_ExFreePool(void *p) { pool_free(p, 0, false, __builtin_return_address(0)); }

void NtPoolDriverGone(NtDriver *drv)
{
    UINT64 blocks = 0, bytes = 0;
    char tags[64] = "";
    int tl = 0;
    IrqState s = spin_lock_irqsave(&g_pool_lock);
    for (unsigned h = 0; h < BUCKETS; h++)
        for (PoolBlock *b = g_pool[h]; b; b = b->next) {
            if (b->drv != drv) continue;
            blocks++;
            bytes += b->size;
            char t[5];
            tag_str(b->tag, t);
            if (tl < (int)sizeof(tags) - 8 && !strstr(tags, t)) tl += ksnprintf(tags + tl, sizeof(tags) - tl, "%s'%s'", tl ? " " : "", t);
            b->drv = NULL;                           /* (left allocated, as Windows leaves it) */
        }
    spin_unlock_irqrestore(&g_pool_lock, s);
    if (blocks)
        kprintf("[NT] %s: %llu pool block%s (%llu bytes, tags %s) never freed\n", drv->image, (unsigned long long)blocks,
                blocks == 1 ? "" : "s", (unsigned long long)bytes, tags);
}

/* -----------------------------------------------------------------------
 * Step 30.2 part 2: interlocked S-lists
 *
 * Windows' S-lists are lock-free (a 128-bit compare-and-swap on the
 * header); NovaOS guards them with one spin lock, which a driver cannot
 * tell apart through the Exp* entry points it calls.
 * ----------------------------------------------------------------------- */
static KSpinLock g_slist_lock = KSPINLOCK_INIT;

NT_API void nt_ExInitializeSListHead(NT_SLIST_HEADER *h) { memset(h, 0, sizeof(*h)); }
NT_API void nt_InitializeSListHead(NT_SLIST_HEADER *h) { memset(h, 0, sizeof(*h)); }

NT_API NT_SLIST_ENTRY *nt_ExpInterlockedPushEntrySList(NT_SLIST_HEADER *h, NT_SLIST_ENTRY *e)
{
    IrqState s = spin_lock_irqsave(&g_slist_lock);
    NT_SLIST_ENTRY *first = h->Next;
    e->Next = first;
    h->Next = e;
    h->Depth++;
    h->Sequence++;
    spin_unlock_irqrestore(&g_slist_lock, s);
    return first;
}

NT_API NT_SLIST_ENTRY *nt_ExpInterlockedPopEntrySList(NT_SLIST_HEADER *h)
{
    IrqState s = spin_lock_irqsave(&g_slist_lock);
    NT_SLIST_ENTRY *e = h->Next;
    if (e) { h->Next = e->Next; h->Depth--; h->Sequence++; }
    spin_unlock_irqrestore(&g_slist_lock, s);
    return e;
}

NT_API NT_SLIST_ENTRY *nt_InterlockedPushEntrySList(NT_SLIST_HEADER *h, NT_SLIST_ENTRY *e)
{
    return nt_ExpInterlockedPushEntrySList(h, e);
}
NT_API NT_SLIST_ENTRY *nt_InterlockedPopEntrySList(NT_SLIST_HEADER *h) { return nt_ExpInterlockedPopEntrySList(h); }

NT_API UINT16 nt_ExQueryDepthSList(NT_SLIST_HEADER *h) { return h->Depth; }

/* -----------------------------------------------------------------------
 * Lookaside lists: a cache of fixed-size blocks over the pool
 * ----------------------------------------------------------------------- */
typedef void *(NT_API *NT_ALLOCATE_FUNCTION)(int pool, UINT64 size, UINT32 tag);
typedef void  (NT_API *NT_FREE_FUNCTION)(void *p);

static void lookaside_init(NT_LOOKASIDE_LIST *l, void *allocate, void *free, UINT64 size, UINT32 tag, UINT16 depth)
{
    memset(l, 0, sizeof(*l));
    l->Depth = depth ? depth : 4;
    l->MaximumDepth = 256;
    l->Size = (UINT32)size;
    l->Tag = tag ? tag : 0x6B414C6Cu;                /* 'lLAk' */
    /* as on Windows, no routine means the pool's own: the WDK's inline
     * ExAllocateFromNPagedLookasideList calls Allocate without a check */
    l->Allocate = allocate ? allocate : (void *)nt_ExAllocatePoolWithTag;
    l->Free = free ? free : (void *)nt_ExFreePool;
}

NT_API void nt_ExInitializeNPagedLookasideList(NT_LOOKASIDE_LIST *l, void *allocate, void *free,
                                               UINT64 flags, UINT64 size, UINT32 tag, UINT16 depth)
{
    (void)flags;
    lookaside_init(l, allocate, free, size, tag, depth);
    l->Type = 0;                                     /* NonPagedPool */
}

NT_API void nt_ExInitializePagedLookasideList(NT_LOOKASIDE_LIST *l, void *allocate, void *free,
                                              UINT64 flags, UINT64 size, UINT32 tag, UINT16 depth)
{
    (void)flags;
    lookaside_init(l, allocate, free, size, tag, depth);
    l->Type = 1;                                     /* PagedPool */
}

static void lookaside_drain(NT_LOOKASIDE_LIST *l)
{
    NT_SLIST_ENTRY *e;
    while ((e = nt_ExpInterlockedPopEntrySList(&l->ListHead))) {
        ((NT_FREE_FUNCTION)l->Free)(e);
    }
}

NT_API void nt_ExDeleteNPagedLookasideList(NT_LOOKASIDE_LIST *l) { lookaside_drain(l); }
NT_API void nt_ExDeletePagedLookasideList(NT_LOOKASIDE_LIST *l) { lookaside_drain(l); }

NT_API void *nt_ExAllocateFromNPagedLookasideList(NT_LOOKASIDE_LIST *l)
{
    l->TotalAllocates++;
    NT_SLIST_ENTRY *e = nt_ExpInterlockedPopEntrySList(&l->ListHead);
    if (e) return e;
    l->AllocateMisses++;
    return ((NT_ALLOCATE_FUNCTION)l->Allocate)(l->Type, l->Size, l->Tag);
}
NT_API void *nt_ExAllocateFromPagedLookasideList(NT_LOOKASIDE_LIST *l) { return nt_ExAllocateFromNPagedLookasideList(l); }

NT_API void nt_ExFreeToNPagedLookasideList(NT_LOOKASIDE_LIST *l, void *p)
{
    l->TotalFrees++;
    if (l->ListHead.Depth >= l->Depth) {
        l->FreeMisses++;
        ((NT_FREE_FUNCTION)l->Free)(p);
        return;
    }
    nt_ExpInterlockedPushEntrySList(&l->ListHead, (NT_SLIST_ENTRY *)p);
}
NT_API void nt_ExFreeToPagedLookasideList(NT_LOOKASIDE_LIST *l, void *p) { nt_ExFreeToNPagedLookasideList(l, p); }

/* -----------------------------------------------------------------------
 * ERESOURCE: a reader/writer lock
 * ----------------------------------------------------------------------- */
static KSpinLock g_eres_lock = KSPINLOCK_INIT;

NT_API NTSTATUS nt_ExInitializeResourceLite(NT_ERESOURCE *r)
{
    memset(r, 0, sizeof(*r));
    nt_KeInitializeEvent(&r->Event, 1 /* SynchronizationEvent */, 0);
    return NT_STATUS_SUCCESS;
}

NT_API UINT8 nt_ExAcquireResourceExclusiveLite(NT_ERESOURCE *r, UINT8 wait)
{
    void *me = (void *)sched_current();
    for (;;) {
        IrqState s = spin_lock_irqsave(&g_eres_lock);
        if (r->OwnerThread == me) { r->ExclusiveCount++; spin_unlock_irqrestore(&g_eres_lock, s); return 1; }
        if (!r->OwnerThread && r->SharedCount == 0) {
            r->OwnerThread = me;
            r->ExclusiveCount = 1;
            spin_unlock_irqrestore(&g_eres_lock, s);
            return 1;
        }
        spin_unlock_irqrestore(&g_eres_lock, s);
        if (!wait) return 0;
        nt_KeWaitForSingleObject(&r->Event, 0, 0, 0, NULL);
    }
}

NT_API UINT8 nt_ExAcquireResourceSharedLite(NT_ERESOURCE *r, UINT8 wait)
{
    void *me = (void *)sched_current();
    for (;;) {
        IrqState s = spin_lock_irqsave(&g_eres_lock);
        if (!r->OwnerThread || r->OwnerThread == me) {
            r->SharedCount++;
            spin_unlock_irqrestore(&g_eres_lock, s);
            return 1;
        }
        spin_unlock_irqrestore(&g_eres_lock, s);
        if (!wait) return 0;
        nt_KeWaitForSingleObject(&r->Event, 0, 0, 0, NULL);
    }
}

NT_API void nt_ExReleaseResourceLite(NT_ERESOURCE *r)
{
    IrqState s = spin_lock_irqsave(&g_eres_lock);
    if (r->OwnerThread && r->ExclusiveCount > 0) {
        if (--r->ExclusiveCount == 0) r->OwnerThread = NULL;
    } else if (r->SharedCount > 0) {
        r->SharedCount--;
    }
    spin_unlock_irqrestore(&g_eres_lock, s);
    nt_KeSetEvent(&r->Event, 0, 0);
}

NT_API void nt_ExReleaseResourceForThreadLite(NT_ERESOURCE *r, UINT64 thread) { (void)thread; nt_ExReleaseResourceLite(r); }

NT_API NTSTATUS nt_ExDeleteResourceLite(NT_ERESOURCE *r) { (void)r; return NT_STATUS_SUCCESS; }

/* -----------------------------------------------------------------------
 * Callback objects (\Callback\...): a driver registers a routine; NovaOS
 * holds the registrations but has no producer firing the system callbacks
 * yet, so a registered routine simply never runs (as on a system where
 * nothing raises that callback).
 * ----------------------------------------------------------------------- */
typedef void (NT_API *NT_CALLBACK_FUNCTION)(void *ctx, void *a1, void *a2);

typedef struct NtCallback {
    UINT32               magic;
    NT_CALLBACK_FUNCTION fn[8];
    void                *ctx[8];
    int                  count;
} NtCallback;
#define CALLBACK_MAGIC 0x6243624Eu                   /* 'NbCb' */

NT_API NTSTATUS nt_ExCreateCallback(void **out, void *attr, UINT8 create, UINT8 allow_multiple)
{
    (void)attr; (void)create; (void)allow_multiple;
    NtCallback *c = kzalloc(sizeof(*c));
    if (!c) return NT_STATUS_INSUFFICIENT_RESOURCES;
    c->magic = CALLBACK_MAGIC;
    *out = c;
    return NT_STATUS_SUCCESS;
}

NT_API void *nt_ExRegisterCallback(void *callback, void *fn, void *ctx)
{
    NtCallback *c = callback;
    if (!c || c->magic != CALLBACK_MAGIC || c->count >= 8) return NULL;
    c->fn[c->count] = (NT_CALLBACK_FUNCTION)fn;
    c->ctx[c->count] = ctx;
    c->count++;
    return (void *)c;                                /* the registration handle: the object itself */
}

NT_API void nt_ExUnregisterCallback(void *registration) { (void)registration; }

NT_API void nt_ExNotifyCallback(void *callback, void *a1, void *a2)
{
    NtCallback *c = callback;
    if (!c || c->magic != CALLBACK_MAGIC) return;
    for (int i = 0; i < c->count; i++) c->fn[i](c->ctx[i], a1, a2);
}

/* -----------------------------------------------------------------------
 * UUIDs: random ones (version 4, RFC 4122 variant) from the kernel's
 * entropy pool
 * ----------------------------------------------------------------------- */
NTSTATUS NtExUuid(UINT8 out[16])
{
    TlsEntropyOutput(out, 16);
    out[7] = (UINT8)((out[7] & 0x0F) | 0x40);        /* (GUID's Data3 is little-endian: its top nibble is in byte 7) */
    out[8] = (UINT8)((out[8] & 0x3F) | 0x80);
    return NT_STATUS_SUCCESS;
}

NT_API NTSTATUS nt_ExUuidCreate(UINT8 *uuid) { return uuid ? NtExUuid(uuid) : NT_STATUS_INVALID_PARAMETER; }
