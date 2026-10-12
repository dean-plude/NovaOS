/*
 * wdm.h — the Windows kernel-mode driver structures, in Windows' x64 layout
 *
 * A Windows driver reads and writes DRIVER_OBJECT, DEVICE_OBJECT, IRP,
 * IO_STACK_LOCATION and FILE_OBJECT directly, at the offsets Microsoft's
 * WDK headers give them, so the export layer (kernel/nt/) uses exactly
 * those layouts, written from Microsoft's public documentation and checked
 * below offset by offset.  NovaOS's own I/O manager (io/io.h) keeps its own
 * structures; the names here carry an NT_ prefix so both can be used side
 * by side.  NovaOS's test drivers (tools/drivertest/) include this file
 * too: it uses only C's own types, and works in the kernel (System V ABI)
 * and in a driver built for x86_64-pc-windows-msvc alike.
 *
 * Every function pointer a driver hands in, and every export it calls, is
 * NT_API: the Microsoft x64 calling convention.
 *
 * Only what the drivers of roadmap steps 30.1 to 30.3 use is complete; the rest is
 * there for the layout (opaque members, reserved space) and is filled in
 * by the steps that need it.
 */
#pragma once

#define NT_API __attribute__((ms_abi))

typedef int                NT_STATUS;
typedef unsigned short     NT_WCHAR;
typedef unsigned long long NT_ULONG_PTR;

#define NT_SUCCESS_(s)     ((NT_STATUS)(s) >= 0)

typedef struct _NT_LIST_ENTRY {
    struct _NT_LIST_ENTRY *Flink, *Blink;
} NT_LIST_ENTRY;

typedef struct _NT_UNICODE_STRING {
    unsigned short Length;              /* bytes, without a terminator */
    unsigned short MaximumLength;
    NT_WCHAR      *Buffer;
} NT_UNICODE_STRING;

typedef struct _NT_IO_STATUS_BLOCK {
    union {
        NT_STATUS Status;
        void     *Pointer;
    };
    NT_ULONG_PTR Information;
} NT_IO_STATUS_BLOCK;

/* DISPATCHER_HEADER and KEVENT: the wait objects (nt/ke.c keeps its
 * waiters in WaitListHead) */
typedef struct _NT_DISPATCHER_HEADER {
    unsigned char Type, Signalling, Size, Reserved1;
    int           SignalState;
    NT_LIST_ENTRY WaitListHead;
} NT_DISPATCHER_HEADER;

typedef struct _NT_KEVENT {
    NT_DISPATCHER_HEADER Header;
} NT_KEVENT;

/* The dispatcher objects drivers wait on (DISPATCHER_HEADER.Type) */
#define NT_NotificationEvent         0
#define NT_SynchronizationEvent      1
#define NT_MutantObject              2
#define NT_SemaphoreObject           5
#define NT_TimerNotificationObject   8
#define NT_TimerSynchronizationObject 9

typedef struct _NT_KSEMAPHORE {
    NT_DISPATCHER_HEADER Header;
    int                  Limit;
} NT_KSEMAPHORE;

typedef struct _NT_KTIMER {
    NT_DISPATCHER_HEADER Header;
    unsigned long long   DueTime;
    NT_LIST_ENTRY        TimerListEntry;
    struct _NT_KDPC     *Dpc;
    unsigned int         Processor;
    unsigned int         Period;        /* ms */
} NT_KTIMER;

/* FAST_MUTEX (ExInitializeFastMutex, inline in the WDK: Count 1, a
 * synchronization event) */
typedef struct _NT_FAST_MUTEX {
    int                  Count;         /* bit 0 set: free */
    void                *Owner;
    unsigned int         Contention;
    NT_KEVENT            Event;
    unsigned int         OldIrql;
} NT_FAST_MUTEX;

typedef unsigned long long NT_KSPIN_LOCK;

typedef struct _NT_KLOCK_QUEUE_HANDLE {
    struct { void *Next; NT_KSPIN_LOCK *Lock; } LockQueue;
    unsigned char        OldIrql;
} NT_KLOCK_QUEUE_HANDLE;

/* MDL: a buffer described by its physical pages (the PFN array follows) */
typedef struct _NT_MDL {
    struct _NT_MDL      *Next;
    short                Size;          /* the MDL with its PFN array */
    short                MdlFlags;
    unsigned short       AllocationProcessorNumber, Reserved;
    void                *Process;
    void                *MappedSystemVa;
    void                *StartVa;       /* page-aligned */
    unsigned int         ByteCount;
    unsigned int         ByteOffset;
} NT_MDL;

#define NT_MDL_MAPPED_TO_SYSTEM_VA      0x0001
#define NT_MDL_PAGES_LOCKED             0x0002
#define NT_MDL_SOURCE_IS_NONPAGED_POOL  0x0004
#define NT_MDL_ALLOCATED_FIXED_SIZE     0x0008
#define NT_MDL_PARTIAL                  0x0010
#define NT_MDL_WRITE_OPERATION          0x0080

static inline unsigned long long *NtMdlPfns(NT_MDL *m) { return (unsigned long long *)(m + 1); }

typedef struct _NT_KDEVICE_QUEUE_ENTRY {
    NT_LIST_ENTRY      DeviceListEntry;
    unsigned int       SortKey;
    unsigned char      Inserted;
} NT_KDEVICE_QUEUE_ENTRY;

typedef struct _NT_KDEVICE_QUEUE {
    short              Type, Size;
    NT_LIST_ENTRY      DeviceListHead;
    NT_ULONG_PTR       Lock;
    unsigned char      Busy;
} NT_KDEVICE_QUEUE;

typedef struct _NT_KDPC {
    unsigned char      Type, Importance;
    unsigned short     Number;
    void              *DpcListEntry;    /* (NovaOS: the next DPC queued on its CPU) */
    NT_ULONG_PTR       ProcessorHistory;
    void              *DeferredRoutine, *DeferredContext, *SystemArgument1, *SystemArgument2;
    void              *DpcData;         /* set while queued */
} NT_KDPC;

