/*
 * ntapi.h — the exports the export layer's own files call one another
 * through (not for exports.c, which declares every export by name only)
 */
#pragma once
#include "nt.h"
NT_API void      nt_KeInitializeEvent(NT_KEVENT *ev, int type, UINT8 state);
NT_API int       nt_KeSetEvent(NT_KEVENT *ev, int increment, UINT8 wait);
NT_API int       nt_KeReadStateEvent(NT_KEVENT *ev);
NT_API void      nt_KeInitializeMutex(NT_KMUTEX *m, UINT32 level);
NT_API int       nt_KeReleaseMutex(NT_KMUTEX *m, UINT8 wait);
NT_API NT_STATUS nt_KeWaitForSingleObject(void *obj, int reason, char mode, UINT8 alertable, const INT64 *timeout);
NT_API UINT8     nt_KeAcquireSpinLockRaiseToDpc(NT_KSPIN_LOCK *l);
NT_API void      nt_KeReleaseSpinLock(NT_KSPIN_LOCK *l, UINT8 old);
NT_API NT_MDL   *nt_IoAllocateMdl(void *va, UINT32 len, UINT8 secondary, UINT8 charge_quota, NT_IRP *irp);
NT_API void      nt_IoFreeMdl(NT_MDL *m);
NT_API void      nt_MmUnlockPages(NT_MDL *m);
NT_API void      nt_KeAcquireSpinLockAtDpcLevel(NT_KSPIN_LOCK *l);
NT_API void      nt_KeReleaseSpinLockFromDpcLevel(NT_KSPIN_LOCK *l);
NT_API void     *nt_ExAllocatePoolWithTag(int type, UINT64 size, UINT32 tag);
NT_API void      nt_ExFreePool(void *p);
NT_API NT_STATUS nt_IoCreateDevice(NT_DRIVER_OBJECT *drv, unsigned int ext_size, NT_UNICODE_STRING *name,
                                   unsigned int type, unsigned int characteristics, unsigned char exclusive,
                                   NT_DEVICE_OBJECT **out);
NT_API NT_STATUS nt_IoCreateSymbolicLink(NT_UNICODE_STRING *link, NT_UNICODE_STRING *target);
NT_API NT_STATUS nt_IoDeleteSymbolicLink(NT_UNICODE_STRING *link);
NT_API NT_STATUS nt_IofCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp);
NT_API void      nt_IofCompleteRequest(NT_IRP *irp, char boost);
NT_API NT_DEVICE_OBJECT *nt_IoGetAttachedDevice(NT_DEVICE_OBJECT *d);
NT_API void      nt_IoDetachDevice(NT_DEVICE_OBJECT *target);
NT_API NT_DMA_ADAPTER *nt_IoGetDmaAdapter(NT_DEVICE_OBJECT *pdo, NT_DEVICE_DESCRIPTION *d, UINT32 *regs);
