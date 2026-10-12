/*
 * novaos_dma_operations.h — DMA_OPERATIONS as of Windows 10
 *
 * build_kmdf.py puts this in place of mingw-w64's (Windows 7) definition in
 * its copy of wdm.h.  DMA version 3 (Windows 8) adds the transfer-context
 * routines KMDF uses for system DMA and for DMA_SYNCHRONOUS_CALLBACK; the
 * later members (DMA domains, common-buffer vectors) complete the table so
 * its size and layout are Windows'.
 */
#ifndef _NODE_REQUIREMENT_DEFINED
#define _NODE_REQUIREMENT_DEFINED
typedef ULONG NODE_REQUIREMENT;
#define MM_ANY_NODE_OK 0x80000000
#endif

#define DMA_SYNCHRONOUS_CALLBACK 0x01
#define DMA_ZERO_BUFFERS         0x02

#define DMA_TRANSFER_CONTEXT_VERSION1 1
#define DMA_TRANSFER_CONTEXT_SIZE_V1  128

#define DMA_ADAPTER_INFO_VERSION1 1
#define ADAPTER_INFO_SYNCHRONOUS_CALLBACK 0x0001
#define ADAPTER_INFO_API_BYPASS           0x0002

typedef struct _DMA_ADAPTER_INFO_V1 {
  ULONG ReadDmaCounterAvailable;
  ULONG ScatterGatherLimit;
  ULONG DmaAddressWidth;
  ULONG Flags;
  ULONG MinimumTransferUnit;
} DMA_ADAPTER_INFO_V1, *PDMA_ADAPTER_INFO_V1;

typedef struct _DMA_ADAPTER_INFO {
  ULONG Version;
  union {
    DMA_ADAPTER_INFO_V1 V1;
  };
} DMA_ADAPTER_INFO, *PDMA_ADAPTER_INFO;

#define DMA_TRANSFER_INFO_VERSION1 1
#define DMA_TRANSFER_INFO_VERSION2 2

typedef struct _DMA_TRANSFER_INFO_V1 {
  ULONG MapRegisterCount;
  ULONG ScatterGatherElementCount;
  ULONG ScatterGatherListSize;
} DMA_TRANSFER_INFO_V1, *PDMA_TRANSFER_INFO_V1;

typedef struct _DMA_TRANSFER_INFO_V2 {
  ULONG MapRegisterCount;
  ULONG ScatterGatherElementCount;
  ULONG ScatterGatherListSize;
  ULONG LogicalPageCount;
} DMA_TRANSFER_INFO_V2, *PDMA_TRANSFER_INFO_V2;

typedef struct _DMA_TRANSFER_INFO {
  ULONG Version;
  union {
    DMA_TRANSFER_INFO_V1 V1;
    DMA_TRANSFER_INFO_V2 V2;
  };
} DMA_TRANSFER_INFO, *PDMA_TRANSFER_INFO;

typedef VOID
(NTAPI DMA_COMPLETION_ROUTINE)(
  IN PDMA_ADAPTER DmaAdapter,
  IN PDEVICE_OBJECT DeviceObject,
  IN PVOID CompletionContext,
  IN DMA_COMPLETION_STATUS Status);
typedef DMA_COMPLETION_ROUTINE *PDMA_COMPLETION_ROUTINE;

typedef NTSTATUS (NTAPI *PGET_DMA_ADAPTER_INFO)(PDMA_ADAPTER DmaAdapter, PDMA_ADAPTER_INFO AdapterInfo);
typedef NTSTATUS (NTAPI *PGET_DMA_TRANSFER_INFO)(PDMA_ADAPTER DmaAdapter, PMDL Mdl, ULONGLONG Offset, ULONG Length,
                                                 BOOLEAN WriteOnly, PDMA_TRANSFER_INFO TransferInfo);
typedef NTSTATUS (NTAPI *PINITIALIZE_DMA_TRANSFER_CONTEXT)(PDMA_ADAPTER DmaAdapter, PVOID DmaTransferContext);
typedef PVOID (NTAPI *PALLOCATE_COMMON_BUFFER_EX)(PDMA_ADAPTER DmaAdapter, PPHYSICAL_ADDRESS MaximumAddress, ULONG Length,
                                                  PPHYSICAL_ADDRESS LogicalAddress, BOOLEAN CacheEnabled,
                                                  NODE_REQUIREMENT PreferredNode);
