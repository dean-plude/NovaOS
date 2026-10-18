/*
 * idle.h — processor idle states (C-states) and the idle CPU's tick
 *
 * An idle CPU (cpu_idle_wait, smp.c) waits in the deepest C-state its
 * firmware describes that suits how long it will be idle: ACPI's _CST
 * (hal/cstate.c reads it), else the MWAIT sub-states the CPU enumerates
 * (CPUID leaf 5), else HLT.  CPUs other than the boot CPU also stop their
 * 10 ms tick while idle: the timer is armed for the next thing due on that
 * CPU instead (scheduler.c, sched_idle_stop_tick).  See idle.c.
 */

#pragma once

#include "../include/types.h"

#define IDLE_MAX_STATES 8

typedef enum { IDLE_HLT, IDLE_MWAIT, IDLE_IO } IdleEntry;

typedef struct {
    IdleEntry entry;
    UINT8     type;             /* ACPI C-state type: 1 (C1), 2 (C2), 3 (C3 and deeper) */
    UINT32    arg;              /* IDLE_MWAIT: the hint (EAX); IDLE_IO: the port to read */
    UINT32    latency_us;       /* worst-case exit latency */
    UINT32    power_mw;         /* average power, as _CST gives it (0: unknown) */
} IdleState;

/* The boot CPU, once: what the CPU itself offers (MWAIT, its sub-states,
 * an APIC timer and TSC that keep running in deep states).  Without
 * firmware tables, CPUs use the MWAIT sub-states CPUID leaf 5 enumerates,
 * else HLT. */
void IdleInit(void);

/* Whether MWAIT is usable and, for a hint, whether CPUID enumerates its
 * C-state and sub-state (true where CPUID enumerates nothing) */
bool IdleMwaitOk(UINT32 hint);

/* CPU @cpu's C-states from its firmware (_CST), shallowest first.  States
 * the CPU cannot enter safely are dropped (deep ones without ARAT or an
 * invariant TSC, outside a virtual machine).  Any thread, any time: the CPU switches at its next idle. */
void IdleSetStates(UINT32 cpu, const IdleState *s, int n, const char *source);

/* cpu_idle_wait, interrupts off, this CPU's Idle flag up and no work
 * waiting: wait in a C-state until an interrupt (handled before this
 * returns, with interrupts off again) or a kick */
void IdleEnter(void);

/* Every interrupt, first (interrupt_dispatch), and IdleEnter's return:
 * charge the time waited to the state, and restart a stopped tick */
void IdleWake(void);

/* Per-CPU statistics: entries and time (100 ns) in ACPI C-state types 1-3
 * (SystemProcessorIdleInformation), the number of states and the one
 * entered last (ProcessorInformation's MaxIdleState, CurrentIdleState),
 * and how often the CPU stopped its tick */
typedef struct {
    UINT64 time[4];             /* [1..3]: per type */
    UINT32 count[4];
    UINT32 states, last, tick_stops;
} IdleStats;
void IdleGetStats(UINT32 cpu, IdleStats *out);

/* The scheduler: this CPU stopped its tick for an idle period */
void IdleCountTickStop(UINT32 cpu);
