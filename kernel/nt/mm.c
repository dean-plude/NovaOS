/*
 * mm.c — memory for drivers: MDLs, physical addresses and system routines
 *
 * An MDL describes a buffer by the physical frames under each of its
 * pages (the PFN array after the MDL).  MmProbeAndLockPages fills it for
 * a program's buffer (pages of the calling process, backed first if they
 * were committed but not touched yet) or a kernel one;
 * MmBuildMdlForNonPagedPool for pool; MmMapLockedPagesSpecifyCache maps
 * the frames at consecutive kernel addresses (vmm.c's kernel_map_frames)
 * so the driver can reach the buffer from any process, as
 * MmGetSystemAddressForMdlSafe (inline in the WDK) does through it.
 *
 * NovaOS does not count references on a program's frames, so "locked"
 * means described: a buffer freed by another of the program's threads
 * while a request on it is in flight is not kept.  MmProbeAndLockPages
 * cannot raise an exception yet (kernel-mode SEH is a later step): a page
 * it cannot use is logged and described by a spare frame instead, so the
 * driver goes on safely; the I/O manager's own direct I/O fails the
 * request with STATUS_ACCESS_VIOLATION as Windows does.
 */
#include "nt.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../arch/x86_64/paging.h"
#include "../arch/x86_64/cpu.h"
#include "../um/um.h"
#include "../ke/probe.h"

#define IoReadAccess    0
#define IoWriteAccess   1
#define IoModifyAccess  2
#define MmNonCached     0

static UINT32 span_pages(UINT64 va, UINT64 len)
{
    return (UINT32)(((va & (PAGE_SIZE - 1)) + len + PAGE_SIZE - 1) / PAGE_SIZE);
}

/* The entry for user address @va in the current address space's tables
 * (the leaf, present or not; a 2 MiB page's own entry), 0 if none */
static UINT64 user_entry(UINT64 va, bool *huge)
{
    UINT64 *t = PHYS_TO_VIRT(read_cr3() & PTE_ADDR_MASK);
    static const int shift[3] = { 39, 30, 21 };
    *huge = false;
    for (int l = 0; l < 3; l++) {
        UINT64 e = t[(va >> shift[l]) & 0x1FF];
        if (!(e & PTE_PRESENT)) return 0;
        if (l == 2 && (e & PTE_HUGE)) { *huge = true; return e; }
        t = PHYS_TO_VIRT(e & PTE_ADDR_MASK);
    }
    return t[(va >> 12) & 0x1FF];
}

/* The frame under page @va for an access (@write: the driver writes it),
 * 0 if there is none it may use */
static uintptr_t frame_of(UINT64 va, bool write, bool user)
{
    if (!user) {
        uintptr_t pa = paging_virt_to_phys(va);
        return pa ? pa & ~(uintptr_t)(PAGE_SIZE - 1) : 0;
    }
    if (!MmIsUserRange(va, PAGE_SIZE)) return 0;
    bool huge;
    UINT64 e = user_entry(va, &huge);
    if (!(e & PTE_PRESENT) && UmDemandFault(va)) e = user_entry(va, &huge);   /* (committed, not touched yet) */
    if (!(e & PTE_PRESENT) || !(e & PTE_USER) || (write && !(e & PTE_WRITE))) return 0;
    if (huge) return (e & PTE_ADDR_MASK & ~(UINT64)(HUGE_PAGE_SIZE - 1)) + (va & (HUGE_PAGE_SIZE - 1) & ~(UINT64)(PAGE_SIZE - 1));
    return e & PTE_ADDR_MASK;
}

NT_API UINT64 nt_MmSizeOfMdl(void *va, UINT64 len)
{
    return sizeof(NT_MDL) + 8ull * span_pages((UINT64)(uintptr_t)va, len);
}

NT_API NT_MDL *nt_IoAllocateMdl(void *va, UINT32 len, UINT8 secondary, UINT8 charge_quota, NT_IRP *irp)
{
    (void)charge_quota;
    UINT64 v = (UINT64)(uintptr_t)va;
    UINT32 pages = span_pages(v, len);
    NT_MDL *m = kzalloc(sizeof(NT_MDL) + 8ull * pages);
    if (!m) return NULL;
    m->Size = (short)(sizeof(NT_MDL) + 8 * pages);
    m->StartVa = (void *)(uintptr_t)(v & ~(UINT64)(PAGE_SIZE - 1));
    m->ByteOffset = (UINT32)(v & (PAGE_SIZE - 1));
    m->ByteCount = len;
    if (irp) {
        if (!secondary) irp->MdlAddress = m;
        else {
            NT_MDL *t = irp->MdlAddress;
            while (t && t->Next) t = t->Next;
            if (t) t->Next = m;
            else irp->MdlAddress = m;
        }
    }
    return m;
}

