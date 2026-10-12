/*
 * irql.h — the interrupt request level, as Windows x64 keeps it: in CR8
 *
 * Windows drivers raise and lower the IRQL by writing CR8 directly (the
 * WDK's KeRaiseIrql and KeLowerIrql are inline there), so NovaOS keeps
 * the IRQL where they put it.  CR8 is the local APIC's task priority: at
 * DISPATCH_LEVEL (2) the APIC holds back vectors 0x00-0x2F, which is
 * where the DPC interrupt (VECTOR_DPC) is, while device interrupts and
 * the timer (0x30 up) still arrive.  The scheduler never switches a
 * thread out at DISPATCH_LEVEL or above: it asks for the DPC interrupt,
 * which the APIC delivers when the IRQL drops, and switches then.
 */
#pragma once
#include "../include/types.h"

#define PASSIVE_LEVEL   0
#define APC_LEVEL       1
#define DISPATCH_LEVEL  2
#define HIGH_LEVEL      15

/* The DPC interrupt: Windows' own vector for it, in the APIC's priority class 2 */
#define VECTOR_DPC      0x2F

static inline UINT8 KeIrql(void)
{
    UINT64 v;
    __asm__ volatile ("mov %%cr8, %0" : "=r"(v));
    return (UINT8)v;
}

static inline void KeIrqlSet(UINT8 irql)
{
    __asm__ volatile ("mov %0, %%cr8" : : "r"((UINT64)irql) : "memory");
}

/* nt/ke.c: VECTOR_DPC (interrupt context): run this CPU's queued DPCs, then
 * the switch the scheduler put off */
void KiDispatchInterrupt(void);
/* Ask for VECTOR_DPC on this CPU: it arrives once the IRQL is below
 * DISPATCH_LEVEL and interrupts are on */
void KiRequestDispatchInterrupt(void);
/* The clock tick (sched_tick, one CPU): driver timers that are due */
void KiTimerTick(UINT64 tick);
