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
#include "../ke/irql.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

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
    if (check_tag && (tag & 0x7FFFFFFF) != (b->tag & 0x7FFFFFFF)) {   /* (bit 31: PROTECTED_POOL) */
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