NT_API void nt_MmUnmapLockedPages(void *base, NT_MDL *m)
{
    if (!(m->MdlFlags & NT_MDL_MAPPED_TO_SYSTEM_VA) || (m->MdlFlags & NT_MDL_SOURCE_IS_NONPAGED_POOL)) return;
    (void)base;
    kernel_unmap_frames((void *)((uintptr_t)m->MappedSystemVa & ~(uintptr_t)(PAGE_SIZE - 1)),
                        span_pages((UINT64)(uintptr_t)m->StartVa + m->ByteOffset, m->ByteCount));
    m->MdlFlags &= ~NT_MDL_MAPPED_TO_SYSTEM_VA;
    m->MappedSystemVa = NULL;
}

NT_API void nt_IoFreeMdl(NT_MDL *m)
{
    if (!m) return;
    nt_MmUnmapLockedPages(m->MappedSystemVa, m);
    kfree(m);
}

/* Describe @m's pages for an access by a driver: STATUS_ACCESS_VIOLATION
 * (the pages before the bad one described) when one cannot be used */
NTSTATUS NtMdlLock(NT_MDL *m, bool user, int op)
{
    UINT64 start = (UINT64)(uintptr_t)m->StartVa;
    UINT32 pages = span_pages(start + m->ByteOffset, m->ByteCount);
    if (user && !MmIsUserRange(start + m->ByteOffset, m->ByteCount ? m->ByteCount : 1)) return STATUS_ACCESS_VIOLATION;
    unsigned long long *pfn = NtMdlPfns(m);
    for (UINT32 i = 0; i < pages; i++) {
        uintptr_t pa = frame_of(start + (UINT64)i * PAGE_SIZE, op != IoReadAccess, user);
        if (!pa) return STATUS_ACCESS_VIOLATION;
        pfn[i] = pa >> 12;
    }
    m->Process = user ? sched_current()->um_proc : NULL;
    m->MdlFlags |= NT_MDL_PAGES_LOCKED | (op != IoReadAccess ? NT_MDL_WRITE_OPERATION : 0);
    return STATUS_SUCCESS;
}

/* A frame for the pages MmProbeAndLockPages could not use (see above) */
static uintptr_t spare_frame(void)
{
    static uintptr_t pa;
    if (!pa) {
        pa = pmm_alloc_page();
        if (pa) memset(PHYS_TO_VIRT(pa), 0, PAGE_SIZE);
    }
    return pa;
}

NT_API void nt_MmProbeAndLockPages(NT_MDL *m, char mode, int op)
{
    UINT64 start = (UINT64)(uintptr_t)m->StartVa;
    bool user = mode == 1 || start < UINT64_C(0x0000800000000000);
    if (NT_SUCCESS(NtMdlLock(m, user, op))) return;
    kprintf("[NT] MmProbeAndLockPages: %p (%u bytes) cannot be %s: STATUS_ACCESS_VIOLATION is not raised yet "
            "(kernel-mode SEH), the bad pages read as zeros\n", (void *)(uintptr_t)(start + m->ByteOffset), m->ByteCount,
            op == IoReadAccess ? "read" : "written");
    UINT32 pages = span_pages(start + m->ByteOffset, m->ByteCount);
    unsigned long long *pfn = NtMdlPfns(m);
    for (UINT32 i = 0; i < pages; i++) {
        uintptr_t pa = frame_of(start + (UINT64)i * PAGE_SIZE, op != IoReadAccess, user);
        pfn[i] = (pa ? pa : spare_frame()) >> 12;
    }
    m->MdlFlags |= NT_MDL_PAGES_LOCKED;
}

NT_API void nt_MmUnlockPages(NT_MDL *m)
{
    nt_MmUnmapLockedPages(m->MappedSystemVa, m);
    m->MdlFlags &= ~(NT_MDL_PAGES_LOCKED | NT_MDL_WRITE_OPERATION);
}