typedef void (NT_API *NT_KDEFERRED_ROUTINE)(NT_KDPC *, void *Context, void *Arg1, void *Arg2);

typedef struct _NT_WAIT_CONTEXT_BLOCK {
    NT_KDEVICE_QUEUE_ENTRY WaitQueueEntry;
    void              *DeviceRoutine, *DeviceContext;
    unsigned int       NumberOfMapRegisters;
    void              *DeviceObject, *CurrentIrp, *BufferChainingDpc;
} NT_WAIT_CONTEXT_BLOCK;

typedef struct _NT_DRIVER_OBJECT   NT_DRIVER_OBJECT;
typedef struct _NT_DEVICE_OBJECT   NT_DEVICE_OBJECT;
typedef struct _NT_IRP             NT_IRP;
typedef struct _NT_FILE_OBJECT     NT_FILE_OBJECT;

typedef NT_STATUS (NT_API *NT_DRIVER_INITIALIZE)(NT_DRIVER_OBJECT *, NT_UNICODE_STRING *RegistryPath);
typedef NT_STATUS (NT_API *NT_DRIVER_DISPATCH)(NT_DEVICE_OBJECT *, NT_IRP *);
typedef void      (NT_API *NT_DRIVER_UNLOAD)(NT_DRIVER_OBJECT *);
typedef NT_STATUS (NT_API *NT_DRIVER_ADD_DEVICE)(NT_DRIVER_OBJECT *, NT_DEVICE_OBJECT *Pdo);
typedef NT_STATUS (NT_API *NT_IO_COMPLETION_ROUTINE)(NT_DEVICE_OBJECT *, NT_IRP *, void *Context);

#define NT_IRP_MJ_CREATE                0x00
#define NT_IRP_MJ_CLOSE                 0x02
#define NT_IRP_MJ_READ                  0x03
#define NT_IRP_MJ_WRITE                 0x04
#define NT_IRP_MJ_DEVICE_CONTROL        0x0E
#define NT_IRP_MJ_CLEANUP               0x12
#define NT_IRP_MJ_POWER                 0x16
#define NT_IRP_MJ_SYSTEM_CONTROL        0x17
#define NT_IRP_MJ_PNP                   0x1B
#define NT_IRP_MJ_MAXIMUM_FUNCTION      0x1B

typedef struct _NT_DRIVER_EXTENSION {
    NT_DRIVER_OBJECT    *DriverObject;
    NT_DRIVER_ADD_DEVICE AddDevice;
    unsigned int         Count;
    NT_UNICODE_STRING    ServiceKeyName;
} NT_DRIVER_EXTENSION;

struct _NT_DRIVER_OBJECT {
    short                Type;          /* 4 */
    short                Size;
    NT_DEVICE_OBJECT    *DeviceObject;  /* its devices, newest first, through NextDevice */
    unsigned int         Flags;
    void                *DriverStart;
    unsigned int         DriverSize;
    void                *DriverSection;
    NT_DRIVER_EXTENSION *DriverExtension;
    NT_UNICODE_STRING    DriverName;    /* \Driver\<service name> */
    NT_UNICODE_STRING   *HardwareDatabase;
    void                *FastIoDispatch;
    NT_DRIVER_INITIALIZE DriverInit;
    void                *DriverStartIo;
    NT_DRIVER_UNLOAD     DriverUnload;
    NT_DRIVER_DISPATCH   MajorFunction[NT_IRP_MJ_MAXIMUM_FUNCTION + 1];
};

/* DEVICE_OBJECT.Flags */
#define NT_DO_BUFFERED_IO           0x00000004
#define NT_DO_EXCLUSIVE             0x00000008
#define NT_DO_DIRECT_IO             0x00000010
#define NT_DO_DEVICE_INITIALIZING   0x00000080
#define NT_DO_POWER_PAGABLE         0x00002000
#define NT_DO_BUS_ENUMERATED_DEVICE 0x00001000

struct _NT_DEVICE_OBJECT {
    short                Type;          /* 3 */
    unsigned short       Size;
    int                  ReferenceCount;
    NT_DRIVER_OBJECT    *DriverObject;
    NT_DEVICE_OBJECT    *NextDevice;
    NT_DEVICE_OBJECT    *AttachedDevice;
    NT_IRP              *CurrentIrp;
    void                *Timer;
    unsigned int         Flags;
    unsigned int         Characteristics;
    void                *Vpb;
    void                *DeviceExtension;
    unsigned int         DeviceType;
    char                 StackSize;
    union {
        NT_LIST_ENTRY         ListEntry;
        NT_WAIT_CONTEXT_BLOCK Wcb;
    } Queue;
    unsigned int         AlignmentRequirement;
    NT_KDEVICE_QUEUE     DeviceQueue;
    NT_KDPC              Dpc;
    unsigned int         ActiveThreadCount;
    void                *SecurityDescriptor;
    NT_KEVENT            DeviceLock;
    unsigned short       SectorSize, Spare1;
    void                *DeviceObjectExtension;
    void                *Reserved;
} __attribute__((aligned(16)));                     /* (DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT)) */

/* IO_STACK_LOCATION: one per driver in the stack the IRP passes through */
typedef struct _NT_IO_STACK_LOCATION {
    unsigned char MajorFunction, MinorFunction, Flags, Control;
    union {
        struct {
            void          *SecurityContext;
            unsigned int   Options;     /* CreateDisposition << 24 | CreateOptions */
            unsigned short FileAttributes, ShareAccess;
            unsigned int   EaLength;
        } Create;
        struct {
            unsigned int   Length;
            unsigned int   Key __attribute__((aligned(8)));
            long long      ByteOffset;
        } Read, Write;
        struct {
            unsigned int   OutputBufferLength;
            unsigned int   InputBufferLength __attribute__((aligned(8)));
            unsigned int   IoControlCode __attribute__((aligned(8)));
            void          *Type3InputBuffer;
        } DeviceIoControl;
        struct {                        /* IRP_MN_START_DEVICE */
            struct _NT_CM_RESOURCE_LIST *AllocatedResources, *AllocatedResourcesTranslated;
        } StartDevice;
        struct {                        /* IRP_MN_QUERY_INTERFACE */
            const struct _NT_GUID *InterfaceType;
            unsigned short Size, Version;
            struct _NT_INTERFACE *Interface;
            void          *InterfaceSpecificData;
        } QueryInterface;
        struct {                        /* IRP_MN_READ_CONFIG, IRP_MN_WRITE_CONFIG */
            unsigned int   WhichSpace;
            void          *Buffer;
            unsigned int   Offset;
            unsigned int   Length __attribute__((aligned(8)));
        } ReadWriteConfig;
        struct {
            void *Argument1, *Argument2, *Argument3, *Argument4;
        } Others;
    } Parameters;
    NT_DEVICE_OBJECT        *DeviceObject;
    NT_FILE_OBJECT          *FileObject;
    NT_IO_COMPLETION_ROUTINE CompletionRoutine;
    void                    *Context;
} NT_IO_STACK_LOCATION;

