/*
 * throttle.h — processor throttling (ACPI T-states) for passive cooling
 *
 * The ACPI thread (aml.c) finds the processors and their throttling
 * controls, and its thermal zones ask for less performance while they are
 * at or above their passive trip point; see throttle.c.
 */

#pragma once

#include "../include/types.h"

/* Find the processors (Processor objects and ACPI0007 devices) and how
 * each is throttled: _PTC/_TSS/_TPC, else P_CNT in its P_BLK.  Runs AML:
 * the acpi thread only, after the namespace is initialized. */
void ThrottleProbe(void);

/* Thermal zone @zone (0..7) asks the processors @procs (uACPI namespace
 * nodes, its _PSL; @n = 0: every processor) to run at @perf percent of
 * full performance (100: no throttling).  Each processor takes the lowest
 * any zone asks for, within its _TPC limit.  The acpi thread only. */
void ThrottlePassive(int zone, void *const *procs, int n, UINT32 perf);

/* The CPU (g_kpcr index) of processor @node (a uACPI namespace node); -1
 * if the MADT has none for it.  The acpi thread only. */
int ThrottleCpuOf(void *node);

/* After waking from S3: the throttling registers lost their states */
void ThrottleResume(void);

/* CPU @cpu's performance as throttled, in percent (100: not throttled):
 * what NtPowerInformation(ProcessorInformation) reports as MhzLimit */
UINT32 ThrottlePercent(UINT32 cpu);

/* Each CPU's timer tick: write its clock modulation MSR when a processor
 * throttled through FFixedHW (IA32_CLOCK_MODULATION) changed state */
void ThrottleTick(void);
