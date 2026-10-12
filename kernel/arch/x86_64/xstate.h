/*
 * xstate.h — the processor's extended state (XSAVE): x87, SSE and AVX
 *
 * When the CPU has XSAVE, xstate_init (CPU 0, before the others start;
 * smp.c gives them the same CR4 and XCR0) sets CR4.OSXSAVE and XCR0 to the
 * x87, SSE and, where the CPU has it, AVX state, as Windows does: CPUID
 * then shows OSXSAVE to programs, XGETBV says the YMM registers are
 * saved, and programs take their AVX and F16C code paths.  A thread's
 * state is then saved with XSAVE into its page-sized, page-aligned area
 * (the legacy FXSAVE image comes first in either format).  Without XSAVE
 * the kernel keeps FXSAVE and the legacy state only.
 */
#pragma once

#include "../../include/types.h"

#define XSTATE_X87      (1ull << 0)
#define XSTATE_SSE      (1ull << 1)
#define XSTATE_AVX      (1ull << 2)
#define XSTATE_LEGACY   512u                    /* the FXSAVE image */
#define XSTATE_HEADER   64u                     /* XSTATE_BV, XCOMP_BV, reserved */

extern uint64_t g_xstate_mask;                  /* XCR0; 0: no XSAVE, FXSAVE only */
extern uint32_t g_xstate_size;                  /* bytes XSAVE writes (512 with FXSAVE) */
extern uint32_t g_xstate_avx_offset;            /* the YMM upper halves in the area (0: no AVX) */

void xstate_init(void);

/* Save / load the current thread's state; @area 64-byte aligned */
static inline void xstate_save(void *area)
{
    if (g_xstate_mask)
        __asm__ volatile ("xsave64 (%0)" : : "r"(area), "a"((uint32_t)g_xstate_mask),
                          "d"((uint32_t)(g_xstate_mask >> 32)) : "memory");
    else
        __asm__ volatile ("fxsave64 (%0)" : : "r"(area) : "memory");
}

static inline void xstate_restore(const void *area)
{
    if (g_xstate_mask)
        __asm__ volatile ("xrstor64 (%0)" : : "r"(area), "a"((uint32_t)g_xstate_mask),
                          "d"((uint32_t)(g_xstate_mask >> 32)) : "memory");
    else
        __asm__ volatile ("fxrstor64 (%0)" : : "r"(area) : "memory");
}