/* IO_STACK_LOCATION.Control: when its completion routine runs */
#define NT_SL_PENDING_RETURNED   0x01
#define NT_SL_INVOKE_ON_CANCEL   0x20
#define NT_SL_INVOKE_ON_SUCCESS  0x40
#define NT_SL_INVOKE_ON_ERROR    0x80

struct _NT_IRP {
    short                Type;          /* 6 */
    unsigned short       Size;
    unsigned short       AllocationProcessorNumber, Reserved;
    void                *MdlAddress;
    unsigned int         Flags;
    union {
        NT_IRP *MasterIrp;
        int     IrpCount;
        void   *SystemBuffer;           /* buffered I/O: the kernel's copy of the caller's buffer */
    } AssociatedIrp;
    NT_LIST_ENTRY        ThreadListEntry;
    NT_IO_STATUS_BLOCK   IoStatus;
    char                 RequestorMode; /* 0 KernelMode, 1 UserMode */
    unsigned char        PendingReturned;
    char                 StackCount;
    char                 CurrentLocation;
    unsigned char        Cancel;
    unsigned char        CancelIrql;
    char                 ApcEnvironment;
    unsigned char        AllocationFlags;
    NT_IO_STATUS_BLOCK  *UserIosb;
    NT_KEVENT           *UserEvent;
    union {
        struct { void *UserApcRoutine, *UserApcContext; } AsynchronousParameters;
        long long AllocationSize;
    } Overlay;
    void                *CancelRoutine;
    void                *UserBuffer;    /* neither I/O: the caller's own buffer */
    union {
        struct {
            union {
                NT_KDEVICE_QUEUE_ENTRY DeviceQueueEntry;
                void *DriverContext[4];
            };
            void                 *Thread;
            char                 *AuxiliaryBuffer;
            NT_LIST_ENTRY         ListEntry;
            NT_IO_STACK_LOCATION *CurrentStackLocation;
            NT_FILE_OBJECT       *OriginalFileObject;
            void                 *IrpExtension;
        } Overlay;
        unsigned char Apc[0x58];        /* KAPC */
        void         *CompletionKey;
    } Tail;
};

struct _NT_FILE_OBJECT {
    short                Type;          /* 5 */
    short                Size;
    NT_DEVICE_OBJECT    *DeviceObject;
    void                *Vpb;
    void                *FsContext, *FsContext2;
    void                *SectionObjectPointer, *PrivateCacheMap;
    NT_STATUS            FinalStatus;
    NT_FILE_OBJECT      *RelatedFileObject;
    unsigned char        LockOperation, DeletePending, ReadAccess, WriteAccess, DeleteAccess,
                         SharedRead, SharedWrite, SharedDelete;
    unsigned int         Flags;
    NT_UNICODE_STRING    FileName;      /* what follows the device's name, e.g. \x for \\.\Dev\x */
    long long            CurrentByteOffset;
    unsigned int         Waiters, Busy;
    void                *LastLock;
    NT_KEVENT            Lock, Event;
    void                *CompletionContext;
    NT_ULONG_PTR         IrpListLock;
    NT_LIST_ENTRY        IrpList;
    void                *FileObjectExtension;
};

/* FILE_OBJECT.Flags */
#define NT_FO_SYNCHRONOUS_IO     0x00000002


/* -----------------------------------------------------------------------
 * Plug and Play (step 30.3)
 * ----------------------------------------------------------------------- */
typedef struct _NT_GUID {
    unsigned int   Data1;
    unsigned short Data2, Data3;
    unsigned char  Data4[8];
} NT_GUID;

/* IRP_MJ_PNP's minor functions */
#define NT_IRP_MN_START_DEVICE                 0x00
#define NT_IRP_MN_QUERY_REMOVE_DEVICE          0x01
#define NT_IRP_MN_REMOVE_DEVICE                0x02
#define NT_IRP_MN_CANCEL_REMOVE_DEVICE         0x03
#define NT_IRP_MN_STOP_DEVICE                  0x04
#define NT_IRP_MN_QUERY_STOP_DEVICE            0x05
#define NT_IRP_MN_CANCEL_STOP_DEVICE           0x06
#define NT_IRP_MN_QUERY_DEVICE_RELATIONS       0x07
#define NT_IRP_MN_QUERY_INTERFACE              0x08
#define NT_IRP_MN_QUERY_CAPABILITIES           0x09
#define NT_IRP_MN_QUERY_RESOURCES              0x0A
#define NT_IRP_MN_QUERY_RESOURCE_REQUIREMENTS  0x0B
#define NT_IRP_MN_QUERY_DEVICE_TEXT            0x0C
#define NT_IRP_MN_FILTER_RESOURCE_REQUIREMENTS 0x0D
#define NT_IRP_MN_READ_CONFIG                  0x0F
#define NT_IRP_MN_WRITE_CONFIG                 0x10
#define NT_IRP_MN_QUERY_ID                     0x13
#define NT_IRP_MN_QUERY_PNP_DEVICE_STATE       0x14
#define NT_IRP_MN_QUERY_BUS_INFORMATION        0x15
#define NT_IRP_MN_DEVICE_USAGE_NOTIFICATION    0x16
#define NT_IRP_MN_SURPRISE_REMOVAL             0x17

