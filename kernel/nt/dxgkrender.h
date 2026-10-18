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
} DxgkRenderInfo;

int    DxgkRenderList(DxgkRenderInfo *out, int max);
bool   DxgkRenderFind(UINT64 luid, DxgkRenderInfo *out);
/* DxgkDdiCreateDevice: dxgkrnl's handle for the device, 0 if it failed */
UINT32 DxgkRenderCreateDevice(UINT64 luid);
/* DxgkDdiDestroyDevice, after its allocations */
void   DxgkRenderDestroyDevice(UINT64 luid, UINT32 dev);
/* DxgkDdiCreateAllocation with the user-mode driver's @priv, placed in a
 * segment and opened on @dev; NTSTATUS, and *size (whole pages) and
 * *alloc on success */
UINT32 DxgkRenderCreateAllocation(UINT64 luid, UINT32 dev, const void *priv, UINT32 priv_size, UINT64 *size,
                                  UINT32 *alloc);
void   DxgkRenderDestroyAllocation(UINT64 luid, UINT32 alloc);