typedef NTSTATUS (NTAPI *PALLOCATE_ADAPTER_CHANNEL_EX)(PDMA_ADAPTER DmaAdapter, PDEVICE_OBJECT DeviceObject,
                                                       PVOID DmaTransferContext, ULONG NumberOfMapRegisters, ULONG Flags,
                                                       PDRIVER_CONTROL ExecutionRoutine, PVOID ExecutionContext,
                                                       PVOID *MapRegisterBase);
typedef NTSTATUS (NTAPI *PCONFIGURE_ADAPTER_CHANNEL)(PDMA_ADAPTER DmaAdapter, ULONG FunctionNumber, PVOID Context);
typedef BOOLEAN (NTAPI *PCANCEL_ADAPTER_CHANNEL)(PDMA_ADAPTER DmaAdapter, PDEVICE_OBJECT DeviceObject,
                                                 PVOID DmaTransferContext);
typedef NTSTATUS (NTAPI *PMAP_TRANSFER_EX)(PDMA_ADAPTER DmaAdapter, PMDL Mdl, PVOID MapRegisterBase, ULONGLONG Offset,
                                           ULONG DeviceOffset, PULONG Length, BOOLEAN WriteToDevice,
                                           PSCATTER_GATHER_LIST ScatterGatherBuffer, ULONG ScatterGatherBufferLength,
                                           PDMA_COMPLETION_ROUTINE DmaCompletionRoutine, PVOID CompletionContext);
typedef NTSTATUS (NTAPI *PGET_SCATTER_GATHER_LIST_EX)(PDMA_ADAPTER DmaAdapter, PDEVICE_OBJECT DeviceObject,
                                                      PVOID DmaTransferContext, PMDL Mdl, ULONGLONG Offset, ULONG Length,
                                                      ULONG Flags, PDRIVER_LIST_CONTROL ExecutionRoutine, PVOID Context,
                                                      BOOLEAN WriteToDevice, PDMA_COMPLETION_ROUTINE DmaCompletionRoutine,
                                                      PVOID CompletionContext, PSCATTER_GATHER_LIST *ScatterGatherList);
typedef NTSTATUS (NTAPI *PBUILD_SCATTER_GATHER_LIST_EX)(PDMA_ADAPTER DmaAdapter, PDEVICE_OBJECT DeviceObject,
                                                        PVOID DmaTransferContext, PMDL Mdl, ULONGLONG Offset, ULONG Length,
                                                        ULONG Flags, PDRIVER_LIST_CONTROL ExecutionRoutine, PVOID Context,
                                                        BOOLEAN WriteToDevice, PVOID ScatterGatherBuffer,
                                                        ULONG ScatterGatherLength,
                                                        PDMA_COMPLETION_ROUTINE DmaCompletionRoutine,
                                                        PVOID CompletionContext, PVOID ScatterGatherList);
typedef NTSTATUS (NTAPI *PFLUSH_ADAPTER_BUFFERS_EX)(PDMA_ADAPTER DmaAdapter, PMDL Mdl, PVOID MapRegisterBase,
                                                    ULONGLONG Offset, ULONG Length, BOOLEAN WriteToDevice);
typedef VOID (NTAPI *PFREE_ADAPTER_OBJECT)(PDMA_ADAPTER DmaAdapter, IO_ALLOCATION_ACTION AllocationAction);
typedef NTSTATUS (NTAPI *PCANCEL_MAPPED_TRANSFER)(PDMA_ADAPTER DmaAdapter, PVOID DmaTransferContext);
typedef NTSTATUS (NTAPI *PALLOCATE_DOMAIN_COMMON_BUFFER)(PDMA_ADAPTER DmaAdapter, HANDLE DomainHandle,
                                                         PPHYSICAL_ADDRESS MaximumAddress, ULONG Length, ULONG Flags,
                                                         MEMORY_CACHING_TYPE *CacheType, NODE_REQUIREMENT PreferredNode,
                                                         PPHYSICAL_ADDRESS LogicalAddress, PVOID *VirtualAddress);
typedef NTSTATUS (NTAPI *PFLUSH_DMA_BUFFER)(PDMA_ADAPTER DmaAdapter, PMDL Mdl, BOOLEAN ReadOperation);
typedef NTSTATUS (NTAPI *PJOIN_DMA_DOMAIN)(PDMA_ADAPTER DmaAdapter, HANDLE DomainHandle);
typedef NTSTATUS (NTAPI *PLEAVE_DMA_DOMAIN)(PDMA_ADAPTER DmaAdapter);
typedef HANDLE (NTAPI *PGET_DMA_DOMAIN)(PDMA_ADAPTER DmaAdapter);
typedef PVOID (NTAPI *PALLOCATE_COMMON_BUFFER_WITH_BOUNDS)(PDMA_ADAPTER DmaAdapter, PPHYSICAL_ADDRESS MinimumAddress,
                                                           PPHYSICAL_ADDRESS MaximumAddress, ULONG Length, ULONG Flags,
                                                           MEMORY_CACHING_TYPE *CacheType, NODE_REQUIREMENT PreferredNode,
                                                           PPHYSICAL_ADDRESS LogicalAddress);