/* CM_RESOURCE_LIST: what IRP_MN_START_DEVICE hands the driver, raw (as the
 * bus sees it) and translated (as the CPU sees it).  The descriptors are
 * packed to 4 bytes, as the WDK packs them. */
#pragma pack(push, 4)
typedef struct _NT_CM_PARTIAL_RESOURCE_DESCRIPTOR {
    unsigned char  Type;                /* CmResourceType* */
    unsigned char  ShareDisposition;
    unsigned short Flags;
    union {
        struct { long long Start; unsigned int Length; } Generic, Port, Memory;
        struct { unsigned short Level, Group; unsigned int Vector; unsigned long long Affinity; } Interrupt;
        struct { unsigned int Channel, Port, Reserved1; } Dma;
        struct { unsigned int Data[3]; } DeviceSpecificData;
    } u;
} NT_CM_PARTIAL_RESOURCE_DESCRIPTOR;
#pragma pack(pop)

typedef struct _NT_CM_PARTIAL_RESOURCE_LIST {
    unsigned short Version, Revision;
    unsigned int   Count;
    NT_CM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptors[1];
} NT_CM_PARTIAL_RESOURCE_LIST;

typedef struct _NT_CM_FULL_RESOURCE_DESCRIPTOR {
    int            InterfaceType;       /* PCIBus 5 */
    unsigned int   BusNumber;
    NT_CM_PARTIAL_RESOURCE_LIST PartialResourceList;
} NT_CM_FULL_RESOURCE_DESCRIPTOR;

typedef struct _NT_CM_RESOURCE_LIST {
    unsigned int   Count;
    NT_CM_FULL_RESOURCE_DESCRIPTOR List[1];
} NT_CM_RESOURCE_LIST;

#define NT_CmResourceTypePort        1
#define NT_CmResourceTypeInterrupt   2
#define NT_CmResourceTypeMemory      3
#define NT_CmResourceShareDeviceExclusive 1
#define NT_CmResourceShareShared     3
#define NT_CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE 0x0000
#define NT_CM_RESOURCE_INTERRUPT_LATCHED         0x0001
#define NT_CM_RESOURCE_MEMORY_READ_WRITE         0x0000
#define NT_CM_RESOURCE_PORT_IO                   0x0001
#define NT_PCIBus                    5

/* INTERFACE, and the PCI bus's BUS_INTERFACE_STANDARD (IRP_MN_QUERY_INTERFACE) */
struct _NT_DMA_ADAPTER;
struct _NT_DEVICE_DESCRIPTION;

typedef struct _NT_INTERFACE {
    unsigned short Size, Version;
    void          *Context;
    void (NT_API  *InterfaceReference)(void *Context);
    void (NT_API  *InterfaceDereference)(void *Context);
} NT_INTERFACE;

typedef struct _NT_BUS_INTERFACE_STANDARD {
    NT_INTERFACE   Header;
    unsigned char (NT_API *TranslateBusAddress)(void *Context, long long BusAddress, unsigned int Length,
                                                unsigned int *AddressSpace, long long *Translated);
    struct _NT_DMA_ADAPTER *(NT_API *GetDmaAdapter)(void *Context, struct _NT_DEVICE_DESCRIPTION *Desc,
                                                    unsigned int *MapRegisters);
    unsigned int  (NT_API *SetBusData)(void *Context, unsigned int DataType, void *Buffer, unsigned int Offset,
                                       unsigned int Length);
    unsigned int  (NT_API *GetBusData)(void *Context, unsigned int DataType, void *Buffer, unsigned int Offset,
                                       unsigned int Length);
} NT_BUS_INTERFACE_STANDARD;

#define NT_PCI_WHICHSPACE_CONFIG     0
#define NT_PCI_CONFIG_DATA           4  /* (BUS_DATA_TYPE PCIConfiguration) */

/* DEVICE_CAPABILITIES (IRP_MN_QUERY_CAPABILITIES) */
typedef struct _NT_DEVICE_CAPABILITIES {
    unsigned short Size, Version;       /* 0x40, 1 */
    unsigned int   Flags;               /* DeviceD1:1 DeviceD2:1 LockSupported:1 EjectSupported:1 Removable:1 ... */
    unsigned int   Address;             /* PCI: device << 16 | function */
    unsigned int   UINumber;
    int            DeviceState[7];      /* by SYSTEM_POWER_STATE */
    int            SystemWake, DeviceWake;
    unsigned int   D1Latency, D2Latency, D3Latency;
} NT_DEVICE_CAPABILITIES;

/* IoConnectInterruptEx's parameters */
#define NT_CONNECT_FULLY_SPECIFIED   1
#define NT_CONNECT_LINE_BASED        2
#define NT_CONNECT_MESSAGE_BASED     3
#define NT_CONNECT_FULLY_SPECIFIED_GROUP 4   /* (KMDF's: FullySpecified with its Group) */
typedef unsigned char (NT_API *NT_KSERVICE_ROUTINE)(void *Interrupt, void *Context);
typedef unsigned char (NT_API *NT_KSYNCHRONIZE_ROUTINE)(void *Context);

typedef struct _NT_IO_CONNECT_INTERRUPT_PARAMETERS {
    unsigned int Version;
    union {
        struct {
            NT_DEVICE_OBJECT   *PhysicalDeviceObject;
            void              **InterruptObject;
            NT_KSERVICE_ROUTINE ServiceRoutine;
            void               *ServiceContext;
            NT_KSPIN_LOCK      *SpinLock;
            unsigned char       SynchronizeIrql, FloatingSave, ShareVector;
            unsigned int        Vector;
            unsigned char       Irql;
            int                 InterruptMode;      /* LevelSensitive 0, Latched 1 */
            unsigned long long  ProcessorEnableMask;
            unsigned short      Group;
        } FullySpecified;
        struct {
            NT_DEVICE_OBJECT   *PhysicalDeviceObject;
            void              **InterruptObject;
            NT_KSERVICE_ROUTINE ServiceRoutine;
            void               *ServiceContext;
            NT_KSPIN_LOCK      *SpinLock;
            unsigned char       SynchronizeIrql, FloatingSave;
        } LineBased;
        struct {
            NT_DEVICE_OBJECT   *PhysicalDeviceObject;
            void              **ConnectionContext;
            void               *MessageServiceRoutine;
            void               *ServiceContext;
            NT_KSPIN_LOCK      *SpinLock;
            unsigned char       SynchronizeIrql, FloatingSave;
            NT_KSERVICE_ROUTINE FallBackServiceRoutine;
        } MessageBased;
    };
} NT_IO_CONNECT_INTERRUPT_PARAMETERS;

