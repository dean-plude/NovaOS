/*
 * dxgkrender.h — dxgkrnl.c's render adapters (the WDDM render miniports
 * that started), for the D3DKMT* calls (um/um_d3dkmt.c)
 *
 * Each adapter is named by its LUID (its LowPart; HighPart 0), its devices
 * and allocations by dxgkrnl's handles: a call on an adapter that has gone
 * fails, or does nothing, rather than reach a driver that stopped.
 */
#pragma once

#include "../include/types.h"

typedef struct {
    UINT64 luid;
    char   service[32];
    UINT32 wddm;                /* DXGK_WDDMVERSION: 0x2000 is WDDM 2.0 */
    UINT64 memory;              /* its segments' bytes */
    bool   system;              /* all of it system memory (no VRAM) */
    UINT32 va_bits;             /* its GPU virtual addresses' bits; 0: none (no GpuMmu) */
} DxgkRenderInfo;

int    DxgkRenderList(DxgkRenderInfo *out, int max);
bool   DxgkRenderFind(UINT64 luid, DxgkRenderInfo *out);
/* DxgkDdiCreateDevice for process @owner (its first device on the adapter
 * also makes its driver process object, DxgkDdiCreateProcess, and its GPU
 * address space): dxgkrnl's handle for the device, 0 if it failed */
UINT32 DxgkRenderCreateDevice(UINT64 luid, void *owner);
/* DxgkDdiDestroyDevice, after its allocations */
void   DxgkRenderDestroyDevice(UINT64 luid, UINT32 dev);
/* DxgkDdiCreateAllocation with the user-mode driver's @priv, placed in a
 * segment and opened on @dev; NTSTATUS, and *size (whole pages) and
 * *alloc on success */
UINT32 DxgkRenderCreateAllocation(UINT64 luid, UINT32 dev, const void *priv, UINT32 priv_size, UINT64 *size,
                                  UINT32 *alloc);
void   DxgkRenderDestroyAllocation(UINT64 luid, UINT32 alloc);

/* @owner's GPU virtual addresses on the adapter (the address space its
 * devices share), each NTSTATUS.  Reserve: @size bytes at @base, or where
 * VidMm finds room in [@min, @max).  Map: @pages 4 KiB pages of allocation
 * @alloc (its pages @frames, from page @first) at @base (in a reserved
 * range, or a free one) or where VidMm finds room; @prot is
 * D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE (bit 0 Write, 1 Execute).  The
 * driver writes the page table entries.  Free: the ranges within [@base,
 * +@size), unmapped. */
UINT32 DxgkRenderReserveVa(UINT64 luid, void *owner, UINT64 base, UINT64 min, UINT64 max, UINT64 size, UINT64 *va);
UINT32 DxgkRenderMapVa(UINT64 luid, void *owner, UINT32 alloc, const PADDR *frames, UINT64 first, UINT64 pages,
                       UINT64 base, UINT64 min, UINT64 max, UINT64 prot, UINT64 *va);
UINT32 DxgkRenderFreeVa(UINT64 luid, void *owner, UINT64 base, UINT64 size);
