/*
 * ntapi.h — the exports the export layer's own files call one another
 * through (not for exports.c, which declares every export by name only)
 */
#pragma once
#include "nt.h"
NT_API void      nt_KeInitializeEvent(NT_KEVENT *ev, int type, UINT8 state);
NT_API int       nt_KeSetEvent(NT_KEVENT *ev, int increment, UINT8 wait);
NT_API int       nt_KeReadStateEvent(NT_KEVENT *ev);
NT_API NT_STATUS nt_KeWaitForSingleObject(void *obj, int reason, char mode, UINT8 alertable, const INT64 *timeout);
NT_API UINT8     nt_KeAcquireSpinLockRaiseToDpc(NT_KSPIN_LOCK *l);
NT_API void      nt_KeReleaseSpinLock(NT_KSPIN_LOCK *l, UINT8 old);
NT_API NT_MDL   *nt_IoAllocateMdl(void *va, UINT32 len, UINT8 secondary, UINT8 charge_quota, NT_IRP *irp);
NT_API void      nt_IoFreeMdl(NT_MDL *m);
NT_API void      nt_MmUnlockPages(NT_MDL *m);