typedef struct _NT_IO_DISCONNECT_INTERRUPT_PARAMETERS {
    unsigned int Version;
    void        *ConnectionContext;
} NT_IO_DISCONNECT_INTERRUPT_PARAMETERS;

/* DMA: DEVICE_DESCRIPTION (IoGetDmaAdapter), the adapter and its operations */
typedef struct _NT_DEVICE_DESCRIPTION {
    unsigned int   Version;             /* 0 to 3 */
    unsigned char  Master, ScatterGather, DemandMode, AutoInitialize, Dma32BitAddresses, IgnoreCount, Reserved1,
                   Dma64BitAddresses;
    unsigned int   BusNumber, DmaChannel;
    int            InterfaceType, DmaWidth, DmaSpeed;
    unsigned int   MaximumLength, DmaPort;
    unsigned int   DmaAddressWidth;     /* (version 3) */
    unsigned int   DmaControllerInstance, DmaRequestLine;
    long long      DeviceAddress;
} NT_DEVICE_DESCRIPTION;

typedef struct _NT_SCATTER_GATHER_ELEMENT {
    long long          Address;
    unsigned int       Length;
    unsigned long long Reserved;
} NT_SCATTER_GATHER_ELEMENT;

typedef struct _NT_SCATTER_GATHER_LIST {
    unsigned int       NumberOfElements;
    unsigned long long Reserved;
    NT_SCATTER_GATHER_ELEMENT Elements[];
} NT_SCATTER_GATHER_LIST;

/* IO_ALLOCATION_ACTION, what an adapter channel's execution routine returns */
#define NT_KeepObject                     1
#define NT_DeallocateObject               2
#define NT_DeallocateObjectKeepRegisters  3
typedef int (NT_API *NT_DRIVER_CONTROL)(NT_DEVICE_OBJECT *, NT_IRP *, void *MapRegisterBase, void *Context);
typedef void (NT_API *NT_DRIVER_LIST_CONTROL)(NT_DEVICE_OBJECT *, NT_IRP *, NT_SCATTER_GATHER_LIST *, void *Context);

typedef struct _NT_DMA_ADAPTER NT_DMA_ADAPTER;
/* DMA_OPERATIONS: the version 1 to 3 functions, in Windows' order */
typedef struct _NT_DMA_OPERATIONS {
    unsigned int Size;
    void (NT_API *PutDmaAdapter)(NT_DMA_ADAPTER *);
    void *(NT_API *AllocateCommonBuffer)(NT_DMA_ADAPTER *, unsigned int Length, long long *Logical, unsigned char Cached);
    void (NT_API *FreeCommonBuffer)(NT_DMA_ADAPTER *, unsigned int Length, long long Logical, void *Va, unsigned char Cached);
    NT_STATUS (NT_API *AllocateAdapterChannel)(NT_DMA_ADAPTER *, NT_DEVICE_OBJECT *, unsigned int MapRegisters,
                                               NT_DRIVER_CONTROL Execution, void *Context);
    unsigned char (NT_API *FlushAdapterBuffers)(NT_DMA_ADAPTER *, NT_MDL *, void *MapRegisterBase, void *CurrentVa,
                                                unsigned int Length, unsigned char WriteToDevice);
    void (NT_API *FreeAdapterChannel)(NT_DMA_ADAPTER *);
    void (NT_API *FreeMapRegisters)(NT_DMA_ADAPTER *, void *MapRegisterBase, unsigned int Count);
    long long (NT_API *MapTransfer)(NT_DMA_ADAPTER *, NT_MDL *, void *MapRegisterBase, void *CurrentVa,
                                    unsigned int *Length, unsigned char WriteToDevice);
    unsigned int (NT_API *GetDmaAlignment)(NT_DMA_ADAPTER *);
    unsigned int (NT_API *ReadDmaCounter)(NT_DMA_ADAPTER *);
    NT_STATUS (NT_API *GetScatterGatherList)(NT_DMA_ADAPTER *, NT_DEVICE_OBJECT *, NT_MDL *, void *CurrentVa,
                                             unsigned int Length, NT_DRIVER_LIST_CONTROL Execution, void *Context,
                                             unsigned char WriteToDevice);
    void (NT_API *PutScatterGatherList)(NT_DMA_ADAPTER *, NT_SCATTER_GATHER_LIST *, unsigned char WriteToDevice);
    NT_STATUS (NT_API *CalculateScatterGatherList)(NT_DMA_ADAPTER *, NT_MDL *, void *CurrentVa, unsigned int Length,
                                                   unsigned int *ListSize, unsigned int *MapRegisters);
    NT_STATUS (NT_API *BuildScatterGatherList)(NT_DMA_ADAPTER *, NT_DEVICE_OBJECT *, NT_MDL *, void *CurrentVa,
                                               unsigned int Length, NT_DRIVER_LIST_CONTROL Execution, void *Context,
                                               unsigned char WriteToDevice, void *Buffer, unsigned int BufferLength);
    void *BuildMdlFromScatterGatherList;
    /* version 2 */
    void *GetDmaAdapterInfo, *GetDmaTransferInfo;
    /* version 3 */
    void *InitializeDmaTransferContext;
    void *(NT_API *AllocateCommonBufferEx)(NT_DMA_ADAPTER *, long long *MaximumAddress, unsigned int Length,
                                           long long *Logical, unsigned char Cached, unsigned int Node);
    void *AllocateAdapterChannelEx, *ConfigureAdapterChannel, *CancelAdapterChannel, *MapTransferEx,
         *GetScatterGatherListEx, *BuildScatterGatherListEx, *FlushAdapterBuffersEx, *FreeAdapterObject,
         *CancelMappedTransfer, *AllocateDomainCommonBuffer, *FlushDmaBuffer, *JoinDmaDomain, *LeaveDmaDomain,
         *GetDmaDomain;
} NT_DMA_OPERATIONS;

