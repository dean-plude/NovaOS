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
/* (ndis.c's) */
NT_API UINT8     nt_KfRaiseIrql(UINT8 irql);
NT_API void      nt_KeLowerIrql(UINT8 irql);
NT_API void      nt_KeInitializeDpc(NT_KDPC *dpc, NT_KDEFERRED_ROUTINE routine, void *ctx);
NT_API UINT8     nt_KeInsertQueueDpc(NT_KDPC *dpc, void *a1, void *a2);
NT_API NT_STATUS nt_KeDelayExecutionThread(char mode, UINT8 alertable, const INT64 *interval);
NT_API void      nt_KeClearEvent(NT_KEVENT *ev);
NT_API void      nt_KeStallExecutionProcessor(UINT32 us);
NT_API UINT32    nt_KeQueryActiveProcessorCountEx(UINT16 group);
NT_API UINT32    nt_KeGetProcessorIndexFromNumber(const void *number);
NT_API NT_STATUS nt_IoConnectInterruptEx(NT_IO_CONNECT_INTERRUPT_PARAMETERS *p);
NT_API void      nt_IoDisconnectInterruptEx(NT_IO_DISCONNECT_INTERRUPT_PARAMETERS *p);
NT_API UINT8     nt_KeSynchronizeExecution(void *obj, NT_KSYNCHRONIZE_ROUTINE fn, void *ctx);
NT_API void     *nt_MmMapIoSpace(INT64 pa, UINT64 size, int cache);
NT_API void      nt_MmUnmapIoSpace(void *va, UINT64 size);
NT_API void     *nt_MmAllocateContiguousMemorySpecifyCache(UINT64 size, INT64 low, INT64 high, INT64 boundary, int cache);
NT_API void      nt_MmFreeContiguousMemory(void *va);
NT_API UINT64    nt_MmGetPhysicalAddress(void *va);
NT_API void      nt_MmBuildMdlForNonPagedPool(NT_MDL *m);
NT_API NT_DEVICE_OBJECT *nt_IoAttachDeviceToDeviceStack(NT_DEVICE_OBJECT *src, NT_DEVICE_OBJECT *target);
NT_API void      nt_IoDeleteDevice(NT_DEVICE_OBJECT *d);
NT_API NT_IO_WORKITEM *nt_IoAllocateWorkItem(NT_DEVICE_OBJECT *dev);
NT_API void      nt_IoQueueWorkItem(NT_IO_WORKITEM *w, NT_IO_WORKITEM_ROUTINE routine, int queue_type, void *ctx);
NT_API void      nt_IoFreeWorkItem(NT_IO_WORKITEM *w);
/* (storport.c's) */
NT_API NT_STATUS nt_IoGetDeviceProperty(NT_DEVICE_OBJECT *pdo, int prop, UINT32 len, void *buf, UINT32 *result);
NT_API void      nt_IoDisconnectInterrupt(void *obj);
NT_API UINT8     nt_KeAcquireInterruptSpinLock(void *obj);
NT_API void      nt_KeReleaseInterruptSpinLock(void *obj, UINT8 old);