/* (common-buffer vectors, Windows 10 2004: KMDF does not call these) */
typedef PVOID PALLOCATE_COMMON_BUFFER_VECTOR;
typedef PVOID PGET_COMMON_BUFFER_FROM_VECTOR_BY_INDEX;
typedef PVOID PFREE_COMMON_BUFFER_FROM_VECTOR;
typedef PVOID PFREE_COMMON_BUFFER_VECTOR;
typedef PVOID PCREATE_COMMON_BUFFER_FROM_MDL;

typedef struct _DMA_OPERATIONS {
  ULONG Size;
  PPUT_DMA_ADAPTER PutDmaAdapter;
  PALLOCATE_COMMON_BUFFER AllocateCommonBuffer;
  PFREE_COMMON_BUFFER FreeCommonBuffer;
  PALLOCATE_ADAPTER_CHANNEL AllocateAdapterChannel;
  PFLUSH_ADAPTER_BUFFERS FlushAdapterBuffers;
  PFREE_ADAPTER_CHANNEL FreeAdapterChannel;
  PFREE_MAP_REGISTERS FreeMapRegisters;
  PMAP_TRANSFER MapTransfer;
  PGET_DMA_ALIGNMENT GetDmaAlignment;
  PREAD_DMA_COUNTER ReadDmaCounter;
  PGET_SCATTER_GATHER_LIST GetScatterGatherList;
  PPUT_SCATTER_GATHER_LIST PutScatterGatherList;
  PCALCULATE_SCATTER_GATHER_LIST_SIZE CalculateScatterGatherList;
  PBUILD_SCATTER_GATHER_LIST BuildScatterGatherList;
  PBUILD_MDL_FROM_SCATTER_GATHER_LIST BuildMdlFromScatterGatherList;
  PGET_DMA_ADAPTER_INFO GetDmaAdapterInfo;
  PGET_DMA_TRANSFER_INFO GetDmaTransferInfo;
  PINITIALIZE_DMA_TRANSFER_CONTEXT InitializeDmaTransferContext;
  PALLOCATE_COMMON_BUFFER_EX AllocateCommonBufferEx;
  PALLOCATE_ADAPTER_CHANNEL_EX AllocateAdapterChannelEx;
  PCONFIGURE_ADAPTER_CHANNEL ConfigureAdapterChannel;
  PCANCEL_ADAPTER_CHANNEL CancelAdapterChannel;
  PMAP_TRANSFER_EX MapTransferEx;
  PGET_SCATTER_GATHER_LIST_EX GetScatterGatherListEx;
  PBUILD_SCATTER_GATHER_LIST_EX BuildScatterGatherListEx;
  PFLUSH_ADAPTER_BUFFERS_EX FlushAdapterBuffersEx;
  PFREE_ADAPTER_OBJECT FreeAdapterObject;
  PCANCEL_MAPPED_TRANSFER CancelMappedTransfer;
  PALLOCATE_DOMAIN_COMMON_BUFFER AllocateDomainCommonBuffer;
  PFLUSH_DMA_BUFFER FlushDmaBuffer;
  PJOIN_DMA_DOMAIN JoinDmaDomain;
  PLEAVE_DMA_DOMAIN LeaveDmaDomain;
  PGET_DMA_DOMAIN GetDmaDomain;
  PALLOCATE_COMMON_BUFFER_WITH_BOUNDS AllocateCommonBufferWithBounds;
  PALLOCATE_COMMON_BUFFER_VECTOR AllocateCommonBufferVector;
  PGET_COMMON_BUFFER_FROM_VECTOR_BY_INDEX GetCommonBufferFromVectorByIndex;
  PFREE_COMMON_BUFFER_FROM_VECTOR FreeCommonBufferFromVector;
  PFREE_COMMON_BUFFER_VECTOR FreeCommonBufferVector;
  PCREATE_COMMON_BUFFER_FROM_MDL CreateCommonBufferFromMdl;
} DMA_OPERATIONS, *PDMA_OPERATIONS;