struct _NT_DMA_ADAPTER {
    unsigned short     Version, Size;
    NT_DMA_OPERATIONS *DmaOperations;
};

/* Status codes the export layer and the test drivers use */
#define NT_STATUS_SUCCESS                  ((NT_STATUS)0x00000000)
#define NT_STATUS_TIMEOUT                  ((NT_STATUS)0x00000102)
#define NT_STATUS_PENDING                  ((NT_STATUS)0x00000103)
#define NT_STATUS_CANCELLED                ((NT_STATUS)0xC0000120)
#define NT_STATUS_ACCESS_VIOLATION         ((NT_STATUS)0xC0000005)
#define NT_STATUS_MORE_PROCESSING_REQUIRED ((NT_STATUS)0xC0000016)
#define NT_STATUS_INVALID_PARAMETER        ((NT_STATUS)0xC000000D)
#define NT_STATUS_INVALID_DEVICE_REQUEST   ((NT_STATUS)0xC0000010)
#define NT_STATUS_END_OF_FILE              ((NT_STATUS)0xC0000011)
#define NT_STATUS_NO_MEMORY                ((NT_STATUS)0xC0000017)
#define NT_STATUS_BUFFER_TOO_SMALL         ((NT_STATUS)0xC0000023)
#define NT_STATUS_OBJECT_NAME_INVALID      ((NT_STATUS)0xC0000033)
#define NT_STATUS_OBJECT_NAME_NOT_FOUND    ((NT_STATUS)0xC0000034)
#define NT_STATUS_OBJECT_NAME_COLLISION    ((NT_STATUS)0xC0000035)
#define NT_STATUS_NOT_SUPPORTED            ((NT_STATUS)0xC00000BB)
#define NT_STATUS_INSUFFICIENT_RESOURCES   ((NT_STATUS)0xC000009A)
#define NT_STATUS_DELETE_PENDING           ((NT_STATUS)0xC0000056)
#define NT_STATUS_DEVICE_NOT_READY         ((NT_STATUS)0xC00000A3)
#define NT_STATUS_DEVICE_BUSY              ((NT_STATUS)0x80000011)
#define NT_STATUS_UNSUCCESSFUL             ((NT_STATUS)0xC0000001)
#define NT_STATUS_NOT_IMPLEMENTED          ((NT_STATUS)0xC0000002)
#define NT_STATUS_INVALID_HANDLE           ((NT_STATUS)0xC0000008)
#define NT_STATUS_DATATYPE_MISALIGNMENT    ((NT_STATUS)0x80000002)
#define NT_STATUS_BUFFER_OVERFLOW          ((NT_STATUS)0x80000005)
#define NT_STATUS_OBJECT_PATH_NOT_FOUND    ((NT_STATUS)0xC000003A)
#define NT_STATUS_RESOURCE_NOT_OWNED       ((NT_STATUS)0xC0000264)
#define NT_STATUS_INFO_LENGTH_MISMATCH     ((NT_STATUS)0xC0000004)
/* KeWaitForMultipleObjects: WAIT_OBJECT_0 + index, or STATUS_TIMEOUT */
#define NT_STATUS_WAIT_0                   ((NT_STATUS)0x00000000)
#define NT_STATUS_DELETE_PENDING           ((NT_STATUS)0xC0000056)
#define NT_STATUS_DEVICE_NOT_READY         ((NT_STATUS)0xC00000A3)

/* CTL_CODE and the transfer methods */
#define NT_CTL_CODE(type, fn, method, access) (((type) << 16) | ((access) << 14) | ((fn) << 2) | (method))
#define NT_METHOD_BUFFERED       0
#define NT_METHOD_IN_DIRECT      1
#define NT_METHOD_OUT_DIRECT     2
#define NT_METHOD_NEITHER        3
#define NT_FILE_DEVICE_UNKNOWN   0x00000022

/* The inline helpers the WDK gives drivers */
static inline NT_IO_STACK_LOCATION *NtIrpCurrentStack(NT_IRP *irp)
{
    return irp->Tail.Overlay.CurrentStackLocation;
}
static inline NT_IO_STACK_LOCATION *NtIrpNextStack(NT_IRP *irp)
{
    return irp->Tail.Overlay.CurrentStackLocation - 1;
}
/* IoMarkIrpPending: the dispatch routine will return STATUS_PENDING */
static inline void NtIrpMarkPending(NT_IRP *irp)
{
    NtIrpCurrentStack(irp)->Control |= NT_SL_PENDING_RETURNED;
}

/* -----------------------------------------------------------------------
 * Step 30.2 part 2: the objects drivers embed by value, in Windows' x64
 * layout.  A driver puts these in its device extension, so the size must
 * match Windows' even where NovaOS keeps less state in them: the fields
 * NovaOS uses are at the front and the rest is reserved to the full size.
 * ----------------------------------------------------------------------- */

/* KMUTANT / KMUTEX: a recursive mutex.  SignalState 1 free, <= 0 the
 * negative of the recursion depth held by OwnerThread. */
typedef struct _NT_KMUTANT {
    NT_DISPATCHER_HEADER Header;
    NT_LIST_ENTRY        MutantListEntry;
    void                *OwnerThread;
} NT_KMUTEX, NT_KMUTANT;

/* SLIST_HEADER: a sequenced singly linked list head (16-byte aligned).
 * NovaOS guards the list with a lock rather than a 128-bit compare-and-
 * swap; Next is the list head, Depth the count. */
typedef struct __attribute__((aligned(16))) _NT_SLIST_HEADER {
    void              *Next;
    unsigned short     Depth;
    unsigned short     Sequence;
    unsigned int       Reserved;
} NT_SLIST_HEADER;

typedef struct _NT_SLIST_ENTRY { struct _NT_SLIST_ENTRY *Next; } NT_SLIST_ENTRY;