NT_API void nt_MmBuildMdlForNonPagedPool(NT_MDL *m)
{
    UINT64 start = (UINT64)(uintptr_t)m->StartVa;
    UINT32 pages = span_pages(start + m->ByteOffset, m->ByteCount);
    unsigned long long *pfn = NtMdlPfns(m);
    for (UINT32 i = 0; i < pages; i++) pfn[i] = paging_virt_to_phys(start + (UINT64)i * PAGE_SIZE) >> 12;
    m->MappedSystemVa = (void *)(uintptr_t)(start + m->ByteOffset);
    m->MdlFlags |= NT_MDL_SOURCE_IS_NONPAGED_POOL;
}

NT_API void *nt_MmMapLockedPagesSpecifyCache(NT_MDL *m, char mode, int cache, void *requested, UINT32 bugcheck,
                                            UINT32 priority)
{
    (void)requested; (void)priority;
    if (m->MdlFlags & (NT_MDL_MAPPED_TO_SYSTEM_VA | NT_MDL_SOURCE_IS_NONPAGED_POOL)) return m->MappedSystemVa;
    if (mode != 0) {
        kprintf("[NT] MmMapLockedPagesSpecifyCache: mapping into a program (UserMode) is not supported yet\n");
        return NULL;
    }
    UINT32 pages = span_pages((UINT64)(uintptr_t)m->StartVa + m->ByteOffset, m->ByteCount);
    uintptr_t stack_pa[16], *pa = pages <= 16 ? stack_pa : kmalloc(sizeof(uintptr_t) * pages);
    void *va = NULL;
    if (pa) {
        for (UINT32 i = 0; i < pages; i++) pa[i] = (uintptr_t)NtMdlPfns(m)[i] << 12;
        va = kernel_map_frames(pa, pages, cache == MmNonCached);
        if (pa != stack_pa) kfree(pa);
    }
    if (!va) {
        if (bugcheck & 0xFF) KPANIC("*** STOP: 0x000000D0 (NO_MORE_SYSTEM_PTES): %u pages for an MDL", pages);
        return NULL;
    }
    m->MappedSystemVa = (UINT8 *)va + m->ByteOffset;
    m->MdlFlags |= NT_MDL_MAPPED_TO_SYSTEM_VA;
    return m->MappedSystemVa;
}

NT_API void *nt_MmMapLockedPages(NT_MDL *m, char mode)
{
    return nt_MmMapLockedPagesSpecifyCache(m, mode, 1, NULL, 1, 16);
}

/* PHYSICAL_ADDRESS (a LARGE_INTEGER, returned in RAX) */
NT_API UINT64 nt_MmGetPhysicalAddress(void *va)
{
    UINT64 v = (UINT64)(uintptr_t)va;
    if (v >= UINT64_C(0x0000800000000000)) return paging_virt_to_phys(v);
    bool huge;
    UINT64 e = user_entry(v, &huge);
    if (!(e & PTE_PRESENT)) return 0;
    return huge ? (e & PTE_ADDR_MASK & ~(UINT64)(HUGE_PAGE_SIZE - 1)) + (v & (HUGE_PAGE_SIZE - 1))
                : (e & PTE_ADDR_MASK) + (v & (PAGE_SIZE - 1));
}

NT_API UINT8 nt_MmIsAddressValid(void *va) { return nt_MmGetPhysicalAddress(va) != 0; }

/* MmCopyMemory: copy from a physical address (MM_COPY_MEMORY_PHYSICAL) or
 * a virtual one (MM_COPY_MEMORY_VIRTUAL; below the kernel half, the
 * current process's) into the driver's buffer without faulting on a page
 * that is not there.  An anti-cheat driver reads memory this way.  Only
 * RAM is read: a physical address outside the firmware's RAM, or a kernel
 * address mapped over device memory, ends the copy (reading MMIO could
 * set off the device); so does an unmapped page.  As on Windows, what was
 * copied is counted and a short copy is STATUS_PARTIAL_COPY, none at all
 * an error */
#define MM_COPY_MEMORY_PHYSICAL  1
#define MM_COPY_MEMORY_VIRTUAL   2
#define NT_STATUS_PARTIAL_COPY   ((NT_STATUS)0x8000000D)

