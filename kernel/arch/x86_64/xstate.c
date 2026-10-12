/*
 * xstate.c — turning on XSAVE and the AVX state (xstate.h)
 */
#include "xstate.h"
#include "cpu.h"
#include "../../ke/printf.h"

uint64_t g_xstate_mask;
uint32_t g_xstate_size = XSTATE_LEGACY;
uint32_t g_xstate_avx_offset;

void xstate_init(void)
{
    CpuidResult id = cpuid(1, 0);
    if (!(id.ecx & (1u << 26)) || cpuid(0, 0).eax < 0xD) return;          /* no XSAVE */
    uint64_t can = cpuid(0xD, 0).eax | ((uint64_t)cpuid(0xD, 0).edx << 32);
    uint64_t mask = XSTATE_X87 | XSTATE_SSE;
    CpuidResult avx = cpuid(0xD, 2);                                       /* (the standard place: 576, 256 bytes) */
    if ((id.ecx & (1u << 28)) && (can & XSTATE_AVX) && avx.ebx == 576 && avx.eax == 256)
        mask |= XSTATE_AVX;                                                /* AVX (F16C, FMA, AVX2 use it) */
    write_cr4(read_cr4() | CR4_OSXSAVE);
    __asm__ volatile ("xsetbv" : : "c"(0), "a"((uint32_t)mask), "d"((uint32_t)(mask >> 32)));
    uint32_t size = cpuid(0xD, 0).ebx;                                     /* for the features now in XCR0 */
    if (size > PAGE_SIZE) {                                                /* (a thread's area is a page) */
        mask = XSTATE_X87 | XSTATE_SSE;
        __asm__ volatile ("xsetbv" : : "c"(0), "a"((uint32_t)mask), "d"(0));
        size = cpuid(0xD, 0).ebx;
    }
    g_xstate_mask = mask;
    g_xstate_size = size;
    if (mask & XSTATE_AVX) g_xstate_avx_offset = avx.ebx;
    kprintf("[CPU] XSAVE: XCR0 0x%llx (%s), %u-byte thread state\n", (unsigned long long)mask,
            !(mask & XSTATE_AVX) ? "x87, SSE" : (id.ecx & (1u << 29)) ? "x87, SSE, AVX; F16C" : "x87, SSE, AVX",
            (unsigned)size);
}