/* ERESOURCE: a reader/writer lock.  NovaOS keeps the owner, the recursion
 * and reader counts and one wait event; the rest is Windows' padding. */
typedef struct _NT_ERESOURCE {
    void      *OwnerThread;        /* the exclusive owner, or NULL */
    int        ExclusiveCount;     /* its recursion */
    int        SharedCount;        /* readers holding it */
    NT_KEVENT  Event;              /* waiters wait here */
    unsigned char Reserved[0x68 - 0x28];
} NT_ERESOURCE;

/* (N)PAGED_LOOKASIDE_LIST: a free list of fixed-size blocks, laid out as
 * Windows x64's GENERAL_LOOKASIDE: the WDK's inline
 * ExAllocateFromNPagedLookasideList and ExFreeToNPagedLookasideList (which
 * KMDF uses) read and count in it directly. */
typedef struct __attribute__((aligned(16))) _NT_LOOKASIDE_LIST {
    NT_SLIST_HEADER ListHead;
    unsigned short  Depth, MaximumDepth;
    unsigned int    TotalAllocates, AllocateMisses, TotalFrees, FreeMisses;
    int             Type;          /* POOL_TYPE */
    unsigned int    Tag;
    unsigned int    Size;
    void           *Allocate;      /* the driver's own, or the pool's */
    void           *Free;
    unsigned char   Reserved[0x80 - 0x40];
} NT_LOOKASIDE_LIST, NT_PAGED_LOOKASIDE_LIST, NT_NPAGED_LOOKASIDE_LIST;

/* IO_REMOVE_LOCK: holds off a device's removal while I/O is in flight. */
typedef struct _NT_IO_REMOVE_LOCK {
    unsigned char  Removed;
    unsigned char  Reserved[3];
    int            IoCount;
    NT_KEVENT      RemoveEvent;
} NT_IO_REMOVE_LOCK;

/* An I/O work item is opaque to the driver: IoAllocateWorkItem returns a
 * pointer it keeps and passes back; its body lives in the kernel. */
typedef struct _NT_IO_WORKITEM NT_IO_WORKITEM;
typedef void (NT_API *NT_IO_WORKITEM_ROUTINE)(NT_DEVICE_OBJECT *, void *Context);
typedef void (NT_API *NT_IO_WORKITEM_ROUTINE_EX)(void *IoObject, void *Context, NT_IO_WORKITEM *);
#define NT_CriticalWorkQueue       0
#define NT_DelayedWorkQueue        1
#define NT_HyperCriticalWorkQueue  2

/* KWAIT_BLOCK: the caller of KeWaitForMultipleObjects supplies an array
 * of these, one per object (its layout is opaque to NovaOS, which keeps
 * its own wait state). */
typedef struct _NT_KWAIT_BLOCK { unsigned char Opaque[0x30]; } NT_KWAIT_BLOCK;
#define NT_WaitAll  0
#define NT_WaitAny  1

/* The layouts as Windows x64 has them (offsets from the WDK's wdm.h) */
#define NT_OFFSET(t, m) __builtin_offsetof(t, m)
_Static_assert(sizeof(NT_KMUTEX) == 0x30 && NT_OFFSET(NT_KMUTANT, OwnerThread) == 0x28, "KMUTANT");
_Static_assert(sizeof(NT_SLIST_HEADER) == 0x10 && sizeof(NT_ERESOURCE) == 0x68, "SLIST_HEADER, ERESOURCE");
_Static_assert(sizeof(NT_PAGED_LOOKASIDE_LIST) == 0x80 && NT_OFFSET(NT_LOOKASIDE_LIST, Size) == 0x2C &&
               NT_OFFSET(NT_LOOKASIDE_LIST, Allocate) == 0x30, "LOOKASIDE_LIST");
_Static_assert(sizeof(NT_IO_REMOVE_LOCK) == 0x20 && NT_OFFSET(NT_IO_REMOVE_LOCK, RemoveEvent) == 0x08, "IO_REMOVE_LOCK");

_Static_assert(sizeof(NT_UNICODE_STRING) == 0x10 && sizeof(NT_IO_STATUS_BLOCK) == 0x10, "UNICODE_STRING, IO_STATUS_BLOCK");
_Static_assert(sizeof(NT_KEVENT) == 0x18 && sizeof(NT_KDEVICE_QUEUE) == 0x28 && sizeof(NT_KDPC) == 0x40, "KEVENT, KDEVICE_QUEUE, KDPC");
_Static_assert(sizeof(NT_WAIT_CONTEXT_BLOCK) == 0x48, "WAIT_CONTEXT_BLOCK");
_Static_assert(sizeof(NT_KSEMAPHORE) == 0x20 && sizeof(NT_KTIMER) == 0x40 && NT_OFFSET(NT_KTIMER, Dpc) == 0x30,
               "KSEMAPHORE, KTIMER");
_Static_assert(sizeof(NT_FAST_MUTEX) == 0x38 && NT_OFFSET(NT_FAST_MUTEX, Event) == 0x18 &&
               sizeof(NT_KLOCK_QUEUE_HANDLE) == 0x18, "FAST_MUTEX, KLOCK_QUEUE_HANDLE");
_Static_assert(sizeof(NT_MDL) == 0x30 && NT_OFFSET(NT_MDL, MappedSystemVa) == 0x18 && NT_OFFSET(NT_MDL, ByteCount) == 0x28,
               "MDL");
_Static_assert(NT_OFFSET(NT_DRIVER_OBJECT, DriverExtension) == 0x30 && NT_OFFSET(NT_DRIVER_OBJECT, DriverName) == 0x38 &&
               NT_OFFSET(NT_DRIVER_OBJECT, DriverUnload) == 0x68 && NT_OFFSET(NT_DRIVER_OBJECT, MajorFunction) == 0x70 &&
               sizeof(NT_DRIVER_OBJECT) == 0x150, "DRIVER_OBJECT");