NT_API NTSTATUS nt_MmCopyMemory(void *target, UINT64 source, UINT64 n, UINT32 flags, UINT64 *done)
{
    if (done) *done = 0;
    if (!target || !done || (flags != MM_COPY_MEMORY_PHYSICAL && flags != MM_COPY_MEMORY_VIRTUAL))
        return NT_STATUS_INVALID_PARAMETER;
    UINT64 copied = 0;
    while (copied < n) {
        UINT64 at = source + copied, chunk = PAGE_SIZE - (at & (PAGE_SIZE - 1));
        if (chunk > n - copied) chunk = n - copied;
        UINT8 *to = (UINT8 *)target + copied;
        if (flags == MM_COPY_MEMORY_PHYSICAL) {
            if (!pmm_is_ram(at, chunk)) break;
            memcpy(to, PHYS_TO_VIRT(at), chunk);
        } else if (at >= UINT64_C(0x0000800000000000)) {
            UINT64 pa = paging_virt_to_phys(at);
            if (!pa || !pmm_is_ram(pa, chunk)) break;
            memcpy(to, (const void *)(uintptr_t)at, chunk);
        } else if (!NT_SUCCESS(CopyFromUser(to, (const void *)(uintptr_t)at, chunk))) {
            break;
        }
        copied += chunk;
    }
    *done = copied;
    return copied == n ? NT_STATUS_SUCCESS : copied ? NT_STATUS_PARTIAL_COPY : NT_STATUS_ACCESS_VIOLATION;
}

/* An export by name (drivers look up what newer Windows versions added) */
NT_API void *nt_MmGetSystemRoutineAddress(NT_UNICODE_STRING *name)
{
    char n[96];
    if (!name || !name->Buffer || name->Length / 2 >= sizeof(n)) return NULL;
    int len = name->Length / 2;
    for (int i = 0; i < len; i++) n[i] = name->Buffer[i] < 0x80 ? (char)name->Buffer[i] : '?';
    n[len] = 0;
    UINT64 fn = NtExport("ntoskrnl.exe", n);
    if (!fn) fn = NtExport("hal.dll", n);
    return (void *)(uintptr_t)fn;
}

/* -----------------------------------------------------------------------
 * Pageable code and data sections.  A NovaOS driver's image and the pool
 * are never paged out, so locking one in memory is nothing to do: the
 * handle a driver passes to MmUnlockPagableImageSection is just the
 * address it locked.
 * ----------------------------------------------------------------------- */
NT_API void *nt_MmLockPagableDataSection(void *addr) { return addr; }
NT_API void *nt_MmLockPagableCodeSection(void *addr) { return addr; }
NT_API void  nt_MmLockPagableSectionByHandle(void *handle) { (void)handle; }
NT_API void  nt_MmUnlockPagableImageSection(void *handle) { (void)handle; }

/* Driver Verifier is not implemented, so no driver is being verified */
NT_API UINT8 nt_MmIsDriverVerifying(void *driver) { (void)driver; return 0; }

/* -----------------------------------------------------------------------
 * IoBuildPartialMdl: describe a subrange [va, va+len) of @src's buffer in
 * @dst (already allocated for the range), copying the frames under it
 * ----------------------------------------------------------------------- */
NT_API void nt_IoBuildPartialMdl(NT_MDL *src, NT_MDL *dst, void *va, UINT32 len)
{
    UINT64 src_base = (UINT64)(uintptr_t)src->StartVa + src->ByteOffset;
    UINT64 want = (UINT64)(uintptr_t)va;
    if (want < src_base) want = src_base;
    UINT64 src_end = src_base + src->ByteCount;
    if (!len || want + len > src_end) len = (UINT32)(src_end > want ? src_end - want : 0);
    dst->StartVa    = (void *)(uintptr_t)(want & ~(UINT64)(PAGE_SIZE - 1));
    dst->ByteOffset = (UINT32)(want & (PAGE_SIZE - 1));
    dst->ByteCount  = len;
    dst->MdlFlags  |= NT_MDL_PARTIAL | (src->MdlFlags & NT_MDL_PAGES_LOCKED);
    UINT32 first = (UINT32)((want >> 12) - ((UINT64)(uintptr_t)src->StartVa >> 12));
    UINT32 pages = span_pages(want, len);
    unsigned long long *s = NtMdlPfns(src), *d = NtMdlPfns(dst);
    for (UINT32 i = 0; i < pages; i++) d[i] = s[first + i];
}

/* MmQuerySystemSize: MmLargeSystem (2), which every machine NovaOS runs on is */
NT_API int nt_MmQuerySystemSize(void) { return 2; }
