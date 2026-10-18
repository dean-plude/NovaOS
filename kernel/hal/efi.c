/*
 * efi.c — the firmware's UEFI runtime services, in physical mode (see efi.h)
 */

#include "efi.h"
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/paging.h"
#include "../arch/x86_64/xstate.h"
#include "../ke/spinlock.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "../mm/pmm.h"

const UINT8 EFI_GLOBAL_GUID[16] = { 0x61, 0xDF, 0xE4, 0x8B, 0xCA, 0x93, 0xD2, 0x11,
                                    0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C };

typedef UINT64 (__attribute__((ms_abi)) *EfiGetVarFn)(const UINT16 *name, const UINT8 *vendor, UINT32 *attrs,
                                                     UINT64 *size, void *data);
typedef UINT64 (__attribute__((ms_abi)) *EfiSetVarFn)(const UINT16 *name, const UINT8 *vendor, UINT32 attrs,
                                                     UINT64 size, const void *data);

/* EFI_RUNTIME_SERVICES as far as the variable services (UEFI 2.10, 4.5) */
typedef struct {
    UINT64 Signature;
    UINT32 Revision, HeaderSize, CRC32, Reserved;
    UINT64 GetTime, SetTime, GetWakeupTime, SetWakeupTime, SetVirtualAddressMap, ConvertPointer;
    UINT64 GetVariable, GetNextVariableName, SetVariable;
} EfiRuntimeTable;

#define EFI_RT_SIGNATURE 0x56524553544e5552ull     /* "RUNTSERV" */
#define GIB              (1ull << 30)
#define IDENT_MAX        (512 * GIB)               /* (one PML4 entry) */

static UINT64     g_rt_phys;                       /* the table, physical */
static UINT64     g_ident_top;                     /* bytes mapped one to one */
static pte_t     *g_ident_pml4;                    /* (through the physmap) */
static UINT8     *g_fpu;                           /* the caller's state during a call */
static KSpinLock  g_lock = KSPINLOCK_INIT;

void EfiBootInfo(const BootInfo *info)
{
    if (!info || info->version < 6 || !info->efi_runtime) return;
    const EfiRuntimeTable *rt = PHYS_TO_VIRT(info->efi_runtime);
    if (rt->Signature != EFI_RT_SIGNATURE || !rt->GetVariable || !rt->SetVariable) {
        kprintf("[EFI] The runtime services table at %p is not one\n", (void *)info->efi_runtime);
        return;
    }
    /* Everything below 4 GiB (the firmware's flash and its own regions on
     * every PC), and any runtime region or MMIO the firmware lists above */
    UINT64 top = 4 * GIB;
    const BootMemDescriptor *m = PHYS_TO_VIRT(info->mem_map);
    for (UINT32 i = 0; i < info->mem_map_count; i++)
        if (m[i].type == BOOT_MEM_RUNTIME || m[i].type == BOOT_MEM_MMIO) {
            UINT64 end = m[i].physical_base + (m[i].num_pages << 12);
            if (end > top) top = end;
        }
    if (top > IDENT_MAX) top = IDENT_MAX;
    g_ident_top = (top + GIB - 1) & ~(GIB - 1);
    g_rt_phys = info->efi_runtime;
    kprintf("[EFI] Runtime services: UEFI %u.%u, variables (physical mode, %u GiB mapped for the calls)\n",
            rt->Revision >> 16, (rt->Revision & 0xFFFF) / 10, (unsigned)(g_ident_top / GIB));
}

bool EfiRuntimeAvailable(void) { return g_rt_phys != 0; }

/* The one-to-one page tables: 2 MiB pages, built on the first call */
static bool ident_tables(void)
{
    if (g_ident_pml4) return true;
    uintptr_t pml4 = pmm_alloc_page(), pdpt = pmm_alloc_page(), fpu = pmm_alloc_page();
    if (!pml4 || !pdpt || !fpu) return false;
    pte_t *l4 = PHYS_TO_VIRT(pml4), *l3 = PHYS_TO_VIRT(pdpt);
    memset(l4, 0, PAGE_SIZE);
    memset(l3, 0, PAGE_SIZE);
    for (UINT64 gb = 0; gb < g_ident_top / GIB; gb++) {
        uintptr_t pd = pmm_alloc_page();
        if (!pd) return false;
        pte_t *l2 = PHYS_TO_VIRT(pd);
        for (UINT64 i = 0; i < 512; i++)
            l2[i] = (gb * GIB + (i << 21)) | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
        l3[gb] = pd | PTE_PRESENT | PTE_WRITE;
    }
    l4[0] = pdpt | PTE_PRESENT | PTE_WRITE;
    g_fpu = PHYS_TO_VIRT(fpu);
    memset(g_fpu, 0, PAGE_SIZE);                   /* (XRSTOR wants the header's reserved bytes zero) */
    g_ident_pml4 = l4;
    return true;
}

/* Run @fn(a..e) as a runtime service: the one-to-one tables with the
 * kernel's half copied in, interrupts off, the FPU state kept */
static UINT64 rt_call(int which, const UINT16 *name, const UINT8 *vendor, UINT64 a, UINT64 b, const void *data)
{
    if (!g_rt_phys) return EFI_UNSUPPORTED_ERR;
    IrqState s = spin_lock_irqsave(&g_lock);
    if (!ident_tables()) {
        spin_unlock_irqrestore(&g_lock, s);
        return EFI_ERR(9);                         /* EFI_OUT_OF_RESOURCES */
    }
    const EfiRuntimeTable *rt = PHYS_TO_VIRT(g_rt_phys);
    UINT64 old = read_cr3();
    const pte_t *cur = PHYS_TO_VIRT(old & PTE_ADDR_MASK);
    for (int i = 256; i < 512; i++) g_ident_pml4[i] = cur[i];
    xstate_save(g_fpu);
    write_cr3(VIRT_TO_PHYS(g_ident_pml4));
    UINT64 st;
    if (which == 0)
        st = ((EfiGetVarFn)rt->GetVariable)(name, vendor, (UINT32 *)a, (UINT64 *)b, (void *)data);
    else
        st = ((EfiSetVarFn)rt->SetVariable)(name, vendor, (UINT32)a, b, data);
    write_cr3(old);
    xstate_restore(g_fpu);
    spin_unlock_irqrestore(&g_lock, s);
    return st;
}

UINT64 EfiGetVariable(const UINT16 *name, const UINT8 vendor[16], UINT32 *attrs, UINT64 *size, void *data)
{
    UINT32 dummy;
    return rt_call(0, name, vendor, (UINT64)(attrs ? attrs : &dummy), (UINT64)size, data);
}

UINT64 EfiSetVariable(const UINT16 *name, const UINT8 vendor[16], UINT32 attrs, UINT64 size, const void *data)
{
    return rt_call(1, name, vendor, attrs, size, data);
}