_Static_assert(NT_OFFSET(NT_DEVICE_OBJECT, Flags) == 0x30 && NT_OFFSET(NT_DEVICE_OBJECT, DeviceExtension) == 0x40 &&
               NT_OFFSET(NT_DEVICE_OBJECT, DeviceType) == 0x48 && NT_OFFSET(NT_DEVICE_OBJECT, StackSize) == 0x4C &&
               NT_OFFSET(NT_DEVICE_OBJECT, AlignmentRequirement) == 0x98 && NT_OFFSET(NT_DEVICE_OBJECT, Dpc) == 0xC8 &&
               NT_OFFSET(NT_DEVICE_OBJECT, DeviceLock) == 0x118 && NT_OFFSET(NT_DEVICE_OBJECT, DeviceObjectExtension) == 0x138 &&
               sizeof(NT_DEVICE_OBJECT) == 0x150, "DEVICE_OBJECT");
_Static_assert(NT_OFFSET(NT_IO_STACK_LOCATION, Parameters) == 0x08 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, Parameters.Read.ByteOffset) == 0x18 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, Parameters.DeviceIoControl.InputBufferLength) == 0x10 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, Parameters.DeviceIoControl.IoControlCode) == 0x18 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, Parameters.DeviceIoControl.Type3InputBuffer) == 0x20 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, DeviceObject) == 0x28 && NT_OFFSET(NT_IO_STACK_LOCATION, CompletionRoutine) == 0x38 &&
               sizeof(NT_IO_STACK_LOCATION) == 0x48, "IO_STACK_LOCATION");
_Static_assert(NT_OFFSET(NT_IRP, MdlAddress) == 0x08 && NT_OFFSET(NT_IRP, AssociatedIrp) == 0x18 &&
               NT_OFFSET(NT_IRP, IoStatus) == 0x30 && NT_OFFSET(NT_IRP, RequestorMode) == 0x40 &&
               NT_OFFSET(NT_IRP, CurrentLocation) == 0x43 && NT_OFFSET(NT_IRP, UserIosb) == 0x48 &&
               NT_OFFSET(NT_IRP, UserEvent) == 0x50 && NT_OFFSET(NT_IRP, CancelRoutine) == 0x68 &&
               NT_OFFSET(NT_IRP, UserBuffer) == 0x70 && NT_OFFSET(NT_IRP, Tail.Overlay.Thread) == 0x98 &&
               NT_OFFSET(NT_IRP, Tail.Overlay.CurrentStackLocation) == 0xB8 &&
               NT_OFFSET(NT_IRP, Tail.Overlay.OriginalFileObject) == 0xC0 && sizeof(NT_IRP) == 0xD0, "IRP");
_Static_assert(sizeof(NT_CM_PARTIAL_RESOURCE_DESCRIPTOR) == 0x14 && NT_OFFSET(NT_CM_PARTIAL_RESOURCE_LIST, PartialDescriptors) == 8 &&
               NT_OFFSET(NT_CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList) == 8 && NT_OFFSET(NT_CM_RESOURCE_LIST, List) == 4,
               "CM_RESOURCE_LIST");
_Static_assert(sizeof(NT_BUS_INTERFACE_STANDARD) == 0x40 && sizeof(NT_DEVICE_CAPABILITIES) == 0x40 &&
               sizeof(NT_IO_REMOVE_LOCK) == 0x20, "BUS_INTERFACE_STANDARD, DEVICE_CAPABILITIES, IO_REMOVE_LOCK");
_Static_assert(NT_OFFSET(NT_IO_CONNECT_INTERRUPT_PARAMETERS, FullySpecified.SynchronizeIrql) == 0x30 &&
               NT_OFFSET(NT_IO_CONNECT_INTERRUPT_PARAMETERS, FullySpecified.Vector) == 0x34 &&
               NT_OFFSET(NT_IO_CONNECT_INTERRUPT_PARAMETERS, FullySpecified.InterruptMode) == 0x3C &&
               NT_OFFSET(NT_IO_CONNECT_INTERRUPT_PARAMETERS, FullySpecified.ProcessorEnableMask) == 0x40 &&
               NT_OFFSET(NT_IO_CONNECT_INTERRUPT_PARAMETERS, MessageBased.FallBackServiceRoutine) == 0x38,
               "IO_CONNECT_INTERRUPT_PARAMETERS");
_Static_assert(NT_OFFSET(NT_DEVICE_DESCRIPTION, BusNumber) == 12 && NT_OFFSET(NT_DEVICE_DESCRIPTION, MaximumLength) == 32 &&
               NT_OFFSET(NT_DEVICE_DESCRIPTION, DmaAddressWidth) == 40 && sizeof(NT_DEVICE_DESCRIPTION) == 64,
               "DEVICE_DESCRIPTION");
_Static_assert(sizeof(NT_SCATTER_GATHER_ELEMENT) == 24 && NT_OFFSET(NT_SCATTER_GATHER_LIST, Elements) == 16 &&
               NT_OFFSET(NT_DMA_OPERATIONS, MapTransfer) == 0x40 && NT_OFFSET(NT_DMA_OPERATIONS, GetScatterGatherList) == 0x58 &&
               NT_OFFSET(NT_DMA_OPERATIONS, AllocateCommonBufferEx) == 0x98, "DMA");
_Static_assert(NT_OFFSET(NT_IO_STACK_LOCATION, Parameters.QueryInterface.Interface) == 0x18 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, Parameters.ReadWriteConfig.Length) == 0x20, "IO_STACK_LOCATION (PnP)");
_Static_assert(NT_OFFSET(NT_FILE_OBJECT, FsContext) == 0x18 && NT_OFFSET(NT_FILE_OBJECT, FinalStatus) == 0x38 &&
               NT_OFFSET(NT_FILE_OBJECT, Flags) == 0x50 && NT_OFFSET(NT_FILE_OBJECT, FileName) == 0x58 &&
               NT_OFFSET(NT_FILE_OBJECT, CurrentByteOffset) == 0x68 && NT_OFFSET(NT_FILE_OBJECT, Event) == 0x98 &&
               NT_OFFSET(NT_FILE_OBJECT, IrpList) == 0xC0 && sizeof(NT_FILE_OBJECT) == 0xD8, "FILE_OBJECT");
